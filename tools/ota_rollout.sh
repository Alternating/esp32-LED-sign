#!/usr/bin/env bash
# Over-the-air rollout from the build box: join the hub's AP on a spare WLAN interface,
# upload LED_Sign_ota.bin to the hub, wait for it to come back and mark itself valid,
# then "Update all boards" and follow progress until the queue is idle. Cleans up the WLAN afterwards.
# Usage: sudo -v; tools/ota_rollout.sh [release_dir] [iface]
#   defaults: newest release/<git>/, iface wlp3s0. Wired default route is never touched.
set -uo pipefail
cd "$(dirname "$0")/.."
REL=${1:-$(ls -dt release/*/ | head -1)}; REL=${REL%/}
IF=${2:-wlp3s0}
BIN="$REL/LED_Sign_ota.bin"
HUB=192.168.4.1; ME=192.168.4.50
[ -f "$BIN" ] || { echo "missing $BIN"; exit 1; }
CONF=$(mktemp); printf 'ctrl_interface=/var/run/wpa_supplicant\nnetwork={\n ssid="LEDSIGN-HUB"\n psk="sign1234"\n}\n' > "$CONF"

cleanup() {
  [ -f /run/wpa_ota.pid ] && sudo kill "$(cat /run/wpa_ota.pid)" 2>/dev/null; sudo rm -f /run/wpa_ota.pid
  sudo ip addr flush dev "$IF" 2>/dev/null; sudo ip link set "$IF" down 2>/dev/null; rm -f "$CONF"
  echo "== WLAN $IF detached"
}
trap cleanup EXIT

join() {
  sudo ip link set "$IF" up
  sudo wpa_supplicant -B -i "$IF" -c "$CONF" -P /run/wpa_ota.pid >/dev/null 2>&1 || true
  sudo ip addr replace "$ME/24" dev "$IF"
  command -v iw >/dev/null && sudo iw dev "$IF" set power_save off 2>/dev/null   # PS on the box card made uploads crawl
  command -v resolvectl >/dev/null && sudo resolvectl default-route "$IF" no 2>/dev/null
  for i in $(seq 30); do ping -c1 -W1 -I "$IF" $HUB >/dev/null 2>&1 && { echo "== joined LEDSIGN-HUB via $IF"; return 0; }; sleep 1; done
  echo "!! could not reach $HUB"; return 1
}
state() { curl -s -m 4 --interface "$IF" http://$HUB/state; }
jq_py() { python3 -c "import sys,json; d=json.load(sys.stdin); $1" 2>/dev/null; }

join || exit 1
echo "== hub before: $(state | jq_py 'o=d["ota"]["hub"]; print(o["buildId"], o["git"], o["slot"], "valid" if o["valid"] else "pending")')"
echo "== uploading $BIN ($(stat -c %s "$BIN") B)"
t0=$(date +%s); curl -s -m 600 --interface "$IF" -F "file=@$BIN" http://$HUB/ota; echo; echo "== upload took $(( $(date +%s)-t0 )) s; hub rebooting"
sleep 8
sudo kill "$(cat /run/wpa_ota.pid)" 2>/dev/null; sudo rm -f /run/wpa_ota.pid; sleep 2; join || exit 1
for i in $(seq 60); do v=$(state | jq_py 'print(1 if d["ota"]["hub"]["valid"] else 0)'); [ "$v" = 1 ] && break; sleep 2; done
echo "== hub after:  $(state | jq_py 'o=d["ota"]["hub"]; print(o["buildId"], o["git"], o["slot"], "valid" if o["valid"] else "pending")')"
echo "== update all boards"
curl -s -m 5 --interface "$IF" -H 'Content-Type: application/json' -d '{"cmd":"ota_nodes"}' http://$HUB/cmd; echo
t0=$(date +%s)
for i in $(seq 300); do
  s=$(state) || { sleep 2; continue; }
  line=$(echo "$s" | jq_py 'o=d["ota"]; print(("RUN " if o["running"] else "IDLE ")+" ".join("%s:%s%s"%(b["pos"],b["status"],("@%d%%"%b["pct"]) if b.get("status")=="updating" else "") for b in o["boards"]))')
  echo "  +$(( $(date +%s)-t0 ))s $line"
  case "$line" in IDLE*) break;; esac
  sleep 3
done
echo "== boards:"; state | jq_py 'h=d["ota"]["hub"]["buildId"]; [print("  pos %s fw %s %s"%(b["pos"],b.get("fw"),"OK" if b.get("fw")==h else "DIFFERENT")) for b in d["state"]["boards"]]'
