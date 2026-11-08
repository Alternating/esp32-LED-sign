// buttons.h — v4 debounce / long-press engine generalised to per-key callbacks
#ifndef BUTTONS_H
#define BUTTONS_H

#include <Arduino.h>
#include "config.h"

namespace buttons {

typedef void (*Callback)();

void begin();                                   // INPUT_PULLUP on K1..K4
void poll();                                    // call from loop()
void onShort(uint8_t pin, Callback cb);         // fires on release before longMs
void onLong(uint8_t pin, Callback cb, uint32_t ms = LONG_PRESS_NODE_MS);  // fires once while held
bool heldAtBoot(uint8_t pin, uint32_t ms = 3000); // true if LOW continuously for ms

} // namespace buttons

#endif
