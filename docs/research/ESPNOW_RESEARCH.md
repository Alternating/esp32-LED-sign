# ESP-NOW + Bluetooth Hub for the Multi-Board LED Sign — Research Report

Date: 2026-11-08. Target: ESP32-WROOM-32 30-pin dev boards, Arduino-ESP32 core 3.x (ESP-IDF 5.x),
FastLED, one 20x20 WS2812B matrix (400 LEDs) per board, LiFePO4 6V 6Ah + XL4015 per board, up to 10 boards.

---

## 0. Bottom line

**Verdict: YES, with caveats. A BLE hub + ESP-NOW fan-out is viable on the original ESP32, but only if the hub is set up a specific way.**

1. **ESP-NOW TX while BLE is active is rated "Y — stable"** by Espressif. The hub → nodes direction (99% of your traffic) is the safe direction. Nodes never run BLE, so their ESP-NOW RX is unaffected.
2. **ESP-NOW RX while BLE is active is rated "S — stable only in STA mode, otherwise not supported."** Older docs (IDF 5.0) rated it "X — not supported." Two independent Arduino-core-3.x reports (GitHub issue #10900, a Reddit thread) found that ESP-NOW RX died after `BLEDevice::init()` in plain `WIFI_STA` mode and came back in `WIFI_AP_STA` mode (or with SW coexistence disabled). The mechanism: an un-associated STA with coexistence enabled is forced into "disconnected-state power management", so the Wi-Fi RF is time-shared with BLE and reception becomes lossy. Mitigations are simple and listed in §2.
3. **Everything the hub RECEIVES (HELLO, ACK, heartbeat) must be retry-tolerant.** Design the protocol so that node → hub traffic is rare, small, and repeated until acknowledged. Then occasional loss in the BLE time-slice is invisible.
4. **Alternative with zero coexistence risk:** hub runs a Wi-Fi AP + web page (like the old firmware) and ESP-NOW on the same channel. AP mode forces the radio on, ESP-NOW RX is reliable, no phone app is needed. Cost: higher hub current and the phone leaves its normal Wi-Fi while controlling. Honest recommendation: build the AP+web version first (lowest risk, reuses your old web UI), keep BLE as a phase-2 upgrade once the ESP-NOW layer is proven.
5. **Classic Bluetooth SPP + ESP-NOW** is also listed as supported (same "S"/"Y" table for BR/EDR), but Bluedroid classic BT + Wi-Fi together is the heaviest combination for flash/RAM on a 4MB WROOM; prefer BLE (NimBLE) if you go the Bluetooth route.
6. **FastLED on 400 LEDs with the radio on:** flicker is a real risk on Arduino core 3.x (RMT5). Mitigations: `FASTLED_RMT5=0` to force the RMT4 driver, or run `FastLED.show()` from a task pinned to core 1 with Wi-Fi/BT on core 0, keep the 470 Ω resistor + level shifter. Details in §4.
7. **Power:** radio-on adds roughly 100–130 mA at 3.3V (~0.4 W) per node vs the current radio-off firmware. Against a ~25 W LED load that is <2% — about 1–2 minutes of the 83-minute runtime. Not a design driver.

---

## 1. ESP-NOW facts (ESP-IDF 5.x / Arduino core 3.x)

| Item | Value | Source |
|---|---|---|
| Max payload v1.0 | **250 B** (`ESP_NOW_MAX_DATA_LEN`) | [IDF esp_now API][idf-espnow] |
| Max payload v2.0 | **1470 B** (`ESP_NOW_MAX_DATA_LEN_V2`), IDF ≥ 5.4 (Arduino core 3.1+ builds on 5.3/5.4/5.5 depending on release). v1 devices drop/truncate >250 B. Keep ≤250 B for safety. | [IDF esp_now API][idf-espnow], [Espressif FAQ][faq] |
| Max peers | **20 total** (`ESP_NOW_MAX_TOTAL_PEER_NUM`); encrypted default 7, max 17 (`CONFIG_ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM`). Broadcast receivers need no peer entry; 10 unencrypted peers is well inside limits. | [IDF esp_now API][idf-espnow], [FAQ][faq] |
| Broadcast | Add peer `FF:FF:FF:FF:FF:FF` first. Receivers do not need the sender registered. Broadcast cannot be encrypted and gets **no MAC-layer ACK** — the send callback always reports success. | [IDF esp_now API][idf-espnow], [MicroPython espnow docs][mp] |
| Unicast ACK | Send callback = `ESP_NOW_SEND_SUCCESS` when the **MAC layer** acked. Does not prove the app consumed it (RX buffer overflow still acks). Use an app-level ACK for anything that matters. | [IDF esp_now API][idf-espnow], [MicroPython espnow docs][mp] |
| Channel | Peer channel 0 = "current channel"; otherwise must equal the device's own channel. All nodes must sit on one fixed channel. | [IDF esp_now API][idf-espnow] |
| PHY rate | Default **1 Mbps** (802.11b). Per-peer rate via `esp_now_set_peer_rate_config()` (IDF ≥ 5.2). Measured 1:1 throughput ≈ 214 kbps open air / 555 kbps shielded. | [FAQ][faq] |
| Latency | <20 ms one-way up to 300 m open field (C6 test); indoor single-hop typically 1–5 ms; QuickESPNow reports sync send confirm in 1–20 ms. | [Espressif outdoor blog][outdoor], [QuickESPNow][qen] |
| Range | ~100% delivery to 150 m open field at default rate, ~60% at 300 m. In a building: strongly wall-dependent; an 8-node test at 4 pkt/s found roughly half the nodes reliable across a university floor. LR mode: ~100% to 450 m open field. | [outdoor blog][outdoor], [reliability blog][reliab] |
| Wi-Fi must be started | `WiFi.mode(WIFI_STA)` + `esp_wifi_start()` before `esp_now_init()`. | [IDF esp_now API][idf-espnow] |

**Realistic packet rate for 10 nodes.** A 250 B frame at 1 Mbps is ~2.3 ms air time plus ~0.5 ms overhead. A hub doing one broadcast per frame could sustain >100 frames/s, but you do not need that: send *state*, not pixels. Plan for:
- Hub → all: 1 broadcast per state change plus a **2–4 Hz heartbeat/tick** (≈ 1 kB/s). Trivial.
- Each node → hub: 1 HELLO on boot (retried at 1 Hz until ASSIGN arrives), 1 ACK per command, heartbeat every 2–5 s. With 10 nodes that is <5 pkt/s inbound.
- Keep **sends serialized**: wait for the previous send callback before the next `esp_now_send()` or callbacks arrive out of order ([IDF][idf-espnow]). Send from `loop()`, never from inside the RX callback.

---

## 2. CRITICAL: BLE + ESP-NOW coexistence on the original ESP32

### What Espressif says
The RF coexistence table (ESP-IDF 5.1 → 6.x) for Wi-Fi + BLE and Wi-Fi + Classic BT:

| Wi-Fi function | with BLE scan/adv/connecting/connected | with BR/EDR |
|---|---|---|
| STA scan / connecting / connected | Y | Y |
| SoftAP TX beacon | Y | Y |
| SoftAP connecting / connected stations | C1 (unstable) | C1 |
| **ESP-NOW RX** | **S** | **S** |
| **ESP-NOW TX** | **Y** | **Y** |

Legend: Y = stable; C1 = supported but unstable; **S = "supported and performance is stable in STA mode, otherwise not supported"**. In the IDF 5.0 docs the ESP-NOW RX row was **X (not supported)**. ([coexist guide v5.4][coex54], [coexist.rst master][coexrst], [coexist v5.0][coex50])

Mechanism: one 2.4 GHz radio, time-division. In "connected" state Wi-Fi and BLE each get ~50% of each coexistence period. Config `CONFIG_ESP_COEX_SW_COEXIST_ENABLE` (already on in Arduino's precompiled libs). Recommended: BT controller/host on one core, Wi-Fi task on the other ([coexist v5.0][coex50]).

Two more IDF statements matter for an ESP-NOW-only hub that never joins an AP:
- Station default power save is `WIFI_PS_MIN_MODEM`; disable with `esp_wifi_set_ps(WIFI_PS_NONE)` (Arduino: `WiFi.setSleep(false)`). ([wifi guide][wifi])
- "Disconnected State Sleep" (`CONFIG_ESP_WIFI_STA_DISCONNECTED_PM_ENABLE`) is on by default **and forced on in coexistence mode**; with coexistence the ESP-NOW RX window is time-shared with BLE. ([wifi guide][wifi], [FAQ][faq])

### Real-world reports
- **arduino-esp32 issue #10900** (Jan 2025, core 3.1.1 / IDF 5.3.2, ESP32-C3, reproduced on an ESP32 DevKit): ESP-NOW RX stops after `initBLE()`; TX still works. Reported fix: `WiFi.mode(WIFI_MODE_APSTA)` instead of `WIFI_STA`. ([#10900 mirror][gh10900])
- **r/esp32 "ESP-NOW and BLE-UART at the same time"** (2025): identical symptom with the BLE UART example in STA mode; APSTA worked; the poster finally fixed it by disabling `ESP_COEX_SW_COEXIST_ENABLE` in IDF (not possible from Arduino IDE without rebuilding the core libs). ([reddit][reddit-ble])
- **MicroPython espnow docs** (the most battle-tested ESP-NOW wrapper): power-save makes RX unreliable; fixes are (1) `PM_NONE`, (2) activate AP_IF which disables power saving, (3) retry on the sender. ([mp docs][mp])
- Both APSTA workarounds are consistent with the IDF text: an AP must beacon, so the radio cannot sleep.

### Working recipe for the hub (Arduino core 3.x)
```cpp
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <NimBLEDevice.h>          // prefer NimBLE over Bluedroid BLEDevice (smaller flash/RAM)

#define ESPNOW_CHANNEL 6           // fixed on ALL boards; pick 1, 6 or 11

void radioInit() {
  // 1. Wi-Fi first, STA only, fixed channel, NO modem sleep
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);     // make sure no stale AP association
  WiFi.setSleep(false);            // == esp_wifi_set_ps(WIFI_PS_NONE)
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_B | WIFI_PROTOCOL_G | WIFI_PROTOCOL_N);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  // Fallback if hub RX is still lossy with BLE up (see #10900): hidden, client-less AP keeps RF awake
  // WiFi.mode(WIFI_AP_STA); WiFi.softAP("ledsign-hub", nullptr, ESPNOW_CHANNEL, /*hidden=*/1, /*max_conn=*/0);

  // 2. ESP-NOW
  esp_now_init();
  esp_now_register_recv_cb(onEspNowRecv);
  esp_now_register_send_cb(onEspNowSent);
  esp_now_peer_info_t bc = {};
  memset(bc.peer_addr, 0xFF, 6);
  bc.channel = ESPNOW_CHANNEL; bc.ifidx = WIFI_IF_STA; bc.encrypt = false;
  esp_now_add_peer(&bc);

  // 3. BLE last (hub only). Conservative advertising/connection intervals reduce RF contention.
  NimBLEDevice::init("LED-SIGN-HUB");
  NimBLEServer* srv = NimBLEDevice::createServer();
  // ... Nordic UART Service (6E400001-B5A3-F393-E0A9-E50E24DCCA9E) TX/RX characteristics ...
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->setMinInterval(0x0140);     // 200 ms
  adv->setMaxInterval(0x0280);     // 400 ms
  adv->start();
}

// Arduino core 3.x callback signatures
void onEspNowRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  // copy into a FreeRTOS queue / ring buffer; do NOT call esp_now_send() or FastLED.show() here
}
void onEspNowSent(const uint8_t* mac, esp_now_send_status_t status) {
  g_sendBusy = false;              // gate the next send
  if (status != ESP_NOW_SEND_SUCCESS) g_lastSendFailed = true;
}
```
Order matters: **Wi-Fi → channel → ESP-NOW → BLE**. Initialising BLE first and Wi-Fi afterwards is the pattern in the failing reports. Nodes run the same block minus step 3 (and may `esp_bt_controller_disable()` as today).

Also do on every board: Arduino "Partition Scheme" = **Huge APP (3MB No OTA)** or **Minimal SPIFFS**; BLE + Wi-Fi + FastLED will not fit the default 1.25 MB app slot ([Arduino forum][size]).

### Classic Bluetooth SPP + ESP-NOW?
Same table applies (BR/EDR: ESP-NOW TX = Y, RX = S), so it is "possible", but `BluetoothSerial` (Bluedroid classic) is the largest stack, iOS cannot use SPP at all, and Wi-Fi + classic BT inquiry/page scanning is the most RF-hungry combination. Not recommended; use BLE (NimBLE NUS/UART service) if you want Bluetooth.

---

## 3. Alternative phone link: Wi-Fi AP + web page on the hub (as the old firmware did)

Hub: `WiFi.mode(WIFI_AP_STA)` (or plain `WIFI_AP`), `softAP(ssid, pass, ESPNOW_CHANNEL)`, WebServer on 192.168.4.1, ESP-NOW peers on the AP interface or STA interface — both share the one channel. This is exactly the RandomNerd "ESP-NOW + Web Server" pattern (receiver in `WIFI_AP_STA`, senders fixed to its channel) ([RNT][rnt]).

| | BLE hub | Wi-Fi AP + web hub |
|---|---|---|
| ESP-NOW RX reliability | "S" — needs the §2 recipe; expect some loss, retries required | AP keeps RF awake; ESP-NOW RX is normal (AP "TX beacon" row = Y, no BLE) |
| Phone app | Needs a BLE app: Web Bluetooth (Chrome/Android only — **not iOS Safari**), or a generic NUS terminal app, or your own app | Any phone browser, no install, your existing HTML UI |
| Phone connectivity while controlling | Phone keeps its own Wi-Fi/LTE | Phone joins the sign's AP; loses internet (Android may auto-drop a no-internet AP; hub needs a captive-portal DNS to be smooth) |
| Hub current | BLE adv ~ +few mA avg over Wi-Fi RX idle | AP beaconing + RX: ~100–130 mA continuous ([datasheet via deneyapkart][pwr]) |
| Channel | ESP-NOW channel is yours to fix | Same — AP channel = ESP-NOW channel; fix it in `softAP()` |
| Co-existence risk | Medium (documented edge cases) | Low |
| Setup friction | Pair/choose device in app | Join Wi-Fi, open URL |

Recommendation: **phase 1 = AP + web (reuse `LED_Sign_WiFi_Symbol_Edition.ino` UI), phase 2 = add BLE/NUS** once the ESP-NOW protocol is stable. The ESP-NOW node firmware is identical either way.

---

## 4. FastLED + WS2812B with the radio on

- Root cause of flicker: high-priority Wi-Fi/BT interrupts delay the RMT refill ISR; the RMT buffer underruns and the WS2812 reset gap fires mid-frame (≈50 µs jitter measured on a ~35 µs refill interval under light web-server load). ([FastLED RMT.md][rmtmd], [esp32.com thread][flick])
- Arduino core 3.x / IDF 5 → FastLED defaults to **RMT5** (wraps Espressif `led_strip`). Users and the FastLED maintainer report RMT5 is *more* prone to gaps under Wi-Fi than the old RMT4 path. ([RMT.md][rmtmd], [r/FastLED flicker thread][flick2])
- 400 LEDs × 30 µs = **12 ms per `show()`** plus 50–300 µs reset. At 30 fps that is ~36% of core time blocked in RMT ISRs — fine, but keep BLE/Wi-Fi tasks off that core.

Mitigations, in order of effort:
1. **Force RMT4:** add `#define FASTLED_RMT5 0` before `#include <FastLED.h>` (FastLED ≥ 3.9). Maintainer-recommended first step. ([flicker thread][flick2], [3.9.0 release notes][rel390])
2. **Pin rendering to core 1.** Arduino `loop()` already runs on core 1 and Wi-Fi/BT drivers on core 0 by default, so: never call `FastLED.show()` from a radio callback, and do not create your own tasks on core 1 with higher priority than `loop()`. If you move rendering into a task: `xTaskCreatePinnedToCore(renderTask, "led", 8192, nullptr, 2, nullptr, 1)`.
3. **Don't write pixels inside ESP-NOW/BLE callbacks.** Queue the command, apply in the render loop.
4. **Hardware:** keep the 470 Ω series resistor, short data lead, common ground; a 74AHCT125 level shifter (3.3 → 5 V) removes marginal-logic flicker that gets blamed on software.
5. **If flicker persists:** FastLED's ESP32 **I2S** driver (`#define FASTLED_ESP32_I2S`) is DMA-based and "super stable against WiFi" per the maintainer; or NeoPixelBus `NeoEsp32I2s0Ws2812xMethod`. Both work for a single 400-LED strand. ([flicker thread][flick2])
6. `FastLED.setMaxRefreshRate(0)` and avoid `delay()` inside show loops; keep `show()` calls paced from a timer (`EVERY_N_MILLIS`).

---

## 5. Discovery, numbering and command protocol

### Roles and NVS
- Hub stores `mac[6] → position` in `Preferences` (NVS namespace `"sign"`), plus `nextPos`. Nodes store their own `position` and the hub MAC in NVS so a power cycle restores state instantly and the node can show its number before the hub even answers.
- Hub keeps RAM table: `{mac, pos, lastSeenMs, rssi, online}`.

### Boot sequence
1. Hub boots, loads table, starts radio (§2), waits for phone.
2. Node boots → draws "?" → broadcasts **HELLO{mac, nvsPos, fwVer}** every 500 ms (jittered) until it gets **ASSIGN**.
3. Hub on HELLO: known MAC → re-send its stored position; unknown MAC → `pos = nextPos++`, persist, reply **ASSIGN{pos, channel, hubMac, msgSeq}** by unicast (and once more 100 ms later — cheap redundancy). Hub adds the node as a peer at that moment (`esp_now_add_peer`, ≤20).
4. Node on ASSIGN: stores pos, shows the digit big for ~2 s, then **ACK{pos}**, then idles waiting for SHOW/TICK.
5. Hub pushes the current message so a late-joining board immediately shows its character.

### Re-ordering
- **Button route (no app):** long-press K1 on a node → node sends **CLAIM{wantPos = 0}**; hub assigns `nextPos` and bumps conflicts. Long-press K2 → "forget me" (node clears NVS, hub drops the row). A long press on the **hub's** K3 → "RESET ORDER" (clear table, nextPos = 1; nodes re-HELLO). Keeps the exact "power them on one at a time" workflow.
- **App route:** app shows the table; drag to reorder → hub sends **SETPOS{mac, pos}** unicast to each affected node and rewrites NVS.
- Positions are 1-based; hub itself may be position 1 or position 0 (not displaying) — make it a setting.

### Heartbeat / timeout
- Hub broadcasts **TICK** at 2 Hz (carries `hubMillis`, `frame`, `epoch`). Nodes that miss ticks for 5 s show a dim border/dot ("lost hub") but keep displaying the last character.
- Nodes unicast **HB{pos, vbat_mV, rssi}** every 3 s (stagger by `pos × 300 ms`). Hub marks offline after 10 s; app greys it out.

### Packet struct (fits ESP-NOW v1, 250 B)
```cpp
#pragma pack(push, 1)
enum MsgType : uint8_t { HELLO=1, ASSIGN=2, ACK=3, CLAIM=4, SETPOS=5, SHOW=6, TICK=7, HB=8, EFFECT=9, RESETORD=10 };

struct Hdr {              // 8 bytes
  uint8_t  magic;         // 0xA5 — drop anything else (other ESP-NOW users nearby)
  uint8_t  ver;           // protocol version
  uint8_t  type;          // MsgType
  uint8_t  flags;         // bit0 = needsAck, bit1 = isRetry
  uint16_t seq;           // sender sequence; receiver drops duplicates (seq == lastSeq)
  uint8_t  srcPos;        // 0 = hub / unassigned
  uint8_t  dstPos;        // 0xFF = all
};

struct Hello  { Hdr h; uint8_t mac[6]; uint8_t nvsPos; uint8_t fw; uint16_t vbat_mV; };           // 19 B
struct Assign { Hdr h; uint8_t pos; uint8_t channel; uint8_t hubMac[6]; uint32_t hubMillis; };    // 20 B
struct Show   {                           // one broadcast describes the whole sign
  Hdr      h;
  uint32_t applyAtHubMs;                  // when to switch (0 = now) — enables synced word changes
  uint8_t  r, g, b;                       // base color
  uint8_t  brightness;                    // 0..100
  uint8_t  effect;                        // 0 static,1 blink,2 fade,3 rainbow,4 chase...
  uint16_t effectPeriodMs;
  uint8_t  mode;                          // 0 = chars[], 1 = word sequence index, 2 = scroll
  uint8_t  nChars;
  char     chars[16];                     // chars[pos-1] is what board `pos` shows; 0 = blank
  uint8_t  perBoardColorIdx[16];          // optional per-board colour (0 = use base)
};                                        // 8+4+3+1+1+2+1+1+16+16 = 53 B
struct Tick   { Hdr h; uint32_t hubMillis; uint16_t frame; uint8_t epoch; };                       // 15 B
struct Hb     { Hdr h; uint8_t mac[6]; uint16_t vbat_mV; int8_t rssi; uint8_t shownChar; };        // 18 B
#pragma pack(pop)
static_assert(sizeof(Show) <= 250, "fits ESP-NOW v1");
```
Rules: hub re-broadcasts `Show` 3× (0, 60, 250 ms) and again on every TICK while `dirty`; nodes de-dupe on `seq`. Anything that *must* land (ASSIGN/SETPOS) is unicast with `needsAck`, retried up to 5× at 200 ms. Node commands with `dstPos` = my pos or 0xFF are applied; others ignored (lets you keep one broadcast stream).

---

## 6. Time sync for synchronized effects

- Hub `TICK` every 500 ms carries `hubMillis`. Node computes `offset = hubMillis + ~1.5 ms airtime − millis()` and low-pass filters it (`offset += (new − offset)/4`). Effects are computed from `hubNow = millis() + offset`, so blink/fade phase is `hubNow % period` on every board.
- ESP32 crystal drift is typically ≤ ±20–40 ppm → ≤ 2.4 ms/min between any two boards; a 500 ms tick holds inter-board skew to roughly ±2 ms, and 1 Hz ticks would still be ≤ ±5 ms — far below the ~30 ms human threshold for "simultaneous". Jitter sources are the ESP-NOW RX path (~1–3 ms) and BLE time-slicing on the hub TX (irrelevant to nodes).
- Word/phrase changes: hub sends `Show.applyAtHubMs = hubNow + 300`; every node switches on the same hub-clock instant even if the frame arrived 200 ms apart. Nodes that missed it catch up on the next re-broadcast.
- Do not rely on ESP-NOW sends being "instant" from the hub when BLE is connected: the frame can be deferred up to a coexistence slot (tens of ms). The timestamp approach makes that irrelevant.

---

## 7. Library choice (Arduino IDE)

| Option | Notes |
|---|---|
| **Raw `esp_now.h` (IDF API, ships with the core)** — *recommended* | Zero extra dependency, always matches your core/IDF version, every tutorial and Espressif doc uses it, trivially supports fixed channel + `WiFi.setSleep(false)` + coexistence tweaks. Core 3.x changed the RX callback to `(const esp_now_recv_info_t*, const uint8_t*, int)`. ~60 lines of boilerplate total. |
| Built-in `ESP_NOW.h` class (core ≥ 3.0, `ESP_NOW_Peer`) | Espressif's own C++ wrapper with Broadcast Master/Slave and Network examples; `onNewPeer()` callback is a nice fit for HELLO discovery. Fine choice if you prefer OOP; same underlying API. ([arduino-esp32 docs][ard-espnow], [Espressif blog][ard-blog]) |
| QuickESPNow (gmag11) | Nice async queue + RSSI in callback, auto peer registration (no 20-peer worry), ESP8266 too. **No encryption**, no formal releases, core 3.x support must be checked. Good, but adds little for 10 nodes. ([QuickESPNow][qen]) |
| ESPNowW | Thin wrapper, little traction; skip. |
| WifiEspNow (yoursunny) | Clean, ESP8266-oriented; works on ESP32 but adds nothing over raw. |
| painlessMesh | Not ESP-NOW — a TCP/IP mesh over SoftAP+STA; incompatible with your BLE-hub plan and heavy. Skip. |
| Espressif `esp-now` component (v2.x, groups/OTA/provisioning) | ESP-IDF only, not Arduino IDE. Skip. |

Pick **raw `esp_now.h`**, wrap it in one `radio.h` shared by hub and node sketches. BLE: **NimBLE-Arduino** (h2zero) rather than the bundled Bluedroid `BLEDevice` — much smaller flash/RAM, same NUS/UART pattern ([NimBLE-Arduino][nimble]).

---

## 8. Power

ESP32 nominal currents ([ESP32 datasheet figures][pwr]): Wi-Fi RX 95–100 mA; Wi-Fi TX 180–240 mA (bursts of ms); BT/BLE RX 95–100 mA, TX 130 mA; modem-sleep CPU only 20–68 mA. Your current firmware (`esp_wifi_stop(); esp_bt_controller_disable();`) runs at the modem-sleep-ish ~40 mA level.

- **Node with ESP-NOW listening (WIFI_PS_NONE):** ~+100 mA @ 3.3 V ≈ +0.35–0.4 W after the dev board's LDO/USB path (budget ~0.5 W from the 6 V pack). The matrix at 25% brightness draws ~25 W. Radio overhead ≈ 2% → **83 min becomes ~81 min**. Irrelevant.
- **Hub with ESP-NOW + BLE:** coexistence keeps the radio awake more than either alone (one field write-up measured 45 mA avg dual-mode vs 6 mA Wi-Fi-PS-only in a *sleeping sensor* design; for an always-on display the ceiling is simply "radio RX continuously" ≈ 100–130 mA) ([hubble guide][hubble]). Still <3% of LED power.
- **Light sleep + ESP-NOW wake window:** possible (`esp_now_set_wake_window()` + `esp_wifi_connectionless_module_set_wake_interval()`), but the IDF warns the sender must align to the window and coexistence makes it worse ([IDF esp_now][idf-espnow], [coexist][coex54]). Pointless while 400 LEDs are lit. Only worth it for a "display off, standby" mode — and even then, use a hub `SLEEP` command plus a button wake.
- Real power wins are elsewhere: brightness caps you already have, and a per-board `vbat` ADC (6 V pack through a 2:1 divider) reported in `HB` so the app shows battery per board.

---

## 9. Risks and open questions

1. **Hub ESP-NOW RX under BLE** — documented edge case (S / was X). Mitigated by WIFI_PS_NONE, init order, and retries; fallback is a hidden client-less AP or the Wi-Fi-AP-web design. *Test this first, before writing any UI.* Test: hub with BLE connected to phone, 3 nodes sending HB at 1 Hz for 10 min; count loss. <5% is fine for this protocol.
2. **Core 3.x API churn** — RX callback signature, `WiFi.setChannel()`, ESP-NOW v2 payload differences between core 3.0/3.1/3.2/3.3. Pin one core version in the README.
3. **Flash size** — BLE + Wi-Fi + FastLED + 20x20 font will exceed the default partition; use Huge APP. 4 MB WROOM has no OTA room with Huge APP — acceptable for 10 boards flashed over USB.
4. **FastLED flicker with radio on** — likely on RMT5; have `FASTLED_RMT5=0` and the I2S fallback ready; keep level shifter in the BOM.
5. **Channel congestion** — all boards on one fixed channel in a venue full of Wi-Fi; ESP-NOW uses CSMA/CA so it degrades gracefully, but pick the emptiest of 1/6/11 at setup (hub can scan once at boot and broadcast the choice — but nodes must then *find* it: simplest is a compile-time channel for now).
6. **Phone side for BLE** — iOS Safari has no Web Bluetooth; you would need a native/Flutter app or a generic NUS terminal app with a text command grammar (e.g. `SHOW HELLO;COL FF0000;BRI 50`). Web page over Wi-Fi AP avoids this entirely.
7. **Duplicate/foreign packets** — other ESP-NOW devices on the channel; `magic` + `ver` + MAC table filtering handles it. Encryption (LMK per peer) is optional and limited to 17 peers; unencrypted is fine for a sign.
8. **Hub also drives LEDs** — 12 ms `show()` blocks interrupts on core 1 while BLE/Wi-Fi run on core 0; fine, but if the hub gets busy, consider making the hub a non-display controller box (saves one 20x20 board's worth of complexity).
9. **Position persistence vs. re-ordering UX** — decide whether a *power-on order* re-assigns positions every event (clear table on hub boot with a long-press) or whether MAC → position is sticky. Recommend sticky by default with explicit "RESET ORDER".
10. **Open question:** do you want the hub to be a display board or a dedicated small controller (ESP32 + buttons + battery, no matrix)? A dedicated hub removes the FastLED-interrupt contention from the only board that does BLE.

---

## Sources

- [idf-espnow]: ESP-IDF v5.5 ESP-NOW API reference — https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/network/esp_now.html
- [faq]: Espressif FAQ, ESP-NOW (payload v1/v2, peers, rate, sleep) — https://docs.espressif.com/projects/esp-faq/en/latest/application-solution/esp-now.html
- [coex54]: ESP-IDF v5.4 RF Coexistence guide (ESP-NOW RX = S, TX = Y) — https://docs.espressif.com/projects/esp-idf/en/v5.4-beta2/esp32/api-guides/coexist.html
- [coexrst]: ESP-IDF master coexist.rst — https://github.com/espressif/esp-idf/blob/401816e/docs/en/api-guides/coexist.rst
- [coex50]: ESP-IDF v5.0 RF Coexistence guide (ESP-NOW RX = X) — https://docs.espressif.com/projects/esp-idf/en/v5.0/esp32/api-guides/coexist.html
- [wifi]: ESP-IDF v5.5 Wi-Fi driver guide (power save defaults, disconnected-state PM, LR mode, connectionless modules) — https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-guides/wifi.html
- [gh10900]: arduino-esp32 issue #10900 "ESP NOW stops receiving data after BLE initialization" (mirror) — https://github.com/espressif/arduino-esp32/issues/10900
- [reddit-ble]: r/esp32 "ESP-NOW and BLE-UART at the same time" — https://reddit.birdcat.cafe/r/esp32/comments/1k27jkb/espnow_and_bleuart_at_the_same_time
- [mp]: MicroPython `espnow` docs (Wi-Fi/power-save interaction, broadcast ack semantics, rx buffer drops) — https://docs.micropython.org/en/latest/library/espnow.html
- [outdoor]: Espressif Developer blog, ESP-NOW for outdoor applications (range/latency) — https://developer.espressif.com/blog/esp-now-for-outdoor-applications/
- [reliab]: Espressif Developer blog, Reliability of ESP-NOW in a building — https://developer.espressif.com/blog/reliability-esp-now/
- [ard-espnow]: Arduino-ESP32 ESP-NOW library docs — https://docs.espressif.com/projects/arduino-esp32/en/latest/api/espnow.html
- [ard-blog]: Espressif blog, new Arduino ESP-NOW library (core 3.x) — https://developer.espressif.com/blog/2024/08/arduino-esp-now-lib/
- [qen]: QuickESPNow — https://github.com/gmag11/QuickESPNow
- [nimble]: NimBLE-Arduino — https://github.com/h2zero/NimBLE-Arduino
- [rnt]: RandomNerdTutorials, ESP-NOW + Wi-Fi web server (WIFI_AP_STA, channel matching) — https://randomnerdtutorials.com/esp32-esp-now-wi-fi-web-server/
- [rmtmd]: FastLED RMT.md (RMT4 vs RMT5, ISR priority, Wi-Fi jitter) — https://gh.r-mj.com/coryking/FastLED/blob/master/RMT.md
- [rel390]: FastLED 3.9.0 release notes (RMT5, FASTLED_RMT5=0) — https://reddit.birdcat.cafe/r/FastLED/comments/1gdqtw5/fastled_390_beta_40_released
- [flick2]: r/FastLED flicker thread, maintainer advice (RMT5=0, I2S driver) — https://redlib.ssps.io/r/FastLED/comments/1l4jfly/flicker_problems_yes_another_post_about_that/mwddb69/?context=3
- [flick]: esp32.com "RMT based NeoPixels flicker when WiFi is used" — https://esp32.com/viewtopic.php?p=107129
- [pwr]: ESP32 datasheet current-consumption table (reproduced) — https://deneyapkart.org/en/docs/guc-tuketimi-esp32-2
- [hubble]: ESP32 Wi-Fi and BLE coexistence power write-up — https://hubble.com/community/guides/esp32-wi-fi-and-ble-coexistence-why-enabling-both-kills-your-battery/
- [size]: Arduino forum, BLE + Wi-Fi sketch too big / partition scheme — https://forum.arduino.cc/t/ble-and-mqtt-sketch-on-esp32-too-big/1253242
- ESP32-WROOM-32 datasheet (TX power) — https://documentation.espressif.com/esp32-wroom-32_datasheet_en.pdf
