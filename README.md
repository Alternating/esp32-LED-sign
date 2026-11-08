# LED Sign — 10-Board ESP-NOW Mesh

Up to 10 battery-powered 20×20 WS2812B boards, each with its own ESP32. One board is the hub (phone connects to it); the hub drives the rest over ESP-NOW. Boards are powered on one at a time and display their position number while registering, then each shows one character of the message.

**Start here:** [`docs/PROJECT_PLAN.md`](docs/PROJECT_PLAN.md) — architecture, protocol, roadmap. Status and decisions: [`Progress.md`](Progress.md).

## Layout
```
README.md                 this file
Progress.md               status log, decisions, open questions
docs/
  PROJECT_PLAN.md         the plan (review this first)
  research/               ESP-NOW feasibility + mobile app research reports (with sources)
  HARDWARE_README_v3.md   single-board hardware guide, parts list, battery/power wiring
  LIFEPO4_PERFECT_SOLUTION.md, XL4015_SETUP_GUIDE.md, CAPACITOR_GUIDE.md, BATTERY_HOLDER_PRINT_GUIDE.md
firmware/
  legacy/                 current single-board v4 (4-button) firmware and the old WiFi web-UI firmware
  LED_Sign_Mesh/          (Phase 0+) new mesh firmware — not yet created
hardware/
  battery_hanger_-_led_sign_v4.stl
```

## Next step
Answer the open questions in `Progress.md`, then Phase 0 bench test (two boards, ESP-NOW ping, flicker check). `character_patterns.h` must be added to the repo first.
