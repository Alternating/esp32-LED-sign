/*
 * LED Sign - Final Production Version (BACKWARDS BOARD)
 * LOLIN32 + 4-Button Control Board + 10000µF Capacitor
 * 
 * ⚠️ THIS VERSION IS FOR BACKWARDS-WIRED BOARDS ⚠️
 * Use this if your LED data line starts on the OPPOSITE side
 * and characters appear mirrored/backwards
 * 
 * ONLY DIFFERENCE FROM NORMAL VERSION:
 * Horizontal mirroring removed in displayCurrentCharacter()
 * 
 * If characters look correct with normal version, don't use this!
 * 
 * HARDWARE CONFIGURATION:
 * ========================
 * LED Matrix: 20x20 WS2812B (400 LEDs)
 * Controller: LOLIN32 ESP32
 * Buttons: 4-button control board
 * Power: 10000µF 25V capacitor at buck converter (CRITICAL!)
 * 
 * FEATURES:
 * =========
 * - 46 Characters: A-Z (26) + 0-9 (10) + Symbols (10)
 * - 7 Colors + Rainbow mode
 * - 4 Brightness levels (25%, 50%, 75%, 100%)
 * - EEPROM memory (saves all settings)
 * - Color-aware power management (prevents crashes)
 * - Smooth fade transitions
 * 
 * BUTTON FUNCTIONS:
 * =================
 * K1 (GPIO 17): Character FORWARD (A→B→C...)
 * K2 (GPIO 16): Character REVERSE (C→B→A...)
 * K3 (GPIO 18): Color select (Red→Green→Blue→...)
 * K4 (GPIO 19): Brightness (25%→50%→75%→100%)
 * 
 * POWER REQUIREMENTS:
 * ===================
 * CRITICAL: 10000µF 25V capacitor at buck converter output!
 * Without capacitor: Voltage sag → crashes on color change
 * With capacitor: Stable voltage → reliable operation
 * 
 * Wait 2-3 seconds after power-on before pressing buttons
 * (allows capacitor to fully charge)
 * 
 * WIRING:
 * =======
 * LED Data:     GPIO 4 → 470Ω → LED Data In
 * Button K1:    GPIO 17 → K1 → GND (internal pullup)
 * Button K2:    GPIO 16 → K2 → GND (internal pullup)
 * Button K3:    GPIO 18 → K3 → GND (internal pullup)
 * Button K4:    GPIO 19 → K4 → GND (internal pullup)
 * 
 * Power:        5V → 10000µF cap → Buck → LEDs + LOLIN32
 * Common GND:   All grounds connected together!
 * 
 * Version: 4.0 - Final Production
 * Date: January 2026
 */

#include <FastLED.h>
#include <EEPROM.h>
#include "esp_wifi.h"
#include "esp_bt.h"

// ----- LED CONFIGURATION -----
#define LEDS_PER_BOARD    400
#define MATRIX_WIDTH      20
#define MATRIX_HEIGHT     20
#define LED_PIN           4
#define LED_TYPE          WS2812B
#define COLOR_ORDER       GRB

// ----- 4-BUTTON BOARD CONFIGURATION -----
#define BUTTON_K1         17          // Character forward
#define BUTTON_K2         16          // Character reverse
#define BUTTON_K3         18          // Color select
#define BUTTON_K4         19          // Brightness control

// ----- SETTINGS -----
#define DEBOUNCE_DELAY    50
#define LONG_PRESS_TIME   1000
#define FADE_DELAY        20
#define EEPROM_SIZE       512

// ----- BRIGHTNESS PRESETS -----
#define NUM_BRIGHTNESS_LEVELS  4
const uint8_t BRIGHTNESS_PRESETS[NUM_BRIGHTNESS_LEVELS] = {
  25,   // Level 1: Low (good for battery life)
  50,   // Level 2: Medium (balanced)
  75,   // Level 3: High (bright)
  100   // Level 4: Maximum (full power)
};

// ----- COLOR-AWARE BRIGHTNESS LIMITS -----
// These are MAXIMUM values - actual brightness scaled by user selection
#define MAX_BRIGHTNESS_SINGLE    100   // Red, Green, Blue
#define MAX_BRIGHTNESS_DUAL      80    // Pink, Purple, Orange
#define MAX_BRIGHTNESS_WHITE     60    // White (R+G+B)
#define MAX_BRIGHTNESS_RAINBOW   70    // Rainbow mode
#define BOOT_BRIGHTNESS          25    // Boot animation

// ----- BOOT DELAYS -----
#define POWER_STABILIZE   1000
#define LED_INIT_DELAY    500

// ----- LED ARRAY -----
CRGB leds[LEDS_PER_BOARD];

// ----- CHARACTER SET -----
const char CHARACTER_SET[] = {
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
  'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
  '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
  1, 2, 3, 4, 5, 6, 7, 8, 9, 10
};
const int NUM_CHARACTERS = sizeof(CHARACTER_SET);

// ----- COLOR DEFINITIONS -----
struct ColorOption {
  const char* name;
  CRGB color;
  bool isRainbow;
  uint8_t maxBrightness;    // Maximum safe brightness
  uint8_t numLEDsOn;        // 1=single, 2=dual, 3=triple
};

const ColorOption COLOR_OPTIONS[] = {
  {"Red",     CRGB::Red,     false, MAX_BRIGHTNESS_SINGLE,  1},
  {"Green",   CRGB::Green,   false, MAX_BRIGHTNESS_SINGLE,  1},
  {"Blue",    CRGB::Blue,    false, MAX_BRIGHTNESS_SINGLE,  1},
  {"Pink",    CRGB::Pink,    false, MAX_BRIGHTNESS_DUAL,    2},
  {"Purple",  CRGB::Purple,  false, MAX_BRIGHTNESS_DUAL,    2},
  {"Orange",  CRGB::Orange,  false, MAX_BRIGHTNESS_DUAL,    2},
  {"White",   CRGB::White,   false, MAX_BRIGHTNESS_WHITE,   3},
  {"Rainbow", CRGB::White,   true,  MAX_BRIGHTNESS_RAINBOW, 3}
};
const int NUM_COLORS = sizeof(COLOR_OPTIONS) / sizeof(ColorOption);

// ----- STATE -----
int currentCharIndex = 0;
int currentColorIndex = 1;      // Default: Green
int currentBrightnessLevel = 1; // Default: 50% (index 1)
uint8_t rainbowHue = 0;

// ----- BUTTON STATE -----
struct ButtonState {
  bool lastState;
  bool currentState;
  unsigned long lastDebounceTime;
  unsigned long pressStartTime;
  bool longPressHandled;
};

ButtonState buttonK1 = {HIGH, HIGH, 0, 0, false};
ButtonState buttonK2 = {HIGH, HIGH, 0, 0, false};
ButtonState buttonK3 = {HIGH, HIGH, 0, 0, false};
ButtonState buttonK4 = {HIGH, HIGH, 0, 0, false};

// ----- EEPROM ADDRESSES -----
#define EEPROM_CHAR_INDEX        0
#define EEPROM_COLOR_INDEX       1
#define EEPROM_BRIGHTNESS_LEVEL  2
#define EEPROM_MAGIC             3
#define MAGIC_NUMBER             42

// ----- CHARACTER PATTERNS -----
#include "character_patterns.h"

// ========================================
// SETUP
// ========================================
void setup() {
  Serial.begin(115200);
  delay(POWER_STABILIZE);
  
  Serial.println("\n================================");
  Serial.println("LED SIGN v4.0 - Production");
  Serial.println("4-Button Control Board");
  Serial.println("10000µF Capacitor Stabilized");
  Serial.println("================================");
  
  // Disable WiFi/BT for lower power
  esp_wifi_stop();
  esp_bt_controller_disable();
  Serial.println("WiFi/BT disabled");
  
  // Initialize 4-button board
  pinMode(BUTTON_K1, INPUT_PULLUP);
  pinMode(BUTTON_K2, INPUT_PULLUP);
  pinMode(BUTTON_K3, INPUT_PULLUP);
  pinMode(BUTTON_K4, INPUT_PULLUP);
  Serial.println("4-button board ready");
  
  // Load settings
  EEPROM.begin(EEPROM_SIZE);
  loadSettings();
  
  // Delay before LED init
  delay(LED_INIT_DELAY);
  
  // Init LEDs
  Serial.println("Starting LEDs...");
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, LEDS_PER_BOARD);
  FastLED.setBrightness(map(BOOT_BRIGHTNESS, 0, 100, 0, 255));
  FastLED.clear();
  FastLED.show();
  
  delay(200);
  
  // Startup animation
  startupAnimation();
  
  // Display initial character at user brightness
  updateBrightnessDisplay();
  displayCurrentCharacter();
  
  Serial.println("\nSystem Ready!");
  Serial.println("\nButton Functions:");
  Serial.println("  K1 (GPIO 17): Character Forward");
  Serial.println("  K2 (GPIO 16): Character Reverse");
  Serial.println("  K3 (GPIO 18): Color Select");
  Serial.println("  K4 (GPIO 19): Brightness Level");
  Serial.println("================================\n");
  
  printCurrentStatus();
}

// ========================================
// MAIN LOOP
// ========================================
void loop() {
  checkButton(BUTTON_K1, buttonK1, handleButtonK1);
  checkButton(BUTTON_K2, buttonK2, handleButtonK2);
  checkButton(BUTTON_K3, buttonK3, handleButtonK3);
  checkButton(BUTTON_K4, buttonK4, handleButtonK4);
  
  if (COLOR_OPTIONS[currentColorIndex].isRainbow) {
    updateRainbow();
  }
  
  delay(10);
}

// ========================================
// BUTTON HANDLING
// ========================================
void checkButton(int buttonPin, ButtonState &state, void (*shortPressCallback)()) {
  bool reading = digitalRead(buttonPin);
  
  if (reading != state.lastState) {
    state.lastDebounceTime = millis();
  }
  
  if ((millis() - state.lastDebounceTime) > DEBOUNCE_DELAY) {
    if (reading != state.currentState) {
      state.currentState = reading;
      
      if (state.currentState == LOW) {
        state.pressStartTime = millis();
        state.longPressHandled = false;
      }
      
      if (state.currentState == HIGH) {
        unsigned long pressDuration = millis() - state.pressStartTime;
        
        if (!state.longPressHandled && pressDuration < LONG_PRESS_TIME) {
          shortPressCallback();
        }
      }
    }
    
    if (state.currentState == LOW && !state.longPressHandled) {
      if (millis() - state.pressStartTime > LONG_PRESS_TIME) {
        state.longPressHandled = true;
      }
    }
  }
  
  state.lastState = reading;
}

// K1: Character Forward
void handleButtonK1() {
  currentCharIndex++;
  if (currentCharIndex >= NUM_CHARACTERS) {
    currentCharIndex = 0;
  }
  
  fadeOutAndUpdate();
  saveSettings();
  printCurrentStatus();
}

// K2: Character Reverse
void handleButtonK2() {
  currentCharIndex--;
  if (currentCharIndex < 0) {
    currentCharIndex = NUM_CHARACTERS - 1;
  }
  
  fadeOutAndUpdate();
  saveSettings();
  printCurrentStatus();
}

// K3: Color Select
void handleButtonK3() {
  currentColorIndex++;
  if (currentColorIndex >= NUM_COLORS) {
    currentColorIndex = 0;
  }
  
  if (COLOR_OPTIONS[currentColorIndex].isRainbow) {
    rainbowHue = 0;
  }
  
  fadeOutAndUpdate();
  saveSettings();
  printCurrentStatus();
}

// K4: Brightness Level
void handleButtonK4() {
  currentBrightnessLevel++;
  if (currentBrightnessLevel >= NUM_BRIGHTNESS_LEVELS) {
    currentBrightnessLevel = 0;
  }
  
  Serial.print("Brightness changed to: ");
  Serial.print(BRIGHTNESS_PRESETS[currentBrightnessLevel]);
  Serial.println("%");
  
  // Smooth brightness transition (no full fade out)
  uint8_t targetBrightness = getEffectiveBrightness();
  uint8_t currentBrightness = FastLED.getBrightness();
  
  if (targetBrightness > currentBrightness) {
    // Fade up
    for (int i = currentBrightness; i <= targetBrightness; i += 5) {
      FastLED.setBrightness(i);
      FastLED.show();
      delay(10);
    }
  } else {
    // Fade down
    for (int i = currentBrightness; i >= targetBrightness; i -= 5) {
      FastLED.setBrightness(i);
      FastLED.show();
      delay(10);
    }
  }
  
  FastLED.setBrightness(targetBrightness);
  FastLED.show();
  
  saveSettings();
  printCurrentStatus();
}

// ========================================
// DISPLAY FUNCTIONS
// ========================================
void fadeOutAndUpdate() {
  // Fade out
  for (int i = 255; i > 0; i -= 10) {
    FastLED.setBrightness(i);
    FastLED.show();
    delay(FADE_DELAY);
  }
  
  displayCurrentCharacter();
  
  // Fade in to effective brightness
  uint8_t targetBrightness = getEffectiveBrightness();
  for (int i = 0; i <= targetBrightness; i += 10) {
    FastLED.setBrightness(i);
    FastLED.show();
    delay(FADE_DELAY);
  }
}

uint8_t getEffectiveBrightness() {
  // Get user-selected brightness
  uint8_t userBrightness = BRIGHTNESS_PRESETS[currentBrightnessLevel];
  
  // Get color-specific max brightness
  uint8_t colorMaxBrightness = COLOR_OPTIONS[currentColorIndex].maxBrightness;
  
  // Scale user brightness by color max (prevent overload)
  uint8_t effectiveBrightness = (userBrightness * colorMaxBrightness) / 100;
  
  // Convert to FastLED scale (0-255)
  return map(effectiveBrightness, 0, 100, 0, 255);
}

void updateBrightnessDisplay() {
  FastLED.setBrightness(getEffectiveBrightness());
}

void displayCurrentCharacter() {
  FastLED.clear();
  
  char currentChar = CHARACTER_SET[currentCharIndex];
  const bool* pattern = getCharacterPattern(currentChar);
  
  if (pattern == nullptr) {
    Serial.println("ERROR: Pattern not found!");
    return;
  }
  
  CRGB color = COLOR_OPTIONS[currentColorIndex].color;
  
  for (int row = 0; row < MATRIX_HEIGHT; row++) {
    for (int col = 0; col < MATRIX_WIDTH; col++) {
      // BACKWARDS BOARD: No horizontal mirror (data starts opposite side)
      int bitmapIndex = row * MATRIX_WIDTH + col;
      
      int ledIndex;
      if (row % 2 == 0) {
        ledIndex = row * MATRIX_WIDTH + col;
      } else {
        ledIndex = row * MATRIX_WIDTH + (MATRIX_WIDTH - 1 - col);
      }
      
      if (pattern[bitmapIndex]) {
        leds[ledIndex] = color;
      } else {
        leds[ledIndex] = CRGB::Black;
      }
    }
  }
  
  FastLED.show();
}

void updateRainbow() {
  static unsigned long lastUpdate = 0;
  unsigned long currentTime = millis();
  
  if (currentTime - lastUpdate > 50) {
    rainbowHue += 2;
    
    for (int i = 0; i < LEDS_PER_BOARD; i++) {
      if (leds[i]) {
        leds[i] = CHSV(rainbowHue, 255, 255);
      }
    }
    
    FastLED.show();
    lastUpdate = currentTime;
  }
}

// ========================================
// SETTINGS (EEPROM)
// ========================================
void loadSettings() {
  uint8_t magic = EEPROM.read(EEPROM_MAGIC);
  
  if (magic == MAGIC_NUMBER) {
    currentCharIndex = EEPROM.read(EEPROM_CHAR_INDEX);
    currentColorIndex = EEPROM.read(EEPROM_COLOR_INDEX);
    currentBrightnessLevel = EEPROM.read(EEPROM_BRIGHTNESS_LEVEL);
    
    // Validate
    if (currentCharIndex >= NUM_CHARACTERS) currentCharIndex = 0;
    if (currentColorIndex >= NUM_COLORS) currentColorIndex = 1;
    if (currentBrightnessLevel >= NUM_BRIGHTNESS_LEVELS) currentBrightnessLevel = 1;
    
    Serial.println("Settings loaded from memory");
  } else {
    currentCharIndex = 0;          // 'A'
    currentColorIndex = 1;         // Green
    currentBrightnessLevel = 1;    // 50%
    
    EEPROM.write(EEPROM_MAGIC, MAGIC_NUMBER);
    EEPROM.commit();
    
    Serial.println("First boot - defaults set");
  }
}

void saveSettings() {
  EEPROM.write(EEPROM_CHAR_INDEX, currentCharIndex);
  EEPROM.write(EEPROM_COLOR_INDEX, currentColorIndex);
  EEPROM.write(EEPROM_BRIGHTNESS_LEVEL, currentBrightnessLevel);
  EEPROM.commit();
}

// ========================================
// UTILITIES
// ========================================
void printCurrentStatus() {
  Serial.println("Current Status:");
  Serial.print("  Character: ");
  printCharacterName(CHARACTER_SET[currentCharIndex]);
  Serial.print(" (");
  Serial.print(currentCharIndex + 1);
  Serial.print("/");
  Serial.print(NUM_CHARACTERS);
  Serial.println(")");
  
  Serial.print("  Color: ");
  Serial.print(COLOR_OPTIONS[currentColorIndex].name);
  Serial.print(" [");
  Serial.print(COLOR_OPTIONS[currentColorIndex].numLEDsOn);
  Serial.print(" LED");
  if (COLOR_OPTIONS[currentColorIndex].numLEDsOn > 1) Serial.print("s");
  Serial.print("] (");
  Serial.print(currentColorIndex + 1);
  Serial.print("/");
  Serial.print(NUM_COLORS);
  Serial.println(")");
  
  Serial.print("  Brightness: ");
  Serial.print(BRIGHTNESS_PRESETS[currentBrightnessLevel]);
  Serial.print("% (Level ");
  Serial.print(currentBrightnessLevel + 1);
  Serial.print("/");
  Serial.print(NUM_BRIGHTNESS_LEVELS);
  Serial.println(")");
  
  uint8_t effectiveBrightness = (BRIGHTNESS_PRESETS[currentBrightnessLevel] * 
                                 COLOR_OPTIONS[currentColorIndex].maxBrightness) / 100;
  Serial.print("  Effective: ");
  Serial.print(effectiveBrightness);
  Serial.println("% (color-limited)");
  Serial.println();
}

void printCharacterName(char c) {
  if (c >= 'A' && c <= 'Z') {
    Serial.print(c);
  } else if (c >= '0' && c <= '9') {
    Serial.print(c);
  } else {
    switch((uint8_t)c) {
      case 1: Serial.print("Heart"); break;
      case 2: Serial.print("Star"); break;
      case 3: Serial.print("Check"); break;
      case 4: Serial.print("Cross"); break;
      case 5: Serial.print("Arrow Right"); break;
      case 6: Serial.print("Arrow Left"); break;
      case 7: Serial.print("Arrow Up"); break;
      case 8: Serial.print("Arrow Down"); break;
      case 9: Serial.print("Diamond"); break;
      case 10: Serial.print("Circle"); break;
      default: Serial.print("Unknown"); break;
    }
  }
}

void startupAnimation() {
  Serial.println("Startup animation...");
  
  FastLED.setBrightness(map(BOOT_BRIGHTNESS, 0, 100, 0, 255));
  
  // Quick single-color flashes (low current)
  fill_solid(leds, LEDS_PER_BOARD, CRGB::Red);
  FastLED.show();
  delay(100);
  
  fill_solid(leds, LEDS_PER_BOARD, CRGB::Green);
  FastLED.show();
  delay(100);
  
  fill_solid(leds, LEDS_PER_BOARD, CRGB::Blue);
  FastLED.show();
  delay(100);
  
  FastLED.clear(true);
  delay(100);
}