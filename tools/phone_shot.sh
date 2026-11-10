#!/usr/bin/env bash
# Screenshot the ADB-connected phone, downscale, and push it to the `phone-shots` branch (force, single commit)
# so the cloud session can fetch and view it. Usage: tools/phone_shot.sh [name]   (default: shot)
# Phone must be awake/unlocked (screencap returns 0 bytes on a locked/off screen).
set -euo pipefail
NAME=${1:-shot}
cd "$(dirname "$0")/.."
WT=~/phone-shots-wt
if [ ! -d "$WT/.git" ] && [ ! -f "$WT/.git" ]; then
  git worktree prune
  git show-ref -q --verify refs/heads/phone-shots || git branch phone-shots "$(git commit-tree "$(git hash-object -t tree /dev/null)" -m 'phone screenshots')"
  git worktree add -q "$WT" phone-shots
fi
adb exec-out screencap -p > /tmp/$NAME.png
[ -s /tmp/$NAME.png ] || { echo "!! empty screenshot — phone locked or screen off"; exit 1; }
python3 - /tmp/$NAME.png "$WT/$NAME.jpg" <<'PY'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGB")
w, h = im.size
im = im.resize((w * 1100 // h, 1100)) if h > 1100 else im
im.save(sys.argv[2], "JPEG", quality=80)
print("saved", sys.argv[2], im.size)
PY
cd "$WT"
git add -A
git -c user.name="qwen box" -c user.email="qwen@localhost" commit -q --amend -m "phone screenshots" || git -c user.name="qwen box" -c user.email="qwen@localhost" commit -q -m "phone screenshots"
git push -q -f origin phone-shots
echo "pushed $NAME.jpg to phone-shots"
