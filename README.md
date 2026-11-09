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
  LED_Sign_Mesh/          mesh firmware (hub + node, one binary; Phase 4 OTA)
hardware/
  battery_hanger_-_led_sign_v4.stl
```

## Which board is the hub?
A **dedicated board**, not whichever powers on first. The hub role is a flag saved in that board's flash (NVS); every other board, including freshly flashed ones, defaults to **node**. Label the hub sign ("HUB / 1").

- Current hub: MAC `9C:9C:1F:E3:CE:54`. It is always position **1** and runs the `LEDSIGN-HUB` Wi-Fi that phones join.
- Power-on order only numbers the **nodes**: first node powered after the hub gets 2, the next 3, … Positions stick by MAC until cleared (hub K3 long-press ≥ 3 s, or **Reset order** in the Boards tab), then power the boards on again in the order you want.
- To make a different board the hub: hold **K4 while applying power** (~3 s) → it shows "H" and saves the role; flip the old hub back the same way (shows "N"). Boards without buttons: send `H`/`N` over serial in the first 3 s after reset. Only one hub should be powered at a time.
## Firmware: wired install once, then over the air forever
Build: `esp32:esp32:lolin32:PartitionScheme=min_spiffs` (two 1.9 MB app slots for OTA). One source tree builds one program; the **role is pre-seeded in the settings partition** of the install images so a board is a hub or a node from its very first boot. `tools/make_images.sh` (build box) writes `build_id.h`, compiles, and puts the images in `release/<commit>/` with a README and SHA256SUMS:

| file | goes to | how | effect |
|---|---|---|---|
| `LED_Sign_HUB_install.bin` | **the one hub board** | USB, offset `0x0` | full install, role = HUB, settings wiped |
| `LED_Sign_NODE_install.bin` | every other board | USB, offset `0x0` | full install, role = NODE, settings wiped |
| `LED_Sign_ota.bin` | the hub, from the phone | Wi-Fi upload | update — keeps role, positions, message |

**Install (once per board, and once more for every board when moving to the Phase 4 partition layout):** `python tools/flasher.py` (guided) or `tools/flash_board.sh /dev/ttyUSBx hub|node` on the build box. Windows: `esptool --chip esp32 --port COMx --baud 115200 write-flash -z 0x0 <file>`. Never flash the HUB image onto more than one powered board. **Keep the board plugged in until the flash reports "Hash of data verified".**

**Update (every release after that):** join the sign's Wi-Fi → Settings → **Firmware** → choose `release/<commit>/LED_Sign_ota.bin` → **Upload to hub**. The hub reboots into the new image (~20 s). Then press **Update all boards**: the hub updates every node whose build differs from its own, one at a time (~30 s each; the board shows its number in blue with a green progress bar). From a computer on the sign's Wi-Fi: `curl -F "file=@LED_Sign_ota.bin" http://192.168.4.1/ota`, then `curl -d '{"cmd":"ota_nodes"}' http://192.168.4.1/cmd`. Serial on the hub: `o` = update all, `O5` = update board 5, `s` shows build / slot / queue.

**Rollback:** a freshly updated board must run healthy for 30 s (hub: running with its Wi-Fi up; node: on the radio channel and joined/discovering) before the new image is confirmed. If it resets before that — crash, watchdog, or a power cycle — the bootloader starts the previous image on that **one** reset. A failed node download never touches the running image. There is no wired "app-only update" image any more (with two app slots an app-only write at `0x10000` can hit the wrong slot).

**Mirrored board** (letters appear backwards because the LED data line enters from the other side): Boards tab → that board's card → **Flip**. Or hold **K1 while applying power** (shows "E"), or serial `m`. The flag is saved on that board. Re-numbering after a NODE install: the hub remembers the board's MAC, so it gets its old position back unless you reset the order.

## Next step
Answer the open questions in `Progress.md`, then Phase 0 bench test (two boards, ESP-NOW ping, flicker check). `character_patterns.h` must be added to the repo first.
