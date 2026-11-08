/*
 * Bench: font_test — hardware check for the extended character_patterns.h
 * Board: LOLIN32 (ESP32-WROOM-32), 20x20 WS2812B on GPIO 4 (serpentine rows, like legacy v4)
 *
 * What it does:
 *  - Prints chip model/revision, flash size, MAC (needed for the hub position table), core version
 *  - Cycles through every new glyph at low brightness, 1.5 s each, printing which one is shown
 *  - Then cycles the full set once
 * Radios are disabled (same as legacy standalone firmware) so this is a pure display/toolchain check.
 *
 * Serial: 115200. Send 'n' to skip to the next glyph, 'm' to toggle mirror, 'p' to pause/resume.
 */
// NOTE: FastLED >= 3.10 on IDF 5.x has NO RMT4 fallback (FASTLED_RMT5 0 is a hard #error).
// Using the RMT5 default. If flicker appears with radios on: try #define FASTLED_ESP32_I2S 1, or pin FastLED 3.9.x.
#include <FastLED.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include "esp_bt.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_mac.h"
#include "character_patterns.h"

#define LEDS_PER_BOARD  400
#define MATRIX_WIDTH    20
#define MATRIX_HEIGHT   20
#define LED_PIN         4
#define LED_TYPE        WS2812B
#define COLOR_ORDER     GRB
#define BRIGHTNESS_PCT  10      // keep current low: 400 LEDs green @10% ~ 0.5 A
#define HOLD_MS         1500

CRGB leds[LEDS_PER_BOARD];
bool mirror = false;
bool paused = false;

struct Glyph { const char* name; char code; };
const Glyph NEW_GLYPHS[] = {
  {"?",'?'},{"!",'!'},{"\"",'"'},{":",':'},{"&",'&'},{"$",'$'},{"#",'#'},{"+",'+'},
  {".",'.'},{",",','},{"'",'\''},{"-",'-'},{"/",'/'},{"@",'@'},{"%",'%'},{"(",'('},{")",')'},{"=",'='},
  {"HAPPY(13)",13},{"SAD(14)",14},{"TEN(15)",15}
};
const char FULL_SET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

void renderChar(char c, CRGB color) {
  const bool* pattern = getCharacterPattern(c);
  for (int row = 0; row < MATRIX_HEIGHT; row++) {
    for (int col = 0; col < MATRIX_WIDTH; col++) {
      int bitmapCol = mirror ? col : (MATRIX_WIDTH - 1 - col);   // v4 normal = horizontally mirrored bitmap
      int bitmapIndex = row * MATRIX_WIDTH + bitmapCol;
      int ledIndex = (row % 2 == 0) ? row * MATRIX_WIDTH + col
                                    : row * MATRIX_WIDTH + (MATRIX_WIDTH - 1 - col);
      leds[ledIndex] = pattern[bitmapIndex] ? color : CRGB::Black;
    }
  }
  FastLED.show();
}

void printBoardInfo() {
  esp_chip_info_t ci; esp_chip_info(&ci);
  uint32_t flash = 0; esp_flash_get_size(NULL, &flash);
  uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_STA);
  Serial.println("\n================ BOARD INFO ================");
  Serial.printf("Chip: %s rev %d, %d cores, features:%s%s\n",
    (ci.model == CHIP_ESP32) ? "ESP32" : "other", ci.revision, ci.cores,
    (ci.features & CHIP_FEATURE_WIFI_BGN) ? " WiFi" : "", (ci.features & CHIP_FEATURE_BT) ? " BT" : "");
  Serial.printf("Flash: %u MB   Free heap: %u B   PSRAM: %u B\n", flash / (1024 * 1024), ESP.getFreeHeap(), ESP.getPsramSize());
  Serial.printf("STA MAC: %02X:%02X:%02X:%02X:%02X:%02X   (short id 0x%02X%02X)\n", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5], mac[4], mac[5]);
  Serial.printf("Arduino-ESP32 core: %s   IDF: %s   FastLED: %d.%d.%d\n", ESP_ARDUINO_VERSION_STR, esp_get_idf_version(),
    FASTLED_VERSION / 1000000, (FASTLED_VERSION / 1000) % 1000, FASTLED_VERSION % 1000);
  Serial.printf("Sketch: %u B used of %u B\n", ESP.getSketchSize(), ESP.getSketchSize() + ESP.getFreeSketchSpace());
  Serial.println("============================================\n");
}

void waitHold() {
  unsigned long t0 = millis();
  while (paused || millis() - t0 < HOLD_MS) {
    if (Serial.available()) {
      char k = Serial.read();
      if (k == 'n') return;
      if (k == 'p') { paused = !paused; Serial.println(paused ? "[paused]" : "[resumed]"); }
      if (k == 'm') { mirror = !mirror; Serial.printf("[mirror %s]\n", mirror ? "ON" : "OFF"); return; }
    }
    delay(10);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  esp_wifi_stop();
  esp_bt_controller_disable();
  printBoardInfo();

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, LEDS_PER_BOARD);
  FastLED.setBrightness(map(BRIGHTNESS_PCT, 0, 100, 0, 255));
  FastLED.clear(true);

  unsigned long t = micros(); FastLED.show(); t = micros() - t;
  Serial.printf("FastLED.show() for 400 LEDs: %lu us\n", t);
  Serial.println("Keys: n=next  p=pause  m=mirror\n");
}

void loop() {
  Serial.println("--- NEW GLYPHS ---");
  for (auto &g : NEW_GLYPHS) {
    Serial.printf("Showing %s\n", g.name);
    renderChar(g.code, CRGB::Green);
    waitHold();
  }
  Serial.println("--- FULL SET (A-Z, 0-9) ---");
  for (const char* p = FULL_SET; *p; p++) {
    Serial.printf("Showing %c\n", *p);
    renderChar(*p, CRGB::Blue);
    waitHold();
  }
}
