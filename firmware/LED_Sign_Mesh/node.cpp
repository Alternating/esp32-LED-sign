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
// handlePacket() runs on core 0 and only updates state/flags; render() on core 1 draws.

namespace node {

static volatile NodeState st = NodeState::BOOT;
static volatile uint8_t  myPos = 0;
static uint32_t stateEnteredAt = 0;

static uint8_t hubMac[6] = {0};
static volatile bool hubKnown = false;

// Latest Show, copied under showMux by the RX handler, read by render()
static Show latestShow;
static volatile bool     haveShow = false;
static volatile uint32_t lastShowMs = 0;
static portMUX_TYPE showMux = portMUX_INITIALIZER_UNLOCKED;

static volatile bool showBang = false;          // SHOW_POSITION shows "!" (ASSIGN pos 0)
static uint32_t nextHelloAt = 0;
static uint32_t nextHbAt = 0;
static uint32_t statusLedAt = 0;
static int8_t   hubRssi = 0;
static char     renderedChar = 0;

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
  m.state = (uint8_t)st; m.lastShowSeq = haveShow ? latestShow.showSeq : 0;
  radio::send(hubMac, &m, sizeof(m), false);
}

static void savePos(uint8_t p) {
  if (p != myPos) storage::saveNodePos(p);
  myPos = p;
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
  bool apply = !haveShow || s->showSeq != latestShow.showSeq || st != NodeState::RUNNING;
  if (apply) {
    portENTER_CRITICAL(&showMux);
    memcpy(&latestShow, s, sizeof(Show));
    haveShow = true;
    portEXIT_CRITICAL(&showMux);
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
          savePos(sp->newPos);
          st = NodeState::BOOT;                       // force the transition log even if already SHOW_POSITION
          setState(NodeState::SHOW_POSITION);
        }
      }
      break;
    case IDENTIFY:
      if (len >= sizeof(Identify)) {
        const Identify* id = (const Identify*)buf;
        display::identifyStart(id->ms);
        Serial.printf("[NODE] IDENTIFY %u ms\n", id->ms);
      }
      break;
    case RESETORD:
      if (st != NodeState::UNASSIGNED) {
        Serial.println("[NODE] RESETORD received");
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
  setState(NodeState::DISCOVER);                    // DISCOVER's HELLO loop takes over
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
      else if (haveShow && now - lastShowMs < LOST_HUB_TIMEOUT) setState(NodeState::RUNNING);
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

  Show s; bool have;
  portENTER_CRITICAL(&showMux);
  have = haveShow;
  if (have) memcpy(&s, &latestShow, sizeof(Show));
  portEXIT_CRITICAL(&showMux);

  CRGB hubColor = have ? CRGB(s.r, s.g, s.b) : CRGB::Green;
  if (have && s.brightness < 100) hubColor.nscale8((uint8_t)((uint16_t)s.brightness * 255 / 100));

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
  Serial.printf("role=NODE pos=%d state=%s hub=%s haveShow=%d showSeq=%u lastShow=%lums ago rssi=%d mirror=%d\n",
                myPos, stateName(st), hubKnown ? ms : "none", haveShow ? 1 : 0,
                haveShow ? latestShow.showSeq : 0, (unsigned long)(millis() - lastShowMs), hubRssi, display::mirror);
}

} // namespace node
