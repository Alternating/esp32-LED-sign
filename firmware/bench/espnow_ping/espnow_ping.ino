/*
 * Bench: espnow_ping — Phase 0 radio test (PROJECT_PLAN.md §7, Phase 0)
 * Board: LOLIN32, 20x20 WS2812B on GPIO 4
 *
 * One binary, two roles, role persisted in NVS:
 *   - Send 'H' or 'N' over serial within 3 s of boot to set HUB or NODE (saved; default NODE).
 * HUB : WIFI_AP_STA, SoftAP "LEDSIGN-BENCH" pinned to channel 6, tiny web page at 192.168.4.1,
 *       broadcasts PING at 5 Hz, counts PONGs, prints loss % and RTT. Matrix shows "H".
 * NODE: WIFI_STA on channel 6, replies PONG (unicast) to every PING, tracks missed seqs + RSSI.
 *       Matrix shows "N".
 * Both print a stats line every 5 s. Watch the matrix for flicker while the radio is busy.
 */
#include <FastLED.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include "character_patterns.h"

// ---------------- config ----------------
#define LEDS_PER_BOARD  400
#define MATRIX_WIDTH    20
#define MATRIX_HEIGHT   20
#define LED_PIN         4
#define BRIGHTNESS_PCT  10
#define ESPNOW_CHANNEL  6
#define PING_PERIOD_MS  200          // 5 Hz
#define STATS_PERIOD_MS 5000
const char* AP_SSID = "LEDSIGN-BENCH";
const char* AP_PASS = "sign1234";

// ---------------- protocol (subset of plan §4.1) ----------------
#pragma pack(push, 1)
constexpr uint8_t PROTO_MAGIC = 0xA5, PROTO_VER = 1;
enum MsgType : uint8_t { PING = 11, PONG = 12 };
struct Hdr  { uint8_t magic, ver, type, flags; uint16_t seq; uint8_t srcPos, dstPos; };
struct Ping { Hdr h; uint32_t hubMillis; };
struct Pong { Hdr h; uint32_t hubMillis; int8_t rssiAtNode; uint32_t nodeFreeHeap; };
#pragma pack(pop)

// ---------------- state ----------------
enum Role : uint8_t { NODE = 0, HUB = 1 };
Role role = NODE;
Preferences prefs;
CRGB leds[LEDS_PER_BOARD];
WebServer server(80);
const uint8_t BCAST[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
uint8_t hubMac[6] = {0}; bool hubKnown = false;

// hub stats
volatile uint32_t pingsSent = 0, pongsRx = 0, sendFail = 0;
volatile uint32_t rttSum = 0, rttMin = 0xFFFFFFFF, rttMax = 0;
volatile int8_t lastNodeRssi = 0, lastHubRssi = 0;
volatile uint32_t nodeHeap = 0;
// node stats
volatile uint32_t pingsRx = 0, missed = 0; volatile uint16_t lastSeq = 0; bool haveSeq = false;
volatile uint16_t txSeq = 0;
volatile bool sendBusy = false;
unsigned long lastPing = 0, lastStats = 0, showT = 0;

// ---------------- display ----------------
void renderChar(char c, CRGB color) {
  const bool* p = getCharacterPattern(c);
  for (int row = 0; row < MATRIX_HEIGHT; row++)
    for (int col = 0; col < MATRIX_WIDTH; col++) {
      int bi = row * MATRIX_WIDTH + (MATRIX_WIDTH - 1 - col);
      int li = (row % 2 == 0) ? row * MATRIX_WIDTH + col : row * MATRIX_WIDTH + (MATRIX_WIDTH - 1 - col);
      leds[li] = p[bi] ? color : CRGB::Black;
    }
}

// ---------------- ESP-NOW callbacks ----------------
void onSent(const wifi_tx_info_t* info, esp_now_send_status_t status) {
  sendBusy = false;
  if (status != ESP_NOW_SEND_SUCCESS) sendFail++;
}

void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (len < (int)sizeof(Hdr)) return;
  const Hdr* h = (const Hdr*)data;
  if (h->magic != PROTO_MAGIC || h->ver != PROTO_VER) return;
  int8_t rssi = info->rx_ctrl ? info->rx_ctrl->rssi : 0;

  if (role == NODE && h->type == PING && len >= (int)sizeof(Ping)) {
    const Ping* p = (const Ping*)data;
    pingsRx++; lastHubRssi = rssi;
    if (haveSeq) { uint16_t gap = (uint16_t)(h->seq - lastSeq - 1); if (gap < 1000) missed += gap; }
    lastSeq = h->seq; haveSeq = true;
    if (!hubKnown) {
      memcpy(hubMac, info->src_addr, 6);
      esp_now_peer_info_t peer = {}; memcpy(peer.peer_addr, hubMac, 6);
      peer.channel = ESPNOW_CHANNEL; peer.ifidx = WIFI_IF_STA; peer.encrypt = false;
      if (esp_now_add_peer(&peer) == ESP_OK) { hubKnown = true; Serial.printf("Hub learned: %02X:%02X:%02X:%02X:%02X:%02X\n", hubMac[0],hubMac[1],hubMac[2],hubMac[3],hubMac[4],hubMac[5]); }
    }
    Pong r; r.h = {PROTO_MAGIC, PROTO_VER, PONG, 0, h->seq, 2, 0};
    r.hubMillis = p->hubMillis; r.rssiAtNode = rssi; r.nodeFreeHeap = ESP.getFreeHeap();
    esp_now_send(hubMac, (uint8_t*)&r, sizeof(r));   // reply immediately from callback context is OK for a test
  }
  else if (role == HUB && h->type == PONG && len >= (int)sizeof(Pong)) {
    const Pong* r = (const Pong*)data;
    uint32_t rtt = millis() - r->hubMillis;
    pongsRx++; rttSum += rtt; if (rtt < rttMin) rttMin = rtt; if (rtt > rttMax) rttMax = rtt;
    lastNodeRssi = r->rssiAtNode; lastHubRssi = rssi; nodeHeap = r->nodeFreeHeap;
  }
}

// ---------------- setup ----------------
void pickRole() {
  prefs.begin("bench", false);
  role = (Role)prefs.getUChar("role", NODE);
  Serial.printf("Role in NVS: %s. Send H or N within 3 s to change...\n", role == HUB ? "HUB" : "NODE");
  unsigned long t0 = millis();
  while (millis() - t0 < 3000) {
    if (Serial.available()) {
      char k = toupper(Serial.read());
      if (k == 'H' || k == 'N') { role = (k == 'H') ? HUB : NODE; prefs.putUChar("role", role); Serial.printf("Role set to %s (saved)\n", role == HUB ? "HUB" : "NODE"); break; }
    }
    delay(10);
  }
  prefs.end();
}

void radioInit() {
  WiFi.persistent(false);
  if (role == HUB) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID, AP_PASS, ESPNOW_CHANNEL, 0, 2);
    Serial.printf("SoftAP %s up at %s on channel %d\n", AP_SSID, WiFi.softAPIP().toString().c_str(), WiFi.channel());
    server.on("/", []() { server.send(200, "text/plain", String("LEDSIGN bench hub OK. pings=") + pingsSent + " pongs=" + pongsRx); });
    server.begin();
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true, true);
    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  }
  WiFi.setSleep(false);
  uint8_t ch; wifi_second_chan_t sc; esp_wifi_get_channel(&ch, &sc);
  Serial.printf("Wi-Fi channel: %d (want %d) %s\n", ch, ESPNOW_CHANNEL, ch == ESPNOW_CHANNEL ? "OK" : "MISMATCH!");

  if (esp_now_init() != ESP_OK) { Serial.println("esp_now_init FAILED"); return; }
  esp_now_register_send_cb(onSent);
  esp_now_register_recv_cb(onRecv);
  if (role == HUB) {
    esp_now_peer_info_t peer = {}; memcpy(peer.peer_addr, BCAST, 6);
    peer.channel = ESPNOW_CHANNEL; peer.ifidx = WIFI_IF_STA; peer.encrypt = false;
    Serial.printf("Broadcast peer: %s\n", esp_now_add_peer(&peer) == ESP_OK ? "OK" : "FAIL");
  }
  uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_STA);
  Serial.printf("My STA MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
}

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println("\n=== espnow_ping bench ===");
  pickRole();
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, LEDS_PER_BOARD);
  FastLED.setBrightness(map(BRIGHTNESS_PCT, 0, 100, 0, 255));
  renderChar(role == HUB ? 'H' : 'N', role == HUB ? CRGB::Green : CRGB::Blue);
  FastLED.show();
  radioInit();
  Serial.printf("Running as %s. Free heap %u\n\n", role == HUB ? "HUB" : "NODE", ESP.getFreeHeap());
  lastStats = millis();
}

// ---------------- loop ----------------
void loop() {
  unsigned long now = millis();
  if (role == HUB) {
    server.handleClient();
    if (now - lastPing >= PING_PERIOD_MS && !sendBusy) {
      lastPing = now;
      Ping p; p.h = {PROTO_MAGIC, PROTO_VER, PING, 0, ++txSeq, 0, 0xFF}; p.hubMillis = now;
      sendBusy = true;
      if (esp_now_send(BCAST, (uint8_t*)&p, sizeof(p)) == ESP_OK) pingsSent++; else { sendFail++; sendBusy = false; }
    }
  }
  // keep refreshing the matrix at ~30 Hz so radio-induced glitches would be visible
  if (now - showT >= 33) { showT = now; FastLED.show(); }

  if (now - lastStats >= STATS_PERIOD_MS) {
    lastStats = now;
    if (role == HUB) {
      float loss = pingsSent ? 100.0f * (1.0f - (float)pongsRx / pingsSent) : 0;
      Serial.printf("[HUB] sent=%u pong=%u loss=%.1f%% txfail=%u rtt min/avg/max=%u/%.1f/%u ms rssi node<-hub=%d hub<-node=%d nodeHeap=%u myHeap=%u clients=%d\n",
        pingsSent, pongsRx, loss, sendFail, rttMin == 0xFFFFFFFF ? 0 : rttMin, pongsRx ? (float)rttSum / pongsRx : 0.0f, rttMax,
        lastNodeRssi, lastHubRssi, nodeHeap, ESP.getFreeHeap(), WiFi.softAPgetStationNum());
    } else {
      Serial.printf("[NODE] pingsRx=%u missed=%u lastSeq=%u rssi=%d hub=%s heap=%u\n",
        pingsRx, missed, lastSeq, lastHubRssi, hubKnown ? "known" : "none", ESP.getFreeHeap());
    }
  }
  delay(1);
}
