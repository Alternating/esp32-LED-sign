#include "node.h"
#include "protocol.h"
#include "radio_espnow.h"
#include "storage.h"
#include "display.h"
#include "buttons.h"
#include "battery.h"
#include <string.h>

// State machine (plan §3.2):
//   BOOT -> DISCOVER      : HELLO{nvsPos} broadcast every 1 s +-200 ms; remembered digit (or '?') dim pulse
//   DISCOVER -> SHOW_POSITION : on ASSIGN (pos saved, hub peer added, ACK sent)
//   DISCOVER -> LOST_HUB  : 15 s without ASSIGN
//   SHOW_POSITION -> RUNNING : after 3.0 s minimum, once a Show is buffered
//   SHOW_POSITION -> UNASSIGNED : ASSIGN pos 0 ("!" for 3 s, table full)
//   RUNNING -> LOST_HUB   : 5 s without Show
//   LOST_HUB -> RUNNING   : any Show
//   * -> UNASSIGNED       : RESETORD (no HELLO until power cycle or CLAIM)
//   UNASSIGNED -> DISCOVER: K1 long / 'c' (CLAIM)
//   RUNNING/LOST_HUB -> SHOW_POSITION : SETPOS
//   * -> UNASSIGNED       : FORGET addressed to us (hub dropped our row; NVS pos cleared)
// handlePacket() runs on core 0 and only updates state/flags; render() on core 1 draws.
// Styling (brief item 5): render() colours the glyph with display::styled() at hubNow(), the
// hub clock reconstructed from Show.hubMillis with a low-pass offset (alpha = 1/4).
//
// Show application (step-2 brief item 4): two slots under showMux, `applied` (drawn) and
// `pending` (scheduled). Ordering is by signed seq difference against the NEWEST seq we know
// (pending if any, else applied), so a uint16 wrap is safe and a heartbeat repeating an older
// or the pending seq is ignored. A newer Show with applyAtHubMs 0 or already past applies at
// once; otherwise it waits in `pending` and render() swaps it in when hubNow() >= applyAtHubMs.
// Applying a Show drops a pending one that is not newer than it.

namespace node {

static volatile NodeState st = NodeState::BOOT;
static volatile uint8_t  myPos = 0;
static uint32_t stateEnteredAt = 0;

static uint8_t hubMac[6] = {0};
static volatile bool hubKnown = false;

// Show slots: written under showMux by the RX handler (core 0) and render() (core 1)
static Show applied, pending;
static volatile bool     haveApplied = false, havePending = false;
static int32_t           pendingLead = 0;           // applyAtHubMs - hubNow() when `pending` arrived (for the log)
static volatile uint32_t lastShowMs = 0;
static volatile uint32_t lastHubMillis = 0;         // hub clock of the last Show (hub-restart detection)
static portMUX_TYPE showMux = portMUX_INITIALIZER_UNLOCKED;

static volatile bool showBang = false;          // SHOW_POSITION shows "!" (ASSIGN pos 0)
static uint32_t nextHelloAt = 0;
static uint32_t nextHbAt = 0;
static uint32_t statusLedAt = 0;
static int8_t   hubRssi = 0;
static char     renderedChar = 0;

// Hub clock: offset = hubMillis - millis(), low-pass filtered. Written on core 0 (onShow),
// read on core 1 (render); a single aligned 32-bit write, hence volatile is enough.
static volatile int32_t hubOffset = 0;
static volatile bool    hubOffsetInit = false;
static uint32_t hubNow() { return millis() + (uint32_t)hubOffset; }

// ---------------- helpers ----------------
const char* stateName(NodeState s) {
  switch (s) {
    case NodeState::BOOT:          return "BOOT";
    case NodeState::DISCOVER:      return "DISCOVER";
    case NodeState::SHOW_POSITION: return "SHOW_POSITION";
    case NodeState::RUNNING:       return "RUNNING";
    case NodeState::LOST_HUB:      return "LOST_HUB";
    case NodeState::UNASSIGNED:    return "UNASSIGNED";
  }
  return "?";
}

NodeState state() { return st; }
uint8_t   pos()   { return myPos; }

static void setState(NodeState s) {
  if (s == st) return;
  Serial.printf("[NODE] %s -> %s pos=%d\n", stateName(st), stateName(s), myPos);
  st = s;
  uint32_t now = millis();
  stateEnteredAt = now;
  if (s == NodeState::DISCOVER) nextHelloAt = now;                       // HELLO immediately
  if (s == NodeState::LOST_HUB) nextHelloAt = now + HELLO_LOST_INTERVAL;
  if (s == NodeState::RUNNING)  nextHbAt = now + HB_INTERVAL + (uint32_t)myPos * HB_STAGGER_PER_POS;
}

static void learnHub(const uint8_t* mac) {
  if (hubKnown && memcmp(hubMac, mac, 6) == 0) return;
  memcpy(hubMac, mac, 6);
  radio::addPeer(hubMac);
  hubKnown = true;
  char s[18]; radio::macToStr(mac, s);
  Serial.printf("[NODE] hub learned %s\n", s);
}

static void sendHello() {
  Hello m;
  m.h = makeHdr(HELLO, 0, POS_HUB);
  m.nvsPos = myPos; m.fw = FW_VERSION; m.vbat_mV = battery::readMilliVolts(); m.mirror = display::mirror;
  radio::send(radio::BCAST, &m, sizeof(m), false);
}

static void sendAck(uint8_t ackType, uint16_t ackSeq) {
  Ack a;
  a.h = makeHdr(ACK, myPos, POS_HUB);
  a.ackType = ackType; a.ackSeq = ackSeq; a.pos = myPos;
  radio::send(hubMac, &a, sizeof(a), false);
}

static void sendHb() {
  Hb m;
  m.h = makeHdr(HB, myPos, POS_HUB);
  m.vbat_mV = battery::readMilliVolts(); m.rssi = hubRssi; m.shownChar = renderedChar;
  m.state = (uint8_t)st; m.lastShowSeq = haveApplied ? applied.showSeq : 0;
  radio::send(hubMac, &m, sizeof(m), false);
}

static void savePos(uint8_t p) {
  if (p != myPos) storage::saveNodePos(p);
  myPos = p;
}

static void dropPending() {                 // position changed: a frame scheduled for the old position must not apply
  portENTER_CRITICAL(&showMux);
  havePending = false;
  portEXIT_CRITICAL(&showMux);
}

// One log format for every applied Show (immediate and scheduled): lead = applyAtHubMs - hubNow()
// at arrival (0 for apply-now frames, negative = arrived late).
static void logApply(const Show& s, int32_t lead) {
  uint8_t c = (myPos >= 1 && myPos <= MAX_BOARDS) ? (uint8_t)s.chars[myPos - 1] : 0;
  char cs[12];
  if (c >= 32) snprintf(cs, sizeof(cs), "'%c'", (char)c); else snprintf(cs, sizeof(cs), "code %u", c);
  Serial.printf("[NODE] apply seq=%u word=%u/%u %s (%+ld ms) color=%02X%02X%02X bri=%u fx=%u period=%u numBoards=%u\n",
                (unsigned)s.showSeq, s.wordIdx, s.wordCount, cs, (long)lead, s.r, s.g, s.b, s.brightness, s.effect,
                s.effectPeriodMs, s.numBoards);
}

// ---------------- RX (core 0) ----------------
static void onAssign(const uint8_t* srcMac, const Assign* a) {
  learnHub(srcMac);
  sendAck(ASSIGN, a->h.seq);                        // always ACK, even repeats
  if (st == NodeState::UNASSIGNED) return;          // stale retry after RESETORD: ignore
  if (a->pos == 0) {                                // table full
    Serial.println("[NODE] ASSIGN pos 0 (hub table full)");
    savePos(0);
    showBang = true;
    setState(NodeState::SHOW_POSITION);
    return;
  }
  if (a->pos == myPos && (st == NodeState::RUNNING || st == NodeState::LOST_HUB || st == NodeState::SHOW_POSITION))
    return;                                         // same position, nothing to re-show
  showBang = false;
  savePos(a->pos);
  setState(NodeState::SHOW_POSITION);
}

static void onShow(const uint8_t* srcMac, const Show* s, int8_t rssi) {
  learnHub(srcMac);
  hubRssi = rssi;
  lastShowMs = millis();                            // any Show feeds the LOST_HUB timer

  // Hub restart: its clock and showSeq start over. Forget our ordering and re-seed the offset
  // (the low-pass would otherwise take seconds to converge and the signed seq test would
  // ignore every frame until the hub caught up).
  bool hubReset = haveApplied && (int32_t)(s->hubMillis - lastHubMillis) < -5000;
  lastHubMillis = s->hubMillis;
  if (hubReset) {
    Serial.println("[NODE] hub clock went backwards: hub restarted, resyncing");
    portENTER_CRITICAL(&showMux);
    haveApplied = false; havePending = false;
    portEXIT_CRITICAL(&showMux);
    hubOffsetInit = false;
  }

  // Clock first, so the lead below uses the fresh offset
  int32_t sample = (int32_t)(s->hubMillis + 2 - millis());     // +2 ms air/queue latency
  if (!hubOffsetInit) { hubOffset = sample; hubOffsetInit = true; }
  else hubOffset = hubOffset + (sample - hubOffset) / 4;

  // Ordering against the newest seq we know; <= 0 = duplicate/older (heartbeat of the applied
  // group, repeat of the pending burst, a stale heartbeat overtaking a scheduled frame): ignore.
  bool known; uint16_t newest;
  portENTER_CRITICAL(&showMux);
  known = haveApplied || havePending;
  newest = havePending ? pending.showSeq : applied.showSeq;
  portEXIT_CRITICAL(&showMux);
  if (known && (int16_t)(s->showSeq - newest) <= 0) {
    if (st == NodeState::LOST_HUB) setState(NodeState::RUNNING);
    return;
  }

  int32_t lead = s->applyAtHubMs ? (int32_t)(s->applyAtHubMs - hubNow()) : 0;
  if (s->applyAtHubMs == 0 || lead <= 0) {          // apply now (a pending frame is never newer than this one)
    portENTER_CRITICAL(&showMux);
    memcpy(&applied, s, sizeof(Show));
    haveApplied = true;
    havePending = false;
    portEXIT_CRITICAL(&showMux);
    logApply(*s, lead);
  } else {                                          // scheduled: wait for applyAtHubMs in render()
    portENTER_CRITICAL(&showMux);
    memcpy(&pending, s, sizeof(Show));
    pendingLead = lead;
    havePending = true;
    portEXIT_CRITICAL(&showMux);
    Serial.printf("[NODE] pending seq=%u word=%u/%u in %ld ms\n", (unsigned)s->showSeq, s->wordIdx, s->wordCount, (long)lead);
  }
  if (st == NodeState::LOST_HUB) setState(NodeState::RUNNING);
}

void handlePacket(const uint8_t* srcMac, const uint8_t* buf, uint8_t len, int8_t rssi) {
  const Hdr* h = (const Hdr*)buf;
  switch (h->type) {
    case ASSIGN:
      if (len >= sizeof(Assign)) onAssign(srcMac, (const Assign*)buf);
      break;
    case SHOW:
      if (len >= sizeof(Show)) onShow(srcMac, (const Show*)buf, rssi);
      break;
    case SETPOS:
      if (len >= sizeof(SetPos)) {
        const SetPos* sp = (const SetPos*)buf;
        learnHub(srcMac);
        sendAck(SETPOS, h->seq);
        if (sp->newPos >= 1 && sp->newPos <= MAX_BOARDS) {
          Serial.printf("[NODE] SETPOS %d -> %d\n", myPos, sp->newPos);
          showBang = false;
          dropPending();
          savePos(sp->newPos);
          st = NodeState::BOOT;                       // force the transition log even if already SHOW_POSITION
          setState(NodeState::SHOW_POSITION);
        }
      }
      break;
    case IDENTIFY:
      if (len >= sizeof(Identify)) {
        const Identify* id = (const Identify*)buf;
        if (h->flags & FLAG_NEEDS_ACK) { learnHub(srcMac); sendAck(IDENTIFY, h->seq); }   // unicast identify (D1)
        if (display::identifyActive() && (h->flags & FLAG_NEEDS_ACK)) break;             // retry of one we already started
        display::identifyStart(id->ms, myPos);
        Serial.printf("[NODE] IDENTIFY %u ms\n", id->ms);
      }
      break;
    case RESETORD:
      if (st != NodeState::UNASSIGNED) {
        Serial.println("[NODE] RESETORD received");
        dropPending();
        savePos(0);
        showBang = false;
        setState(NodeState::UNASSIGNED);
      }
      break;
    case FORGET:                                      // hub dropped our row (web/serial `forget`)
      if (h->flags & FLAG_NEEDS_ACK) { learnHub(srcMac); sendAck(FORGET, h->seq); }      // ACK even when already UNASSIGNED (D1 retries)
      if (myPos && h->dstPos == myPos) {
        Serial.printf("[NODE] FORGET from hub (pos %d): clearing NVS pos -> UNASSIGNED\n", myPos);
        dropPending();
        savePos(0);
        showBang = false;
        setState(NodeState::UNASSIGNED);
      }
      break;
    default:
      break;
  }
}

// ---------------- actions (core 1) ----------------
void onClaim() {
  if (st != NodeState::UNASSIGNED && st != NodeState::LOST_HUB) {
    Serial.printf("[NODE] CLAIM ignored in %s\n", stateName(st));
    return;
  }
  Hdr h = makeHdr(CLAIM, 0, POS_HUB);
  radio::send(radio::BCAST, &h, sizeof(h), false);
  Serial.println("[NODE] CLAIM sent");
  setState(NodeState::DISCOVER);                    // DISCOVER's HELLO loop takes over...
  nextHelloAt = millis() + HELLO_INTERVAL;          // ...after giving the CLAIM's ASSIGN a chance to land
}

void onForget() {
  Hdr h = makeHdr(FORGET, myPos, POS_HUB);
  radio::send(hubKnown ? hubMac : radio::BCAST, &h, sizeof(h), false);
  Serial.println("[NODE] FORGET sent");
  savePos(0);
  showBang = false;
  setState(NodeState::UNASSIGNED);
}

// ---------------- lifecycle ----------------
void begin() {
  myPos = storage::loadNodePos();
  Serial.printf("[NODE] NVS pos=%d\n", myPos);
  buttons::onLong(K1_PIN, onClaim, LONG_PRESS_NODE_MS);
  buttons::onLong(K2_PIN, onForget, LONG_PRESS_NODE_MS);
  pinMode(STATUS_PIN, OUTPUT);
  setState(NodeState::DISCOVER);
}

void tick() {
  uint32_t now = millis();
  switch (st) {
    case NodeState::DISCOVER: {
      if ((int32_t)(now - nextHelloAt) >= 0) {
        sendHello();
        nextHelloAt = now + HELLO_INTERVAL + random(-HELLO_JITTER, HELLO_JITTER + 1);
      }
      if (now - stateEnteredAt >= DISCOVER_TIMEOUT) setState(NodeState::LOST_HUB);
      break;
    }
    case NodeState::SHOW_POSITION: {
      if (now - stateEnteredAt < SHOW_POSITION_MIN) break;
      if (showBang) { showBang = false; setState(NodeState::UNASSIGNED); }
      else if (haveApplied && now - lastShowMs < LOST_HUB_TIMEOUT) setState(NodeState::RUNNING);
      else if (now - stateEnteredAt >= SHOW_POSITION_MIN + LOST_HUB_TIMEOUT) setState(NodeState::LOST_HUB);
      break;
    }
    case NodeState::RUNNING: {
      if (now - lastShowMs > LOST_HUB_TIMEOUT) { setState(NodeState::LOST_HUB); break; }
      if (hubKnown && (int32_t)(now - nextHbAt) >= 0) {
        sendHb();
        nextHbAt = now + HB_INTERVAL + (uint32_t)myPos * HB_STAGGER_PER_POS;
      }
      break;
    }
    case NodeState::LOST_HUB: {
      if ((int32_t)(now - nextHelloAt) >= 0) {      // 0.2 Hz so a rebooted hub re-learns us
        sendHello();
        nextHelloAt = now + HELLO_LOST_INTERVAL;
      }
      break;
    }
    case NodeState::UNASSIGNED:                     // no HELLO: wait for power cycle or CLAIM
    case NodeState::BOOT:
    default:
      break;
  }

  // Status LED: 1 Hz DISCOVER, solid RUNNING, fast LOST_HUB, off otherwise
  uint32_t period = 0;
  switch (st) {
    case NodeState::DISCOVER:      period = 500; break;
    case NodeState::LOST_HUB:      period = 100; break;
    case NodeState::SHOW_POSITION: period = 250; break;
    default: break;
  }
  if (st == NodeState::RUNNING) digitalWrite(STATUS_PIN, HIGH);
  else if (period == 0) digitalWrite(STATUS_PIN, LOW);
  else if (now - statusLedAt >= period) { statusLedAt = now; digitalWrite(STATUS_PIN, !digitalRead(STATUS_PIN)); }
}

// Draw the frame for the current state. Only called from loop() (core 1).
void render() {
  if (display::identifyActive()) { display::identifyRender(); return; }

  // Scheduled Show due? Swap it in at the hub-clock instant (same test as hub::applyDue()).
  if (havePending) {
    Show p; bool due = false; int32_t lead = 0;
    portENTER_CRITICAL(&showMux);
    if (havePending && (int32_t)(hubNow() - pending.applyAtHubMs) >= 0) {
      memcpy(&p, &pending, sizeof(Show));
      memcpy(&applied, &pending, sizeof(Show));
      lead = pendingLead;
      haveApplied = true; havePending = false; due = true;
    }
    portEXIT_CRITICAL(&showMux);
    if (due) logApply(p, lead);
  }

  Show s; bool have;
  portENTER_CRITICAL(&showMux);
  have = haveApplied;
  if (have) memcpy(&s, &applied, sizeof(Show));
  portEXIT_CRITICAL(&showMux);

  CRGB hubColor = have ? display::styled(s.r, s.g, s.b, s.brightness, s.effect, s.effectPeriodMs, hubNow())
                       : CRGB::Green;

  char showChar = 0;
  if (have && myPos >= 1 && myPos <= s.numBoards) showChar = s.chars[myPos - 1];

  switch (st) {
    case NodeState::DISCOVER: {
      uint8_t lvl = display::pulseLevel();
      if (myPos) display::renderDigit(myPos, display::scaled(CRGB::Green, lvl));
      else       display::renderChar('?', display::scaled(CRGB::Orange, lvl));
      break;
    }
    case NodeState::SHOW_POSITION:
      if (showBang) display::renderChar('!', CRGB::Red);
      else          display::renderDigit(myPos, hubColor);
      break;
    case NodeState::RUNNING:
      if (showChar) display::renderChar(showChar, hubColor); else display::clear();
      if (showChar != renderedChar) {
        renderedChar = showChar;
        if (showChar >= 32) Serial.printf("[NODE] render '%c'\n", showChar);
        else Serial.printf("[NODE] render code %d\n", showChar);
      }
      break;
    case NodeState::LOST_HUB:
      if (showChar)    display::renderChar(showChar, hubColor);   // keep last character
      else if (myPos)  display::renderDigit(myPos, CRGB::Green);
      else             display::renderChar('?', CRGB::Orange);
      display::setCornerDot(true, CRGB(60, 0, 0));
      break;
    case NodeState::UNASSIGNED:
      display::renderChar('?', display::scaled(CRGB::Red, display::pulseLevel()));
      break;
    default:
      display::clear();
      break;
  }
}

void printStatus() {
  char ms[18]; radio::macToStr(hubMac, ms);
  Serial.printf("role=NODE pos=%d state=%s hub=%s haveShow=%d showSeq=%u lastShow=%lums ago rssi=%d mirror=%d hubOffset=%ld\n",
                myPos, stateName(st), hubKnown ? ms : "none", haveApplied ? 1 : 0,
                haveApplied ? applied.showSeq : 0, (unsigned long)(millis() - lastShowMs), hubRssi, display::mirror,
                (long)hubOffset);
  if (haveApplied)
    Serial.printf("show: color=%02X%02X%02X bri=%u fx=%u period=%u numBoards=%u word=%u/%u wspeed=%u\n",
                  applied.r, applied.g, applied.b, applied.brightness, applied.effect,
                  applied.effectPeriodMs, applied.numBoards, applied.wordIdx, applied.wordCount, applied.wordPeriodMs);
  if (havePending)
    Serial.printf("pending: seq=%u word=%u/%u applyAt=%lu (in %ld ms)\n", (unsigned)pending.showSeq, pending.wordIdx,
                  pending.wordCount, (unsigned long)pending.applyAtHubMs, (long)(int32_t)(pending.applyAtHubMs - hubNow()));
}

} // namespace node
