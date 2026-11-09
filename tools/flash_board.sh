#!/usr/bin/env bash
# Flash one board with a labeled release image and print its boot banner.
# Usage: tools/flash_board.sh /dev/ttyUSB2 node      -> LED_Sign_NODE_install.bin  (full install, role=NODE)
#        tools/flash_board.sh /dev/ttyUSB2 hub       -> LED_Sign_HUB_install.bin   (full install, role=HUB; only ONE board!)
#        tools/flash_board.sh /dev/ttyUSB2 update    -> LED_Sign_update_app.bin    (app only at 0x10000, keeps settings)
# Images come from tools/make_images.sh (release/<commit>/). Run from the repo root on the build box.
set -euo pipefail
PORT="${1:?usage: flash_board.sh /dev/ttyUSBx hub|node|update}"
KIND="${2:?usage: flash_board.sh /dev/ttyUSBx hub|node|update}"
ROLE=""
cd "$(dirname "$0")/.."
REL=$(ls -d release/* 2>/dev/null | sort | tail -1)
[ -n "$REL" ] || { echo "no release/ images — run tools/make_images.sh first"; exit 1; }
case "$KIND" in
  hub)    BIN="$REL/LED_Sign_HUB_install.bin";  ADDR=0x0 ;;
  node)   BIN="$REL/LED_Sign_NODE_install.bin"; ADDR=0x0 ;;
  update) BIN="$REL/LED_Sign_update_app.bin";   ADDR=0x10000 ;;
  *) echo "kind must be hub|node|update"; exit 1 ;;
esac
ESPTOOL=$(ls -d ~/.arduino15/packages/esp32/tools/esptool_py/*/esptool 2>/dev/null | head -1)
[ -x "$ESPTOOL" ] || ESPTOOL="python3 -m esptool"
echo "== $PORT : flashing $KIND image $BIN ($(stat -c %s "$BIN") bytes) at 115200, offset $ADDR"
$ESPTOOL --chip esp32 --port "$PORT" --baud 115200 --before default-reset --after hard-reset \
  write-flash -z --flash-mode dio --flash-freq 80m --flash-size 4MB $ADDR "$BIN" | grep -E "Hash of data verified|Hard resetting|error|Error" || true
echo "== $PORT : boot banner"
python3 tools/serial_capture.py "$PORT" 9 "$ROLE" | grep -E "MAC|Role|role|SoftAP|pos=|->" | head -12
