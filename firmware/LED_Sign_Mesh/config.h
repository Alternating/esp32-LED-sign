// config.h — GPIO map, radio channel, timings, build constants (PROJECT_PLAN.md §5.5, §3)
#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ---- Hardware pins (LOLIN32, every board identical) ----
#define LED_PIN         4
#define K1_PIN          17
#define K2_PIN          16
#define K3_PIN          18
#define K4_PIN          19
#define VBAT_PIN        34
#define STATUS_PIN      5

// ---- Radio ----
#define ESPNOW_CHANNEL  6
#define AP_SSID         "LEDSIGN-HUB"
#define AP_PASS         "sign1234"
#define MAX_FRAME       64      // largest protocol frame is Show (40 B)

// ---- Timings (ms) ----
#define POWER_STABILIZE     800
#define ROLE_SERIAL_WINDOW  3000    // boot window for serial 'H'/'N'
#define HELLO_INTERVAL      1000
#define HELLO_JITTER        200
#define HELLO_LOST_INTERVAL 5000    // LOST_HUB: HELLO at 0.2 Hz
#define HB_INTERVAL         3000
#define HB_STAGGER_PER_POS  300
#define SHOW_BURST_GAP1     60      // Show burst on change: 0 / 60 / 250 ms
#define SHOW_BURST_GAP2     190
#define SHOW_HEARTBEAT      500
constexpr uint16_t WORD_LEAD_MS = 300;   // word-advance Show: applyAtHubMs = hubMillis + lead (plan §4.2)
#define LOST_HUB_TIMEOUT    5000
#define DISCOVER_TIMEOUT    15000
#define SHOW_POSITION_MIN   3000
#define RESET_ORD_BCAST     5
#define RESET_ORD_GAP       200
#define ACK_RETRY           5
#define ACK_RETRY_MS        200
#define IDENTIFY_MS         3000
#define OFFLINE_AFTER_MS    10000
#define LONG_PRESS_NODE_MS  2000
#define LONG_PRESS_HUB_MS   3000
#define FRAME_MS            16      // FastLED.show() pacing (~60 Hz)

// ---- System ----
#define FW_VERSION      1
#define MAX_BOARDS      10
#define LEDS_PER_BOARD  400
#define MATRIX_WIDTH    20
#define MATRIX_HEIGHT   20
#define BRIGHTNESS_PCT  10

enum class Role : uint8_t { NODE = 0, HUB = 1 };

#endif
