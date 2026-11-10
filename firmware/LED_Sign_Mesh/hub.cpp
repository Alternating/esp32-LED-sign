#include "hub.h"
#include "protocol.h"
#include "radio_espnow.h"
#include "storage.h"
#include "display.h"
#include "buttons.h"
#include "sequencer.h"
#include "ota.h"
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
// `pending` under signMux and raise pendingFlag; tick() swaps it in, regroups the text if
// text/boards/mode changed, bumps the Show seq, bursts, and arms the 2 s auto-save. NVS writes
// for the sign happen in tick().
//
// Word sequencer (step 2): `seq` holds the groups (core 1 only). Two Show "slots":
//   disp*  = the group the hub shows and the 500 ms heartbeat carries (applyAtHubMs 0, dispSeq)
//   next*  = the scheduled group: on each advance, nextSeq = dispSeq + 1, applyAt = now + 300 ms,
//            3x burst with applyAtHubMs = applyAt; swapped into disp* in render() when due, so hub
//            and nodes switch at the same hub-clock instant. nextSwitchAt = lastApplyAt + wordPeriodMs is the
//            SWITCH instant; the burst is sent WORD_LEAD_MS before it (D1 fix: period no longer includes the lead).
//   Content/style changes (relayout or restyle) bump dispSeq past any nextSeq already on the air
//   and burst the applied frame (applyAtHubMs 0); a pending advance is re-sent with a newer seq.
//
// OTA queue (Phase 4, review §R3): otaQ (FIFO of positions), otaRec[pos] (per-board status for the UI) and
// the in-flight node (otaCur + MAC) live under tMux. Commands (any core) only edit the queue; tick() (core 1)
// starts the next node (unicast OTA_ENTER, needsAck), and times it out (no ACK/status in 10 s -> "no
// response"; 120 s overall -> "timeout"). handlePacket (core 0) records ACK / OTA_STATUS progress and marks
// DONE when the in-flight MAC says HELLO/HB with OUR buildId (the real completion signal: it rebooted into
// the new image and rejoined). Cancel drops the queued rows only; the in-flight node finishes or times out.

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
  uint8_t  mirror;              // from HELLO, updated by setMirrorPos()
  uint8_t  buildId[4];          // from HELLO/HB; 00000000 = unknown (pre-Phase-4 frame or never heard)
};
static BoardInfo info[MAX_BOARDS + 1];          // guarded by tMux like table[]
static portMUX_TYPE tMux = portMUX_INITIALIZER_UNLOCKED;

// OTA queue, all under tMux
enum class OtaRow : uint8_t { NONE, QUEUED, SENT, JOINING, UPDATING, REBOOTING, DONE, FAILED };
struct OtaRec { OtaRow st; uint8_t pct; char err[24]; };
static OtaRec   otaRec[MAX_BOARDS + 1];
static uint8_t  otaQ[MAX_BOARDS];
static uint8_t  otaQLen = 0;
static uint8_t  otaCur = 0;                     // position in flight, 0 = none
static uint8_t  otaCurMac[6] = {0};
static uint32_t otaCurStart = 0;
static bool     otaGotReply = false;            // ACK or OTA_STATUS seen for the in-flight node

// Sign state and the core-0 -> core-1 handover
static SignState live;                          // core 1 only
static SignState pending;
static volatile bool pendingFlag = false;
static portMUX_TYPE signMux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t dirtyAt = 0;           // auto-save timer; 1 = save on next tick

// Word sequencer state (core 1 only, except the volatile snapshots read by buildState)
static sequencer::Sequencer seq;
static uint8_t  lastNumBoards = 0;              // effective numBoards the groups were built for
static char     dispChars[MAX_BOARDS] = {0};    // applied group (hub display + heartbeat)
static volatile uint8_t  dispIdx = 0;
static volatile uint16_t dispSeq = 1;
static char     nextChars[MAX_BOARDS] = {0};    // scheduled group (armed advance)
static uint8_t  nextIdx = 0;
static uint16_t nextSeq = 1;
static uint32_t applyAt = 0;                    // hub millis at which next* becomes disp*
static bool     nextArmed = false;
static uint32_t lastApplyAt = 0;                // when the displayed group last changed
static uint32_t nextSwitchAt = 0;                // hub-clock instant of the next group change
static volatile bool burstApplied = true;       // burst frames include the applied slot (false = advance-only burst)
static char     renderedChar = 0;

// Timers (tick)
static uint32_t lastPeriodicAt = 0;
static volatile uint8_t  burstLeft = 0;
static volatile uint32_t burstNextAt = 0;
static volatile uint8_t  resetLeft = 0;      // onResetOrd may run on core 0 (web) or core 1 (serial/button)
static volatile uint32_t resetNextAt = 0;
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

static void scheduleBurst() {              // content/style/join burst: applied frame (+ pending if armed)
  burstApplied = true;
  burstLeft = 3;
  burstNextAt = millis();
}

static void scheduleAdvanceBurst() {       // word advance: pending frame only
  burstLeft = 3;
  burstNextAt = millis();
}

// ---------------- sequencer (core 1) ----------------
static void loadGroup(uint8_t idx, char out[MAX_BOARDS]) { seq.group(idx, out); }

// Regroup live.text for nb boards and restart at group 0 (text / boards / mode / numBoards change).
static void relayout(uint8_t nb, bool burst) {
  uint32_t now = millis();
  lastNumBoards = nb;
  seq.rebuild(live, nb);
  seq.reset();
  loadGroup(0, dispChars);
  dispIdx = 0;
  dispSeq = (uint16_t)((nextArmed ? nextSeq : dispSeq) + 1);   // newer than anything already on the air
  nextArmed = false;
  lastApplyAt = now;
  nextSwitchAt = now + live.wordPeriodMs;
  if (burst) scheduleBurst();
  Serial.printf("[HUB] sequencer rebuilt: groups=%u mode=%s boards=%u period=%u%s\n",
                seq.groupCount(), modeName(live.mode), nb, live.wordPeriodMs, seq.truncated() ? " (truncated)" : "");
}

// Style-only change (colour/bri/fx/wspeed): keep the group index, re-sequence, burst.
static void restyle(uint16_t oldPeriod) {
  if (nextArmed) { dispSeq = (uint16_t)(nextSeq + 1); nextSeq = (uint16_t)(dispSeq + 1); }
  else           { dispSeq = (uint16_t)(dispSeq + 1); }
  if (live.wordPeriodMs != oldPeriod && !nextArmed) nextSwitchAt = lastApplyAt + live.wordPeriodMs;
  scheduleBurst();
}

// Swap the scheduled group in when its instant has come (same rule as node::render()).
static void applyDue(uint32_t now) {
  if (!nextArmed || (int32_t)(now - applyAt) < 0) return;
  memcpy(dispChars, nextChars, MAX_BOARDS);
  dispIdx = nextIdx;
  dispSeq = nextSeq;
  nextArmed = false;
  lastApplyAt = applyAt;
  nextSwitchAt = applyAt + live.wordPeriodMs;
  char c = dispChars[0];
  if (c >= 32) Serial.printf("[HUB] apply seq=%u word=%u/%u '%c'\n", (unsigned)dispSeq, dispIdx, seq.groupCount(), c);
  else         Serial.printf("[HUB] apply seq=%u word=%u/%u code %d\n", (unsigned)dispSeq, dispIdx, seq.groupCount(), c);
}

void nextGroup() {                         // serial 'n' (core 1): advance at the next tick, with the normal lead
  if (seq.groupCount() <= 1) { Serial.println("[HUB] next: only one group"); return; }
  if (nextArmed) { Serial.println("[HUB] next: advance already scheduled"); return; }
  nextSwitchAt = millis() + WORD_LEAD_MS;
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
// scheduled = the armed next group (applyAtHubMs = applyAt, nextSeq); else the applied group (0, dispSeq).
static void sendShow(bool periodic, bool scheduled) {
  Show s;
  s.h = makeHdr(SHOW, MY_POS, POS_ALL, (periodic ? FLAG_PERIODIC : 0) | (live.bands ? FLAG_BANDS : 0));
  s.hubMillis = millis();
  s.applyAtHubMs = scheduled ? applyAt : 0;
  s.showSeq = scheduled ? nextSeq : dispSeq;
  s.r = live.r; s.g = live.g; s.b = live.b;
  s.brightness = signBriEff(live);
  s.effect = live.effect;
  s.effectPeriodMs = live.effectPeriodMs;
  s.wordPeriodMs = live.wordPeriodMs;
  s.numBoards = lastNumBoards;
  s.wordIdx = scheduled ? nextIdx : dispIdx;
  s.wordCount = seq.groupCount();
  memcpy(s.chars, scheduled ? nextChars : dispChars, MAX_BOARDS);
  radio::send(radio::BCAST, &s, sizeof(s), false);
  lastPeriodicAt = millis();
  if (!periodic)
    Serial.printf("[SHOW] seq=%u color=%02X%02X%02X bri=%u fx=%u group=%u/%u applyAt=%+ld\n",
                  (unsigned)s.showSeq, s.r, s.g, s.b, s.brightness, s.effect, s.wordIdx, s.wordCount,
                  scheduled ? (long)(int32_t)(applyAt - s.hubMillis) : 0L);
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

// A RUNNING node never re-HELLOs, so after a hub reinstall (table wiped) it would stay invisible: no Boards
// card, no OTA, no unicast. Its HB carries its saved position (srcPos): adopt it when that position is free.
// A MAC already in the table, or a position owned by another MAC, is left alone (HELLO/CLAIM rules decide).
static void adoptFromHb(const uint8_t* mac, uint8_t pos) {
  bool adopted = false;
  portENTER_CRITICAL(&tMux);
  if (findMac(mac) < 0 && !owned(pos)) {
    int slot = freeSlot();
    if (slot >= 0) { memcpy(table[slot].mac, mac, 6); table[slot].pos = pos; memset(&info[pos], 0, sizeof(BoardInfo)); adopted = true; }
  }
  portEXIT_CRITICAL(&tMux);
  if (!adopted) return;
  radio::addPeer(mac);
  persistTable();
  char ms[18]; radio::macToStr(mac, ms);
  Serial.printf("[HUB] adopted %s at pos %d from HB (table had lost it)\n", ms, pos);
  scheduleBurst();
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

// ---------------- OTA queue ----------------
static const char* otaRowName(OtaRow s) {
  switch (s) {
    case OtaRow::QUEUED:    return "queued";
    case OtaRow::SENT:      return "starting";
    case OtaRow::JOINING:   return "joining";
    case OtaRow::UPDATING:  return "updating";
    case OtaRow::REBOOTING: return "rebooting";
    case OtaRow::DONE:      return "done";
    case OtaRow::FAILED:    return "failed";
    default:                return "";
  }
}

static void otaSetErr(OtaRec& r, const char* e) { strncpy(r.err, e, sizeof(r.err) - 1); r.err[sizeof(r.err) - 1] = 0; }

static bool inQueue(uint8_t pos) {               // tMux held
  for (uint8_t i = 0; i < otaQLen; i++) if (otaQ[i] == pos) return true;
  return false;
}

static bool sameBuild(const uint8_t* id) { return memcmp(id, ota::buildId(), 4) == 0; }

// HELLO/HB from the in-flight MAC carrying our buildId = it rebooted into the new image (core 0)
static void otaSeen(const uint8_t* mac, const uint8_t* bid) {
  uint8_t pos = 0; uint32_t took = 0;
  portENTER_CRITICAL(&tMux);
  if (otaCur && memcmp(mac, otaCurMac, 6) == 0 && sameBuild(bid)) {
    OtaRec& r = otaRec[otaCur];
    if (r.st != OtaRow::DONE && r.st != OtaRow::FAILED) { r.st = OtaRow::DONE; r.pct = 100; r.err[0] = 0; pos = otaCur; took = millis() - otaCurStart; }
  }
  portEXIT_CRITICAL(&tMux);
  if (pos) Serial.printf("[OTA] pos %d done in %u s (rejoined with the hub's buildId)\n", pos, (unsigned)(took / 1000));
}

static void otaAcked(const uint8_t* mac) {        // core 0
  portENTER_CRITICAL(&tMux);
  if (otaCur && memcmp(mac, otaCurMac, 6) == 0) {
    otaGotReply = true;
    if (otaRec[otaCur].st == OtaRow::SENT) otaRec[otaCur].st = OtaRow::JOINING;
  }
  portEXIT_CRITICAL(&tMux);
}

static void otaStatus(const uint8_t* mac, const OtaStatus* m) {   // core 0, matched by MAC (not srcPos)
  uint8_t pos = 0; OtaRow st = OtaRow::NONE;
  char msg[sizeof(m->msg) + 1]; memcpy(msg, m->msg, sizeof(m->msg)); msg[sizeof(m->msg)] = 0;
  portENTER_CRITICAL(&tMux);
  if (otaCur && memcmp(mac, otaCurMac, 6) == 0) {
    OtaRec& r = otaRec[otaCur];
    otaGotReply = true;
    if (r.st != OtaRow::DONE && r.st != OtaRow::FAILED) {
      switch (m->state) {
        case OTA_ST_JOINING:     r.st = OtaRow::JOINING; break;
        case OTA_ST_DOWNLOADING: r.st = OtaRow::UPDATING; break;
        case OTA_ST_VERIFIED:    r.st = OtaRow::REBOOTING; break;
        case OTA_ST_FAILED:      r.st = OtaRow::FAILED; otaSetErr(r, msg[0] ? msg : "node error"); break;
        default: break;
      }
      r.pct = m->pct > 100 ? 100 : m->pct;
      pos = otaCur; st = r.st;
    }
  }
  portEXIT_CRITICAL(&tMux);
  if (!pos) { char s[18]; radio::macToStr(mac, s); Serial.printf("[OTA] status from %s ignored (not in flight)\n", s); return; }
  if (st == OtaRow::FAILED) Serial.printf("[OTA] pos %d failed: %s\n", pos, msg);
  else Serial.printf("[OTA] pos %d %s %u %%\n", pos, otaRowName(st), m->pct);
}

static uint8_t otaQueueOne(uint8_t pos, uint32_t now, const char*& err) {   // tMux held; 1 = queued
  int idx = findPos(pos);
  if (idx < 0) { err = "bad_arg"; return 0; }
  const BoardInfo& bi = info[pos];
  if (pos == otaCur || inQueue(pos)) { err = "queued"; return 0; }
  if (sameBuild(bi.buildId)) { err = "up_to_date"; return 0; }
  if (!(bi.lastHb && now - bi.lastHb < OFFLINE_AFTER_MS)) { err = "offline"; return 0; }
  otaQ[otaQLen++] = pos;
  otaRec[pos].st = OtaRow::QUEUED; otaRec[pos].pct = 0; otaRec[pos].err[0] = 0;
  return 1;
}

bool otaNodes(uint8_t& queued, uint8_t& skipped, const char*& err) {
  queued = skipped = 0; err = nullptr;
  if (ota::uploadActive()) { err = "busy"; return false; }
  if (!ota::imageLen()) { err = "no_image"; return false; }
  uint32_t now = millis();
  char list[48]; size_t ll = 0; list[0] = 0;
  portENTER_CRITICAL(&tMux);
  for (uint8_t pos = 2; pos <= MAX_BOARDS; pos++) {
    if (findPos(pos) < 0) continue;
    const char* why = nullptr;
    if (otaQueueOne(pos, now, why)) { queued++; if (ll < sizeof(list) - 4) ll += snprintf(list + ll, sizeof(list) - ll, " %d", pos); }
    else if (strcmp(why, "queued") != 0) skipped++;
  }
  portEXIT_CRITICAL(&tMux);
  char hex[9]; ota::buildIdHex(ota::buildId(), hex);
  Serial.printf("[OTA] queue:%s (%u queued, %u up to date/offline, target buildId %s)\n", queued ? list : " empty",
                queued, skipped, hex);
  return true;
}

bool otaNode(uint8_t pos, const char*& err) {
  err = nullptr;
  if (pos < 2 || pos > MAX_BOARDS) { err = "bad_arg"; return false; }
  if (ota::uploadActive()) { err = "busy"; return false; }
  if (!ota::imageLen()) { err = "no_image"; return false; }
  portENTER_CRITICAL(&tMux);
  uint8_t ok = otaQueueOne(pos, millis(), err);
  portEXIT_CRITICAL(&tMux);
  Serial.printf("[OTA] queue pos %d: %s\n", pos, ok ? "queued" : err);
  return ok != 0;
}

uint8_t otaCancel() {
  portENTER_CRITICAL(&tMux);
  uint8_t n = otaQLen;
  for (uint8_t i = 0; i < otaQLen; i++) otaRec[otaQ[i]].st = OtaRow::NONE;
  otaQLen = 0;
  uint8_t cur = otaCur;
  portEXIT_CRITICAL(&tMux);
  Serial.printf("[OTA] cancel: %u queued dropped%s\n", n, cur ? " (node in flight continues until done/timeout)" : "");
  return n;
}

bool otaBusy() {
  portENTER_CRITICAL(&tMux);
  bool b = otaCur != 0 || otaQLen != 0;
  portEXIT_CRITICAL(&tMux);
  return b;
}

// core 1: time out the in-flight node, start the next one
static void otaTick(uint32_t now) {
  uint8_t endedPos = 0; char endedWhy[24] = ""; bool endedOk = false;
  uint8_t startPos = 0; uint8_t mac[6] = {0}; bool skipUpToDate = false;
  portENTER_CRITICAL(&tMux);
  if (otaCur) {
    OtaRec& r = otaRec[otaCur];
    if (r.st != OtaRow::DONE && r.st != OtaRow::FAILED) {
      if (now - otaCurStart > OTA_NODE_TIMEOUT_MS) { r.st = OtaRow::FAILED; otaSetErr(r, "timeout"); }
      else if (!otaGotReply && now - otaCurStart > OTA_ACK_TIMEOUT_MS) { r.st = OtaRow::FAILED; otaSetErr(r, "no response"); }
    }
    if (r.st == OtaRow::DONE || r.st == OtaRow::FAILED) {
      endedPos = otaCur; endedOk = r.st == OtaRow::DONE; memcpy(endedWhy, r.err, sizeof(endedWhy));
      otaCur = 0;
    }
  }
  if (!otaCur && otaQLen) {
    uint8_t p = otaQ[0];
    for (uint8_t i = 1; i < otaQLen; i++) otaQ[i - 1] = otaQ[i];
    otaQLen--;
    int idx = findPos(p);
    if (idx < 0) { otaRec[p].st = OtaRow::FAILED; otaSetErr(otaRec[p], "forgotten"); }
    else if (sameBuild(info[p].buildId)) { otaRec[p].st = OtaRow::DONE; otaRec[p].pct = 100; skipUpToDate = true; startPos = p; }
    else {
      otaCur = p; memcpy(otaCurMac, table[idx].mac, 6); memcpy(mac, otaCurMac, 6);
      otaCurStart = now; otaGotReply = false;
      otaRec[p].st = OtaRow::SENT; otaRec[p].pct = 0; otaRec[p].err[0] = 0;
      startPos = p;
    }
  }
  portEXIT_CRITICAL(&tMux);

  if (endedPos && !endedOk) Serial.printf("[OTA] pos %d failed: %s -> next\n", endedPos, endedWhy);
  if (!startPos) return;
  if (skipUpToDate) { Serial.printf("[OTA] pos %d already up to date\n", startPos); return; }

  OtaEnter e;
  memset(&e, 0, sizeof(e));
  e.h = makeHdr(OTA_ENTER, MY_POS, startPos);
  strncpy(e.ssid, radio::apSsid(), sizeof(e.ssid) - 1);
  strncpy(e.pass, AP_PASS, sizeof(e.pass) - 1);
  e.ip = (uint32_t)WiFi.softAPIP();
  e.port = OTA_PORT;
  memcpy(e.buildId, ota::buildId(), 4);
  e.size = ota::imageLen();
  WiFi.softAPmacAddress(e.bssid);
  radio::send(mac, &e, sizeof(e), true);
  char ms[18], hex[9], oldHex[9]; radio::macToStr(mac, ms); ota::buildIdHex(e.buildId, hex);
  portENTER_CRITICAL(&tMux);
  uint8_t old[4]; memcpy(old, info[startPos].buildId, 4);
  portEXIT_CRITICAL(&tMux);
  ota::buildIdHex(old, oldHex);
  Serial.printf("[OTA] OTA_ENTER pos %d (%s) buildId %s -> %s, %u B from :%d\n", startPos, ms, oldHex, hex,
                (unsigned)e.size, OTA_PORT);
}

bool healthy() {
  return st == HubState::RUNNING && (uint32_t)WiFi.softAPIP() != 0 && radio::channelOk();
}

// /state.ota: {hub:{buildId,version,git,slot,valid,verifyIn,imageLen}, running, upload, current, boards:[...]}
void buildOta(JsonObject o) {
  storage::TableEntry tbl[MAX_BOARDS];
  BoardInfo inf[MAX_BOARDS + 1];
  OtaRec rec[MAX_BOARDS + 1];
  uint8_t cur, qlen;
  portENTER_CRITICAL(&tMux);
  memcpy(tbl, table, sizeof(tbl)); memcpy(inf, info, sizeof(inf)); memcpy(rec, otaRec, sizeof(rec));
  cur = otaCur; qlen = otaQLen;
  portEXIT_CRITICAL(&tMux);
  uint32_t now = millis();
  char hex[9];

  JsonObject h = o["hub"].to<JsonObject>();
  ota::buildIdHex(ota::buildId(), hex);
  h["buildId"] = hex;
  h["version"] = ota::versionStr();
  h["git"] = ota::gitStr();
  h["slot"] = ota::slotName();
  h["valid"] = !ota::pendingVerify();
  h["verifyIn"] = (ota::verifyLeftMs() + 999) / 1000;
  h["imageLen"] = ota::imageLen();
  o["running"] = cur != 0 || qlen != 0;
  o["upload"] = ota::uploadActive();
  if (cur) {
    JsonObject c = o["current"].to<JsonObject>();
    c["pos"] = cur; c["pct"] = rec[cur].pct; c["state"] = otaRowName(rec[cur].st);
  } else {
    o["current"] = nullptr;
  }
  JsonArray arr = o["boards"].to<JsonArray>();
  for (uint8_t pos = 2; pos <= MAX_BOARDS; pos++) {
    bool have = false;
    for (int i = 0; i < MAX_BOARDS; i++) if (tbl[i].pos == pos) { have = true; break; }
    if (!have) continue;
    const BoardInfo& bi = inf[pos];
    const OtaRec& r = rec[pos];
    bool same = memcmp(bi.buildId, ota::buildId(), 4) == 0;
    bool online = bi.lastHb != 0 && (now - bi.lastHb) < OFFLINE_AFTER_MS;
    JsonObject b = arr.add<JsonObject>();
    b["pos"] = pos;
    ota::buildIdHex(bi.buildId, hex);
    b["buildId"] = hex;
    const char* status;
    if (r.st != OtaRow::NONE && !(r.st == OtaRow::DONE && !same)) status = otaRowName(r.st);
    else if (same) status = "up-to-date";
    else if (!online) status = "offline";
    else status = "needs-update";
    b["status"] = status;
    b["pct"] = r.pct;
    b["err"] = r.err;
  }
}

// ---------------- RX (core 0) ----------------
void handlePacket(const uint8_t* srcMac, const uint8_t* buf, uint8_t len, int8_t rssi) {
  const Hdr* h = (const Hdr*)buf;
  switch (h->type) {
    case HELLO:
      if (len >= HELLO_V1_LEN) {                       // v1 (13 B) and Phase-4 (17 B) frames
        const Hello* m = (const Hello*)buf;
        uint8_t bid[4] = {0, 0, 0, 0};
        if (len >= sizeof(Hello)) memcpy(bid, m->buildId, 4);
        onHello(srcMac, m->nvsPos, false, rssi, m->vbat_mV);
        { portENTER_CRITICAL(&tMux); int i = findMac(srcMac);
          if (i >= 0 && table[i].pos) { info[table[i].pos].mirror = m->mirror; memcpy(info[table[i].pos].buildId, bid, 4); }
          portEXIT_CRITICAL(&tMux); }
        otaSeen(srcMac, bid);
      }
      break;
    case CLAIM:
      onHello(srcMac, 0, true, rssi, 0);
      break;
    case ACK:
      if (len >= sizeof(Ack)) {
        const Ack* a = (const Ack*)buf;
        radio::ackReceived(srcMac, a->ackSeq);
        if (a->ackType == OTA_ENTER) otaAcked(srcMac);
        if (a->pos >= 1 && a->pos <= MAX_BOARDS) {
          portENTER_CRITICAL(&tMux);
          info[a->pos].lastSeen = millis(); info[a->pos].rssi = rssi;
          portEXIT_CRITICAL(&tMux);
        }
      }
      break;
    case HB:
      if (len >= HB_V1_LEN) {                          // v1 (15 B) and Phase-4 (19 B) frames
        const Hb* hb = (const Hb*)buf;
        uint8_t p = h->srcPos;
        uint8_t bid[4] = {0, 0, 0, 0};
        if (len >= sizeof(Hb)) memcpy(bid, hb->buildId, 4);
        if (p >= 2 && p <= MAX_BOARDS) adoptFromHb(srcMac, p);   // re-register a running node the table lost
        if (p >= 1 && p <= MAX_BOARDS) {
          portENTER_CRITICAL(&tMux);
          BoardInfo& bi = info[p];
          bi.lastHb = bi.lastSeen = millis();
          bi.rssi = rssi; bi.mv = hb->vbat_mV; bi.shownChar = hb->shownChar;
          bi.nodeState = hb->state; bi.lastShowSeq = hb->lastShowSeq;
          memcpy(bi.buildId, bid, 4);
          portEXIT_CRITICAL(&tMux);
          radio::addPeer(srcMac);                      // D1 belt-and-braces: idempotent, cheap when it exists
        }
        otaSeen(srcMac, bid);
      }
      break;
    case OTA_STATUS:
      if (len >= sizeof(OtaStatus)) otaStatus(srcMac, (const OtaStatus*)buf);
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
  memset(otaRec, 0, sizeof(otaRec));         // positions are gone; the in-flight node (matched by MAC) runs out
  otaQLen = 0;
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
  display::identifyStart(ms, MY_POS);   // hub flashes its digit too
  Serial.printf("[HUB] IDENTIFY ALL %u ms\n", ms);
}

void onIdentifyAll() { identifyAll(IDENTIFY_MS); }

bool identifyPos(uint8_t pos, uint16_t ms) {
  ms = constrain(ms, (uint16_t)200, (uint16_t)10000);
  if (pos == POS_ALL) { identifyAll(ms); return true; }
  if (pos == MY_POS) { display::identifyStart(ms, MY_POS); Serial.printf("[HUB] IDENTIFY self %u ms\n", ms); return true; }
  uint8_t mac[6]; bool found = false;
  portENTER_CRITICAL(&tMux);
  int idx = findPos(pos);
  if (idx >= 0) { memcpy(mac, table[idx].mac, 6); found = true; }
  portEXIT_CRITICAL(&tMux);
  if (!found) return false;
  Identify id;
  id.h = makeHdr(IDENTIFY, MY_POS, pos);
  id.ms = ms;
  radio::send(mac, &id, sizeof(id), true);    // D1: unicast with ACK/retry
  char ms18[18]; radio::macToStr(mac, ms18);
  Serial.printf("[HUB] IDENTIFY pos %d (%s) %u ms\n", pos, ms18, ms);
  return true;
}

// Remote mirror flip (web/serial `mirror`): pos 1 = hub itself, else unicast SETMIRROR with ACK.
bool setMirrorPos(uint8_t pos, bool v) {
  if (pos == MY_POS) {
    display::mirror = v; storage::saveMirror(v);
    Serial.printf("[HUB] mirror self -> %d (saved)\n", v);
    return true;
  }
  uint8_t mac[6]; bool found = false;
  portENTER_CRITICAL(&tMux);
  int idx = findPos(pos);
  if (idx >= 0) { memcpy(mac, table[idx].mac, 6); found = true; info[pos].mirror = v; }
  portEXIT_CRITICAL(&tMux);
  if (!found) return false;
  SetMirror sm;
  sm.h = makeHdr(SETMIRROR, MY_POS, pos);
  sm.mirror = v ? 1 : 0;
  radio::send(mac, &sm, sizeof(sm), true);
  char ms18[18]; radio::macToStr(mac, ms18);
  Serial.printf("[HUB] SETMIRROR pos %d (%s) -> %d\n", pos, ms18, v);
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
    memset(&otaRec[pos], 0, sizeof(OtaRec));
    found = true;
  }
  portEXIT_CRITICAL(&tMux);
  if (!found) return false;
  Hdr h = makeHdr(FORGET, MY_POS, pos);
  radio::send(mac, &h, sizeof(h), true);      // D1: a lost FORGET would undo itself at the node's next boot
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
  uint32_t now = millis();
  uint8_t highest = MY_POS; for (auto& e : tbl) if (e.pos > highest) highest = e.pos;
  uint8_t nb = s.boards ? s.boards : highest;
  // Groups are recomputed from the (pending-or-live) state with a local Sequencer so the reply
  // predicts the layout the next tick applies; the hub's real index is dispIdx.
  sequencer::Sequencer sq;
  sq.rebuild(s, nb);

  JsonObject cfg = root["cfg"].to<JsonObject>();
  cfg["text"] = s.text;
  char hex[7]; snprintf(hex, sizeof(hex), "%02X%02X%02X", s.r, s.g, s.b);
  cfg["color"] = hex;
  cfg["rainbow"] = s.rainbow;
  cfg["bands"] = s.bands != 0;
  cfg["bri"] = s.bri;
  cfg["briEff"] = signBriEff(s);
  cfg["fx"] = fxName(s.effect);
  cfg["speed"] = s.effectPeriodMs;
  cfg["boards"] = nb;
  cfg["boardsAuto"] = (s.boards == 0);
  cfg["wspeed"] = s.wordPeriodMs;
  cfg["mode"] = modeName(s.mode);
  {
    JsonArray groups = cfg["groups"].to<JsonArray>();
    char gs[sequencer::GROUP_STR_MAX];
    for (uint8_t i = 0; i < sq.groupCount(); i++) { sq.groupString(i, gs, sizeof(gs)); groups.add(gs); }
    cfg["groupCount"] = sq.groupCount();
    cfg["truncated"] = sq.truncated();
  }
  cfg["name"] = radio::apSsid();
  cfg["fw"] = FW_VERSION;

  JsonObject state = root["state"].to<JsonObject>();
  state["hubPos"] = MY_POS;
  state["numBoards"] = nb;
  state["nextPos"] = nextPosCached;
  state["wordIdx"] = dispIdx;
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
    b["online"] = true; b["age"] = 0; b["mv"] = 0; b["rssi"] = 0; b["mirror"] = display::mirror;
    char fw[9]; ota::buildIdHex(ota::buildId(), fw); b["fw"] = fw;
    putChar(b, dispChars[0]);
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
    b["mv"] = bi.mv; b["rssi"] = bi.rssi; b["mirror"] = bi.mirror != 0;
    char fw[9]; ota::buildIdHex(bi.buildId, fw); b["fw"] = fw;
    putChar(b, (char)bi.shownChar);
  }
  buildOta(root["ota"].to<JsonObject>());
}

// ---------------- lifecycle ----------------
void begin() {
  uint8_t storedNext;
  storage::loadTable(table, storedNext);
  nextPosCached = computeNextPos();     // never trust the stored counter
  memset(info, 0, sizeof(info));
  int n = 0;
  for (auto& e : table) if (e.pos) { n++; radio::addPeer(e.mac); }   // D1: re-register every known node as a
                                                                      // unicast peer after a hub reboot (nodes that
                                                                      // never re-HELLO would otherwise be unreachable)
  Serial.printf("[HUB] table loaded: %d boards (peers registered), nextPos=%d\n", n, nextPosCached);

  bool migrated = storage::loadSign(live);   // restored before the first Show
  pending = live;
  Serial.printf("[HUB] sign loaded: text=\"%s\" color=%02X%02X%02X bri=%u fx=%s speed=%u boards=%u wspeed=%u mode=%s\n",
                live.text, live.r, live.g, live.b, live.bri, fxName(live.effect), live.effectPeriodMs, live.boards,
                live.wordPeriodMs, modeName(live.mode));
  if (migrated) {
    Serial.println("[HUB] sign blob migrated to v3, rewriting");
    dirtyAt = 1;                                 // saved as v2 on the first tick
  }
  relayout(live.boards ? live.boards : highestOwned(), false);   // first heartbeat carries group 0

  buttons::onLong(K3_PIN, onResetOrd, LONG_PRESS_HUB_MS);
  buttons::onLong(K2_PIN, onIdentifyAll, LONG_PRESS_HUB_MS);
  pinMode(STATUS_PIN, OUTPUT);

  setState(HubState::SHOW_POSITION);
  Serial.printf("[HUB] showing position %d for %d ms\n", MY_POS, SHOW_POSITION_MIN);
}

void tick() {
  uint32_t now = millis();

  // Apply a queued sign change (core-0 commands -> core-1 Show). Text/boards/mode -> regroup
  // from group 0; anything else (colour, bri, fx, wspeed) keeps the group index.
  bool regroup = false, changed = false;
  uint16_t oldPeriod = live.wordPeriodMs;
  if (pendingFlag) {
    portENTER_CRITICAL(&signMux);
    regroup = strcmp(live.text, pending.text) != 0 || live.boards != pending.boards || live.mode != pending.mode;
    live = pending;
    pendingFlag = false;
    portEXIT_CRITICAL(&signMux);
    changed = true;
    dirtyAt = now ? now : 1;
  }
  // Effective numBoards (pinned, or highest owned position): a node joining or being forgotten
  // changes it from core 0 / any core; detected here so the regroup always runs on core 1.
  uint8_t nb;
  portENTER_CRITICAL(&tMux);
  nb = live.boards ? live.boards : highestOwned();
  portEXIT_CRITICAL(&tMux);
  if (regroup || nb != lastNumBoards) relayout(nb, true);
  else if (changed) restyle(oldPeriod);
  if (dirtyAt && (dirtyAt == 1 || now - dirtyAt >= 2000)) {
    dirtyAt = 0;
    storage::saveSign(live);
    Serial.println("[HUB] sign saved");
  }

  if (st == HubState::SHOW_POSITION && now - stateEnteredAt >= SHOW_POSITION_MIN) {
    setState(HubState::RUNNING);
    lastApplyAt = now;                      // the first word period starts when the sign starts showing text
    nextSwitchAt = now + live.wordPeriodMs;
  }

  // Word advance: schedule the next group WORD_LEAD_MS ahead so every board switches together
  if (st == HubState::RUNNING && seq.groupCount() > 1 && !nextArmed && (int32_t)(now - (nextSwitchAt - WORD_LEAD_MS)) >= 0) {
    nextIdx = seq.advance();
    loadGroup(nextIdx, nextChars);
    nextSeq = (uint16_t)(dispSeq + 1);
    applyAt = nextSwitchAt;                                   // anchored to the previous switch: no per-period drift
    if ((int32_t)(applyAt - now) < 100) applyAt = now + WORD_LEAD_MS;   // we are late (restyle/'n'): keep a real lead
    nextArmed = true;
    scheduleAdvanceBurst();
    Serial.printf("[HUB] advance group %u/%u seq=%u applyAt=+%ld\n", nextIdx, seq.groupCount(), (unsigned)nextSeq, (long)(int32_t)(applyAt - now));
  }

  // RESETORD burst
  if (resetLeft && (int32_t)(now - resetNextAt) >= 0) {
    Hdr h = makeHdr(RESETORD, MY_POS, POS_ALL);
    radio::send(radio::BCAST, &h, sizeof(h), false);
    resetLeft = resetLeft - 1;
    resetNextAt = now + RESET_ORD_GAP;
  }

  // Show: burst on change (0/60/250 ms) and periodic heartbeat every 500 ms. The heartbeat
  // always carries the applied group (applyAtHubMs 0); a burst carries the applied frame and/or
  // the scheduled one.
  if (burstLeft && (int32_t)(now - burstNextAt) >= 0) {
    if (burstApplied || !nextArmed) sendShow(false, false);
    if (nextArmed) sendShow(false, true);
    burstLeft = burstLeft - 1;
    burstNextAt = now + (burstLeft == 2 ? SHOW_BURST_GAP1 : SHOW_BURST_GAP2);
    if (burstLeft == 0) burstApplied = false;
  } else if (now - lastPeriodicAt >= SHOW_HEARTBEAT) {
    sendShow(true, false);
  }

  otaTick(now);

  // Status LED: blink in SHOW_POSITION, solid in RUNNING
  if (st == HubState::RUNNING) digitalWrite(STATUS_PIN, HIGH);
  else if (now - statusLedAt >= 500) { statusLedAt = now; digitalWrite(STATUS_PIN, !digitalRead(STATUS_PIN)); }
}

void render() {
  applyDue(millis());                     // scheduled group becomes the displayed one, like node::render()
  if (display::identifyActive()) { display::identifyRender(); return; }
  if (st == HubState::SHOW_POSITION) { display::renderDigit(MY_POS, CRGB::Green); return; }
  char c = dispChars[0];
  if (c != renderedChar) {
    renderedChar = c;
    if (c >= 32) Serial.printf("[HUB] render '%c'\n", c); else Serial.printf("[HUB] render code %d\n", c);
  }
  if (c == 0) { display::clear(); return; }
  if (live.bands) {
    CRGB lvl = display::styled(255, 255, 255, signBriEff(live), live.effect, live.effectPeriodMs, millis());
    display::renderCharBands(c, lvl.r);
    return;
  }
  CRGB col = display::styled(live.r, live.g, live.b, signBriEff(live), live.effect, live.effectPeriodMs, millis());
  display::renderChar(c, col);
}

void printStatus() {
  char ms[18];
  Serial.printf("role=HUB pos=%d state=%s nextPos=%d numBoards=%d showSeq=%u\n",
                MY_POS, stateName(st), nextPosCached, lastNumBoards, (unsigned)dispSeq);
  SignState s = sign();
  Serial.printf("text=\"%s\" color=%02X%02X%02X bri=%u/%u fx=%s speed=%u boards=%u mirror=%d%s\n",
                s.text, s.r, s.g, s.b, s.bri, signBriEff(s), fxName(s.effect), s.effectPeriodMs, s.boards,
                display::mirror, dirtyAt ? " (unsaved)" : "");
  uint32_t now = millis();
  if (seq.groupCount() > 1)
    Serial.printf("groups=%u idx=%u period=%u nextIn=%ld ms mode=%s%s\n", seq.groupCount(), dispIdx, live.wordPeriodMs,
                  nextArmed ? (long)(int32_t)(applyAt - now) : (long)(int32_t)(nextSwitchAt - now), modeName(live.mode),
                  nextArmed ? " (advance armed)" : "");
  else
    Serial.printf("groups=%u idx=0 period=%u nextIn=- mode=%s (no timer)\n", seq.groupCount(), live.wordPeriodMs, modeName(live.mode));
  {
    char gs[sequencer::GROUP_STR_MAX];
    for (uint8_t i = 0; i < seq.groupCount(); i++) {
      seq.groupString(i, gs, sizeof(gs));
      Serial.printf("  [%u]%s \"%s\"\n", i, i == dispIdx ? "*" : " ", gs);
    }
    if (seq.truncated()) Serial.println("  (truncated at 20 groups)");
  }
  int n = 0;
  for (auto& e : table) {
    if (!e.pos) continue;
    n++;
    radio::macToStr(e.mac, ms);
    const BoardInfo& bi = info[e.pos];
    uint32_t age = bi.lastHb ? (now - bi.lastHb) / 1000 : 0;
    char fw[9]; ota::buildIdHex(bi.buildId, fw);
    Serial.printf("  %s -> pos %d  %s age=%us rssi=%d char=%d nodeState=%u lastShowSeq=%u fw=%s%s\n", ms, e.pos,
                  (bi.lastHb && now - bi.lastHb < OFFLINE_AFTER_MS) ? "online " : "offline",
                  (unsigned)age, bi.rssi, bi.shownChar, bi.nodeState, bi.lastShowSeq, fw,
                  memcmp(bi.buildId, ota::buildId(), 4) == 0 ? "" : " (update)");
  }
  if (!n) Serial.println("  (table empty)");
  ota::printStatus();
  {
    OtaRec rec[MAX_BOARDS + 1]; uint8_t q[MAX_BOARDS]; uint8_t ql, cur;
    portENTER_CRITICAL(&tMux);
    memcpy(rec, otaRec, sizeof(rec)); memcpy(q, otaQ, sizeof(q)); ql = otaQLen; cur = otaCur;
    portEXIT_CRITICAL(&tMux);
    Serial.printf("otaQueue: current=%s", cur ? "" : "none");
    if (cur) Serial.printf("pos %d %s %u %%", cur, otaRowName(rec[cur].st), rec[cur].pct);
    Serial.print(" queued=[");
    for (uint8_t i = 0; i < ql; i++) Serial.printf("%s%d", i ? " " : "", q[i]);
    Serial.printf("] fwServer=%s served=%u transfers=%u upload=%d\n", ota::fwServing() ? "busy" : "idle",
                  (unsigned)ota::fwServed(), (unsigned)ota::fwTransfers(), ota::uploadActive() ? 1 : 0);
    for (uint8_t p = 2; p <= MAX_BOARDS; p++)
      if (rec[p].st == OtaRow::FAILED) Serial.printf("  pos %d last OTA failed: %s\n", p, rec[p].err);
  }
}

} // namespace hub
