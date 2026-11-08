# Bench sketches

Build host: Qwen box (`qwen@192.168.1.8`), LOLIN32 on `/dev/ttyUSB0`. Toolchain: `~/.local/bin/arduino-cli` 1.5.1, core `esp32:esp32` 3.3.12 (IDF 5.5.5), FastLED 3.10.9, ArduinoJson 7.4.3.

```bash
cd ~/esp32-led-sign/firmware/bench
~/.local/bin/arduino-cli compile --fqbn esp32:esp32:lolin32:PartitionScheme=no_ota <sketch>
~/.local/bin/arduino-cli upload  --fqbn esp32:esp32:lolin32:PartitionScheme=no_ota -p /dev/ttyUSB0 <sketch>
python3 ~/esp32-led-sign/tools/serial_capture.py /dev/ttyUSB0 20        # reset + capture 20 s
```
LOLIN32 partition options are `default | no_ota | min_spiffs` (no `huge_app`); `no_ota` gives a 2.6 MB app slot.

## font_test
Hardware check for `character_patterns.h`: prints chip/MAC/version info, measures `FastLED.show()`, cycles the 21 new glyphs then A–Z 0–9. Keys over serial: `n` next, `p` pause, `m` mirror.

Result 2026-11-08 (board MAC 9C:9C:1F:E3:C6:8C): compiles clean, 517 kB, 296 kB free heap, `show()` = 13.7 ms for 400 LEDs on the RMT5 driver.
