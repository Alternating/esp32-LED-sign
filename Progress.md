# Progress — LED Sign 10-Board Mesh

Tracking file per project rules. Newest log entries at top.

## Status
- **Phase:** 0 — Bench test, pending owner review of `docs/PROJECT_PLAN.md` (Human Gate)
- **Active agent:** Orchestrator (Claude session 2026-11-08); research + planning agents on Standby
- **Blocker cleared:** `character_patterns.h` extended to 68 glyphs; ready for Phase 0.

## Log
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
5. Raw `esp_now.h`, FastLED with `FASTLED_RMT5 0`, ArduinoJson 7, Huge APP partition, Arduino-ESP32 core 3.x pinned at Phase 0. Fixed ESP-NOW channel 6.
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
