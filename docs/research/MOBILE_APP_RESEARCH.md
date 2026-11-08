# Mobile App / Phone Control Research — Multi-Board LED Sign (ESP32 hub + ESP-NOW)

Date: 2026-11-08. Scope: how the phone talks to the hub ESP32, and what the "app" should be.
Baseline features to match: the WiFi Symbol Edition web UI (message text, board-count selector,
quick symbols, color picker, brightness, static/blink/fade/cycle, word-change speed) plus new
board list / positions / battery / reorder.

---

## 1. Bottom line

**Transport: BLE GATT on the hub using NimBLE-Arduino 2.x.** Not Classic SPP (iOS cannot use it),
not Bluedroid (roughly 2x the flash and ~100 kB more RAM than NimBLE). Run the hub as
**BLE peripheral + WiFi STA mode for ESP-NOW**, with the old SoftAP web UI kept only as a
*separate boot mode* (long-press at power-on), never concurrently with BLE: Espressif's coexistence
table marks SoftAP + BLE as unstable and ESP-NOW RX as stable only in STA mode.

**Protocol: Nordic UART Service (standard NUS UUIDs) carrying newline-terminated JSON** ("JSON
lines"), plus one small custom service with a compact binary STATE characteristic (board
positions/battery) and a reserved OTA slot. Using the real NUS UUIDs means Serial Bluetooth
Terminal (Android), Bluefruit Connect (iOS/Android) and nRF Connect all work on day one for
testing, before any UI exists.

**App, primary: a single-file Web Bluetooth PWA** (HTML + vanilla JS, hosted on GitHub Pages or
Cloudflare Pages, both of which Tyler already operates). Works in Chrome/Edge on Android, Windows,
macOS, Linux and ChromeOS. Zero toolchain, no store, no signing, no permissions code — the
browser's device chooser handles everything. Effort: 1–2 weekends. The existing web UI's HTML/CSS
can be lifted nearly verbatim; only `fetch('/update')` is replaced by a BLE write.

**iOS answer: Bluefy ($2.99 App Store browser that implements Web Bluetooth) runs the same page.**
If a "real" iOS app is ever wanted, port to **Flutter + flutter_blue_plus** (fallback), reusing the
exact same JSON protocol. Skip React Native, .NET MAUI and ESPHome as the primary controller
(reasons in section 3). Home Assistant becomes a *secondary* controller later via ESPHome
`ble_client` writing to the same NUS RX characteristic.

---

## 2. Transport on the ESP32 side

| Option | iOS | Android | Flash/RAM on WROOM-32 | Verdict |
|---|---|---|---|---|
| Classic BT SPP (`BluetoothSerial`, Bluedroid) | No (Apple requires MFi for SPP) | Yes | Bluedroid dual-mode stack: heaviest; forum reports of 700–900 KiB flash and large heap | Reject |
| BLE via Arduino `BLEDevice` (Bluedroid) | Yes | Yes | Hackaday build log: BLE lib ">900 KiB", forced `min_spiffs` partition | Works, but wasteful |
| **BLE via NimBLE-Arduino 2.x** | Yes | Yes | esp-nimble-cpp README: "nearly 50% reduction in flash use and approx. 100 kB less RAM" vs Bluedroid | **Choose** |

Notes:
- NimBLE-Arduino 2.3.x is current (2.3.7 released late 2025) and targets arduino-esp32 core 3.x
  (ESP-IDF 5). It is BLE-only, which is exactly what we want; the ESP32-WROOM-32's Classic BT
  radio goes unused. The same code compiles unchanged on ESP32-C3/S3 (BLE-only chips) if the hub
  is ever swapped.
- Memory budget sanity check: hub firmware = FastLED (400 LEDs x 3 B = 1.2 kB; negligible) +
  WiFi STA for ESP-NOW (~50–70 kB heap) + NimBLE (~60–80 kB heap) + ArduinoJson. Fits comfortably
  in the ~300 kB of usable DRAM. Flash: expect ~1.1–1.4 MB; select the **"Huge APP (3 MB No OTA)"**
  or **"Minimal SPIFFS (1.9 MB APP with OTA)"** partition scheme in Arduino IDE to avoid the
  default 1.2 MB app slot.
- Coexistence (ESP-IDF RF coexistence guide, v5.5): WiFi and BLE time-share one 2.4 GHz radio.
  Table rates WiFi STA (scan/connect/connected) + BLE (adv/conn) as stable; **SoftAP + BLE as
  "C1" (unstable)**; **ESP-NOW TX stable, ESP-NOW RX stable only in STA mode**. Arduino core 3.x
  ships with `CONFIG_ESP_COEX_SW_COEXIST_ENABLE` on. Keep the hub in `WIFI_STA` (not connected to
  any AP, just for ESP-NOW) and pin nothing manually; the Arduino defaults put BT and WiFi on
  core 0, `loop()`/FastLED on core 1.
- FastLED + radios: WS2812 timing uses the RMT peripheral on ESP32; occasional glitches under
  heavy WiFi/BT interrupt load are a known issue. Mitigations if seen: keep `FastLED.show()` on
  core 1 (default), minimise notify rate (state updates every 1–2 s, not per frame), and
  consider FastLED's I2S driver (`FASTLED_ESP32_I2S`) which is interrupt-tolerant.

---

## 3. App approach options

| # | Approach | iOS | Android | Dev skill needed | Effort (first usable) | Distribution | Fit for Tyler |
|---|---|---|---|---|---|---|---|
| a | No app: hub SoftAP + captive-portal web page (existing pattern) | Yes | Yes | None new | 1 evening (reuse) | None | Good as **fallback boot mode**; phone loses internet; Android auto-switches off no-internet APs; iOS nags; ties ESP-NOW channel to AP channel; SoftAP+BLE unstable so can't run both |
| b | **Web Bluetooth PWA** (one HTML/JS file on GitHub/Cloudflare Pages) | Only via Bluefy / iOSWebBLE extension | Chrome 56+, Samsung Internet, Edge | HTML/JS (LLM-friendly) | **1–2 weekends** | URL; "Add to Home Screen" | **Primary.** Also works on Windows desktop Chrome/Edge — handy for a PowerShell person |
| c | Flutter + flutter_blue_plus | Yes (needs a Mac + Xcode; free Apple ID = 7-day re-sign, $99/yr for TestFlight) | Yes (APK sideload trivial) | Dart, Android Studio | 1–2 weeks part-time | APK / TestFlight | **Fallback** if native iOS is needed. flutter_blue_plus is mature (1.2k likes), handles MTU, autoConnect, permissions |
| d | React Native + react-native-ble-plx (Expo dev client) | Yes (EAS cloud build avoids local Mac, still needs paid Apple account to install on device) | Yes | JS/TS + RN tooling | 1–2 weeks | APK / TestFlight | Viable but no advantage over (c); must leave Expo Go for a custom dev client |
| e | Generic BLE terminal apps + text protocol | Bluefruit Connect, nRF Connect | Serial Bluetooth Terminal (macros/buttons), nRF Connect, Bluefruit Connect | None | **Day 1** | Store apps | Use for testing and as emergency remote; ugly UX |
| f | .NET MAUI + Plugin.BLE | Yes (Mac required for build) | Yes | C# — Tyler's comfort zone, but MAUI tooling is heavy | 1–2 weeks | APK / TestFlight | Plugin.BLE is alive (3.2.1 stable, 3.3 beta, net9/net10, 2.2M downloads). Realistic for Android-only in C#, but more friction than (b) or (c). Not recommended as primary |
| g | Home Assistant / ESPHome | via HA app | via HA app | YAML | 1 weekend after (b) | HA | **Secondary.** Sign is battery/portable and used away from home WiFi, so HA can't be the only path. ESPHome has `esp32_ble_server`, `ble_client` (`ble_client.ble_write` to arbitrary UUIDs) and since 2025.8 an `espnow` component, so an ESPHome node could bridge HA to the hub |

### Recommendation matrix (1 = poor, 5 = great)

| Criterion | a SoftAP | b WebBT PWA | c Flutter | d RN | e Terminal | f MAUI | g HA |
|---|---|---|---|---|---|---|---|
| Effort for non-mobile-dev | 5 | 4 | 2 | 2 | 5 | 2 | 3 |
| iOS support | 5 | 2 (Bluefy) | 5 | 5 | 4 | 4 | 3 |
| Android support | 4 | 5 | 5 | 5 | 4 | 5 | 3 |
| Coexists with BLE+ESP-NOW hub | 1 | 5 | 5 | 5 | 5 | 5 | 4 |
| UX ceiling | 3 | 4 | 5 | 5 | 1 | 4 | 3 |
| No store / no signing | 5 | 5 | 3 | 3 | 5 | 3 | 5 |
| Reuses existing web UI code | 5 | 5 | 1 | 2 | 0 | 1 | 1 |
| **Total** | 28 | **33** | 26 | 27 | 24 | 24 | 22 |

**Primary: (b) Web Bluetooth PWA. Fallback: (c) Flutter.** Keep (a) as a boot-mode escape hatch
and (e) for bring-up. Add (g) later for home use.

---

## 4. BLE GATT design

### 4.1 Why NUS-style JSON lines
- One write characteristic (RX) + one notify characteristic (TX) is the simplest thing that can
  work and is what every generic terminal app already speaks.
- JSON is self-describing, trivially extended, and ArduinoJson 7 parses it in ~2 kB RAM. A binary
  protocol saves bytes but costs debuggability; our payloads are tiny and infrequent.
- Standard NUS UUIDs buy free tooling. A second *custom* service carries structured state so the
  app does not need to parse log noise, and leaves room for OTA.

### 4.2 Proposed GATT table

| Service / Characteristic | UUID | Props | Size | Purpose |
|---|---|---|---|---|
| **Nordic UART Service** | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` | — | — | Command channel (standard) |
| RX (app -> hub) | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` | Write, Write-no-rsp | up to MTU-3 per write | JSON line fragments; hub buffers to `\n` |
| TX (hub -> app) | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` | Notify | MTU-3 per notify | JSON line fragments (acks, events, errors) |
| **LED Sign Service** (custom) | `4C45445F-5349-474E-0001-000000000001` ("LED_SIGN") | — | — | Structured state |
| INFO | `...-0002-...` | Read | <=64 B | `"fw=1.0;hw=wroom32;max=10;name=LEDSIGN-1A2B"` |
| STATE | `...-0003-...` | Read, Notify | 4 + 6*N B | Binary: `seq u8, nBoards u8, curWordIdx u8, flags u8`, then per board `id u16, pos u8, battDeciVolt u8, rssi i8, ageSec u8` |
| CONFIG | `...-0004-...` | Read | <=200 B | Current config as JSON (so a fresh app can populate controls on connect) |
| OTA (reserved) | `...-0005-...` | Write | — | Future; or use NimBLE OTA example / ArduinoOTA over WiFi STA |
| Device Information (standard 0x180A) | `0x180A` | Read | — | Optional; nRF Connect shows it nicely |

Advertising: name `LEDSIGN-xxxx` (last 2 bytes of MAC), advertise NUS UUID and the custom service
UUID. Web Bluetooth `requestDevice({filters:[{namePrefix:'LEDSIGN'}], optionalServices:[NUS, SIGN]})`.
Note: Web Bluetooth requires every service you will touch to be listed in `optionalServices`.

### 4.3 MTU and chunking rules
- Hub: `NimBLEDevice::setMTU(517)` before `init()`; read the negotiated value in
  `onMTUChange(mtu, connInfo)` and store it per connection.
- Reality: iOS negotiates 185 (Apple central); Android apps request 512–517 (flutter_blue_plus
  requests 512 by default; Chrome on Android negotiates up to 517); desktop Chrome similar.
  Bluefy on iOS inherits 185.
- **Hub -> app:** split each outgoing JSON line into `mtu-3` byte notifications; the app
  reassembles by scanning for `\n`. Rate-limit notifications to one per connection interval
  (~30 ms) — NimBLE queues but will drop under flooding.
- **App -> hub:** app splits lines into `mtu-3` chunks and uses write-without-response (fast)
  except the final chunk, which uses write-with-response (acts as flow control). Hub appends to a
  512-byte line buffer; on overflow discard and send `{"err":"line_too_long"}`.
- Design commands to fit <180 bytes where possible so iOS never needs to chunk; only the `msg`
  command (up to 10 boards x 20 chars + JSON) and `state` events may exceed that.

### 4.4 Proposed JSON-lines command set (app -> hub)

```json
{"cmd":"get"}                                   // hub replies with full config + state
{"cmd":"msg","text":"HELLO {heart} WORLD"}      // symbols as {name} tokens; hub maps to codes 1..14
{"cmd":"boards","n":4}                          // number of active positions (1..10)
{"cmd":"color","hex":"FF8800"}                  // or "rainbow":true
{"cmd":"bri","v":25}                            // 0..100 (hub applies colour-aware caps)
{"cmd":"fx","name":"blink","speed":500}         // static|blink|fade|cycle, speed ms
{"cmd":"wspeed","ms":2000}                      // word-change interval
{"cmd":"order","ids":[41253,9120,33021,7788]}   // explicit board id -> position 1..N
{"cmd":"identify","pos":3,"ms":3000}            // flash that board white so user can find it
{"cmd":"swap","a":2,"b":3}                      // convenience reorder
{"cmd":"save"}                                  // persist to NVS/EEPROM
{"cmd":"name","v":"LEDSIGN-GARAGE"}             // advertised name
{"cmd":"pin","v":"4321"}                        // optional app-level PIN (see section 5)
```
Symbol tokens: `{heart} {star} {check} {cross} {right} {left} {up} {down} {diamond} {circle}
{happy} {sad}`; `?` and `!` are literal. Tokens avoid JSON control-character escaping of the
existing 0x01–0x0E codes and are readable in a terminal app.

### 4.5 Hub -> app events (TX notify)

```json
{"ok":"msg"}                                               // ack, echoes cmd
{"err":"bad_json"} / {"err":"unknown_cmd","cmd":"foo"}
{"ev":"cfg","text":"HELLO WORLD","n":4,"hex":"00FF00","bri":25,"fx":"static","speed":500,"wspeed":2000}
{"ev":"state","seq":17,"word":1,"boards":[
   {"id":41253,"pos":1,"mv":6210,"rssi":-58,"age":1},
   {"id":9120,"pos":2,"mv":5980,"rssi":-71,"age":2}]}       // every 2 s while connected, or on change
{"ev":"board","id":55012,"join":true}                      // new board seen via ESP-NOW
{"ev":"low_batt","id":9120,"mv":5820}
```
Battery mapping for the LiFePO4 6 V packs (from README): 6400 mV=100 %, 6100=50 %, 6000=30 %,
5800=10 % — do the percentage in the app, send raw millivolts from boards.

---

## 5. Pairing, security, reconnect

- **Recommendation: Just Works, no bonding, no encryption on the GATT link.** It is a hobby sign;
  the attack is "someone within ~10 m changes my message". Bonding adds real failure modes:
  iOS error `Code=14 "Peer removed pairing information"` when the ESP32 loses its bond table
  (reflash/NVS erase) and the only fix is Settings > Bluetooth > Forget This Device; Android
  re-pairs automatically. NimBLE-Arduino forum threads show people struggling to get
  `WRITE_ENC` characteristics working with Android even when bonding "succeeds".
- If you want a speed bump, do it in the protocol: hub ignores all commands except `pin` until
  the right 4-digit PIN arrives on that connection (stored in NVS, default off). Trivial to
  implement, portable across every app option, no OS pairing dialogs.
- If you later want real BLE security: `NimBLEDevice::setSecurityAuth(true /*bond*/, false
  /*mitm*/, true /*sc*/)` + `setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT)` = Just Works with
  bonding; mark characteristics `WRITE_ENC`. Passkey needs a display on the sign (you have one:
  show 6 digits across boards) — cute, but defer.
- **How phones remember devices:** iOS exposes a per-app random UUID for the peripheral
  (`retrievePeripherals(withIdentifiers:)`), Android uses the MAC; Flutter/RN wrap both.
  Web Bluetooth: the user picks the device from the chooser each visit unless
  `navigator.bluetooth.getDevices()` is available (Chrome 85+, enabled by default on Android and
  desktop in current Chrome; check `chrome://flags/#enable-web-bluetooth-new-permissions-backend`
  on older builds). Plan the PWA around a one-tap "Connect" button with `namePrefix` filter;
  reconnect = `device.gatt.connect()` on the remembered `BluetoothDevice` object while the tab
  lives, plus `gattserverdisconnected` listener for auto-retry.
- Single central: set NimBLE `max connections = 1`, stop advertising on connect, restart on
  disconnect. Avoids two phones fighting.

---

## 6. Reference projects / starter code

1. **NimBLE-Arduino** (h2zero) — library, examples `NimBLE_Server`, `NimBLE_Notify`,
   `NimBLE_Secure_Server`. https://github.com/h2zero/NimBLE-Arduino and tutorial
   https://lastminuteengineers.com/esp32-nimble-arduino-tutorial/
2. **nimble-nordic-uart** (masuidrive) — minimal NUS on NimBLE (ESP-IDF component; the service
   layout is directly reusable in Arduino): `nordic_uart_start`, `sendln`, receive callback.
   https://components.espressif.com/components/masuidrive/nimble-nordic-uart
3. **nightlark/web-bluetooth-controller** — single `index.html` Web Bluetooth control-page
   template, GitHub Pages hosted; drop in your UUIDs. https://github.com/nightlark/web-bluetooth-controller
4. **Hackaday "ESP32 and the non-standard Web Bluetooth API"** — ESP32 WS2812B matrix controlled
   from a Web Bluetooth PWA; documents HTTPS requirement, `writeValueWithoutResponse`, 512-byte
   write cap, partition-size issue. https://hackaday.io/project/183450/log/202740-esp32-and-the-non-standard-web-bluetooth-api
5. **LSchlierf/LED-Controller** — Flutter app controlling ESP32 LED controllers (Classic serial;
   swap the transport for flutter_blue_plus). Shows the screen structure for a device list +
   control panel. https://github.com/LSchlierf/LED-Controller
6. **flutter_blue_plus** docs — MTU, autoConnect, permissions manifest.
   https://pub.dev/packages/flutter_blue_plus
7. **ESPHome `ble_client` / `esp32_ble_server` / `espnow`** for the HA bridge later.
   https://esphome.io/components/ble_client/ , https://esphome.io/components/esp32_ble_server/ ,
   https://www.esphome.io/components/espnow
8. WLED: note its app is WiFi/HTTP/MQTT only — not a BLE reference, but its JSON API shape
   (`{"on":true,"bri":128,"seg":[...]}`) is a good model for terse JSON state.

---

## 7. UI sketch (PWA, also the Flutter spec)

```
[Connect screen]  LEDSIGN logo | [Connect] (opens browser chooser) | last device name | status pill
                  "Need Chrome/Edge on Android or desktop. iPhone: open in Bluefy."

[Main / Dashboard]  tab bar: Message | Look | Boards | Settings
  header: name, RSSI bars, hub battery, N boards, [disconnect]
  live preview strip: N squares showing the word group currently on the sign (from ev:state.word)

[Message]
  textarea (uppercase enforced, allowed charset filtered to A-Z 0-9 ? ! space + tokens)
  symbol keypad: ❤ ★ ✓ ✗ → ← ↑ ↓ ◆ ● ? ! :) :(   (insert {token} at caret)
  word-group preview: chips showing how text splits across N boards (port parseMessageIntoWords to JS)
  word speed slider 0.5–5.0 s (live)        [Send]  (or auto-send on change, debounced 300 ms)

[Look]
  7 preset swatches + Rainbow toggle + <input type=color>
  brightness slider 0–100 with the colour-aware cap shown ("max 60% for white")
  effect segmented control: Static Blink Fade Cycle ; effect speed slider appears for non-static

[Boards]
  "Active boards: [1][2][3]...[10]" selector (existing board-box pattern)
  list rows, drag handle | pos # | id | battery bar (mV + %) | RSSI | last seen | [Identify]
  drag-and-drop reorder -> {"cmd":"order"} ; unseen boards greyed; "Save order" button
  low-battery rows highlighted (<6000 mV)

[Settings]
  device name, optional PIN, [Save to hub], firmware version from INFO,
  theme toggle, "Open AP mode instructions", protocol console (raw JSON log, send box) for debugging
```
Implementation notes: keep it one `index.html` (~600–900 lines) with no framework; add a
`manifest.json` + tiny service worker for installability. Auto-apply brightness/color/effect on
change; require explicit Send for text (matches existing UX).

---

## 8. Risks and mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| Web Bluetooth not in Safari/iOS WebKit (caniuse: no version) | iPhone users need Bluefy or iOSWebBLE extension | Document it; same page works unchanged in Bluefy; Flutter fallback if unacceptable |
| Web Bluetooth needs HTTPS + user gesture | Can't run from `file://` or plain HTTP | Host on GitHub/Cloudflare Pages (free HTTPS); `localhost` also allowed for dev |
| iOS background BLE: scans coalesced/slow, ~10 s wake budget, `bluetooth-central` mode required | Only matters for native apps wanting background control | Not needed — control is foreground-only |
| Android 12+ permissions: `BLUETOOTH_SCAN` (+`neverForLocation`), `BLUETOOTH_CONNECT`; Android <=11 needs `ACCESS_FINE_LOCATION` | Native apps must request runtime perms | PWA avoids it entirely (browser owns the permission); flutter_blue_plus/RN plugin generate the manifest |
| iOS MTU 185 vs Android 517 | Long `msg`/`state` lines fragment | Chunking rules in 4.3; test with Bluefy |
| SoftAP + BLE coexistence unstable; ESP-NOW RX only stable in STA | Can't keep the old web UI running alongside BLE | Make AP web UI a separate boot mode |
| Bluedroid flash bloat / default 1.2 MB app partition | Build fails to fit | NimBLE + Huge APP / min_spiffs partition |
| WS2812 glitches under radio interrupt load | Flicker | Low notify rate; FastLED I2S driver if needed |
| Bond-table mismatch (`Code=14` on iOS) | Phone refuses to reconnect after reflash | Don't bond; optional protocol PIN instead |
| Two phones connecting at once | Conflicting commands | max 1 connection, stop advertising when connected |
| App-store not needed | — | PWA = URL; Flutter Android = APK sideload; iOS native = free 7-day signing or $99/yr |

---

## 9. Effort estimates (solo, evenings/weekends)

| Deliverable | Estimate |
|---|---|
| Hub firmware: NimBLE NUS + custom STATE service + JSON parser + NVS save, integrated with ESP-NOW relay | 2–3 evenings |
| Bring-up with Serial Bluetooth Terminal / nRF Connect | included above |
| Web Bluetooth PWA (port existing HTML, add Boards tab, chunking, reconnect) | 1–2 weekends |
| GitHub Pages / Cloudflare Pages hosting + manifest | 1 hour |
| iOS check in Bluefy | 1 hour |
| Flutter fallback (if ever): scaffold + flutter_blue_plus + same screens | 1–2 weeks part-time; needs Mac for iOS |
| HA bridge via ESPHome `ble_client` (home use) | 1 weekend |

---

## Sources

- esp-nimble-cpp README (flash/RAM savings vs Bluedroid): https://components.espressif.com/components/h2zero/esp-nimble-cpp/versions/2.4.0/readme?language=en
- NimBLE-Arduino 2.3.7 release notes: https://newreleases.io/project/github/h2zero/NimBLE-Arduino/release/2.3.7
- NimBLE-Arduino tutorial: https://lastminuteengineers.com/esp32-nimble-arduino-tutorial/
- ESP-IDF RF Coexistence guide (WiFi/BLE/ESP-NOW stability table): https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-guides/coexist.html
- Hackaday: ESP32 + Web Bluetooth (BLE lib >900 KiB, HTTPS, 512 B writes): https://hackaday.io/project/183450/log/202740-esp32-and-the-non-standard-web-bluetooth-api
- caniuse Web Bluetooth (Safari/Firefox unsupported; Chrome/Edge/Samsung supported): https://caniuse.com/web-bluetooth
- BeaconZone browser support + Bluefy workaround: https://www.beaconzone.co.uk/blog/browser-support-for-web-bluetooth/
- Bluefy – Web BLE Browser (iOS): https://apps.apple.com/app/id1492822055
- iOS MTU 185 discussion: https://devzone.nordicsemi.com/f/nordic-q-a/44825/ios-mtu-size-why-only-185-bytes/176067
- flutter_blue_plus docs (MTU 512 Android, iOS auto, permissions, autoConnect): https://pub.dev/packages/flutter_blue_plus/versions/1.30.3
- Android Bluetooth permissions (BLUETOOTH_SCAN, neverForLocation): https://developer.android.com/develop/connectivity/bluetooth/bt-permissions
- Apple Core Bluetooth background processing: https://developer.apple.com/library/archive/documentation/NetworkingInternetWeb/Conceptual/CoreBluetooth_concepts/CoreBluetoothBackgroundProcessingForIOSApps/PerformingTasksWhileYourAppIsInTheBackground.html
- Apple forum, Code=14 "Peer removed pairing information": https://developer.apple.com/forums/thread/732738
- NimBLE Just Works thread (setSecurityAuth / IOCap, WRITE_ENC pitfalls): https://forum.arduino.cc/t/seeking-example-of-just-works-for-nimble/1427257
- Plugin.BLE (MAUI) on NuGet: https://www.nuget.org/packages/Plugin.BLE/3.3.0-beta.2
- Expo config plugin for react-native-ble-plx: https://www.npmjs.com/package/@config-plugins/react-native-ble-plx
- nimble-nordic-uart component: https://components.espressif.com/components/masuidrive/nimble-nordic-uart/versions/1.0.3/readme?language=en
- nightlark/web-bluetooth-controller template: https://github.com/nightlark/web-bluetooth-controller
- LSchlierf/LED-Controller (Flutter): https://github.com/LSchlierf/LED-Controller
- SimpleBluetoothLeTerminal (Serial Bluetooth Terminal source, NUS profiles): https://github.com/kai-morich/SimpleBluetoothLeTerminal
- Adafruit Bluefruit Connect features (UART, Controller/Color Picker): https://learn.adafruit.com/bluefruit-le-connect/features
- ESPHome esp32_ble_server: https://esphome.io/components/esp32_ble_server/
- ESPHome ble_client writing custom characteristics (HA community): https://community.home-assistant.io/t/bluetooth-communication-specific-protocol-with-esphome/499808
- ESPHome espnow component (2025.8+): https://www.esphome.io/components/espnow
- ESP32 Classic SPP not usable from iOS (esp32.com): https://esp32.com/viewtopic.php?p=41762
