#include "display.h"
// character_patterns.h defines getCharacterPattern() (non-inline) in the header, so it
// is included from exactly this one translation unit.
#include "character_patterns.h"

namespace display {

CRGB leds[LEDS_PER_BOARD];
bool mirror = false;

static uint32_t lastShowMs = 0;
static uint32_t identifyUntil = 0;
static uint8_t  identifyPos   = 0;    // digit shown while identifying (0 = '?')

void begin() {
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, LEDS_PER_BOARD);
  FastLED.setBrightness(map(BRIGHTNESS_PCT, 0, 100, 0, 255));
  FastLED.setMaxRefreshRate(0);        // we pace show() ourselves
  fill(CRGB::Black);
}

void bootAnimation() {
  const CRGB seq[3] = { CRGB::Red, CRGB::Green, CRGB::Blue };
  for (int i = 0; i < 3; i++) {
    fill(seq[i]);       FastLED.show(); delay(300);
    fill(CRGB::Black);  FastLED.show(); delay(300);
  }
}

// Pattern arrays are row-major, row 0 = top, col 0 = left. The wiring flips columns
// (as in espnow_ping: bi uses W-1-col), so a "normal" board maps pattern col c to
// physical col W-1-c; a mirrored board maps it straight. Rows are serpentine: even rows
// run left->right, odd rows right->left.
int ledIndex(int row, int col, bool mirror) {
  int pc = mirror ? col : (MATRIX_WIDTH - 1 - col);
  return (row % 2 == 0) ? row * MATRIX_WIDTH + pc
                        : row * MATRIX_WIDTH + (MATRIX_WIDTH - 1 - pc);
}

void clear() { fill(CRGB::Black); }

void fill(CRGB color) { fill_solid(leds, LEDS_PER_BOARD, color); }

void renderChar(char c, CRGB color) { renderChar(c, color, mirror); }

void renderChar(char c, CRGB color, bool mir) {
  const bool* p = getCharacterPattern(c);
  if (!p) { clear(); return; }
  for (int row = 0; row < MATRIX_HEIGHT; row++)
    for (int col = 0; col < MATRIX_WIDTH; col++)
      leds[ledIndex(row, col, mir)] = p[row * MATRIX_WIDTH + col] ? color : CRGB::Black;
}

void renderDigit(uint8_t pos, CRGB color) {
  char c;
  if (pos >= 1 && pos <= 9) c = (char)('0' + pos);
  else if (pos == 10)       c = (char)15;          // PATTERN_TEN
  else                      c = '?';
  renderChar(c, color, mirror);
}

void setCornerDot(bool on, CRGB color) {
  leds[ledIndex(0, MATRIX_WIDTH - 1, mirror)] = on ? color : CRGB::Black;
}

CRGB styled(uint8_t r, uint8_t g, uint8_t b, uint8_t bri, uint8_t effect, uint16_t periodMs, uint32_t hubNowMs) {
  if (periodMs < 50) periodMs = 500;
  CRGB c(r, g, b);
  switch (effect) {
    case 1:                                                   // blink: on/off every period
      if ((hubNowMs / periodMs) & 1) return CRGB::Black;
      break;
    case 2: {                                                 // fade: triangle 30..100 % over the period
      uint32_t half = periodMs / 2;
      uint32_t ph = hubNowMs % periodMs;
      uint32_t tri = ph < half ? ph : periodMs - ph;
      uint8_t lvl = (uint8_t)(77 + (uint32_t)178 * tri / half);
      c.nscale8(lvl);
      break;
    }
    case 3: {                                                 // cycle: hue steps 5 per period
      uint8_t hue = (uint8_t)((hubNowMs / periodMs) * 5);
      c = CHSV(hue, 255, 255);
      break;
    }
    default: break;
  }
  if (bri > 100) bri = 100;
  if (bri < 100) c.nscale8((uint8_t)((uint16_t)bri * 255 / 100));
  return c;
}

uint8_t pulseLevel() { return beatsin8(20, 40, 160); }   // 20 BPM, dim breathing

CRGB scaled(CRGB c, uint8_t scale) { c.nscale8(scale); return c; }

void identifyStart(uint16_t ms, uint8_t pos) { identifyUntil = millis() + ms; identifyPos = pos; }

bool identifyActive() { return identifyUntil != 0 && (int32_t)(identifyUntil - millis()) > 0; }

void identifyRender() {
  // Plan §3.3: identify = flash this board's POSITION DIGIT (white), 2 Hz, so the user
  // can read the assigned number off the sign rather than just seeing a white strobe.
  // Owner preference 2026-11-08: purple at 50 % so identify is distinct from any chosen message colour.
  bool on = (millis() / 250) & 1;
  if (on) renderDigit(identifyPos, scaled(CRGB::Purple, 128)); else clear();
}

void show() {
  uint32_t now = millis();
  if (now - lastShowMs < FRAME_MS) return;
  lastShowMs = now;
  FastLED.show();
}

} // namespace display
