#!/usr/bin/env bash
# Flash one board with a labeled release image and print its boot banner.
# Usage: tools/flash_board.sh /dev/ttyUSB2 node      -> LED_Sign_NODE_install.bin  (full install, role=NODE)
#        tools/flash_board.sh /dev/ttyUSB2 hub       -> LED_Sign_HUB_install.bin   (full install, role=HUB; only ONE board!)
# Images come from tools/make_images.sh (release/<commit>/). Run from the repo root on the build box.
# Wired = full install only (once per board). Updates after that are over the air: upload
# release/<commit>/LED_Sign_ota.bin from the phone (Settings -> Firmware), then "Update all boards".
set -euo pipefail
PORT="${1:?usage: flash_board.sh /dev/ttyUSBx hub|node}"
KIND="${2:?usage: flash_board.sh /dev/ttyUSBx hub|node}"
ROLE=""
cd "$(dirname "$0")/.."
REL=$(ls -dt release/*/ 2>/dev/null | head -1 | sed "s|/$||")   # newest build by mtime
[ -n "$REL" ] || { echo "no release/ images — run tools/make_images.sh first"; exit 1; }
case "$KIND" in
  hub)    BIN="$REL/LED_Sign_HUB_install.bin";  ADDR=0x0 ;;
  node)   BIN="$REL/LED_Sign_NODE_install.bin"; ADDR=0x0 ;;
  update) echo "no wired update any more: upload $REL/LED_Sign_ota.bin from the phone (Settings -> Firmware)"; exit 1 ;;
  *) echo "kind must be hub|node"; exit 1 ;;
esac
ESPTOOL=$(ls -d ~/.arduino15/packages/esp32/tools/esptool_py/*/esptool 2>/dev/null | head -1)
[ -x "$ESPTOOL" ] || ESPTOOL="python3 -m esptool"
echo "== $PORT : flashing $KIND image $BIN ($(stat -c %s "$BIN") bytes) at 115200, offset $ADDR"
[ -e "$PORT" ] || { echo "!! $PORT does not exist — is the board plugged in?"; exit 1; }
set +e
OUT=$($ESPTOOL --chip esp32 --port "$PORT" --baud 115200 --before default-reset --after hard-reset \
  write-flash -z --flash-mode dio --flash-freq 80m --flash-size 4MB $ADDR "$BIN" 2>&1); RC=$?
set -e
echo "$OUT" | grep -E "Hash of data verified|Hard resetting|rror" || true
if [ $RC -ne 0 ] || ! echo "$OUT" | grep -q "Hash of data verified"; then echo "!! flash FAILED on $PORT (rc=$RC) — not verified; do not power-cycle until reflashed"; exit 1; fi
echo "== $PORT : boot banner"
python3 tools/serial_capture.py "$PORT" 9 "$ROLE" | grep -E "MAC|Role|role|SoftAP|pos=|->" | head -12
echo "== updates after this install are over the air: phone -> Settings -> Firmware ($REL/LED_Sign_ota.bin)"
