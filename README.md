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

## Which board is the hub?
A **dedicated board**, not whichever powers on first. The hub role is a flag saved in that board's flash (NVS); every other board, including freshly flashed ones, defaults to **node**. Label the hub sign ("HUB / 1").

- Current hub: MAC `9C:9C:1F:E3:CE:54`. It is always position **1** and runs the `LEDSIGN-HUB` Wi-Fi that phones join.
- Power-on order only numbers the **nodes**: first node powered after the hub gets 2, the next 3, … Positions stick by MAC until cleared (hub K3 long-press ≥ 3 s, or **Reset order** in the Boards tab), then power the boards on again in the order you want.
- To make a different board the hub: hold **K4 while applying power** (~3 s) → it shows "H" and saves the role; flip the old hub back the same way (shows "N"). Boards without buttons: send `H`/`N` over serial in the first 3 s after reset. Only one hub should be powered at a time.
- Flashing: use `firmware/LED_Sign_Mesh/build/esp32.esp32.lolin32/LED_Sign_Mesh.ino.merged.bin` at offset `0x0`, 115200 baud (`tools/flash_board.sh /dev/ttyUSBx` on the build box). Flashing does not erase NVS, so roles and positions survive updates.

## Next step
Answer the open questions in `Progress.md`, then Phase 0 bench test (two boards, ESP-NOW ping, flicker check). `character_patterns.h` must be added to the repo first.
