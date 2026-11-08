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

## espnow_ping (Phase 0)
One binary, role in NVS. Set role over serial within 3 s of boot: `serial_capture.py /dev/ttyUSB0 8 H` (hub) / `... /dev/ttyUSB1 8 N` (node).
Hub: `WIFI_AP_STA`, SoftAP `LEDSIGN-BENCH` / `sign1234` pinned to channel 6, page at http://192.168.4.1, broadcasts PING @ 5 Hz, shows "H".
Node: `WIFI_STA` on channel 6, unicast PONG, shows "N". Both print stats every 5 s.

Result 2026-11-08, boards side by side on the bench:
| metric | value |
|---|---|
| hub MAC / node MAC | 9C:9C:1F:E3:C6:8C / 78:E3:6D:19:18:38 |
| pings / pongs (60 s) | 248 / 248 → **0.0 % loss**, txfail 0 |
| RTT min / avg / max | 3 / 6.5 / 25 ms |
| RSSI node←hub / hub←node | −33 / −25 dBm |
| heap hub / node | 213.6 kB / 221.7 kB, flat over the run |
| sketch | 1.01 MB (48 % of `no_ota`), 50 kB static RAM |
| AP channel == ESP-NOW channel | 6 == 6 ✓ (`esp_wifi_get_channel` check in `radioInit`) |

Still to do for Phase 0 sign-off: visual flicker check on a matrix while pinging; phone joined to `LEDSIGN-BENCH` loading 192.168.4.1 during the run (loss must stay < 2 %); repeat at 5 m range.
