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
// radio::ackReceived; HB -> BoardInfo telemetry; FORGET -> free slot. RESETORD burst and the
// identify flash are timers serviced in tick(), never delays.
//
// Sign state: `live` is owned by core 1 (tick/render/sendShow). Commands (any core) write
// `pending` under signMux and raise pendingFlag; tick() swaps it in, lays the text out,
// bumps showSeq, bursts, and arms the 2 s auto-save. NVS writes for the sign happen in tick().

namespace hub {

static const uint8_t MY_POS = 1;

static volatile HubState st = HubState::BOOT;
static uint32_t stateEnteredAt = 0;

// Table: unordered set of {mac, pos}, pos 0 = empty slot. Written from handlePacket
// (core 0), onResetOrd (core 1) and forgetPos (any core); guarded by tMux.
static storage::TableEntry table[MAX_BOARDS];
static uint8_t  nextPosCached = 2;

struct BoardInfo {              // indexed by pos 1..MAX_BOARDS; all-zero = never heard
  uint32_t lastHb;              // millis of last HB (drives age/online)
  uint32_t lastSeen;            // HELLO/ACK/HB (diagnostics)
  int8_t   rssi;
  uint16_t mv;
  uint8_t  shownChar;
  uint8_t  nodeState;
  uint16_t lastShowSeq;
};
static BoardInfo info[MAX_BOARDS + 1];          // guarded by tMux like table[]
static portMUX_TYPE tMux = portMUX_INITIALIZER_UNLOCKED;

// Sign state and the core-0 -> core-1 handover
static SignState live;                          // core 1 only
static char      liveChars[MAX_BOARDS] = {0};   // layout of live.text, core 1 only
static SignState pending;
static volatile bool pendingFlag = false;
static portMUX_TYPE signMux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t dirtyAt = 0;           // auto-save timer; 1 = save on next tick

static volatile uint16_t showSeq = 1;
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

uint8_t numBoardsEffective() {
  uint8_t pinned = sign().boards;
  return pinned ? pinned : highestOwned();
}

static int findMac(const uint8_t* mac) {
  for (int i = 0; i < MAX_BOARDS; i++)
    if (table[i].pos != 0 && memcmp(table[i].mac, mac, 6) == 0) return i;
  return -1;
}

static int findPos(uint8_t pos) {
  for (int i = 0; i < MAX_BOARDS; i++) if (table[i].pos == pos) return i;
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

// ---------------- sign state handover ----------------
void applySign(const SignState& s) {
  portENTER_CRITICAL(&signMux);
  pending = s;
  pendingFlag = true;
  portEXIT_CRITICAL(&signMux);
}

SignState sign() {
  SignState c;
  portENTER_CRITICAL(&signMux);
  c = pendingFlag ? pending : live;
  portEXIT_CRITICAL(&signMux);
  return c;
}

void saveNow() { dirtyAt = 1; }

// Build and queue one Show frame. Always the full 40 B. Core 1 only.
static void sendShow(bool periodic) {
  Show s;
  s.h = makeHdr(SHOW, MY_POS, POS_ALL, periodic ? FLAG_PERIODIC : 0);
  s.hubMillis = millis();
  s.applyAtHubMs = 0;
  s.showSeq = showSeq;
  s.r = live.r; s.g = live.g; s.b = live.b;
  s.brightness = signBriEff(live);
  s.effect = live.effect;
  s.effectPeriodMs = live.effectPeriodMs;
  s.wordPeriodMs = 2000;
  s.numBoards = live.boards ? live.boards : highestOwned();
  s.wordIdx = 0; s.wordCount = 1;
  memcpy(s.chars, liveChars, MAX_BOARDS);
  radio::send(radio::BCAST, &s, sizeof(s), false);
  lastPeriodicAt = millis();
  if (!periodic)
    Serial.printf("[SHOW] seq=%u color=%02X%02X%02X bri=%u fx=%u\n",
                  (unsigned)s.showSeq, s.r, s.g, s.b, s.brightness, s.effect);
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
static void onHello(const uint8_t* mac, uint8_t nvsPos, bool claim, int8_t rssi, uint16_t mv) {
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
      if (slot >= 0) { memcpy(table[slot].mac, mac, 6); table[slot].pos = pos; memset(&info[pos], 0, sizeof(BoardInfo)); }
      else pos = 0;   // cannot happen (positions and slots are both MAX_BOARDS), kept defensive
    }
  }
  if (pos) { info[pos].lastSeen = millis(); info[pos].rssi = rssi; if (mv) info[pos].mv = mv; }
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
  if (idx >= 0) {
    pos = table[idx].pos;
    memset(&table[idx], 0, sizeof(table[idx]));
    memset(&info[pos], 0, sizeof(BoardInfo));
    changed = true;
  }
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
        onHello(srcMac, m->nvsPos, false, rssi, m->vbat_mV);
      }
      break;
    case CLAIM:
      onHello(srcMac, 0, true, rssi, 0);
      break;
    case ACK:
      if (len >= sizeof(Ack)) {
        const Ack* a = (const Ack*)buf;
        radio::ackReceived(srcMac, a->ackSeq);
        if (a->pos >= 1 && a->pos <= MAX_BOARDS) {
          portENTER_CRITICAL(&tMux);
          info[a->pos].lastSeen = millis(); info[a->pos].rssi = rssi;
          portEXIT_CRITICAL(&tMux);
        }
      }
      break;
    case HB:
      if (len >= sizeof(Hb)) {
        const Hb* hb = (const Hb*)buf;
        uint8_t p = h->srcPos;
        if (p >= 1 && p <= MAX_BOARDS) {
          portENTER_CRITICAL(&tMux);
          BoardInfo& bi = info[p];
          bi.lastHb = bi.lastSeen = millis();
          bi.rssi = rssi; bi.mv = hb->vbat_mV; bi.shownChar = hb->shownChar;
          bi.nodeState = hb->state; bi.lastShowSeq = hb->lastShowSeq;
          portEXIT_CRITICAL(&tMux);
        }
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
  memset(info, 0, sizeof(info));
  portEXIT_CRITICAL(&tMux);
  storage::clearTable();
  persistTable();                       // nextPos back to 2
  resetLeft = RESET_ORD_BCAST;          // 5 broadcasts, 200 ms apart, from tick()
  resetNextAt = millis();
  Serial.printf("[HUB] RESET ORDER: table cleared, nextPos=%d, broadcasting RESETORD x%d\n", nextPosCached, RESET_ORD_BCAST);
}

static void identifyAll(uint16_t ms) {
  Identify id;
  id.h = makeHdr(IDENTIFY, MY_POS, POS_ALL);
  id.ms = ms;
  radio::send(radio::BCAST, &id, sizeof(id), false);
  display::identifyStart(ms);           // hub flashes too
  Serial.printf("[HUB] IDENTIFY ALL %u ms\n", ms);
}

void onIdentifyAll() { identifyAll(IDENTIFY_MS); }

bool identifyPos(uint8_t pos, uint16_t ms) {
  ms = constrain(ms, (uint16_t)200, (uint16_t)10000);
  if (pos == POS_ALL) { identifyAll(ms); return true; }
  if (pos == MY_POS) { display::identifyStart(ms); Serial.printf("[HUB] IDENTIFY self %u ms\n", ms); return true; }
  uint8_t mac[6]; bool found = false;
  portENTER_CRITICAL(&tMux);
  int idx = findPos(pos);
  if (idx >= 0) { memcpy(mac, table[idx].mac, 6); found = true; }
  portEXIT_CRITICAL(&tMux);
  if (!found) return false;
  Identify id;
  id.h = makeHdr(IDENTIFY, MY_POS, pos);
  id.ms = ms;
  radio::send(mac, &id, sizeof(id), false);
  char ms18[18]; radio::macToStr(mac, ms18);
  Serial.printf("[HUB] IDENTIFY pos %d (%s) %u ms\n", pos, ms18, ms);
  return true;
}

bool forgetPos(uint8_t pos) {
  if (pos == MY_POS || pos < 1 || pos > MAX_BOARDS) return false;
  uint8_t mac[6]; bool found = false;
  portENTER_CRITICAL(&tMux);
  int idx = findPos(pos);
  if (idx >= 0) {
    memcpy(mac, table[idx].mac, 6);
    memset(&table[idx], 0, sizeof(table[idx]));
    memset(&info[pos], 0, sizeof(BoardInfo));
    found = true;
  }
  portEXIT_CRITICAL(&tMux);
  if (!found) return false;
  Hdr h = makeHdr(FORGET, MY_POS, pos);
  radio::send(mac, &h, sizeof(h), false);
  persistTable();
  char ms18[18]; radio::macToStr(mac, ms18);
  Serial.printf("[HUB] forget pos %d (%s): row removed, FORGET sent\n", pos, ms18);
  scheduleBurst();                      // numBoards may have shrunk
  return true;
}

// ---------------- /state ----------------
static void putChar(JsonObject o, char c) {
  const char* sym = symbolName(c);
  if (sym) { char buf[18]; snprintf(buf, sizeof(buf), "{%s}", sym); o["char"] = buf; }
  else if (c >= 32) { char buf[2] = { c, 0 }; o["char"] = buf; }
  else o["char"] = "";
}

void buildState(JsonObject root) {
  // Snapshot everything shared under the locks, then serialise lock-free.
  storage::TableEntry tbl[MAX_BOARDS];
  BoardInfo inf[MAX_BOARDS + 1];
  portENTER_CRITICAL(&tMux);
  memcpy(tbl, table, sizeof(tbl));
  memcpy(inf, info, sizeof(inf));
  portEXIT_CRITICAL(&tMux);
  SignState s = sign();
  char chars[MAX_BOARDS];
  signLayout(s, chars);
  uint32_t now = millis();

  JsonObject cfg = root["cfg"].to<JsonObject>();
  cfg["text"] = s.text;
  char hex[7]; snprintf(hex, sizeof(hex), "%02X%02X%02X", s.r, s.g, s.b);
  cfg["color"] = hex;
  cfg["rainbow"] = s.rainbow;
  cfg["bri"] = s.bri;
  cfg["briEff"] = signBriEff(s);
  cfg["fx"] = fxName(s.effect);
  cfg["speed"] = s.effectPeriodMs;
  uint8_t highest = MY_POS; for (auto& e : tbl) if (e.pos > highest) highest = e.pos;
  cfg["boards"] = s.boards ? s.boards : highest;
  cfg["boardsAuto"] = (s.boards == 0);
  cfg["name"] = radio::apSsid();
  cfg["fw"] = FW_VERSION;

  JsonObject state = root["state"].to<JsonObject>();
  state["hubPos"] = MY_POS;
  state["numBoards"] = s.boards ? s.boards : highest;
  state["nextPos"] = nextPosCached;
  state["clients"] = WiFi.softAPgetStationNum();
  state["heap"] = ESP.getFreeHeap();
  state["uptime"] = now / 1000;
  state["hubState"] = stateName(st);

  JsonArray boards = state["boards"].to<JsonArray>();
  char macs[18], id[5];
  // hub row first
  {
    const uint8_t* mm = radio::myMac();
    radio::macToStr(mm, macs);
    snprintf(id, sizeof(id), "%02X%02X", mm[4], mm[5]);
    JsonObject b = boards.add<JsonObject>();
    b["pos"] = MY_POS; b["id"] = id; b["mac"] = macs; b["self"] = true;
    b["online"] = true; b["age"] = 0; b["mv"] = 0; b["rssi"] = 0;
    putChar(b, chars[0]);
  }
  for (uint8_t pos = 2; pos <= MAX_BOARDS; pos++) {           // sorted by pos
    int idx = -1;
    for (int i = 0; i < MAX_BOARDS; i++) if (tbl[i].pos == pos) { idx = i; break; }
    if (idx < 0) continue;
    const BoardInfo& bi = inf[pos];
    radio::macToStr(tbl[idx].mac, macs);
    snprintf(id, sizeof(id), "%02X%02X", tbl[idx].mac[4], tbl[idx].mac[5]);
    JsonObject b = boards.add<JsonObject>();
    b["pos"] = pos; b["id"] = id; b["mac"] = macs; b["self"] = false;
    b["online"] = bi.lastHb != 0 && (now - bi.lastHb) < OFFLINE_AFTER_MS;
    b["age"] = bi.lastHb ? (int32_t)((now - bi.lastHb) / 1000) : -1;
    b["mv"] = bi.mv; b["rssi"] = bi.rssi;
    putChar(b, (char)bi.shownChar);
  }
}

// ---------------- lifecycle ----------------
void begin() {
  uint8_t storedNext;
  storage::loadTable(table, storedNext);
  nextPosCached = computeNextPos();     // never trust the stored counter
  memset(info, 0, sizeof(info));
  int n = 0; for (auto& e : table) if (e.pos) n++;
  Serial.printf("[HUB] table loaded: %d boards, nextPos=%d\n", n, nextPosCached);

  storage::loadSign(live);              // restored before the first Show
  signLayout(live, liveChars);
  pending = live;
  Serial.printf("[HUB] sign loaded: text=\"%s\" color=%02X%02X%02X bri=%u fx=%s speed=%u boards=%u\n",
                live.text, live.r, live.g, live.b, live.bri, fxName(live.effect), live.effectPeriodMs, live.boards);

  buttons::onLong(K3_PIN, onResetOrd, LONG_PRESS_HUB_MS);
  buttons::onLong(K2_PIN, onIdentifyAll, LONG_PRESS_HUB_MS);
  pinMode(STATUS_PIN, OUTPUT);

  setState(HubState::SHOW_POSITION);
  Serial.printf("[HUB] showing position %d for %d ms\n", MY_POS, SHOW_POSITION_MIN);
}

void tick() {
  uint32_t now = millis();

  // Apply a queued sign change (core-0 commands -> core-1 Show)
  if (pendingFlag) {
    portENTER_CRITICAL(&signMux);
    live = pending;
    pendingFlag = false;
    portEXIT_CRITICAL(&signMux);
    signLayout(live, liveChars);
    showSeq = showSeq + 1;
    scheduleBurst();
    dirtyAt = now ? now : 1;
  }
  if (dirtyAt && (dirtyAt == 1 || now - dirtyAt >= 2000)) {
    dirtyAt = 0;
    storage::saveSign(live);
    Serial.println("[HUB] sign saved");
  }

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
  char c = liveChars[0];
  if (c != renderedChar) {
    renderedChar = c;
    if (c >= 32) Serial.printf("[HUB] render '%c'\n", c); else Serial.printf("[HUB] render code %d\n", c);
  }
  if (c == 0) { display::clear(); return; }
  CRGB col = display::styled(live.r, live.g, live.b, signBriEff(live), live.effect, live.effectPeriodMs, millis());
  display::renderChar(c, col);
}

void printStatus() {
  char ms[18];
  Serial.printf("role=HUB pos=%d state=%s nextPos=%d numBoards=%d showSeq=%u\n",
                MY_POS, stateName(st), nextPosCached, highestOwned(), (unsigned)showSeq);
  SignState s = sign();
  Serial.printf("text=\"%s\" color=%02X%02X%02X bri=%u/%u fx=%s speed=%u boards=%u mirror=%d%s\n",
                s.text, s.r, s.g, s.b, s.bri, signBriEff(s), fxName(s.effect), s.effectPeriodMs, s.boards,
                display::mirror, dirtyAt ? " (unsaved)" : "");
  uint32_t now = millis();
  int n = 0;
  for (auto& e : table) {
    if (!e.pos) continue;
    n++;
    radio::macToStr(e.mac, ms);
    const BoardInfo& bi = info[e.pos];
    uint32_t age = bi.lastHb ? (now - bi.lastHb) / 1000 : 0;
    Serial.printf("  %s -> pos %d  %s age=%us rssi=%d vbat=%u char=%d nodeState=%u lastShowSeq=%u\n", ms, e.pos,
                  (bi.lastHb && now - bi.lastHb < OFFLINE_AFTER_MS) ? "online " : "offline",
                  (unsigned)age, bi.rssi, bi.mv, bi.shownChar, bi.nodeState, bi.lastShowSeq);
  }
  if (!n) Serial.println("  (table empty)");
}

} // namespace hub
