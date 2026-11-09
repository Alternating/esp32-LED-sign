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
## Firmware images — three files, clearly labeled
One source tree builds one program; the **role is pre-seeded in the settings partition** of the install images so a board is a hub or a node from its very first boot. `tools/make_images.sh` (build box) writes them to `release/<commit>/` with a README and SHA256SUMS:

| file | flash to | offset | effect |
|---|---|---|---|
| `LED_Sign_HUB_install.bin` | **the one hub board** | `0x0` | full install, role = HUB, settings wiped |
| `LED_Sign_NODE_install.bin` | every other board | `0x0` | full install, role = NODE, settings wiped |
| `LED_Sign_update_app.bin` | any board, later updates | `0x10000` | app only — keeps role, positions, message |

Build box: `tools/flash_board.sh /dev/ttyUSBx hub|node|update` (flashes at 115200 and prints the boot banner, which states the role and MAC).
Windows: `esptool --chip esp32 --port COMx --baud 115200 write-flash -z <offset> <file>`.
Never flash the HUB image onto more than one powered board. **Keep the board plugged in until the flash reports "Hash of data verified"** — an interrupted write erases the app and the board boot-loops until reflashed (settings survive).

**Mirrored board** (letters appear backwards because the LED data line enters from the other side): Boards tab → that board's card → **Flip**. Or hold **K1 while applying power** (shows "E"), or serial `m`. The flag is saved on that board. Re-numbering after a NODE install: the hub remembers the board's MAC, so it gets its old position back unless you reset the order.

## Next step
Answer the open questions in `Progress.md`, then Phase 0 bench test (two boards, ESP-NOW ping, flicker check). `character_patterns.h` must be added to the repo first.
