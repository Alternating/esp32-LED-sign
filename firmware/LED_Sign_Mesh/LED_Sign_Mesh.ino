/*
 * LED_Sign_Mesh — Phase 1 step 1: ESP-NOW radio, protocol, hub/node numbering state machines.
 * Board: LOLIN32 (esp32:esp32:lolin32:PartitionScheme=no_ota), core 3.3.12, FastLED 3.10.9.
 * One binary for hub and node; role persisted in NVS.
 *
 * Role selection at boot (after the boot animation):
 *   - K4 held             -> toggle role (saved)
 *   - serial 'H' / 'N' within 3 s -> set role (saved)
 *   Either way the new role is used immediately, no restart.
 *
 * Serial console (single letters act immediately; 't' takes the rest of the line):
 *   hub : r = reset order, i = identify all, t<text> = test Show string, s = status, m = toggle mirror
 *   node: c = claim, s = status, m = toggle mirror
 *
 * Threading: loop() on core 1 = buttons, serial, role tick, render, FastLED.show() <= 60 Hz.
 *            netTask on core 0 = radio::pump() every 5 ms (RX dispatch + gated TX queue).
 */
#include <WiFi.h>
#include "config.h"
#include "protocol.h"
#include "radio_espnow.h"
#include "storage.h"
#include "display.h"
#include "buttons.h"
#include "battery.h"
#include "hub.h"
#include "node.h"

static Role role = Role::NODE;
static TaskHandle_t netTaskHandle = nullptr;

static const char* roleName(Role r) { return r == Role::HUB ? "HUB" : "NODE"; }

// ---------------- net task (core 0) ----------------
static void netTask(void*) {
  for (;;) {
    radio::pump();
    delay(5);
  }
}

// ---------------- serial console (non-blocking line buffer) ----------------
static char lineBuf[32];
static uint8_t lineLen = 0;
static bool collectingText = false;

static void printStatus() {
  Serial.println("---- status ----");
  if (role == Role::HUB) hub::printStatus(); else node::printStatus();
  uint8_t ch = WiFi.channel();
  Serial.printf("heap=%u minHeap=%u channel=%d(%s) uptime=%lus tx=%u txFail=%u rx=%u rxDrop=%u netStackFree=%u\n",
                ESP.getFreeHeap(), ESP.getMinFreeHeap(), ch, radio::channelOk() ? "OK" : "MISMATCH",
                (unsigned long)(millis() / 1000), radio::txCount(), radio::txFailCount(),
                radio::rxCount(), radio::rxDropCount(),
                netTaskHandle ? (unsigned)uxTaskGetStackHighWaterMark(netTaskHandle) : 0u);
  Serial.println("----------------");
}

static void runCommand(char c) {
  switch (c) {
    case 's': printStatus(); break;
    case 'm':
      display::mirror = !display::mirror;
      storage::saveMirror(display::mirror);
      Serial.printf("mirror=%d (saved)\n", display::mirror);
      break;
    case 'r': if (role == Role::HUB) hub::onResetOrd();    else Serial.println("? (hub only)"); break;
    case 'i': if (role == Role::HUB) hub::onIdentifyAll(); else Serial.println("? (hub only)"); break;
    case 'c': if (role == Role::NODE) node::onClaim();     else Serial.println("? (node only)"); break;
    case 'f': if (role == Role::NODE) node::onForget();    else Serial.println("? (node only)"); break;
    case '\r': case '\n': case ' ': break;
    default:  Serial.printf("? unknown '%c' (r i t<text> s m | c f s m)\n", c); break;
  }
}

static void handleSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (collectingText) {
      if (c == '\n' || c == '\r') {
        lineBuf[lineLen] = 0;
        if (role == Role::HUB) hub::setTestString(lineBuf); else Serial.println("? t is hub only");
        collectingText = false; lineLen = 0;
      } else if (lineLen < sizeof(lineBuf) - 1) {
        lineBuf[lineLen++] = c;
      }
      continue;
    }
    if (c == 't') { collectingText = true; lineLen = 0; continue; }
    runCommand(c);
  }
}

// ---------------- role selection ----------------
static void showRoleGlyph() {
  display::renderChar(role == Role::HUB ? 'H' : 'N', role == Role::HUB ? CRGB::Green : CRGB::Blue);
  FastLED.show();     // setup() only
}

static void selectRole() {
  role = storage::loadRole();
  if (buttons::heldAtBoot(K4_PIN, 3000)) {
    role = (role == Role::HUB) ? Role::NODE : Role::HUB;
    storage::saveRole(role);
    Serial.printf("K4 held: role toggled to %s (saved)\n", roleName(role));
  }
  showRoleGlyph();
  Serial.printf("Role in NVS: %s. Send H or N within %d ms to change...\n", roleName(role), ROLE_SERIAL_WINDOW);
  uint32_t t0 = millis();
  while (millis() - t0 < ROLE_SERIAL_WINDOW) {
    if (Serial.available()) {
      char k = toupper(Serial.read());
      if (k == 'H' || k == 'N') {
        Role r = (k == 'H') ? Role::HUB : Role::NODE;
        if (r != role) { role = r; storage::saveRole(role); showRoleGlyph(); }
        Serial.printf("Role set to %s (saved)\n", roleName(role));
        break;
      }
    }
    delay(10);
  }
}

// ---------------- setup / loop ----------------
void setup() {
  Serial.begin(115200);
  delay(POWER_STABILIZE);
  Serial.printf("\n=== LED_Sign_Mesh fw %d ===\n", FW_VERSION);

  storage::begin();
  buttons::begin();
  display::begin();
  display::mirror = storage::loadMirror();
  display::bootAnimation();
  battery::begin();

  selectRole();

  bool radioOk = (role == Role::HUB) ? radio::beginHub() : radio::beginNode();
  if (role == Role::HUB) { hub::begin();  radio::setHandler(hub::handlePacket); }
  else                   { node::begin(); radio::setHandler(node::handlePacket); }

  xTaskCreatePinnedToCore(netTask, "net", 8192, nullptr, 1, &netTaskHandle, 0);

  char mac[18]; radio::macToStr(radio::myMac(), mac);
  Serial.printf("Running as %s  MAC %s  radio=%s channel=%d %s  mirror=%d  free heap %u\n",
                roleName(role), mac, radioOk ? "OK" : "FAILED", WiFi.channel(),
                radio::channelOk() ? "OK" : "MISMATCH", display::mirror, ESP.getFreeHeap());
}

void loop() {
  buttons::poll();
  handleSerial();
  if (role == Role::HUB) { hub::tick();  hub::render(); }
  else                   { node::tick(); node::render(); }
  display::show();      // paced to <= 60 Hz inside
  delay(1);
}
