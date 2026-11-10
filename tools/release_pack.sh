#!/usr/bin/env bash
# Package a built release/<git>/ folder (from tools/make_images.sh) as a GitHub release "firmware pack".
# Dry run by default; add --publish to create the tag + release with gh (build box: gh is logged in).
#
# Usage: tools/release_pack.sh <version> [release_dir] [--publish]
#   <version>      e.g. 0.4.0 — must equal FW_VERSION_STR embedded in the images (config.h)
#   [release_dir]  default: newest release/*/ by mtime
# Needs: docs/releases/v<version>.md (release notes). Output: dist/v<version>/ (gitignored).
#
# Refuses when: checksums fail; an image lacks the version or the build commit; the build was -dirty;
# firmware/ or web/ changed between the build commit and HEAD (tag would not match the binaries);
# HEAD is not on origin/main; the tag already exists.
set -euo pipefail
cd "$(dirname "$0")/.."
VER=""; REL=""; PUBLISH=0
for a in "$@"; do case "$a" in --publish) PUBLISH=1;; *) if [ -z "$VER" ]; then VER=$a; else REL=$a; fi;; esac; done
[ -n "$VER" ] || { sed -n '2,12p' "$0"; exit 1; }
REL=${REL:-$(ls -dt release/*/ | head -1)}; REL=${REL%/}
BUILD=$(basename "$REL")
TAG="v$VER"; NOTES="docs/releases/$TAG.md"; OUT="dist/$TAG"
BINS="LED_Sign_HUB_install.bin LED_Sign_NODE_install.bin LED_Sign_ota.bin"
fail() { echo "!! $*"; exit 1; }

echo "== $TAG from $REL (build $BUILD)"
[ -f "$NOTES" ] || fail "missing release notes $NOTES"
case "$BUILD" in *-dirty*) fail "build is -dirty; rebuild from a clean commit";; esac
( cd "$REL" && sha256sum -c --quiet SHA256SUMS ) || fail "checksum mismatch in $REL"
for b in $BINS; do
  grep -qa "$BUILD" "$REL/$b" || fail "$b does not contain build id $BUILD"
  grep -qaF "$VER" "$REL/$b"  || fail "$b does not contain version $VER"
done
grep -q "FW_VERSION_STR *\"$VER\"" firmware/LED_Sign_Mesh/config.h || fail "config.h FW_VERSION_STR is not $VER"
git diff --quiet "$BUILD" HEAD -- firmware web || fail "firmware/web changed since $BUILD — rebuild at HEAD first"
git fetch -q origin main
git merge-base --is-ancestor HEAD origin/main || fail "HEAD $(git rev-parse --short HEAD) is not pushed to origin/main"
if git ls-remote --tags origin "refs/tags/$TAG" | grep -q .; then fail "tag $TAG already exists on GitHub"; fi
echo "== checks OK: checksums, version $VER + build $BUILD in all images, firmware unchanged $BUILD..$(git rev-parse --short HEAD)"

rm -rf "$OUT"; mkdir -p "$OUT"
for b in $BINS; do cp "$REL/$b" "$OUT/"; done
cat > "$OUT/INSTALL.txt" <<EOF
ESP32 LED Sign firmware $TAG  (build $BUILD)
https://github.com/Alternating/esp32-LED-sign/releases/tag/$TAG

FIRST INSTALL — USB, once per board (erases that board's saved settings):
  LED_Sign_HUB_install.bin   the ONE hub board only
  LED_Sign_NODE_install.bin  every other board
    esptool --chip esp32 --port <COMx or /dev/ttyUSBx> --baud 115200 write-flash -z 0x0 <file>
    or: python tools/flasher.py   (guided, from the repo)
  Keep the board plugged in until esptool prints "Hash of data verified".
  Then power the hub first and the other boards one at a time, in sign order.

UPDATES — over the air, keeps role, positions and message:
  LED_Sign_ota.bin  phone on the sign's Wi-Fi (LEDSIGN-HUB / sign1234) -> Settings -> Firmware
                    -> choose this file -> Upload to hub -> wait for the hub to come back
                    -> Update all boards (about 20 s per board).

VERIFY:  sha256sum -c SHA256SUMS      (Windows: certutil -hashfile <file> SHA256)
EOF
( cd "$OUT" && sha256sum $BINS > SHA256SUMS )
python3 - "$OUT" "LED_Sign_firmware_$TAG.zip" <<'PY'
import sys, os, zipfile
d, name = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(os.path.join(d, name), "w", zipfile.ZIP_DEFLATED) as z:
    for f in sorted(os.listdir(d)):
        if f != name: z.write(os.path.join(d, f), f"LED_Sign_firmware_{name.split('_')[-1][:-4]}/{f}")
PY
ls -la "$OUT"

TARGET=$(git rev-parse HEAD)
if [ "$PUBLISH" = 1 ]; then
  gh release create "$TAG" --repo Alternating/esp32-LED-sign --target "$TARGET" \
    --title "LED Sign firmware $TAG" --notes-file "$NOTES" \
    "$OUT"/*.bin "$OUT/SHA256SUMS" "$OUT/INSTALL.txt" "$OUT/LED_Sign_firmware_$TAG.zip"
  echo "== published $TAG at $(git rev-parse --short HEAD)"
else
  echo "== dry run: would tag $TAG at $(git rev-parse --short HEAD) and upload the files above. Re-run with --publish."
fi
