# How to build and add a board — checklist

One page, in order. Details, photos and part links: [`HARDWARE_ASSEMBLY.md`](HARDWARE_ASSEMBLY.md).

**Per board you need:** LOLIN32 · one 10 m reel WS2812B 5 V 60 LED/m · LiFePO4 6 V 6 Ah · XL4015 · 10 000 µF 25 V capacitor · 470 Ω resistor · 2 F1 spade lugs · DC barrel pigtail pair · 22 AWG + 18 AWG wire · foam board · printed battery hanger (`hardware/battery_hanger_-_led_sign_v4.stl`).

## 1. Matrix
- [ ] Cut a square foam panel; rule 20 row lines and a start/stop line at each side.
- [ ] Cut 20 strips × 20 LEDs (cut across the copper pads only).
- [ ] Stick the rows down **alternating direction** (follow the arrows — it should zig-zag).
- [ ] Join row ends with short 3-wire U-jumpers: +5 V→+5 V, DO→DIN, GND→GND. Hot-glue the joints.
- [ ] Data input (first LED's DIN) at the **top-right, seen from the front**.

## 2. Power
- [ ] Charge the battery over USB until green.
- [ ] Battery (F1 lugs) → XL4015 **input only**.
- [ ] Multimeter on XL4015 output → trim to **5.0 V**. Mark the pot.
- [ ] 10 000 µF across the output (stripe = −).
- [ ] Output → matrix +5 V/GND through the barrel pigtail.
- [ ] Output → LOLIN32 **JST battery connector** — check + / − against the board marking first.
- [ ] LOLIN32 **GPIO 4 → 470 Ω → DIN**.
- [ ] All grounds common.

## 3. Mount
- [ ] Print the hanger (PLA/PETG, 0.2 mm, 20–30 % infill).
- [ ] Cut a notch in the top edge of the foam board, hang the board from the hanger, seat the battery.

## 4. Flash (USB, once per board)
- [ ] The hub board only: `LED_Sign_HUB_install.bin`. Every other board: `LED_Sign_NODE_install.bin`.
- [ ] `python tools/flasher.py`, or `esptool --chip esp32 --baud 115200 write-flash -z 0x0 <file>`.
- [ ] Keep the board plugged in until **"Hash of data verified"**.
- [ ] Label the hub board "HUB / 1".

## 5. Add it to the sign
- [ ] Hub already on? Power the new board — it takes the **lowest free position**, shows that number for 3 s and keeps it.
- [ ] **Replacing a dead board** (sign already has 10): Boards tab → the old board's offline card → **Forget** first, then power the new one — it takes the freed position. With 10 positions taken, a new board gets no number.
- [ ] Phone: join `LEDSIGN-HUB` (`sign1234`) → page opens → **Boards** tab shows it online.
- [ ] **Identify** on its card → it shows its number in purple for 10 s.
- [ ] Letters backwards? **Flip** on its card.
- [ ] Firmware badge red? Settings → Firmware → **Update all boards**.
- [ ] Want it in a different spot? Boards → **Reset order**, then power the boards on in the order you want.

## Quick fixes
| Symptom | Fix |
|---|---|
| Nothing lights | 5.0 V at the XL4015 output? data on **DIN** (not DO)? common ground? |
| Stops partway down | bad jumper or DO/DIN crossed at the last working row |
| Flicker / resets on colour change | capacitor missing or reversed; battery low |
| Not in the Boards tab | flashed with the NODE image? only one hub powered? |
