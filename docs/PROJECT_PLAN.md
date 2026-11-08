# LED Sign 10-Board Mesh — Project Plan

Date: 2026-11-08 (rev 2, owner feedback applied). Owner: Tyler Neelis. Author: planning agent. Status: APPROVED DIRECTION — control over hub Wi-Fi AP, board-to-board over ESP-NOW; BLE deferred.
Inputs: `docs/research/ESPNOW_RESEARCH.md`, `docs/research/MOBILE_APP_RESEARCH.md`,
`firmware/legacy/LED_Sign_v4_4Button_Backwards.ino`, `firmware/legacy/LED_Sign_WiFi_Symbol_Edition.ino`,
`docs/HARDWARE_README_v3.md`.

---

## 1. Executive summary

We are building one firmware image for up to 10 battery-powered 20x20 WS2812B boards, each driven by its own ESP32-WROOM-32. One board is flagged as the **hub**; the rest are **nodes**. The phone talks only to the hub; the hub fans state out to nodes over **ESP-NOW** on a fixed Wi-Fi channel. Nodes are powered on one at a time, each is handed the next sequential position and shows that digit on its matrix while registering; afterwards the sign shows one character per board with word groups sequenced over time, colour/brightness/effects as in the legacy web UI.

Key decisions:
1. **Phone transport = Wi-Fi SoftAP + web page served by the hub** (reuses the Symbol Edition UI). **Confirmed by owner 2026-11-08.** Board-to-board is ESP-NOW only; **BLE is not used anywhere** (kept in §6.2 as an optional future phase only). Reasons in section 2.4: zero radio-coexistence risk while the ESP-NOW layer is still unproven, works on iPhone without a paid third-party browser, and it is one evening of porting instead of a new GATT stack. The owner asked for Bluetooth; we honour that by making the transport a swappable module behind a transport-agnostic JSON command set, so the BLE module slots in without touching the mesh or display code.
2. **Role is a persisted NVS flag set by a boot-time button gesture**, not auto-election (section 2.1).
3. **Positions are sticky (MAC → position in hub NVS)** with an explicit "reset order" gesture; after a reset, nodes wait to be power-cycled or claimed, so "power them on in order" still works (section 3).
4. **One self-describing `Show` broadcast carries the complete display state** and is repeated at 2 Hz as the heartbeat, so any missed packet heals within 500 ms (section 4).
5. Raw `esp_now.h`, FastLED with `FASTLED_RMT5 0`, Huge APP partition, Arduino-ESP32 core 3.x pinned (section 5).

Drift resolution: the two research reports disagreed on the phone transport (AP+web first vs BLE first). Resolved in favour of AP+web for Phase 1 with BLE in Phase 3; the full drift list and the protocol fixes it triggers are in section 10.

---

## 2. System architecture

### 2.1 Roles and how a board gets its role

Options considered:
- (a) Compile-time `#define ROLE_HUB` — two binaries to keep straight across 10 boards; rejected.
- (b) Auto-election ("first board that hears no hub for N s becomes hub") — rejected. It fails exactly in the stated workflow: if the hub is powered a few seconds late, or the first node is powered out of range, a node silently promotes itself and the user now has two hubs and no indication which one the phone will find. It also adds an N-second dead time to every node boot and makes behaviour depend on radio conditions.
- **(c) Persisted NVS flag, set by a boot gesture — chosen.** Hold **K4** while applying power (held for 3 s after the boot animation) toggles `role` between NODE and HUB and persists it; the board then shows **"H"** (hub) or **"N"** (node) for 2 s and reboots. Default on a fresh flash is NODE. Deterministic, one binary, survives power cycles, no race conditions. The hub is a display board too (it holds position 1 by default; see open question 1).

Hub-collision guard: a hub that hears a `Show`/`TICK` from another hub MAC stops transmitting, alternates "H" / "!" on its matrix and logs the other MAC. Fix is to toggle the role on one of them.

### 2.2 Block diagram

```
          Phone (any browser)                      Phase 3: Phone (Chrome / Bluefy)
                 | Wi-Fi join "LEDSIGN-xxxx"                 | BLE GATT (NUS JSON lines)
                 | http://192.168.4.1  (captive portal)       |
                 v                                            v
  +--------------------------------------------------------------------------+
  | HUB board  (role=HUB in NVS, position 1)                                 |
  |  transport_web  (SoftAP ch.6 + WebServer + DNSServer)   <- swappable ->  |
  |  transport_ble  (NimBLE, Phase 3, mutually exclusive with SoftAP)        |
  |            \                                       /                     |
  |             +--> protocol.cpp : JSON cmd -> SignState (words, colour..)  |
  |                        |                                                 |
  |          +-------------+---------------+                                 |
  |          v                             v                                 |
  |   display/patterns (own char)   radio_espnow : broadcast Show @2Hz,      |
  |   buttons, battery, storage      unicast ASSIGN/SETPOS/IDENTIFY, RX HB   |
  +--------------------------------------------------------------------------+
                 | ESP-NOW, fixed channel 6, broadcast + unicast, <=250 B
     +-----------+-----------+-----------+ ... up to 9 nodes
     v           v           v           v
  +------+    +------+    +------+    +------+
  | NODE |    | NODE |    | NODE |    | NODE |   each: WIFI_STA, no AP, no BLE
  | pos2 |    | pos3 |    | pos4 |    | posN |   radio_espnow + display + buttons
  +------+    +------+    +------+    +------+   + battery ADC + storage
```

### 2.3 Radio plan

| Item | Decision |
|---|---|
| ESP-NOW channel | Fixed, compile-time `ESPNOW_CHANNEL 6` on every board (changeable to 1 or 11 by reflash). Channel scan/negotiation is deferred; it would need a side channel to tell nodes the result. |
| Hub Wi-Fi mode (Phase 1/2) | `WIFI_AP_STA`. `softAP(ssid, pass, ESPNOW_CHANNEL, hidden=0, max_conn=2)` pins the AP to channel 6. The STA interface is never associated (`WiFi.begin()` is never called), so nothing can drag the channel elsewhere. ESP-NOW peers registered with `ifidx = WIFI_IF_STA`, `channel = ESPNOW_CHANNEL`. `WiFi.setSleep(false)`. |
| Hub Wi-Fi mode (Phase 3, BLE active) | `WIFI_STA` only, AP off, `WiFi.setSleep(false)`, `esp_wifi_set_channel(6)`, init order Wi-Fi -> ESP-NOW -> NimBLE (ESPNOW_RESEARCH.md section 2). SoftAP and BLE are never on together (coexistence row "SoftAP connected + BLE" = C1 unstable). |
| Node Wi-Fi mode | `WIFI_STA`, `WiFi.disconnect(true,true)`, `WiFi.setSleep(false)`, `esp_wifi_set_channel(6)`, `esp_bt_controller_disable()`. |
| Addressing | Hub -> all: broadcast `FF:FF:FF:FF:FF:FF` (no peer limit, no MAC ACK). Hub -> one node and node -> hub: unicast (MAC ACK + app-level ACK where it matters). Hub adds a node as a peer on first HELLO (<= 20 peers; we need <= 9). |
| Send discipline | One outstanding `esp_now_send()` at a time, gated by the send callback; all sends from the main loop or the net task, never from an RX callback. |
| Traffic budget | Hub: `Show` broadcast 2 Hz (~50 B) + bursts on change. Nodes: HELLO 1 Hz until assigned, HB every 3 s staggered by `pos*300 ms`. Total < 10 pkt/s at 10 boards. |

Channel-pinning pitfall to document in the README: if anyone later adds "connect the hub to home Wi-Fi" (OTA, HA), the STA will follow the router's channel and all nodes go deaf. Phase 4 OTA therefore uses the hub's own AP, not a router.

### 2.4 Phone transport decision (drift resolution)

| Criterion | SoftAP + web (ESPNOW_RESEARCH.md section 3) | BLE + Web Bluetooth PWA (MOBILE_APP_RESEARCH.md) |
|---|---|---|
| Radio risk to the untested ESP-NOW layer | None (AP keeps RF awake; no BLE time-slicing) | Documented "S" case for hub RX; needs recipe + retries + bench proof |
| iPhone | Works in Safari | Needs Bluefy ($2.99) or a native app |
| Effort to first working UI | 1 evening (port Symbol Edition HTML, swap form POST for JSON) | 2-3 evenings firmware + 1-2 weekends PWA + HTTPS hosting |
| Phone keeps internet | No (joins sign AP; Android "no internet" prompt) | Yes |
| Flash/RAM | WebServer + DNSServer, small | NimBLE ~60-80 kB heap, +flash |
| Matches owner's original ask (Bluetooth) | No | Yes |

Decision: **Phase 1 = SoftAP + web**, because the riskiest unknown in this project is the ESP-NOW mesh (numbering, sync, 10 boards in a room), and we should not stack a second radio-coexistence unknown on top of it while debugging the first. **BLE is Phase 3** and is explicitly on the roadmap because the owner asked for it and it is the better everyday UX (phone keeps LTE/Wi-Fi, one-tap connect on Android/desktop). The transport is a module (`transport.h` interface, section 5) and the command set is JSON lines on both transports, so Phase 3 adds a file and a boot-mode switch and changes nothing in protocol, radio or display code. Both reports already agree that SoftAP and BLE must never run concurrently, so the two transports are boot modes selected from the hub's K3 long-press at power-on (default remembered in NVS).

### 2.5 Data flow

1. Phone sends a JSON command to the hub transport (HTTP POST `/cmd` in Phase 1; NUS write in Phase 3).
2. `protocol.cpp` validates it, updates `SignState` (message text, word groups, colour, brightness, effect, word speed, board count), persists on `save`, replies `{"ok":...}`.
3. The hub's sequencer advances the word-group index on `wordSpeed` and builds a `Show` packet for the current group (char for each position, style fields, `applyAtHubMs = now + 300`).
4. `radio_espnow` broadcasts `Show` 3x on change (0 / 60 / 250 ms) and once every 500 ms regardless (this is also the TICK/heartbeat).
5. Each node applies `Show` if `seq` is new, renders `chars[pos-1]` with the style, phase-locked to the hub clock; the hub renders `chars[0]` itself.
6. Nodes unicast `HB` (battery mV, RSSI, shown char) every 3 s; hub keeps the board table and exposes it to the phone as `ev:state`.

---

## 3. Boot and numbering sequence

### 3.1 States

```cpp
enum class NodeState : uint8_t { BOOT, DISCOVER, SHOW_POSITION, RUNNING, LOST_HUB, UNASSIGNED };
enum class HubState  : uint8_t { BOOT, SHOW_POSITION, RUNNING, CONFLICT };
```

### 3.2 Node state machine

| State | Entry | Matrix shows | Exit |
|---|---|---|---|
| BOOT | power on | boot animation (R/G/B flashes, 300 ms, as v4) then role check (K4 held -> toggle role) | always -> DISCOVER after ~1.5 s (`POWER_STABILIZE` + `LED_INIT_DELAY` retained) |
| DISCOVER | | if NVS has a position: that digit, dim (25 %), slow pulse. If not: "?" dim pulse. Broadcast `HELLO{nvsPos, fw, vbat}` every 1000 ms ± 200 ms jitter. | `ASSIGN` received -> SHOW_POSITION. 15 s with no hub -> LOST_HUB. |
| SHOW_POSITION | store pos in NVS if changed; add hub as peer; send `ACK` | the position digit, full configured brightness, hub colour if a `Show` is known else green | **3.0 s minimum**, then -> RUNNING on the next `Show` (or immediately if one is already buffered) |
| RUNNING | | `chars[pos-1]` from the latest `Show`, with effects | no `Show`/TICK for 5 s -> LOST_HUB. `RESETORD` -> UNASSIGNED. `SETPOS` -> SHOW_POSITION (shows the new digit 3 s). `IDENTIFY` -> flash white for N ms then back. |
| LOST_HUB | | keeps last character, adds a 1-pixel dim red corner dot; local 4-button control is live (v4 behaviour: K1/K2 char, K3 colour, K4 brightness) | any `Show` from the known hub -> RUNNING (corner dot off). Keeps HELLO at 0.2 Hz so a rebooted hub re-learns it. |
| UNASSIGNED | NVS position cleared | "?" dim pulse; **does not HELLO** | power cycle -> BOOT -> DISCOVER (HELLO with nvsPos=0). Or K1 long-press (2 s) -> sends `CLAIM` and goes to DISCOVER. |

Position digit rendering: positions 1-9 use the existing 0-9 glyphs; position 10 is drawn as "1" and "0" side by side at half width (one new 20x20 glyph in `patterns.h`); "?" and "H"/"N" already exist or are trivial additions.

### 3.3 Hub state machine

| State | Behaviour |
|---|---|
| BOOT | boot animation, role check, load MAC->pos table + `nextPos` + `SignState` from NVS, start Wi-Fi (section 2.3), start ESP-NOW, start transport. |
| SHOW_POSITION | shows its own position digit ("1" by default) for 3 s so the hub matches the nodes' behaviour, then RUNNING. |
| RUNNING | serves the phone, sequences words, broadcasts `Show`, answers HELLO/CLAIM/HB. On HELLO: known MAC -> reply its stored position (unicast `ASSIGN`, needsAck, retry 5x @200 ms); unknown MAC -> `pos = nextPos++` (skipping positions already owned), persist, `ASSIGN`. Immediately follows with a `Show` so a late joiner lights up. Marks a board offline after 10 s without HB; the app greys it out. |
| CONFLICT | another hub heard (section 2.1). |

Hub gestures (held >= 3 s, confirmed by a white flash):
- **K3 long-press -> RESET ORDER.** Clears the table, `nextPos = 2` (hub keeps 1), broadcasts `RESETORD` 5x over 1 s. Nodes go UNASSIGNED and stay dark-"?" until power-cycled or claimed, so the user re-powers them in the order wanted. This is the exact "power them on one at a time" workflow, repeatable at any event.
- **K2 long-press -> IDENTIFY ALL.** Every node flashes its position digit for 3 s (hub too). Useful before hanging boards.
- K3 held at power-on -> toggles the remembered phone transport (web / BLE) once Phase 3 exists.

### 3.4 Edge cases

| Case | Behaviour |
|---|---|
| Re-join after power cycle of a middle board | DISCOVER shows the remembered digit dim; hub recognises the MAC and re-ASSIGNs the same position; node shows the digit 3 s then resumes. Other boards unaffected. |
| Late-joining board (sign already running) | gets `nextPos`; the hub's `numBoards` auto-grows to the highest online position unless the user has pinned the board count in the app; the current word group is re-parsed so the new board shows a character within one word period. |
| Two boards powered simultaneously | both HELLO with random jitter; the hub processes serially and assigns in arrival order. Each board displays its digit, so the user sees instantly which one got which number and can fix it with the app's swap or by RESET ORDER and re-powering. No protocol ambiguity, only possible user surprise. |
| Board never heard from before and hub table full (10) | hub replies `ASSIGN{pos=0}` -> node shows "!" for 3 s then UNASSIGNED. |
| Hub power-cycled mid-show | nodes go LOST_HUB (keep last char, red dot), hub reloads table and `SignState` from NVS and resumes broadcasting; nodes snap back on the first `Show`. The phone must re-join the AP. |
| Node reflashed (NVS wiped) | HELLO with nvsPos=0 but a known MAC -> hub re-sends the old position. Sticky by MAC, not by node NVS. |
| Hub reflashed with table wiped | all nodes HELLO with their nvsPos; hub accepts a claimed nvsPos if unused and marks it owned, otherwise assigns `nextPos`. Positions survive a hub reflash as long as nodes were not also wiped. |
| Forget one board | node K2 long-press (2 s) -> sends `FORGET`, clears its NVS, goes UNASSIGNED; hub frees the position. |

---

## 4. Protocol specification

### 4.1 ESP-NOW framing (tightened from ESPNOW_RESEARCH.md section 5)

```cpp
#pragma pack(push, 1)
constexpr uint8_t PROTO_MAGIC = 0xA5, PROTO_VER = 1, POS_ALL = 0xFF, POS_HUB = 0;

enum MsgType : uint8_t {
  HELLO=1, ASSIGN=2, ACK=3, CLAIM=4, SETPOS=5, SHOW=6, HB=7,
  IDENTIFY=8, RESETORD=9, FORGET=10, PING=11, PONG=12
};

struct Hdr {            // 8 bytes
  uint8_t  magic, ver, type;
  uint8_t  flags;       // bit0 needsAck, bit1 isRetry, bit2 periodic (Show sent as heartbeat)
  uint16_t seq;         // per-sender; receiver drops seq == lastSeq[srcMac]
  uint8_t  srcPos;      // 0 = hub or unassigned
  uint8_t  dstPos;      // 0xFF = all
};

struct Hello    { Hdr h; uint8_t nvsPos; uint8_t fw; uint16_t vbat_mV; uint8_t mirror; };  // 13 B (MAC comes from esp_now_recv_info, not payload)
struct Assign   { Hdr h; uint8_t pos; uint8_t channel; uint32_t hubMillis; uint8_t hubPos; };// 15 B, unicast, needsAck
struct Ack      { Hdr h; uint8_t ackType; uint16_t ackSeq; uint8_t pos; };                  // 12 B
struct SetPos   { Hdr h; uint8_t newPos; };                                                 // 9 B, unicast, needsAck
struct Identify { Hdr h; uint16_t ms; };                                                    // 10 B, unicast or broadcast
struct Hb       { Hdr h; uint16_t vbat_mV; int8_t rssi; uint8_t shownChar; uint8_t state; uint16_t lastShowSeq; }; // 15 B

struct Show {                       // the entire display state; broadcast
  Hdr      h;
  uint32_t hubMillis;               // hub clock at send (time sync)
  uint32_t applyAtHubMs;            // 0 = apply now
  uint16_t showSeq;                 // increments on every content/style change
  uint8_t  r, g, b;                 // base colour (ignored when effect == CYCLE)
  uint8_t  brightness;              // 0..100, already colour-capped by the hub
  uint8_t  effect;                  // 0 static, 1 blink, 2 fade, 3 cycle, 4 identify-flash
  uint16_t effectPeriodMs;
  uint16_t wordPeriodMs;            // informational, lets a node predict the next change
  uint8_t  numBoards;               // active positions (1..10)
  uint8_t  wordIdx, wordCount;      // current group / total groups (for the app preview via hub)
  char     chars[10];               // chars[pos-1]; 0 = blank. 1..14 = symbol codes as in Symbol Edition
};                                  // 8+4+4+2+3+1+1+2+2+1+2+10 = 40 B
#pragma pack(pop)
static_assert(sizeof(Show) <= 250, "fits ESP-NOW v1");
```

Rules:
- Every receiver drops frames whose `magic`/`ver` mismatch, then de-dupes on `(srcMac, seq)`.
- `Show` is broadcast 3x on change (0/60/250 ms, same `seq`) and once every **500 ms** with `flags.periodic` set and `applyAtHubMs = 0`. There is no separate TICK message: the periodic `Show` is the heartbeat and the clock source. A node that misses a change gets it on the next periodic frame (<= 500 ms late).
- Unicast with `needsAck` (ASSIGN, SETPOS) is retried up to 5x at 200 ms until an `Ack{ackType, ackSeq}` arrives.
- `CLAIM`, `FORGET`, `HELLO` are node -> hub unicast or broadcast (node does not know the hub MAC before the first ASSIGN; HELLO is broadcast, everything after is unicast).
- `PING/PONG` exist only for the Phase 0 bench test and the app's "identify + RTT" diagnostic.

### 4.2 Time sync and effect phase

Node: on every `Show`, `offset = hubMillis + 2 - millis()`, low-pass `offset += (new - offset) / 4`; `hubNow() = millis() + offset`. Blink phase = `(hubNow() / effectPeriodMs) & 1`; fade = triangle of `hubNow() % effectPeriodMs`; cycle hue = `(hubNow() / effectPeriodMs) * 5 & 0xFF`. Word changes take effect at `applyAtHubMs` (hub sets `now + 300`). Expected inter-board skew <= 5 ms (ESPNOW_RESEARCH.md section 6).

### 4.3 Battery telemetry

- Source: the 6 V LiFePO4 pack, measured **before** the XL4015, through a resistor divider to **GPIO 34** (ADC1_CH6, input-only; ADC2 is unusable while Wi-Fi is on).
- Divider: **100 kOhm (top, to battery +) / 47 kOhm (bottom, to GND)**, ratio 0.3197. 6.4 V -> 2.05 V; worst case 7.2 V (BMS overcharge cut-off) -> 2.30 V, inside the ESP32 ADC's linear range at 11 dB attenuation (~0.15-2.45 V). Add 100 nF from GPIO 34 to GND to quiet the reading. Common ground is already required by the power wiring.
- Firmware: `analogSetPinAttenuation(34, ADC_11db)`, average 16 `analogReadMilliVolts()` samples every 3 s, `vbat_mV = mv * 147 / 47 * cal / 1000` with a per-board `cal` (default 1000) stored in NVS and set once via the app (`{"cmd":"vcal","mv":6230}` with a multimeter reading).
- Reported raw mV in `HB`; the app maps to percent using the README table (6400=100 %, 6100=50 %, 6000=30 %, 5800=10 %). Hub raises `ev:low_batt` under 6000 mV and the node itself blinks its red corner dot under 5800 mV.

### 4.4 Phone-side JSON command set and mapping (from MOBILE_APP_RESEARCH.md section 4.4, same on HTTP and BLE)

| JSON (phone -> hub) | Hub action | ESP-NOW effect |
|---|---|---|
| `{"cmd":"get"}` | reply `ev:cfg` + `ev:state` | none |
| `{"cmd":"msg","text":"HELLO {heart} WORLD"}` | map `{token}` to codes 1..14, uppercase, re-run `parseMessageIntoWords()` for `numBoards`, restart sequencer | new `Show` (3x) |
| `{"cmd":"boards","n":4}` | pin `numBoards` (0 = auto = highest online position) | new `Show` |
| `{"cmd":"color","hex":"FF8800"}` / `{"cmd":"color","rainbow":true}` | set colour / effect=cycle; compute colour-aware brightness cap (v4 rules: single 100, dual 80, white 60, rainbow 70) | new `Show` |
| `{"cmd":"bri","v":25}` | 0..100 before cap | new `Show` |
| `{"cmd":"fx","name":"blink","speed":500}` | static/blink/fade/cycle | new `Show` |
| `{"cmd":"wspeed","ms":2000}` | word period | new `Show` |
| `{"cmd":"order","ids":[...]}` / `{"cmd":"swap","a":2,"b":3}` | rewrite table | unicast `SETPOS` to each affected node |
| `{"cmd":"identify","pos":3,"ms":3000}` | | unicast `IDENTIFY` (pos 0xFF = all) |
| `{"cmd":"reset_order"}` | same as K3 long-press | broadcast `RESETORD` |
| `{"cmd":"forget","id":9120}` | drop row | unicast `FORGET` |
| `{"cmd":"save"}` | persist `SignState` | none |
| `{"cmd":"vcal","mv":6230}` (hub only in Phase 1) | battery calibration | Phase 2: unicast to node |
| `{"cmd":"name","v":"LEDSIGN-GARAGE"}` | AP SSID / BLE name | none |

Hub -> phone events are exactly MOBILE_APP_RESEARCH.md section 4.5 (`ok`, `err`, `ev:cfg`, `ev:state` with `boards[{id,pos,mv,rssi,age,state}]`, `ev:board`, `ev:low_batt`). `id` is the last 16 bits of the node MAC. On HTTP, events are fetched by polling `GET /state` every 2 s; on BLE they are notifications.

---

## 5. Firmware design

### 5.1 Sketch layout (one Arduino IDE sketch folder, one binary for hub and node)

```
firmware/LED_Sign_Mesh/
  LED_Sign_Mesh.ino       setup()/loop(): init order, role dispatch, state machines
  config.h                GPIO map, ESPNOW_CHANNEL, timings, FW_VERSION, feature flags
  protocol.h              packed structs + MsgType (section 4.1), shared by hub and node
  radio_espnow.h/.cpp     init (per role), send queue + gating, RX ring buffer, peer table, retries
  transport.h             abstract: begin(), poll(), sendEvent(const char* json), onCommand(cb)
  transport_web.h/.cpp    SoftAP + WebServer + DNSServer captive portal, GET / (PROGMEM gz), POST /cmd, GET /state
  transport_ble.h/.cpp    Phase 3: NimBLE NUS + LED Sign service (compiled only when ENABLE_BLE)
  commands.h/.cpp         JSON (ArduinoJson 7) -> SignState; builds ev:cfg/ev:state; symbol tokens
  sign_state.h            SignState struct, parseMessageIntoWords() ported from Symbol Edition
  hub.h/.cpp              hub state machine, board table (MAC->pos), sequencer, Show builder
  node.h/.cpp             node state machine, Show application, time offset
  display.h/.cpp          FastLED setup, renderChar(char, CRGB, mirror), position digit, corner dot, identify flash
  patterns.h              character_patterns.h from the Symbol Edition (A-Z, 0-9, codes 1..14, ?, !) + "10", "H", "N"
  buttons.h/.cpp          v4 debounce/long-press engine, generalised to short/long callbacks per key
  battery.h/.cpp          ADC divider read, averaging, calibration (section 4.3)
  storage.h/.cpp          Preferences namespaces: "sys" (role, mirror, transport), "sign" (SignState), "tbl" (MAC->pos)
  web_page.h              gzipped index.html as PROGMEM byte array (generated by tools/build_web.ps1)
tools/build_web.ps1       gzip web/index.html -> web_page.h (PowerShell, fits the owner's workflow)
web/index.html            the single-page UI (also the Phase 3 PWA source)
```

### 5.2 Init order (hub), which is also the Phase 3 recipe

```cpp
void setup() {
  Serial.begin(115200); delay(POWER_STABILIZE);
  storage::begin(); buttons::begin(); display::begin();      // FastLED first so boot anim works
  display::bootAnimation(); role = buttons::roleGestureOrNvs();
  battery::begin();
  if (role == HUB) { radio::beginHub(); transport = transportFromNvs(); transport->begin(); hub::begin(); }
  else             { radio::beginNode(); node::begin(); }
  xTaskCreatePinnedToCore(netTask, "net", 8192, nullptr, 1, nullptr, 0);  // transport->poll(), radio::pump()
}
void loop() {                                                  // core 1: render only
  buttons::poll(); role == HUB ? hub::tick() : node::tick(); display::render(); EVERY_N_MILLIS(16) FastLED.show();
}
```
Radio init order: Wi-Fi mode -> `setSleep(false)` -> channel -> `esp_now_init` -> callbacks -> broadcast peer -> (Phase 3) NimBLE last.

### 5.3 Libraries, versions, build settings

| Item | Choice |
|---|---|
| Arduino-ESP32 core | 3.x, pinned to one exact version in the README at Phase 0 (expected 3.2.x or 3.3.x; `esp_now_recv_info_t` RX callback signature). Record `Tools > Board` = "ESP32 Dev Module". |
| FastLED | 3.9.x or later, with `#define FASTLED_RMT5 0` before the include (RMT4 path, maintainer-recommended for Wi-Fi jitter). Fallback flag `FASTLED_ESP32_I2S` if flicker persists. `setMaxRefreshRate(0)`, `show()` paced at ~60 Hz from the loop only. |
| ArduinoJson | 7.x |
| Preferences, WiFi, WebServer, DNSServer, esp_now.h, esp_wifi.h | bundled with the core |
| NimBLE-Arduino | 2.3.x (Phase 3 only, behind `ENABLE_BLE`) |
| Partition scheme | "Huge APP (3 MB No OTA / 1 MB SPIFFS)" for Phases 0-3. Phase 4 OTA switches to "Minimal SPIFFS (1.9 MB APP with OTA)". |
| CPU / flash | 240 MHz, QIO 80 MHz, 4 MB. Core debug level: Info during bring-up. |

### 5.4 Core pinning and flicker mitigation

Wi-Fi/BT drivers and our `netTask` run on core 0; `loop()` (render + `FastLED.show()`, 12 ms per frame for 400 LEDs) runs on core 1 and nothing else is created on core 1. ESP-NOW RX and HTTP handlers only push into FreeRTOS queues; the render loop drains them. If flicker still appears with the radio on: (1) confirm `FASTLED_RMT5 0` took effect, (2) switch to the I2S driver, (3) check the 470 Ohm resistor and add the 74AHCT125 level shifter already listed as an optional upgrade.

### 5.5 GPIO map (every board identical)

| Function | GPIO | Notes |
|---|---|---|
| LED data | 4 | unchanged from v4; 470 Ohm series |
| K1 / K2 / K3 / K4 | 17 / 16 / 18 / 19 | unchanged, `INPUT_PULLUP` |
| Battery sense | 34 | ADC1_CH6, 100k/47k divider + 100 nF (section 4.3) |
| Status LED | 5 (LOLIN32 — confirmed) | `STATUS_LED_PIN` in config.h. Blinks: 1 Hz = DISCOVER, solid = RUNNING, fast = LOST_HUB |
| Reserved / do not use | 0, 2, 6-11, 12, 15 | boot strapping / flash. LOLIN32: GPIO 34/35/36/39 are input-only and broken out — 34 is used for battery sense. |

### 5.6 Local 4-button behaviour preserved

Short presses keep the v4 meaning on **every** board: K1 next char, K2 previous char, K3 next colour, K4 next brightness level. While RUNNING under a hub, a short press changes the **whole sign** on the hub (K1/K2 step the word group forward/back, K3/K4 change colour/brightness and are pushed to all nodes) and is a **local override on a node** until the next `Show` with a new `showSeq` arrives (so pressing K4 on a dim board brightens it until the phone changes something). In LOST_HUB and UNASSIGNED the buttons behave exactly like the v4 firmware, so a single board is still a usable standalone sign. Long presses (>= 2 s nodes, >= 3 s hub) are the mesh gestures in section 3. The v4 mirroring variant becomes a per-board NVS flag `mirror` set by holding K1 at power-on (board shows "E" so the user can see it flip), replacing the two separate .ino files.

---

## 6. App / UI design

### 6.1 Phase 1 web UI (served by the hub)

- Phone joins `LEDSIGN-xxxx` (password `sign1234` as before), captive portal (DNSServer answering every name with 192.168.4.1) pops the page automatically on iOS/Android; manual fallback http://192.168.4.1.
- One `index.html` (~700 lines, vanilla JS, dark mode retained from the Symbol Edition), tabs per MOBILE_APP_RESEARCH.md section 7: **Message** (textarea, symbol keypad inserting `{token}`, word-group preview chips using a JS port of `parseMessageIntoWords`, word-speed slider, Send), **Look** (7 presets + rainbow + colour input, brightness slider showing the colour cap, effect segmented control + speed), **Boards** (1-10 board boxes, live rows with pos / id / battery bar / RSSI / last seen / Identify, swap arrows, Reset order with confirm), **Settings** (name, save, firmware, battery cal, raw JSON console).
- Transport shim in JS: `send(obj)` -> `fetch('/cmd', {method:'POST', body: JSON.stringify(obj)})`; `poll()` -> `GET /state` every 2 s. In Phase 3 the same page swaps `send/poll` for BLE write/notify behind a feature detect.
- Hosting: embedded **PROGMEM gzip** in `web_page.h`, regenerated by `tools/build_web.ps1`. Chosen over LittleFS because the Arduino IDE 2.x filesystem uploader is a separate plugin and a second upload step per board; PROGMEM keeps "one Upload" and ~15 kB gz is trivial in a 3 MB app slot.

### 6.2 Phase 3 BLE / PWA

`transport_ble.cpp`: NimBLE NUS (standard UUIDs so nRF Connect / Serial Bluetooth Terminal work on day one) + custom LED Sign service (INFO, STATE, CONFIG) exactly per MOBILE_APP_RESEARCH.md section 4.2; JSON lines with `\n` framing and MTU-3 chunking; max 1 connection; Just Works, no bonding; optional protocol PIN. The same `web/index.html` is published to GitHub Pages or Cloudflare Pages (HTTPS required) with a `manifest.json` as a Web Bluetooth PWA; iPhone uses Bluefy. BLE mode and AP mode are exclusive boot modes selected by K3 at power-on and remembered in NVS; the AP mode remains the iPhone-without-Bluefy path. Native Flutter app stays a documented fallback only.

---

## 7. Phased roadmap with acceptance tests

Effort assumes a solo hobbyist working weekends with AI-assisted coding. "Done when" criteria must all pass.

### Phase 0 — Bench test (1 weekend)
Scope: two boards, `radio_espnow` + `protocol.h` + serial console; no UI, no numbering. Pin the core version. Measure flicker with the radio on.
Done when:
- Board A (hub role, SoftAP up, ESPNOW_CHANNEL 6) broadcasts `PING` at 5 Hz for 10 min; board B replies `PONG` by unicast; loss < 1 % at 5 m indoors, RTT logged < 20 ms median.
- Both boards render a static character on 400 LEDs during the test with no visible flicker (RMT4). Record the result and the FastLED/core versions in `Progress.md`.
- A phone joined to the hub AP can load a "hello" page while the ping test runs with no loss increase > 2 %.

### Phase 1 — Hub + nodes numbering, Show broadcast, web UI (3 weekends)
Scope: role gesture, node/hub state machines, sticky MAC->pos table, `Show` broadcast + periodic heartbeat, LOST_HUB, local buttons, web UI with Message/Look/Boards(basic)/Settings, PROGMEM page, captive portal.
Done when:
- Hub powered, phone connected, then 3 nodes powered one at a time: each shows "2", "3", "4" within 2 s of boot for 3 s, then shows its character of the current word group.
- Typing "WALK THE DOG" with 4 boards shows "WALK", "THE", "DOG" cycling at the chosen word speed; colour/brightness/effect changes reach all boards within 1 s.
- Power-cycling node 3 brings it back as "3" with no other board changing. Powering the hub off: nodes keep their last character and show the red dot within 5 s; hub back on: nodes recover within 1 s.
- Hub K3 long-press -> all nodes show "?"; re-powering them in a new order yields the new numbering.
- Single board with no hub reachable behaves like the v4 firmware (K1-K4) after 15 s.
- Flash fits Huge APP with > 20 % headroom; heap free > 80 kB while serving the page.

### Phase 2 — Synced effects, battery telemetry, reorder (2 weekends)
Scope: hub-clock phase lock, `applyAtHubMs`, HB with battery/RSSI, Boards tab live rows, identify, swap/order, forget, battery calibration, low-battery event, 10-board soak.
Done when:
- Blink at 500 ms on 10 boards looks simultaneous to the eye; phone-camera slow-mo shows < 1 frame (33 ms) skew.
- Boards tab shows every board's position, mV within ± 50 mV of a multimeter after calibration, RSSI and age; a board switched off is greyed within 10 s.
- Identify flashes the right physical board; swap 2<->3 makes both boards show their new digit for 3 s and then the right characters.
- 10 boards running 60 min at 25 % brightness: no board drops to LOST_HUB, hub heap stable (no leak > 5 kB/h).

### Phase 3 — (OPTIONAL / DEFERRED) BLE transport + Web Bluetooth PWA (2 weekends)
*Owner decision 2026-11-08: control stays on the Wi-Fi AP; this phase is only revisited if the AP workflow proves annoying in the field. Phase 4 may be done before it.*
Scope: `transport_ble.cpp`, boot-mode switch, STA-only radio mode with BLE, PWA hosting, Bluefy test.
Done when:
- Hub in BLE mode: nRF Connect can send `{"cmd":"bri","v":50}` and receive `ev:state`.
- Hub with a phone connected over BLE, 5 nodes sending HB at 1 Hz for 10 min: hub-side HB loss < 5 % (the ESPNOW_RESEARCH.md section 9 test). If it fails, the hidden client-less AP fallback is tried and recorded.
- PWA on Android Chrome and on iOS Bluefy drives all Phase 1/2 functions; the same `index.html` still works in AP mode.

### Phase 4 — OTA and polish (1-2 weekends)
Scope: hub OTA over its own AP (`/update` with ElegantOTA or `Update.h`), node OTA relayed via the hub AP (nodes temporarily join the hub AP as STA on channel 6 on an `OTA_ENTER` command), partition switch to Minimal SPIFFS, README/how-to per project rules, presets, brightness caps from v4 exposed in UI.
Done when: all 10 boards updated from one phone session without USB; documented runbook in `docs/HOWTO_FLASH_AND_OTA.md`.

---

## 8. Risks and mitigations

| # | Risk | Likelihood / impact | Mitigation |
|---|---|---|---|
| 1 | FastLED RMT5 flicker with Wi-Fi active | Med / Med | `FASTLED_RMT5 0`, render only on core 1, queues from callbacks, I2S driver fallback, level shifter; tested in Phase 0 |
| 2 | Arduino core 3.x API churn breaks the ESP-NOW callback or WiFi channel calls | Med / Low | pin exact core version in README and `Progress.md`; one `radio_espnow.cpp` isolates the API |
| 3 | SoftAP channel drifts from ESP-NOW channel | Low / High | never call `WiFi.begin()`; channel set in `softAP()`; assert `WiFi.channel() == ESPNOW_CHANNEL` at boot and after any mode change, show "!" if not |
| 4 | Android drops the no-internet AP | Med / Low | captive portal; document "stay connected"; Phase 3 BLE removes the issue |
| 5 | Hub ESP-NOW RX lossy once BLE is added (Phase 3) | Med / Med | protocol already retry-tolerant and self-healing; Phase 3 test gate; hidden AP fallback |
| 6 | Two boards powered at once get swapped numbers | Med / Low | digits displayed immediately; app swap or RESET ORDER |
| 7 | ADC divider mis-wired to ADC2 pin or exceeds 3.3 V | Low / High | GPIO 34 only, divider values fixed in docs, 7.2 V worst case checked |
| 8 | Flash overflow when BLE is added | Med / Low | NimBLE not Bluedroid, Huge APP, `ENABLE_BLE` flag keeps Phase 1 image small |
| 9 | Hub also drives 400 LEDs and serves HTTP: page load stutters the sign | Med / Low | HTTP in core-0 task, gz page ~15 kB, render loop never blocks on network |
| 10 | Venue Wi-Fi congestion on channel 6 | Med / Low | CSMA degrades gracefully; periodic `Show` heals; channel reflashable to 1/11; scan-and-announce is a later option |
| 11 | character_patterns.h missing from the repo | High / High (blocks build) | copy from the Symbol Edition sketch folder before Phase 0 (see Progress.md open question) |
| 12 | Owner has 10 boards but two different dev-board variants (LOLIN32 vs 30-pin DevKit) | Med / Low | single `config.h` pin map; only `STATUS_LED_PIN` differs; verify GPIO 34 exposed on both |

---

## 9. Open questions for Tyler (only those that change the design)

1. **Is the hub a display board (position 1) or a dedicated controller box?** Plan assumes display board at position 1. A dedicated hub removes FastLED interrupt load from the only board running Wi-Fi/BLE and frees one matrix, at the cost of an extra enclosure.
2. ~~Which exact dev board?~~ **Answered: LOLIN32.** `STATUS_LED_PIN 5`; GPIO 34 available for battery sense.
3. **Do all 10 boards have the 4-button board fitted, and are some wired "backwards" (mirrored)?** Plan proposes a per-board NVS `mirror` flag set by a boot gesture instead of two firmware variants.
4. **Primary phone: iPhone or Android?** Does not change Phase 1, but decides whether Phase 3 is worth Bluefy on iOS or whether AP mode remains the iPhone path permanently.
5. **Should positions be sticky across events (plan) or re-assigned by power-on order every time?** Sticky needs the K3 "reset order" gesture; always-fresh means every event starts with a reset automatically on hub boot.
6. ~~Phone loses internet on the AP?~~ **Answered: Wi-Fi AP control accepted; BLE deferred.**
7. ~~Confirm `character_patterns.h`~~ **Answered: file added to `firmware/legacy/`; it has A-Z, 0-9, codes 1-10, space. Missing `? ! :) :(` plus `" : & $ # +` — a dedicated agent is extending it (see `firmware/legacy/CHANGELOG_character_patterns.md`).**
8. **Word-group sequencing (legacy behaviour) only, or also single-character scroll across boards?** Scroll needs per-pixel column offsets in `Show` (extra ~20 B) and tighter sync; excluded from Phases 1-2 unless wanted.

---

## 10. Framework-improvement notes (drift and ambiguities found, and how they were resolved)

| # | Drift / ambiguity | Where | Resolution in this plan | Protocol fix |
|---|---|---|---|---|
| 1 | Phone transport: AP+web first vs BLE+PWA first | ESPNOW_RESEARCH.md section 3 vs MOBILE_APP_RESEARCH.md section 1 | AP+web Phase 1, BLE Phase 3, transport as a module, JSON command set shared | Research briefs must name the decision criteria (risk to untested layers, owner's stated preference, iOS reach) and each report must include a "Conflicts with sibling report" section |
| 2 | "ESP-NOW RX stable only in STA mode" read as "hub must be STA" | MOBILE_APP_RESEARCH.md section 2 | The coexistence table only applies while BT is active. Without BLE (Phase 1) AP_STA is fine and keeps RF awake; with BLE (Phase 3) hub runs STA-only. Both reports actually agree AP and BLE must not run together | Quote the IDF table row with its precondition (BT active) when citing it |
| 3 | Board count 8 vs 10 | README/legacy firmware (8 GPIO pins) vs project goal (10) | 10 boards, each with its own ESP32; pin count no longer matters. `Show.chars[10]`, not 16 | Hardware facts sheet as single source of truth (see 8) |
| 4 | Dev board identity: LOLIN32 vs 30-pin WROOM DevKit | v4 sketch header vs README BOM | `STATUS_LED_PIN` configurable; open question 2 | same facts sheet |
| 5 | LED data pin: GPIO 5 (README, Symbol Edition) vs GPIO 4 (v4 production) | legacy files | GPIO 4 (production) | README must be regenerated from `config.h` in Phase 4 |
| 6 | Symbol code range: 1-10 (v4) vs 1-14 incl. ? ! :) :( (Symbol Edition) | legacy files | 1-14 superset; question 7 confirms the header file | — |
| 7 | Brightness model: v4 colour-aware caps (single/dual/white/rainbow) vs web UI raw 0-100 | legacy files | hub applies the v4 caps to the app's 0-100 and the UI shows the cap | — |
| 8 | Mirrored vs normal boards handled by two .ino variants | v4 "Backwards" file | per-board NVS `mirror` flag | Avoid firmware forks for per-unit variation; prefer runtime flags |
| 9 | `Hello` carried the MAC although ESP-NOW provides it | ESPNOW_RESEARCH.md section 5 | dropped from payload | — |
| 10 | Separate TICK and SHOW messages | ESPNOW_RESEARCH.md section 5 | merged: periodic `Show` is the heartbeat and clock | — |
| 11 | RESET ORDER followed by immediate re-HELLO would randomise the order | implicit in ESPNOW_RESEARCH.md section 5 | nodes go UNASSIGNED and HELLO only on boot or K1 claim | State machines must be walked through for the stated user workflow before publication |
| 12 | Battery sense needed a divider; neither report gave values or a pin | both | 100k/47k, GPIO 34, calibration command | Research briefs should list required hardware deliverables explicitly |
| 13 | README file list names `LED_Sign_WiFi_MultiBoard.ino` + `scrolling_text.h`, which are not in the repo | README vs `firmware/legacy/` | ignored; Symbol Edition is the baseline. `character_patterns.h` still missing | Repo scaffold step must verify every file a sketch includes |
