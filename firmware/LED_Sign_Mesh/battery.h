// battery.h — stub for this step (ADC divider read arrives in Phase 2, plan §4.3)
#ifndef BATTERY_H
#define BATTERY_H

#include <Arduino.h>

namespace battery {
inline void begin() {}
inline uint16_t readMilliVolts() { return 0; }
}

#endif
