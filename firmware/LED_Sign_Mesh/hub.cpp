#include "hub.h"
#include "protocol.h"
#include "radio_espnow.h"
#include "storage.h"
#include "display.h"
#include "buttons.h"
#include <WiFi.h>
#include <string.h>

// State machine:
//   BOOT ----begin()----> SHOW_POSITION ("1" green for SHOW_POSITION_MIN) ----> RUNNING
// RUNNING: periodic Show every 500 ms (heartbeat); Show burst x3 (0/60/250 ms) on content
// change or after an ASSIGN; HELLO/CLAIM -> ASSIGN (needsAck, retried by radio); ACK ->
// radio::ackReceived; HB -> lastSeen; FORGET -> free slot. RESETORD burst and the
// identify flash are timers serviced in tick(), never delays.

namespace hub {

static const uint8_t MY_POS = 1;

static volatile HubState st = HubState::BOOT;
static uint32_t stateEnteredAt = 0;

// Table: unordered set of {mac, pos}, pos 0 = empty slot. Written from handlePacket
// (core 0) and from onResetOrd (core 1); guarded by tMux.
static storage::TableEntry table[MAX_BOARDS];
static uint8_t  nextPosCached = 2;
static uint32_t lastSeen[MAX_BOARDS + 1];       // millis of last HELLO/HB per pos
static int8_t   lastRssi[MAX_BOARDS + 1];
static uint16_t lastVbat[MAX_BOARDS + 1];
static portMUX_TYPE tMux = portMUX_INITIALIZER_UNLOCKED;

// Show content
static char     testString[MAX_BOARDS] = {0};
static volatile uint16_t showSeq = 1;
static CRGB     showColor = CRGB(0, 255, 0);
static char     renderedChar = 0;

// Timers (tick)
static uint32_t lastPeriodicAt = 0;
static volatile uint8_t  burstLeft = 0;
static volatile uint32_t burstNextAt = 0;
static uint8_t  resetLeft = 0;
static uint32_t resetNextAt = 0;
static uint32_t statusLedAt = 0;

// ---------------- helpers ----------------
const char* stateName(HubState s) {
  switch (s) {
    case HubState::BOOT:          return "BOOT";
    case HubState::SHOW_POSITION: return "SHOW_POSITION";
    case HubState::RUNNING:       return "RUNNING";
  }
  return "?";
}

static void setState(HubState s) {
  if (s == st) return;
  Serial.printf("[HUB] %s -> %s\n", stateName(st), stateName(s));
  st = s;
  stateEnteredAt = millis();
}

HubState state() { return st; }
uint8_t  nextPos() { return nextPosCached; }

static bool owned(uint8_t pos) {
  for (auto& e : table) if (e.pos == pos) return true;
  return false;
}

// lowest free position >= 2 (MAX_BOARDS + 1 when every position is owned)
static uint8_t computeNextPos() {
  uint8_t p = 2;
  while (p <= MAX_BOARDS && owned(p)) p++;
  return p;
}

static uint8_t highestOwned() {
  uint8_t m = MY_POS;
  for (auto& e : table) if (e.pos > m) m = e.pos;
  return m;
}

static int findMac(const uint8_t* mac) {
  for (int i = 0; i < MAX_BOARDS; i++)
    if (table[i].pos != 0 && memcmp(table[i].mac, mac, 6) == 0) return i;
  return -1;
}

static int freeSlot() {
  for (int i = 0; i < MAX_BOARDS; i++) if (table[i].pos == 0) return i;
  return -1;
}

static void persistTable() {
  nextPosCached = computeNextPos();
  storage::saveTable(table, nextPosCached);
}

static void scheduleBurst() {
  burstLeft = 3;
  burstNextAt = millis();
}

// Build and queue one Show frame. Always the full 40 B.
static void sendShow(bool periodic) {
  Show s;
  s.h = makeHdr(SHOW, MY_POS, POS_ALL, periodic ? FLAG_PERIODIC : 0);
  s.hubMillis = millis();
  s.applyAtHubMs = 0;
  s.showSeq = showSeq;
  s.r = showColor.r; s.g = showColor.g; s.b = showColor.b;
  s.brightness = 100;
  s.effect = 0;
  s.effectPeriodMs = 0;
  s.wordPeriodMs = 2000;
  s.numBoards = highestOwned();
  s.wordIdx = 0; s.wordCount = 1;
  memcpy(s.chars, testString, MAX_BOARDS);
  radio::send(radio::BCAST, &s, sizeof(s), false);
  lastPeriodicAt = millis();
}

static void sendAssign(const uint8_t* mac, uint8_t pos) {
  Assign a;
  a.h = makeHdr(ASSIGN, MY_POS, pos);
  a.pos = pos; a.channel = ESPNOW_CHANNEL; a.hubMillis = millis(); a.hubPos = MY_POS;
  radio::send(mac, &a, sizeof(a), true);
}

// ---------------- numbering ----------------
// HELLO: known MAC -> its stored pos. Unknown MAC: nvsPos accepted if free (hub-reflash
// case), else lowest free >= 2; table full -> ASSIGN pos 0. CLAIM = HELLO with nvsPos 0.
static void onHello(const uint8_t* mac, uint8_t nvsPos, bool claim) {
  char ms[18]; radio::macToStr(mac, ms);
  radio::addPeer(mac);

  uint8_t pos = 0; const char* how = "known";
  portENTER_CRITICAL(&tMux);
  int idx = findMac(mac);
  if (idx >= 0) {
    pos = table[idx].pos;
  } else {
    how = "new";
    if (!claim && nvsPos >= 2 && nvsPos <= MAX_BOARDS && !owned(nvsPos)) pos = nvsPos;
    else { uint8_t np = computeNextPos(); pos = (np <= MAX_BOARDS) ? np : 0; }
    if (pos) {
      int slot = freeSlot();
      if (slot >= 0) { memcpy(table[slot].mac, mac, 6); table[slot].pos = pos; }
      else pos = 0;   // cannot happen (positions and slots are both MAX_BOARDS), kept defensive
    }
  }
  if (pos) lastSeen[pos] = millis();
  portEXIT_CRITICAL(&tMux);

  if (idx < 0 && pos) persistTable();

  if (pos == 0) {
    Serial.printf("[HUB] %s %s: table full -> ASSIGN pos 0\n", claim ? "CLAIM" : "HELLO", ms);
    sendAssign(mac, 0);
    return;
  }
  Serial.printf("[HUB] ASSIGN %s -> pos %d (%s)%s\n", ms, pos, how, claim ? " [claim]" : "");
  sendAssign(mac, pos);
  scheduleBurst();          // late joiner lights up right away
}

static void onForget(const uint8_t* mac) {
  char ms[18]; radio::macToStr(mac, ms);
  bool changed = false; uint8_t pos = 0;
  portENTER_CRITICAL(&tMux);
  int idx = findMac(mac);
  if (idx >= 0) { pos = table[idx].pos; memset(&table[idx], 0, sizeof(table[idx])); changed = true; }
  portEXIT_CRITICAL(&tMux);
  if (changed) { persistTable(); Serial.printf("[HUB] FORGET %s freed pos %d\n", ms, pos); }
  else Serial.printf("[HUB] FORGET %s (unknown MAC)\n", ms);
}

// ---------------- RX (core 0) ----------------
void handlePacket(const uint8_t* srcMac, const uint8_t* buf, uint8_t len, int8_t rssi) {
  const Hdr* h = (const Hdr*)buf;
  switch (h->type) {
    case HELLO:
      if (len >= sizeof(Hello)) {
        const Hello* m = (const Hello*)buf;
        onHello(srcMac, m->nvsPos, false);
        if (m->nvsPos && m->nvsPos <= MAX_BOARDS) { lastRssi[m->nvsPos] = rssi; lastVbat[m->nvsPos] = m->vbat_mV; }
      }
      break;
    case CLAIM:
      onHello(srcMac, 0, true);
      break;
    case ACK:
      if (len >= sizeof(Ack)) {
        const Ack* a = (const Ack*)buf;
        radio::ackReceived(srcMac, a->ackSeq);
        if (a->pos <= MAX_BOARDS) lastSeen[a->pos] = millis();
      }
      break;
    case HB:
      if (len >= sizeof(Hb)) {
        const Hb* hb = (const Hb*)buf;
        uint8_t p = h->srcPos;
        if (p >= 1 && p <= MAX_BOARDS) { lastSeen[p] = millis(); lastRssi[p] = rssi; lastVbat[p] = hb->vbat_mV; }
      }
      break;
    case FORGET:
      onForget(srcMac);
      break;
    case SHOW: {
      char ms[18]; radio::macToStr(srcMac, ms);
      Serial.printf("[HUB] other hub heard: %s (CONFLICT handling deferred)\n", ms);
      break;
    }
    default:
      break;
  }
}

// ---------------- actions ----------------
void onResetOrd() {
  portENTER_CRITICAL(&tMux);
  memset(table, 0, sizeof(table));
  memset(lastSeen, 0, sizeof(lastSeen));
  portEXIT_CRITICAL(&tMux);
  storage::clearTable();
  persistTable();                       // nextPos back to 2
  resetLeft = RESET_ORD_BCAST;          // 5 broadcasts, 200 ms apart, from tick()
  resetNextAt = millis();
  Serial.printf("[HUB] RESET ORDER: table cleared, nextPos=%d, broadcasting RESETORD x%d\n", nextPosCached, RESET_ORD_BCAST);
}

void onIdentifyAll() {
  Identify id;
  id.h = makeHdr(IDENTIFY, MY_POS, POS_ALL);
  id.ms = IDENTIFY_MS;
  radio::send(radio::BCAST, &id, sizeof(id), false);
  display::identifyStart(IDENTIFY_MS);  // hub flashes too
  Serial.printf("[HUB] IDENTIFY ALL %d ms\n", IDENTIFY_MS);
}

void setTestString(const char* s) {
  memset(testString, 0, sizeof(testString));
  size_t n = strlen(s);
  if (n > MAX_BOARDS) n = MAX_BOARDS;
  memcpy(testString, s, n);
  showSeq = showSeq + 1;
  scheduleBurst();
  Serial.printf("[HUB] test string \"%.*s\" showSeq=%u\n", (int)n, s, (unsigned)showSeq);
}

// ---------------- lifecycle ----------------
void begin() {
  uint8_t storedNext;
  storage::loadTable(table, storedNext);
  nextPosCached = computeNextPos();     // never trust the stored counter
  memset(lastSeen, 0, sizeof(lastSeen));
  int n = 0; for (auto& e : table) if (e.pos) n++;
  Serial.printf("[HUB] table loaded: %d boards, nextPos=%d\n", n, nextPosCached);

  buttons::onLong(K3_PIN, onResetOrd, LONG_PRESS_HUB_MS);
  buttons::onLong(K2_PIN, onIdentifyAll, LONG_PRESS_HUB_MS);
  pinMode(STATUS_PIN, OUTPUT);

  setState(HubState::SHOW_POSITION);
  Serial.printf("[HUB] showing position %d for %d ms\n", MY_POS, SHOW_POSITION_MIN);
}

void tick() {
  uint32_t now = millis();

  if (st == HubState::SHOW_POSITION && now - stateEnteredAt >= SHOW_POSITION_MIN)
    setState(HubState::RUNNING);

  // RESETORD burst
  if (resetLeft && (int32_t)(now - resetNextAt) >= 0) {
    Hdr h = makeHdr(RESETORD, MY_POS, POS_ALL);
    radio::send(radio::BCAST, &h, sizeof(h), false);
    resetLeft--;
    resetNextAt = now + RESET_ORD_GAP;
  }

  // Show: burst on change (0/60/250 ms) and periodic heartbeat every 500 ms
  if (burstLeft && (int32_t)(now - burstNextAt) >= 0) {
    sendShow(false);
    burstLeft = burstLeft - 1;
    burstNextAt = now + (burstLeft == 2 ? SHOW_BURST_GAP1 : SHOW_BURST_GAP2);
  } else if (now - lastPeriodicAt >= SHOW_HEARTBEAT) {
    sendShow(true);
  }

  // Status LED: blink in SHOW_POSITION, solid in RUNNING
  if (st == HubState::RUNNING) digitalWrite(STATUS_PIN, HIGH);
  else if (now - statusLedAt >= 500) { statusLedAt = now; digitalWrite(STATUS_PIN, !digitalRead(STATUS_PIN)); }
}

void render() {
  if (display::identifyActive()) { display::identifyRender(); return; }
  if (st == HubState::SHOW_POSITION) { display::renderDigit(MY_POS, CRGB::Green); return; }
  char c = testString[0];
  if (c != renderedChar) {
    renderedChar = c;
    if (c >= 32) Serial.printf("[HUB] render '%c'\n", c); else Serial.printf("[HUB] render code %d\n", c);
  }
  if (c == 0) display::clear(); else display::renderChar(c, showColor);
}

void printStatus() {
  char ms[18];
  Serial.printf("role=HUB pos=%d state=%s nextPos=%d numBoards=%d showSeq=%u\n",
                MY_POS, stateName(st), nextPosCached, highestOwned(), (unsigned)showSeq);
  Serial.printf("test=\"%.*s\" mirror=%d\n", MAX_BOARDS, testString, display::mirror);
  uint32_t now = millis();
  int n = 0;
  for (auto& e : table) {
    if (!e.pos) continue;
    n++;
    radio::macToStr(e.mac, ms);
    uint32_t age = lastSeen[e.pos] ? (now - lastSeen[e.pos]) / 1000 : 0;
    Serial.printf("  %s -> pos %d  %s age=%us rssi=%d vbat=%u\n", ms, e.pos,
                  (lastSeen[e.pos] && now - lastSeen[e.pos] < OFFLINE_AFTER_MS) ? "online " : "offline",
                  (unsigned)age, lastRssi[e.pos], lastVbat[e.pos]);
  }
  if (!n) Serial.println("  (table empty)");
}

} // namespace hub
