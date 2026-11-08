# Build brief — Phase 1, step 3: web transport + phone UI (done before step 2 by owner decision)

Builds on the verified step-1 firmware in `firmware/LED_Sign_Mesh/` (see `docs/reviews/STEP1_VERIFICATION.md`). Behaviour source of truth: `docs/PROJECT_PLAN.md` §2.4, §2.5, §4.4, §6.1. UI feature baseline: `firmware/legacy/LED_Sign_WiFi_Symbol_Edition.ino` (the embedded HTML/JS there — tabs, symbol buttons, colour, brightness, effect, word speed, dark mode).

## Target
Same sketch, same FQBN (`esp32:esp32:lolin32:PartitionScheme=no_ota`), core 3.3.12, FastLED 3.10.9, ArduinoJson 7.4.3 (installed on the box). Hub only — nodes are unchanged except where noted.

## Scope of THIS step
1. **`transport.h`** — abstract interface: `begin()`, `poll()` (called from the core-0 net task), `sendEvent(const char* json)` (no-op/queue for HTTP), `setCommandHandler(bool(*)(JsonDocument& cmd, JsonDocument& reply))`.
2. **`transport_web.h/.cpp`** — `WebServer` (port 80) + `DNSServer` captive portal (answers every name with 192.168.4.1; handle `/generate_204`, `/hotspot-detect.html`, `/connecttest.txt`, `/ncsi.txt`, `/fwlink` → 302 to `/`). Routes: `GET /` → gzipped `index.html` from PROGMEM (`Content-Encoding: gzip`, `Cache-Control: no-store`); `POST /cmd` body = one JSON object → run through the command handler → JSON reply (`{"ok":true,...}` or `{"ok":false,"err":"..."}`); `GET /state` → the `ev:state`+`ev:cfg` JSON (section 4). All handlers non-blocking and short; reply buffers sized (`JsonDocument` ArduinoJson 7). Log each request on serial as `[WEB] POST /cmd {...}` (truncate body to 120 chars). `WiFi.softAPgetStationNum()` exposed for status.
3. **`commands.h/.cpp`** — JSON → actions, shared by every transport (and reused by the serial console: a serial line starting with `{` is fed to the same handler). Commands this step (plan §4.4, trimmed to what exists after step 1 + what this step adds):
   | cmd | fields | action |
   |---|---|---|
   | `get` | – | reply with cfg + state |
   | `msg` | `text` (≤ 60 chars) | set the display text: uppercase, map `{heart} {star} {check} {cross} {right} {left} {up} {down} {diamond} {circle} {happy} {sad}` → codes 1–10, 13, 14; `?` `!` and the new punctuation pass through. **This step: no word sequencing yet** — text is laid out one char per position starting at pos 1; chars beyond `numBoards` are ignored (step 2 replaces this with `parseMessageIntoWords`). Persist text in NVS (`sign` namespace) and restore at boot. |
   | `color` | `hex` ("RRGGBB") or `rainbow:true` | set Show r,g,b; rainbow sets `effect=3` (cycle) — node side: cycle hue from hub clock (see 5) |
   | `bri` | `v` 0–100 | set Show brightness (node scales colour; hub too). Apply the v4 colour-aware cap on the hub: single-channel colour max 100, dual 80, white 60, rainbow 70; reply includes the effective value. |
   | `fx` | `name` static/blink/fade/cycle, `speed` ms (100–2000) | set Show effect + effectPeriodMs |
   | `boards` | `n` 0–10 | pin `numBoards` (0 = auto = highest owned position) |
   | `identify` | `pos` (1–10 or 255 = all), `ms` | IDENTIFY unicast/broadcast (exists) |
   | `reset_order` | – | `hub::onResetOrd()` (exists) |
   | `forget` | `pos` | hub drops that position from the table and unicasts FORGET to the node |
   | `name` | `v` (≤ 24 chars, SSID-safe) | set AP SSID (persist, applies next boot; reply says so) |
   | `save` | – | persist current SignState (msg/color/bri/fx/boards) — also auto-save 2 s after the last change (debounced) so the phone never has to press save |
   Reply events: `{"ok":true}` / `{"ok":false,"err":"bad_cmd"|"bad_arg"|"hub_only"}`.
4. **`GET /state` JSON** (`ev:state` + `ev:cfg` merged):
   `{"cfg":{"text":"HI","color":"00FF00","rainbow":false,"bri":100,"briEff":100,"fx":"static","speed":500,"boards":2,"boardsAuto":true,"name":"LEDSIGN-HUB","fw":1},"state":{"hubPos":1,"numBoards":2,"nextPos":3,"clients":1,"heap":203840,"uptime":652,"boards":[{"pos":1,"id":"CE54","mac":"9C:9C:1F:E3:CE:54","self":true,"online":true,"age":0,"mv":0,"rssi":0,"char":"H"},{"pos":2,"id":"1838","mac":"78:E3:6D:19:18:38","online":true,"age":1,"mv":0,"rssi":-33,"char":"I"}]}}`
   `age` = seconds since last HB (hub marks offline after 10 s). The hub must track per-node `rssi`, `mv`, `shownChar`, `lastHb` from the HB packets it already receives (step 1 may only count them — extend the table entry).
5. **Node-side styling** (small change in `node.cpp`, `hub.cpp` render): apply `Show.effect`: 0 static; 1 blink (on/off each `effectPeriodMs`, phase from hub clock); 2 fade (triangle 30–100 % over `effectPeriodMs`); 3 cycle (hue = `(hubNow()/effectPeriodMs*5) & 0xFF`, saturation 255). Hub clock: node keeps `offset = Show.hubMillis − millis()` low-pass filtered (α = ¼); `hubNow()` used for phase so all boards blink together. Hub renders itself with the same function. `identify` flash still wins while active.
6. **`web/index.html`** — single file, vanilla JS, dark mode default, ≤ 40 kB uncompressed. Port the Symbol Edition look. Tabs: **Message** (textarea, symbol keypad inserting `{token}`, char-count vs boards, Send), **Look** (7 colour swatches + rainbow + `<input type=color>`, brightness slider showing effective cap, effect segmented control + speed slider), **Boards** (grid 1–10 with online/offline colour, mac/id, rssi, age, Identify per board, "Identify all", "Reset order" with confirm, "Forget" per offline board, auto/pinned board count), **Settings** (AP name, firmware, heap/uptime/clients, raw JSON console: textarea + send + reply log). `send(obj)` → `fetch('/cmd',{method:'POST',body:JSON.stringify(obj)})`; poll `GET /state` every 2 s, pause polling while a text field is focused. Debounce sliders (300 ms). Show a toast on `ok:false`. Must work in iOS Safari and Android Chrome captive-portal webviews (no ES2020+ syntax that old webviews lack: avoid optional chaining; use `var`/`let`, `XMLHttpRequest` fallback not required).
7. **`tools/build_web.py`** — gzip `web/index.html` (level 9) → `firmware/LED_Sign_Mesh/web_page.h` as `const uint8_t WEB_INDEX_GZ[] PROGMEM = {...}; const size_t WEB_INDEX_GZ_LEN = N;` plus a header comment with source size, gz size and sha1. Run it; commit the generated header (the sketch must build from a clean clone). Also `tools/build_web.ps1` wrapper calling the python script for Windows.
8. **Hub `setup()`** wires `transport_web` after `radio::beginHub()`; `poll()` from the net task every 5 ms alongside `radio::pump()`. Restore `SignState` from NVS at boot and broadcast it (replaces the "fixed test string" — keep `t<text>` on serial as an alias for `msg`).
9. **Serial console**: keep step-1 letters; add: a line beginning with `{` → JSON command handler, reply printed; `w` → print web stats (requests served, last client IP, AP clients).

## Out of scope
Word sequencing / `wspeed` (step 2), battery ADC (still stub 0 → `mv:0`), order/swap (Phase 2), BLE, OTA, HTTPS.

## Acceptance (verification agent, on the Qwen box; hub on the port with MAC 9C:9C:1F:E3:CE:54, node 78:E3:6D:19:18:38)
The box has WLAN interfaces `wlp3s0`/`wlp4s0`: use `nmcli dev wifi connect LEDSIGN-HUB password sign1234 ifname wlp4s0` (or wlp3s0) to join the hub AP for HTTP tests; `nmcli con delete LEDSIGN-HUB` afterwards. If NetworkManager is unavailable, fall back to `wpa_supplicant`+`dhclient`; report what worked.
- B1 Build: 0 errors/0 warnings; flash ≤ 1.4 MB; report.
- B2 Boot: hub serial shows `[WEB] listening on 192.168.4.1`, SSID `LEDSIGN-HUB`, channel 6 OK. Node unchanged behaviour (pos 2, RUNNING).
- B3 Portal: from the box, join the AP; `curl -s -m 5 http://192.168.4.1/generate_204 -o /dev/null -w "%{http_code} %{redirect_url}\n"` → 302 to `http://192.168.4.1/`; `curl -s -m5 -D- -o /tmp/index.gz http://192.168.4.1/` → 200, `Content-Encoding: gzip`, and `gunzip -c /tmp/index.gz | head -c 200` shows `<!DOCTYPE html>`; `dig +short anything.example @192.168.4.1` (or `nslookup`) → 192.168.4.1.
- B4 Commands: `curl -s -X POST http://192.168.4.1/cmd -d '{"cmd":"msg","text":"OK"}'` → `{"ok":true...}`; hub serial `render 'O'`, node `render 'K'` within 1 s. `{"cmd":"color","hex":"FF0000"}` → both logs show colour change (add a `[SHOW] seq=N color=RRGGBB bri=B fx=F` log line on the hub when a Show burst is sent and `[NODE] apply seq=N ...` on the node). `{"cmd":"bri","v":50}` → reply `briEff` 50; `{"cmd":"bri","v":100}` with `color` `FFFFFF` → `briEff` 60. `{"cmd":"fx","name":"blink","speed":500}` → node `apply ... fx=1`. `{"cmd":"bogus"}` → `{"ok":false,"err":"bad_cmd"}` with HTTP 200 (or 400 — document which).
- B5 State: `curl -s http://192.168.4.1/state | python3 -m json.tool` matches section 4 shape: 2 boards, pos 1 self, pos 2 online with rssi ≠ 0 and `age` ≤ 3; `clients` ≥ 1. Power-cycle the node (serial_capture reset): within 12 s `online:false`; after it rejoins `online:true` again.
- B6 Persistence: send `msg` "AB" + `color` 0000FF, wait 3 s (auto-save), reset the hub; after boot `GET /state` cfg shows text "AB" colour 0000FF and both boards render A/B blue.
- B7 Identify/reset from HTTP: `identify pos 2` → node `IDENTIFY` log; `reset_order` → node UNASSIGNED; node serial `c` → pos 2 again; `/state` reflects each step.
- B8 Page smoke test (cloud container, no hardware): with Playwright (pre-installed), serve `web/index.html` from a tiny Python HTTP server that stubs `POST /cmd` → `{"ok":true}` and `GET /state` → the sample JSON in section 4; load in Chromium at 390×844 (phone) and 1280×800; assert: no console errors, 4 tabs switch, typing in Message + Send POSTs `{"cmd":"msg","text":...}` (capture request body), colour swatch click POSTs `color`, Boards tab renders 2 board cards with pos 1 marked self; take screenshots of each tab to `docs/reviews/step3_ui_*.png`.
- B9 Load: 60 s of `GET /state` every 500 ms from the box while the node keeps receiving Shows: hub heap stable (±4 kB), node never LOST_HUB, `rxDrop` 0. Hub `s` before/after.
- B10 Soak 5 min with the box associated and polling every 2 s: heap flat on both.

## Output format for the drafting LLM
Emit every new or changed file as
```
=== FILE: <repo-relative path> ===
<content>
=== END FILE ===
```
No prose between files. Unchanged step-1 files are not re-emitted; changed ones are emitted in full.
