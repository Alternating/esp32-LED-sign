// node.h — node state machine and Show application (PROJECT_PLAN.md §3.2)
#ifndef NODE_H
#define NODE_H

#include <Arduino.h>
#include "config.h"

namespace node {

enum class NodeState : uint8_t { BOOT, DISCOVER, SHOW_POSITION, RUNNING, LOST_HUB, UNASSIGNED, OTA };

void begin();                      // load pos, enter DISCOVER, register gestures
void tick();                       // core 1: timers, HELLO/HB sending, state timeouts
void render();                     // core 1: draw this board's frame
void handlePacket(const uint8_t* srcMac, const uint8_t* buf, uint8_t len, int8_t rssi);  // core 0 (radio::pump)

// Console / gesture actions
void onClaim();                    // K1 long / 'c': CLAIM broadcast, -> DISCOVER
void onForget();                   // K2 long: FORGET, clear NVS, -> UNASSIGNED
void printStatus();
bool healthy();                    // rollback confirm: radio on channel 6 and RUNNING reached (or DISCOVERing)

NodeState   state();
const char* stateName(NodeState s);
uint8_t     pos();

} // namespace node

#endif
