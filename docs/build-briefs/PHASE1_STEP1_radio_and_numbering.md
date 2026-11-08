# Build brief — Phase 1, step 1: protocol + radio + role/numbering state machines

Audience: the local code-drafting LLM (Qwen), then review/refactor/verification agents.
Source of truth for behaviour: `docs/PROJECT_PLAN.md` §2.1, §2.3, §3, §4.1, §5. Proven radio code: `firmware/bench/espnow_ping/espnow_ping.ino` (0 % loss on hardware — reuse its init order and callback signatures verbatim).

## Target
Arduino IDE sketch folder `firmware/LED_Sign_Mesh/`, FQBN `esp32:esp32:lolin32:PartitionScheme=no_ota`, core 3.3.12, FastLED 3.10.9, ArduinoJson 7.4.3 (not needed this step). One binary for hub and node.

## Scope of THIS step (nothing more)
1. `config.h` — pins (LED 4, K1 17, K2 16, K3 18, K4 19, VBAT 34, STATUS 5), `ESPNOW_CHANNEL 6`, timings from plan §3, `FW_VERSION 1`, `MAX_BOARDS 10`.
2. `protocol.h` — packed structs exactly as plan §4.1 (`Hdr`, `Hello`, `Assign`, `Ack`, `SetPos`, `Identify`, `Hb`, `Show`, plus `Ping/Pong` kept for diagnostics), `static_assert(sizeof(Show) <= 250)`.
3. `radio_espnow.h/.cpp` — `radio::beginHub()`, `radio::beginNode()`, `radio::send(mac, buf, len, needsAck)`, `radio::pump()` (drains an RX ring buffer filled by the ESP-NOW callback and dispatches to a registered handler; retries unacked unicasts 5× @ 200 ms), `radio::addPeer(mac)`. **All sends from `pump()`/main loop; the RX callback only copies into the ring buffer.** Dedupe on `(srcMac, seq)`. Hub: `WIFI_AP_STA` + `softAP(ssid, pass, 6, 0, 2)`; node: `WIFI_STA` + `esp_wifi_set_channel(6)`. `WiFi.setSleep(false)`. Assert channel == 6 after init, expose `radio::channelOk()`.
4. `storage.h/.cpp` — `Preferences` wrapper: namespace `sys` (`role`, `mirror`, `transport`), `tbl` (hub: MAC→pos table as a blob of `MAX_BOARDS` entries `{mac[6], pos}` + `nextPos`), `node` (`pos`). Functions: `loadRole/saveRole`, `loadTable/saveTable`, `loadNodePos/saveNodePos`, `clearTable`.
5. `display.h/.cpp` — FastLED on GPIO 4, 400 LEDs, `renderChar(char, CRGB, mirror)` using the serpentine mapping from `espnow_ping.ino`, `renderDigit(pos)` (1–9 → '1'..'9', 10 → code 15), `setCornerDot(bool, CRGB)` (pixel at row 0 col 19), `identifyFlash()`, `pulseDim()` helper for DISCOVER state. `show()` paced from `loop()` at ~60 Hz only. Include `character_patterns.h` (copy from `firmware/legacy/`).
6. `buttons.h/.cpp` — the v4 debounce engine generalised: per key `onShort(cb)`, `onLong(cb, ms)`; `heldAtBoot(pin)`.
7. `hub.h/.cpp` and `node.h/.cpp` — the state machines in plan §3.2 / §3.3 / §3.4, including: HELLO/ASSIGN/ACK, sticky positions by MAC, `nextPos` skipping owned positions, RESETORD (hub K3 long ≥3 s; node → UNASSIGNED, no HELLO until power-cycle or K1 long = CLAIM), IDENTIFY, LOST_HUB after 5 s without Show, DISCOVER → LOST_HUB after 15 s, SHOW_POSITION 3.0 s minimum. Hub shows "1" for 3 s at boot then RUNNING. Hub broadcasts a periodic `Show` every 500 ms (heartbeat) — for this step the Show content is a fixed test string set over serial (see 9); no word sequencer yet.
8. `LED_Sign_Mesh.ino` — `setup()` in the order of plan §5.2; `loop()` on core 1: buttons, role tick, display render, `FastLED.show()` ≤ 60 Hz; a `netTask` pinned to core 0 running `radio::pump()` every 5 ms.
9. **Serial console (bench boards have no buttons):** at boot wait 3 s for `H`/`N` to set+save role (as in `espnow_ping`). At runtime, single-letter commands: `r` = reset order (hub), `i` = identify all (hub), `c` = claim (node), `t<text>` = set the test Show string (hub, e.g. `tHI` puts H on pos 1, I on pos 2), `s` = print status (role, pos, state, table, heap, channel), `m` = toggle mirror + save.
10. Serial logging: every state transition on both roles as `[NODE] DISCOVER -> SHOW_POSITION pos=2`, every ASSIGN on the hub as `[HUB] ASSIGN 78:E3:6D:19:18:38 -> pos 2 (new|known)`.

## Out of scope this step
Web UI/transport, word sequencer, battery ADC (stub `battery::readMilliVolts()` returning 0), time-sync phase lock, effects other than static colour, OTA.

## Acceptance (verification agent runs these on the Qwen box, two LOLIN32s on /dev/ttyUSB0 + /dev/ttyUSB1)
- A1 Compiles with zero errors; report flash/RAM.
- A2 Flash both; set USB0 = `H`, USB1 = `N` via `serial_capture.py <port> 8 H|N`. Hub log shows "1" for 3 s then RUNNING; node log shows DISCOVER → SHOW_POSITION pos=2 → RUNNING within 5 s of the hub being up. Hub log shows `ASSIGN ... -> pos 2 (new)`.
- A3 Reset the node (serial_capture resets it): node comes back as pos 2 with `(known)` on the hub; no new position consumed (`s` on hub shows nextPos=3).
- A4 Reset the hub: node logs LOST_HUB within 5 s, then RUNNING again within 1 s of the hub's first Show.
- A5 `r` on hub: node logs → UNASSIGNED and stops HELLO; `c` on node: re-HELLOs and gets pos 2 again (table was cleared, so `(new)`).
- A6 `tAB` on hub: hub renders A, node renders B (confirm via a serial line printing the char each board renders).
- A7 10 min idle soak: hub and node heap flat (±2 kB), no resets (`s` shows uptime).

## Output format for the drafting LLM
Emit every file as
```
=== FILE: firmware/LED_Sign_Mesh/<name> ===
<content>
=== END FILE ===
```
No prose between files. `tools/split_files.py` splits this into the tree.
