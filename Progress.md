# Progress — LED Sign 10-Board Mesh

Tracking file per project rules. Newest log entries at top.

## Status
- **Phase:** 0 complete for radio (all gates passed); flicker/range are observational follow-ups. **Ready for Phase 1.**
- **Active agent:** Orchestrator (Claude session 2026-11-08); research + planning agents on Standby
- **Blocker cleared:** `character_patterns.h` extended to 68 glyphs; ready for Phase 0.

## Log
- 2026-11-08 — Phase 0 phone-on-AP gate passed: owner joined `LEDSIGN-BENCH`, loaded the hub page; 12.6 min run 3795/3791 = 0.1 % loss, RTT avg 6.2 ms (max 58 ms under phone traffic), heap stable. **Phase 0 radio gates all PASSED.** Open: flicker eyeball, 5 m range.
- 2026-11-08 — Repo pushed to GitHub (`Alternating/esp32-LED-sign`). Qwen box cloned at `~/esp32-led-sign` = primary build/flash station (two LOLIN32s on ttyUSB0/1). Windows folder `Code\esp32-LED-sign` is a clone. File shuttling retired; flow is commit/push → `git pull` on the box.
- 2026-11-08 — Local LLM (Ollama on the Qwen box) wired up as a tool via `tools/ask_local_llm.py`; 27b-q4 runs ~29 tok/s on the R9700. Usage rule in `tools/README.md`: drafts only, verify on hardware (its first answer contradicted a bench result).
- 2026-11-08 — **Phase 0 radio test PASSED** on two LOLIN32s (`firmware/bench/espnow_ping`): hub in AP_STA with SoftAP on ch 6 + ESP-NOW broadcast @ 5 Hz, node STA replying unicast. 248/248 pings, 0.0 % loss, RTT 3/6.5/25 ms, RSSI −33 dBm, heaps flat. Confirms the AP+ESP-NOW same-channel design and the core 3.3.12 ESP-NOW callback API (`wifi_tx_info_t`, `esp_now_recv_info_t`). Second board MAC 78:E3:6D:19:18:38. Remaining Phase 0 items: visual flicker check, phone-on-AP during ping, 5 m range.
- 2026-11-08 — **First hardware run.** Toolchain installed on the Qwen box (arduino-cli 1.5.1, esp32 core 3.3.12 / IDF 5.5.5, FastLED 3.10.9, ArduinoJson 7.4.3); LOLIN32 on /dev/ttyUSB0, MAC 9C:9C:1F:E3:C6:8C. `firmware/bench/font_test` compiled, flashed, verified over serial: 517 kB sketch, 296 kB free heap, `FastLED.show()` 13.7 ms/400 LEDs. New glyphs cycling on the matrix.
- 2026-11-08 — **Drift (Framework Improvement Trigger):** FastLED ≥ 3.10 on IDF 5.x has removed the RMT4 backend — `#define FASTLED_RMT5 0` is a hard `#error`. Plan §5.3's primary flicker mitigation is invalid. Replacement options: RMT5 default (test empirically in Phase 0), `FASTLED_ESP32_I2S`, or pin FastLED 3.9.x. Also: LOLIN32 FQBN has no `huge_app` partition — use `PartitionScheme=no_ota` (2.6 MB app). Fix: pin library versions at Phase 0 and verify every build flag on hardware before writing it into the plan.
- 2026-11-08 — GitHub repo `Alternating/esp32-LED-sign` created; local commits ready. Push blocked until the Claude GitHub App is granted access to the repo.
- 2026-11-08 — Font agent done. `firmware/legacy/character_patterns.h` extended 47 → 68 glyphs (adds `? ! " : & $ # + . , ' - / @ % ( ) =`, code 13 HAPPY, 14 SAD, 15 TEN). Originals byte-identical; one dead branch removed. Legacy sketches compile unchanged. Original kept as `character_patterns_v1_original.h`. Tools in `tools/fonts/`, previews + changelog in `docs/fonts/`. RAM 27.2 kB (PROGMEM deferred).
- 2026-11-08 — Owner feedback: board is **LOLIN32**; control via **Wi-Fi AP**, boards talk over **ESP-NOW**; BLE deferred (Phase 3 marked optional). Original `character_patterns.h` and `Led_standalone_lolin32.ino` added to `firmware/legacy/`. Font agent launched to audit the header and add `? ! " : & $ # +`, smiley/frown (codes 13/14) and a "10" glyph.
- 2026-11-08 — `docs/PROJECT_PLAN.md` written (architecture, boot/numbering state machines, ESP-NOW protocol, firmware layout, phased roadmap with acceptance tests, risks, open questions, framework notes). Awaiting owner review.
- 2026-11-08 — Research complete. Reports saved to `docs/research/`. **Drift detected:** ESP-NOW report recommends WiFi-AP web UI first (hub ESP-NOW RX unstable alongside BLE; no Web Bluetooth on iOS); mobile report recommends BLE + Web Bluetooth PWA first. Escalated to planning agent; resolved in PROJECT_PLAN.md §2.4 / §10 (Framework Improvement Trigger).
- 2026-11-08 — Repo scaffolded. Legacy single-board firmware (v4 4-button) and WiFi web-UI firmware copied to `firmware/legacy/`. Hardware docs (battery, XL4015, capacitor, mount STL) copied to `docs/` and `hardware/`.

## Decisions (proposed — confirm or override)
1. **CONFIRMED** — phone link = hub SoftAP + web page (port of Symbol Edition UI). Board-to-board = ESP-NOW. BLE not used; kept only as optional future transport behind `transport.h`.
2. Hub/node role = NVS flag toggled by holding K4 at power-on (shows H/N). No auto-election.
3. Positions sticky by MAC in hub NVS; hub K3 long-press = RESET ORDER, then re-power boards in desired order.
4. One 40-byte `Show` broadcast carries full display state; repeated at 2 Hz as heartbeat + clock.
5. Raw `esp_now.h`, FastLED 3.10.9 on RMT5 (RMT4 no longer exists — see drift log), ArduinoJson 7.4.3, `no_ota` partition, Arduino-ESP32 core **3.3.12** pinned. Fixed ESP-NOW channel 6.
6. Battery sense: 100k/47k divider → GPIO 34, per-board calibration via app.

## Open Questions (for Tyler)
1. Hub = display board at position 1, or a dedicated controller box?
2. ~~Dev board~~ **LOLIN32** (answered)
3. Do all boards have the 4-button board, and are some wired mirrored/backwards?
4. Primary phone: iPhone or Android?
5. Positions sticky across events, or re-numbered by power-on order every time?
6. ~~AP control OK?~~ **Yes** (answered)
7. ~~character_patterns.h~~ **Added; being extended** (answered)
8. Word-group sequencing only, or also scrolling text across boards?

## Framework Improvement Notes
See `docs/PROJECT_PLAN.md` §10 — 13 drift items logged with protocol fixes. Key fix: future research briefs must include a "Conflicts with sibling report" section and name decision criteria up front.
