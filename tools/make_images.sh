#!/usr/bin/env bash
# Build the three distributable images from an exported arduino-cli build:
#   release/<ver>/LED_Sign_HUB_install.bin    full 4 MB image, NVS pre-seeded role=HUB   (flash at 0x0; wipes settings)
#   release/<ver>/LED_Sign_NODE_install.bin   full 4 MB image, NVS pre-seeded role=NODE  (flash at 0x0; wipes settings)
#   release/<ver>/LED_Sign_update_app.bin     app only (flash at 0x10000; keeps role, positions, message)
# Run from the repo root on the build box after:
#   cd firmware && arduino-cli compile --fqbn esp32:esp32:lolin32:PartitionScheme=no_ota --export-binaries LED_Sign_Mesh
set -euo pipefail
cd "$(dirname "$0")/.."
B=firmware/LED_Sign_Mesh/build/esp32.esp32.lolin32
VER=$(git rev-parse --short HEAD)
OUT=release/$VER
mkdir -p "$OUT"
ESPTOOL=$(ls -d ~/.arduino15/packages/esp32/tools/esptool_py/*/esptool | head -1)
NVSGEN="python3 -m esp_idf_nvs_partition_gen.nvs_partition_gen"

make_nvs() {  # $1 = role value (0 node, 1 hub), $2 = out file
  printf 'key,type,encoding,value\nsys,namespace,,\nrole,data,u8,%s\n' "$1" > "$OUT/nvs_role$1.csv"
  $NVSGEN generate "$OUT/nvs_role$1.csv" "$2" 0x5000 >/dev/null
}
make_nvs 1 "$OUT/nvs_hub.bin"
make_nvs 0 "$OUT/nvs_node.bin"

merge() {  # $1 = nvs bin, $2 = out
  $ESPTOOL --chip esp32 merge-bin -o "$2" --flash-mode dio --flash-freq 80m --flash-size 4MB \
    0x1000 "$B/LED_Sign_Mesh.ino.bootloader.bin" 0x8000 "$B/LED_Sign_Mesh.ino.partitions.bin" \
    0x9000 "$1" 0xe000 "$B/boot_app0.bin" 0x10000 "$B/LED_Sign_Mesh.ino.bin" >/dev/null
}
merge "$OUT/nvs_hub.bin"  "$OUT/LED_Sign_HUB_install.bin"
merge "$OUT/nvs_node.bin" "$OUT/LED_Sign_NODE_install.bin"
cp "$B/LED_Sign_Mesh.ino.bin" "$OUT/LED_Sign_update_app.bin"
rm -f "$OUT"/nvs_role*.csv "$OUT"/nvs_*.bin

cat > "$OUT/README.txt" <<EOF
LED_Sign_Mesh firmware images — commit $VER — built $(date -u +%Y-%m-%dT%H:%MZ)

LED_Sign_HUB_install.bin   -> the ONE hub board.      esptool --chip esp32 --port <port> --baud 115200 write-flash -z 0x0     LED_Sign_HUB_install.bin
LED_Sign_NODE_install.bin  -> every other board.      esptool --chip esp32 --port <port> --baud 115200 write-flash -z 0x0     LED_Sign_NODE_install.bin
LED_Sign_update_app.bin    -> firmware update, any board, keeps role/positions/message:
                                                      esptool --chip esp32 --port <port> --baud 115200 write-flash -z 0x10000 LED_Sign_update_app.bin
Install images erase saved settings (positions, message); update image does not.
EOF
( cd "$OUT" && sha256sum *.bin > SHA256SUMS )
ls -la "$OUT"
