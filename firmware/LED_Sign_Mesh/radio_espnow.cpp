#include "radio_espnow.h"
#include "protocol.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include <string.h>

namespace radio {

const uint8_t BCAST[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

// ---------------- RX ring (producer: ESP-NOW callback, consumer: pump) ----------------
struct RxItem {
  uint8_t srcMac[6];
  uint8_t len;
  int8_t  rssi;
  uint8_t buf[MAX_FRAME];
};
static constexpr int RX_RING = 16;
static RxItem rxRing[RX_RING];
static volatile uint8_t rxHead = 0, rxTail = 0;

// ---------------- send queue (slots, one frame in flight at a time) ----------------
struct TxItem {
  bool     used;
  bool     inflight;
  bool     needsAck;
  uint8_t  mac[6];
  uint8_t  len;
  uint8_t  tries;
  uint16_t seq;
  uint32_t order;      // enqueue order, lowest goes first
  uint32_t lastSend;
  uint8_t  buf[MAX_FRAME];
};
static constexpr int TX_SLOTS = 16;
static TxItem txq[TX_SLOTS];
static uint32_t txOrder = 0;
static uint16_t txSeq = 0;
static int      inflightIdx = -1;
static portMUX_TYPE qMux = portMUX_INITIALIZER_UNLOCKED;

static volatile bool sendBusy = false;   // set before esp_now_send, cleared in onSent
static volatile bool txDone   = false;   // onSent fired since pump last looked
static volatile bool txOk     = false;

// ---------------- dedupe table (srcMac -> last seq) ----------------
struct SeqEntry { bool used; uint8_t mac[6]; uint16_t seq; };
static SeqEntry lastSeq[MAX_BOARDS + 1];
static uint8_t  lastSeqRR = 0;

static RxHandler handler = nullptr;
static uint8_t   macSelf[6] = {0};
static uint32_t  cntTx = 0, cntTxFail = 0, cntRx = 0, cntRxDrop = 0;

// ---------------- callbacks (ESP-NOW task context) ----------------
static void onSent(const wifi_tx_info_t* info, esp_now_send_status_t status) {
  (void)info;
  txOk = (status == ESP_NOW_SEND_SUCCESS);
  txDone = true;
  sendBusy = false;
}

static void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (len < (int)sizeof(Hdr) || len > MAX_FRAME) return;
  const Hdr* h = (const Hdr*)data;
  if (h->magic != PROTO_MAGIC || h->ver != PROTO_VER) return;
  uint8_t next = (rxHead + 1) % RX_RING;
  if (next == rxTail) { cntRxDrop++; return; }        // ring full
  RxItem& it = rxRing[rxHead];
  memcpy(it.srcMac, info->src_addr, 6);
  it.len  = (uint8_t)len;
  it.rssi = info->rx_ctrl ? info->rx_ctrl->rssi : 0;
  memcpy(it.buf, data, len);
  rxHead = next;
}

// ---------------- init ----------------
static bool finishInit(bool hub) {
  WiFi.setSleep(false);
  uint8_t ch; wifi_second_chan_t sc; esp_wifi_get_channel(&ch, &sc);
  Serial.printf("[RADIO] Wi-Fi channel: %d (want %d) %s\n", ch, ESPNOW_CHANNEL, ch == ESPNOW_CHANNEL ? "OK" : "MISMATCH!");

  if (esp_now_init() != ESP_OK) { Serial.println("[RADIO] esp_now_init FAILED"); return false; }
  esp_now_register_send_cb(onSent);
  esp_now_register_recv_cb(onRecv);

  // Broadcast peer on both roles (node needs it to broadcast HELLO/CLAIM/FORGET)
  esp_now_peer_info_t peer = {}; memcpy(peer.peer_addr, BCAST, 6);
  peer.channel = ESPNOW_CHANNEL; peer.ifidx = WIFI_IF_STA; peer.encrypt = false;
  Serial.printf("[RADIO] Broadcast peer: %s\n", esp_now_add_peer(&peer) == ESP_OK ? "OK" : "FAIL");

  esp_read_mac(macSelf, ESP_MAC_WIFI_STA);
  char s[18]; macToStr(macSelf, s);
  Serial.printf("[RADIO] %s STA MAC %s\n", hub ? "HUB" : "NODE", s);
  return true;
}

static char apSsidBuf[33] = "";

bool beginHub(const char* ssid) {
  if (!ssid || !ssid[0]) ssid = AP_SSID;
  strncpy(apSsidBuf, ssid, sizeof(apSsidBuf) - 1);
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apSsidBuf, AP_PASS, ESPNOW_CHANNEL, 0, 2);
  Serial.printf("[RADIO] SoftAP %s at %s\n", apSsidBuf, WiFi.softAPIP().toString().c_str());
  return finishInit(true);
}

bool beginNode() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  return finishInit(false);
}

void setHandler(RxHandler fn) { handler = fn; }

// ---------------- peers ----------------
void addPeer(const uint8_t* mac) {
  if (esp_now_is_peer_exist(mac)) return;
  esp_now_peer_info_t peer = {}; memcpy(peer.peer_addr, mac, 6);
  peer.channel = ESPNOW_CHANNEL; peer.ifidx = WIFI_IF_STA; peer.encrypt = false;
  esp_err_t e = esp_now_add_peer(&peer);
  char s[18]; macToStr(mac, s);
  Serial.printf("[RADIO] addPeer %s %s\n", s, e == ESP_OK ? "OK" : "FAIL");
}

// ---------------- send queue ----------------
bool send(const uint8_t* mac, void* buf, uint8_t len, bool needsAck) {
  if (len < sizeof(Hdr) || len > MAX_FRAME) return false;
  Hdr* h = (Hdr*)buf;
  bool ok = false;
  portENTER_CRITICAL(&qMux);
  h->seq = ++txSeq;
  if (needsAck) h->flags |= FLAG_NEEDS_ACK; else h->flags &= ~FLAG_NEEDS_ACK;
  for (int i = 0; i < TX_SLOTS; i++) {
    if (txq[i].used) continue;
    TxItem& t = txq[i];
    t.used = true; t.inflight = false; t.needsAck = needsAck;
    memcpy(t.mac, mac, 6);
    t.len = len; t.tries = 0; t.seq = h->seq; t.order = ++txOrder; t.lastSend = 0;
    memcpy(t.buf, buf, len);
    ok = true;
    break;
  }
  portEXIT_CRITICAL(&qMux);
  if (!ok) { cntTxFail++; Serial.printf("[RADIO] send queue full, dropped type=%d\n", h->type); }
  return ok;
}

void ackReceived(const uint8_t* mac, uint16_t ackSeq) {
  portENTER_CRITICAL(&qMux);
  for (int i = 0; i < TX_SLOTS; i++) {
    TxItem& t = txq[i];
    if (t.used && t.needsAck && t.seq == ackSeq && memcmp(t.mac, mac, 6) == 0) {
      t.used = false;
      if (inflightIdx == i) inflightIdx = -1;
      break;
    }
  }
  portEXIT_CRITICAL(&qMux);
}

// Returns true if (mac, seq) was already seen. Frames that request an ACK are never
// treated as duplicates so a retried ASSIGN/SETPOS is re-ACKed when the first ACK was lost.
static bool isDuplicate(const uint8_t* mac, const Hdr* h) {
  if (h->flags & FLAG_NEEDS_ACK) return false;
  for (auto& e : lastSeq) {
    if (e.used && memcmp(e.mac, mac, 6) == 0) {
      if (e.seq == h->seq) return true;
      e.seq = h->seq; return false;
    }
  }
  SeqEntry& e = lastSeq[lastSeqRR];            // new MAC: take the next slot round-robin
  lastSeqRR = (lastSeqRR + 1) % (MAX_BOARDS + 1);
  e.used = true; memcpy(e.mac, mac, 6); e.seq = h->seq;
  return false;
}

static void pumpRx() {
  while (rxTail != rxHead) {
    RxItem it = rxRing[rxTail];                  // copy out, then release the slot
    rxTail = (rxTail + 1) % RX_RING;
    cntRx++;
    if (isDuplicate(it.srcMac, (const Hdr*)it.buf)) { cntRxDrop++; continue; }
    if (handler) handler(it.srcMac, it.buf, it.len, it.rssi);
  }
}

static void pumpTx() {
  uint32_t now = millis();

  // 1. Resolve the frame that just finished at the MAC layer.
  if (txDone) {
    bool ok = txOk; txDone = false;
    portENTER_CRITICAL(&qMux);
    if (inflightIdx >= 0 && txq[inflightIdx].used) {
      TxItem& t = txq[inflightIdx];
      t.inflight = false;
      if (!t.needsAck) t.used = false;           // fire-and-forget: done
      if (!ok) cntTxFail++;
    }
    inflightIdx = -1;
    portEXIT_CRITICAL(&qMux);
  }
  if (sendBusy) return;

  // 2. Pick the oldest eligible frame: never sent, or a needsAck retry whose timer expired.
  int pick = -1; uint32_t bestOrder = 0;
  uint8_t mac[6]; uint8_t buf[MAX_FRAME]; uint8_t len = 0;
  int gaveUp = 0; uint8_t gaveUpMac[6] = {0}; uint8_t gaveUpType = 0; uint16_t gaveUpSeq = 0;
  portENTER_CRITICAL(&qMux);
  for (int i = 0; i < TX_SLOTS; i++) {
    TxItem& t = txq[i];
    if (!t.used || t.inflight) continue;
    if (t.tries > 0 && now - t.lastSend < ACK_RETRY_MS) continue;
    if (t.tries >= ACK_RETRY) {                  // give up (logged outside the critical section)
      gaveUp++; memcpy(gaveUpMac, t.mac, 6); gaveUpType = ((Hdr*)t.buf)->type; gaveUpSeq = t.seq;
      t.used = false; cntTxFail++;
      continue;
    }
    if (pick < 0 || t.order < bestOrder) { pick = i; bestOrder = t.order; }
  }
  if (pick >= 0) {
    TxItem& t = txq[pick];
    if (t.tries > 0) ((Hdr*)t.buf)->flags |= FLAG_IS_RETRY;
    t.tries++; t.lastSend = now; t.inflight = true;
    inflightIdx = pick;
    memcpy(mac, t.mac, 6); memcpy(buf, t.buf, t.len); len = t.len;
    sendBusy = true;
  }
  portEXIT_CRITICAL(&qMux);
  if (gaveUp) {
    char s[18]; macToStr(gaveUpMac, s);
    Serial.printf("[RADIO] give up %s type=%d seq=%u after %d tries%s\n", s, gaveUpType, gaveUpSeq, ACK_RETRY, gaveUp > 1 ? " (+more)" : "");
  }
  if (pick < 0) return;

  cntTx++;
  if (esp_now_send(mac, buf, len) != ESP_OK) {   // immediate failure: callback will not fire
    cntTxFail++;
    sendBusy = false;
    portENTER_CRITICAL(&qMux);
    if (inflightIdx == pick) {
      txq[pick].inflight = false;
      if (!txq[pick].needsAck) txq[pick].used = false;
      inflightIdx = -1;
    }
    portEXIT_CRITICAL(&qMux);
  }
}

void pump() {
  pumpRx();
  pumpTx();
}

// ---------------- misc ----------------
bool channelOk() {
  uint8_t ch; wifi_second_chan_t sc; esp_wifi_get_channel(&ch, &sc);
  return ch == ESPNOW_CHANNEL;
}

const uint8_t* myMac() { return macSelf; }
const char*    apSsid() { return apSsidBuf; }

void macToStr(const uint8_t* mac, char* out) {
  snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

uint32_t txCount()     { return cntTx; }
uint32_t txFailCount() { return cntTxFail; }
uint32_t rxCount()     { return cntRx; }
uint32_t rxDropCount() { return cntRxDrop; }

} // namespace radio
