/*
 * LED_Sign_Mesh — Phase 1 steps 1-3: ESP-NOW radio + numbering (step 1), the hub's web
 * transport (captive portal, JSON commands, phone UI), styling and persistence (step 3), and
 * the word sequencer with synchronized word changes (step 2: Show.applyAtHubMs, pending Shows).
 * Board: LOLIN32 (esp32:esp32:lolin32:PartitionScheme=no_ota), core 3.3.12, FastLED 3.10.9,
 * ArduinoJson 7.4.3. One binary for hub and node; role persisted in NVS.
 *
 * Role selection at boot (after the boot animation):
 *   - K4 held             -> toggle role (saved)
 *   - serial 'H' / 'N' within 3 s -> set role (saved)
 *   Either way the new role is used immediately, no restart.
 *
 * Serial console (single letters act immediately; 't' and '{' take the rest of the line):
 *   hub : r = reset order, i = identify all, n = next word group now, t<text> = msg alias,
 *         s = status, m = toggle mirror, w = web stats,
 *         {json} = command (same handler as POST /cmd; reply printed)
 *   node: c = claim, f = forget, s = status, m = toggle mirror
 *
 * Threading: loop() on core 1 = buttons, serial, role tick, render, FastLED.show() <= 60 Hz,
 *            sign auto-save (NVS). netTask on core 0 = radio::pump() every 5 ms (RX dispatch +
 *            gated TX queue) and, on the hub, transport->poll() (HTTP + DNS).
 * Init order (hub): radio::beginHub(ssid) -> transport->begin() -> hub::begin() -> netTask.
 */
#include <WiFi.h>
#include <ArduinoJson.h>
#include "config.h"
#include "protocol.h"
#include "radio_espnow.h"
#include "storage.h"
#include "display.h"
#include "buttons.h"
#include "battery.h"
#include "hub.h"
#include "node.h"
#include "commands.h"
#include "transport.h"
#include "transport_web.h"

static Role role = Role::NODE;
static TaskHandle_t netTaskHandle = nullptr;
static Transport* transport = nullptr;        // hub only

static const char* roleName(Role r) { return r == Role::HUB ? "HUB" : "NODE"; }

// ---------------- net task (core 0) ----------------
static void netTask(void*) {
  for (;;) {
    radio::pump();
    if (role == Role::HUB && transport) transport->poll();
    delay(5);
  }
}

// ---------------- commands ----------------
static bool cmdHandler(JsonDocument& cmd, JsonDocument& reply) {
  return commands::handle(cmd, reply, role == Role::HUB);
}

// Run one JSON command from the serial console and print the reply (same path as POST /cmd)
static void runJsonLine(const char* line) {
  JsonDocument cmd, reply;
  DeserializationError e = deserializeJson(cmd, line);
  if (e || !cmd.is<JsonObject>()) {
    Serial.printf("{\"ok\":false,\"err\":\"bad_json\",\"note\":\"%s\"}\n", e.c_str());
    return;
  }
  bool ok = cmdHandler(cmd, reply);
  if (!ok && reply["err"].isNull()) reply["err"] = "bad_cmd";
  reply["ok"] = ok;
  serializeJson(reply, Serial);
  Serial.println();
}

static void runTextLine(const char* text) {       // t<text> = msg alias
  JsonDocument cmd, reply;
  cmd["cmd"] = "msg"; cmd["text"] = text;
  bool ok = cmdHandler(cmd, reply);
  if (!ok && reply["err"].isNull()) reply["err"] = "bad_cmd";
  reply["ok"] = ok;
  serializeJson(reply, Serial);
  Serial.println();
}

// ---------------- serial console (non-blocking line buffer) ----------------
enum class Collect : uint8_t { NONE, TEXT, JSON };
static char lineBuf[256];
static uint16_t lineLen = 0;
static Collect collecting = Collect::NONE;

static void printWebStats() {
  if (role != Role::HUB) { Serial.println("? (hub only)"); return; }
  Serial.printf("web: requests=%u lastClient=%s apClients=%u ssid=%s ip=%s\n",
                (unsigned)transport_web::reqCount(), IPAddress(transport_web::lastClientIp()).toString().c_str(),
                transport_web::apClients(), radio::apSsid(), WiFi.softAPIP().toString().c_str());
}

static void printStatus() {
  Serial.println("---- status ----");
  if (role == Role::HUB) hub::printStatus(); else node::printStatus();
  uint8_t ch = WiFi.channel();
  Serial.printf("heap=%u minHeap=%u channel=%d(%s) uptime=%lus tx=%u txFail=%u rx=%u rxDrop=%u netStackFree=%u\n",
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(), ch, radio::channelOk() ? "OK" : "MISMATCH",
                (unsigned long)(millis() / 1000), (unsigned)radio::txCount(), (unsigned)radio::txFailCount(),
                (unsigned)radio::rxCount(), (unsigned)radio::rxDropCount(),
                netTaskHandle ? (unsigned)uxTaskGetStackHighWaterMark(netTaskHandle) : 0u);
  if (role == Role::HUB)
    Serial.printf("web clients=%u requests=%u\n", transport_web::apClients(), (unsigned)transport_web::reqCount());
  Serial.println("----------------");
}

static void runCommand(char c) {
  switch (c) {
    case 's': printStatus(); break;
    case 'w': printWebStats(); break;
    case 'm':
      display::mirror = !display::mirror;
      storage::saveMirror(display::mirror);
      Serial.printf("mirror=%d (saved)\n", display::mirror);
      break;
    case 'r': if (role == Role::HUB) hub::onResetOrd();    else Serial.println("? (hub only)"); break;
    case 'i': if (role == Role::HUB) hub::onIdentifyAll(); else Serial.println("? (hub only)"); break;
    case 'n': if (role == Role::HUB) hub::nextGroup();     else Serial.println("? (hub only)"); break;
    case 'c': if (role == Role::NODE) node::onClaim();     else Serial.println("? (node only)"); break;
    case 'f': if (role == Role::NODE) node::onForget();    else Serial.println("? (node only)"); break;
    case '\r': case '\n': case ' ': break;
    default:  Serial.printf("? unknown '%c' (r i n t<text> {json} s m w | c f s m)\n", c); break;
  }
}

static void handleSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (collecting != Collect::NONE) {
      if (c == '\n' || c == '\r') {
        lineBuf[lineLen] = 0;
        if (collecting == Collect::JSON) runJsonLine(lineBuf);
        else if (role == Role::HUB) runTextLine(lineBuf);
        else Serial.println("? t is hub only");
        collecting = Collect::NONE; lineLen = 0;
      } else if (lineLen < sizeof(lineBuf) - 1) {
        lineBuf[lineLen++] = c;
      }
      continue;
    }
    if (c == 't') { collecting = Collect::TEXT; lineLen = 0; continue; }
    if (c == '{') { collecting = Collect::JSON; lineLen = 0; lineBuf[lineLen++] = '{'; continue; }
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
  if (buttons::heldAtBoot(K1_PIN, 3000)) {                  // plan §5.6: K1 at power-on flips this board's mirror
    display::mirror = !display::mirror;
    storage::saveMirror(display::mirror);
    display::renderChar('E', CRGB::White); display::show(); delay(1500);   // 'E' makes the flip visible
    Serial.printf("K1 held: mirror toggled to %d (saved)\n", display::mirror);
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

  bool radioOk;
  if (role == Role::HUB) {
    char ssid[33];
    storage::loadSsid(ssid, sizeof(ssid), AP_SSID);
    radioOk = radio::beginHub(ssid);               // the ONLY softAP call (channel 6)
    transport = &transport_web::instance();
    transport->setCommandHandler(cmdHandler);
    transport->begin();                            // before the net task exists
    hub::begin();                                  // restores SignState from NVS before the first Show
    radio::setHandler(hub::handlePacket);
  } else {
    radioOk = radio::beginNode();
    node::begin();
    radio::setHandler(node::handlePacket);
  }

  // Hub net task also serves HTTP (WebServer + ArduinoJson on its stack): 12 kB there, node unchanged.
  xTaskCreatePinnedToCore(netTask, "net", role == Role::HUB ? 12288 : 8192, nullptr, 1, &netTaskHandle, 0);

  char mac[18]; radio::macToStr(radio::myMac(), mac);
  Serial.printf("Running as %s  MAC %s  radio=%s channel=%d %s  mirror=%d  free heap %u\n",
                roleName(role), mac, radioOk ? "OK" : "FAILED", (int)WiFi.channel(),
                radio::channelOk() ? "OK" : "MISMATCH", display::mirror, (unsigned)ESP.getFreeHeap());
}

void loop() {
  buttons::poll();
  handleSerial();
  if (role == Role::HUB) { hub::tick();  hub::render(); }
  else                   { node::tick(); node::render(); }
  display::show();      // paced to <= 60 Hz inside
  delay(1);
}
