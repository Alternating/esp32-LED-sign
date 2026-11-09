#!/usr/bin/env bash
# Flash one LED_Sign_Mesh board from the exported merged image and print its boot banner.
# Usage: tools/flash_board.sh /dev/ttyUSB2            (run from the repo root on the build box)
#        tools/flash_board.sh /dev/ttyUSB2 H          (also set the role to HUB in the 3 s boot window; default role is NODE)
# Requires: arduino-cli compile --export-binaries has been run (firmware/LED_Sign_Mesh/build/...merged.bin exists).
set -euo pipefail
PORT="${1:?usage: flash_board.sh /dev/ttyUSBx [H|N]}"
ROLE="${2:-}"
cd "$(dirname "$0")/.."
BIN=firmware/LED_Sign_Mesh/build/esp32.esp32.lolin32/LED_Sign_Mesh.ino.merged.bin
[ -f "$BIN" ] || { echo "missing $BIN — run: cd firmware && arduino-cli compile --fqbn esp32:esp32:lolin32:PartitionScheme=no_ota --export-binaries LED_Sign_Mesh"; exit 1; }
ESPTOOL=$(ls -d ~/.arduino15/packages/esp32/tools/esptool_py/*/esptool 2>/dev/null | head -1)
[ -x "$ESPTOOL" ] || ESPTOOL="python3 -m esptool"
echo "== $PORT : flashing $(git rev-parse --short HEAD) merged image ($(stat -c %s "$BIN") bytes) at 115200"
$ESPTOOL --chip esp32 --port "$PORT" --baud 115200 --before default-reset --after hard-reset \
  write-flash -z --flash-mode dio --flash-freq 80m --flash-size 4MB 0x0 "$BIN" | grep -E "Hash of data verified|Hard resetting|error|Error" || true
echo "== $PORT : boot banner"
python3 tools/serial_capture.py "$PORT" 9 "$ROLE" | grep -E "MAC|Role|role|SoftAP|pos=|->" | head -12
