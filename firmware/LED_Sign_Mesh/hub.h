// hub.h — hub state machine, MAC->pos table, per-board telemetry, word sequencer + Show builder
// (PROJECT_PLAN.md §2.5, §3.3, step-3 brief items 3-5, step-2 brief item 3).
//
// Threading: tick()/render() run on core 1 (loop); handlePacket() on core 0 (radio::pump);
// applySign()/sign()/buildState()/identifyPos()/forgetPos() may be called from either core
// (web handlers on core 0, serial console on core 1). Every mutation of the live sign state
// lands in a pending copy under signMux and is applied in tick() — commands never touch the
// Show that sendShow()/render() read.
#ifndef HUB_H
#define HUB_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"
#include "sign_state.h"

namespace hub {

enum class HubState : uint8_t { BOOT, SHOW_POSITION, RUNNING };

void begin();                      // load table + SignState, enter SHOW_POSITION, register gestures
void tick();                       // core 1: pending->live swap, auto-save, Show heartbeat / bursts, RESETORD burst
void render();                     // core 1: apply a due word change, draw this board's frame
void handlePacket(const uint8_t* srcMac, const uint8_t* buf, uint8_t len, int8_t rssi);  // core 0 (radio::pump)

// Sign state (any core)
void      applySign(const SignState& s);   // queue for tick(): layout, showSeq++, burst, auto-save in 2 s
SignState sign();                          // the state the next Show will carry (pending if queued, else live)
void      saveNow();                       // persist on the next tick (NVS write stays on core 1)
uint8_t   numBoardsEffective();            // pinned boards, or highest owned position

// /state JSON (brief §4): root gets "cfg" and "state" (hub row first, self:true)
void buildState(JsonObject root);

// Console / gesture / command actions
void onResetOrd();                                    // clear table, RESETORD x5
void onIdentifyAll();                                 // IDENTIFY broadcast + flash self (IDENTIFY_MS)
bool identifyPos(uint8_t pos, uint16_t ms);           // 255 = all; 1 = self; else unicast. false = unknown pos
bool setMirrorPos(uint8_t pos, bool v);              // 1 = self; else unicast SETMIRROR (ACK). false = unknown pos
bool forgetPos(uint8_t pos);                          // drop row, save, unicast FORGET. false = pos 1 or unknown
void nextGroup();                                     // serial 'n': advance to the next word group now (core 1)
void printStatus();

// OTA queue (Phase 4, review §R3). Callable from any core; the queue lives under tMux, tick() drives it.
// otaNodes(): queue every ONLINE node whose buildId differs from ours (unknown 00000000 counts as different).
// otaNode(pos): queue one. otaCancel(): drop the queued rows; the node in flight finishes or times out.
// err (on false): "busy" (hub upload running), "no_image" (image length unknown), "bad_arg", "offline",
// "up_to_date", "queued" (already queued / in flight).
bool otaNodes(uint8_t& queued, uint8_t& skipped, const char*& err);
bool otaNode(uint8_t pos, const char*& err);
uint8_t otaCancel();                                  // returns the number of queued rows dropped
bool otaBusy();                                       // a node OTA is queued or in flight (POST /ota refuses)
void buildOta(JsonObject o);                          // the /state.ota object (also GET /ota)
bool healthy();                                       // rollback confirm: RUNNING + AP up + channel 6

HubState    state();
const char* stateName(HubState s);
uint8_t     nextPos();

} // namespace hub

#endif
