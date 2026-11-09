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
#define AP_MAX_CONN     4       // 2 phones + 1 updating node + spare (Phase 4)
#define MAX_FRAME       128     // largest protocol frame is OtaEnter (94 B); static_asserts in protocol.h

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
#define IDENTIFY_MS         10000      // identify shows the position digit for 10 s (owner preference)
#define OFFLINE_AFTER_MS    10000
#define LONG_PRESS_NODE_MS  2000
#define LONG_PRESS_HUB_MS   3000
#define FRAME_MS            16      // FastLED.show() pacing (~60 Hz)

// ---- OTA (Phase 4) ----
#define OTA_PORT              8080      // hub fw server (raw HTTP/1.0, GET /fw.bin), separate from the UI on :80
#define OTA_NODE_TIMEOUT_MS   120000    // hub: per-node budget, OTA_ENTER -> rejoin with the hub's buildId
#define OTA_ACK_TIMEOUT_MS    10000     // hub: no ACK / status for OTA_ENTER within this -> failed "no response"
#define OTA_NODE_DL_MS        90000     // node: join + download + verify must finish within this, else abort
#define OTA_JOIN_MS           10000     // node: Wi-Fi association budget
#define OTA_STALL_MS          10000     // node: no bytes for this long -> abort
#define OTA_VERIFY_MS         30000     // healthy this long after boot -> esp_ota_mark_app_valid_cancel_rollback()

// ---- System ----
#define FW_VERSION      4               // HELLO.fw / cfg.fw (informational; OTA compares buildId, not this)
#define FW_VERSION_STR  "0.4.0"         // bump by hand per release; shown in /state.ota.hub.version and the banner
#define MAX_BOARDS      10
#define LEDS_PER_BOARD  400
#define MATRIX_WIDTH    20
#define MATRIX_HEIGHT   20
#define BRIGHTNESS_PCT  10

enum class Role : uint8_t { NODE = 0, HUB = 1 };

#endif
