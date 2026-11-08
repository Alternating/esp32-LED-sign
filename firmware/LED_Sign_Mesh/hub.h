// hub.h — hub state machine, MAC->pos table, Show builder (PROJECT_PLAN.md §3.3)
#ifndef HUB_H
#define HUB_H

#include <Arduino.h>
#include "config.h"

namespace hub {

enum class HubState : uint8_t { BOOT, SHOW_POSITION, RUNNING };

void begin();                      // load table, enter SHOW_POSITION, register gestures
void tick();                       // core 1: timers, Show heartbeat / bursts, RESETORD burst
void render();                     // core 1: draw this board's frame
void handlePacket(const uint8_t* srcMac, const uint8_t* buf, uint8_t len, int8_t rssi);  // core 0 (radio::pump)

// Console / gesture actions
void onResetOrd();                 // clear table, RESETORD x5
void onIdentifyAll();              // IDENTIFY broadcast + flash self
void setTestString(const char* s); // bumps showSeq, Show burst
void printStatus();

HubState    state();
const char* stateName(HubState s);
uint8_t     nextPos();

} // namespace hub

#endif
