// display.h — FastLED on GPIO 4, 400 LEDs, 20x20 serpentine. Pure frame-buffer
// rendering; FastLED.show() happens ONLY in display::show(), called from loop() (core 1).
#ifndef DISPLAY_H
#define DISPLAY_H

#include <FastLED.h>
#include "config.h"

namespace display {

extern CRGB leds[LEDS_PER_BOARD];
extern bool mirror;          // single source of truth, loaded from NVS in setup()

void begin();                // addLeds + brightness; call once in setup()
void bootAnimation();        // R/G/B flashes, blocking — setup() only

// Logical (row, col) of a glyph -> physical LED index (serpentine wiring + mirror flag)
int  ledIndex(int row, int col, bool mirror);

void clear();
void renderChar(char c, CRGB color);                 // uses display::mirror
void renderChar(char c, CRGB color, bool mirror);
void renderDigit(uint8_t pos, CRGB color);           // 1..9 -> '1'..'9', 10 -> code 15, else '?'
void setCornerDot(bool on, CRGB color);              // logical row 0, col 19
void fill(CRGB color);

// Effect/brightness styling shared by hub and node renders (brief item 5). Pure, no FastLED
// calls. bri is the already-capped 0..100 value carried in Show.brightness; hubNowMs is the
// hub clock so every board is in phase. effect: 0 static, 1 blink, 2 fade (30-100 %), 3 cycle.
CRGB    styled(uint8_t r, uint8_t g, uint8_t b, uint8_t bri, uint8_t effect, uint16_t periodMs, uint32_t hubNowMs);

uint8_t pulseLevel();                                // 0..255 slow breathing scale for DISCOVER/UNASSIGNED
CRGB    scaled(CRGB c, uint8_t scale);               // c.nscale8(scale), returns a copy

// Non-blocking identify: shows this board's position digit steadily (purple, 50 %) for ms. Render from tick().
void identifyStart(uint16_t ms, uint8_t pos);
bool identifyActive();
void identifyRender();                               // draws the current flash phase

void show();                                         // paced <= 60 Hz; loop() only

} // namespace display

#endif
