#include "buttons.h"

namespace buttons {

static const uint8_t  PINS[4] = { K1_PIN, K2_PIN, K3_PIN, K4_PIN };
static const uint32_t DEBOUNCE_MS = 40;

struct Btn {
  uint8_t  pin;
  bool     rawLast;        // last raw reading (true = pressed)
  bool     stable;         // debounced state
  uint32_t rawChangedAt;   // when raw last changed
  uint32_t pressedAt;      // when the debounced press began
  bool     longFired;
  Callback shortCb;
  Callback longCb;
  uint32_t longMs;
};

static Btn btns[4];

static Btn* find(uint8_t pin) {
  for (auto& b : btns) if (b.pin == pin) return &b;
  return nullptr;
}

void begin() {
  for (int i = 0; i < 4; i++) {
    pinMode(PINS[i], INPUT_PULLUP);
    btns[i] = Btn{ PINS[i], false, false, millis(), 0, false, nullptr, nullptr, LONG_PRESS_NODE_MS };
  }
}

// Debounce: a raw change must hold for DEBOUNCE_MS before it is accepted.
// Short press = debounced release before longMs. Long press = fires once at longMs while held.
void poll() {
  uint32_t now = millis();
  for (auto& b : btns) {
    bool raw = digitalRead(b.pin) == LOW;
    if (raw != b.rawLast) { b.rawLast = raw; b.rawChangedAt = now; }
    if (raw != b.stable && now - b.rawChangedAt >= DEBOUNCE_MS) {
      b.stable = raw;
      if (raw) { b.pressedAt = now; b.longFired = false; }
      else if (!b.longFired && b.shortCb) b.shortCb();
    }
    if (b.stable && !b.longFired && b.longCb && now - b.pressedAt >= b.longMs) {
      b.longFired = true;
      b.longCb();
    }
  }
}

void onShort(uint8_t pin, Callback cb) {
  if (Btn* b = find(pin)) b->shortCb = cb;
}

void onLong(uint8_t pin, Callback cb, uint32_t ms) {
  if (Btn* b = find(pin)) { b->longCb = cb; b->longMs = ms; }
}

bool heldAtBoot(uint8_t pin, uint32_t ms) {
  pinMode(pin, INPUT_PULLUP);
  delay(50);
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    if (digitalRead(pin) == HIGH) return false;   // unconnected / released: returns quickly
    delay(10);
  }
  return true;
}

} // namespace buttons
