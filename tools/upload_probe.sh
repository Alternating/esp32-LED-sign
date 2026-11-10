#!/usr/bin/env bash
# Upload a partial image (rejected at the end, no reboot) while logging the hub's serial output with host timestamps.
# Usage: tools/upload_probe.sh [kB] [iface] [hub_port]
KB=${1:-300}; IF=${2:-wlp3s0}; PORT=${3:-/dev/ttyUSB0}
cd "$(dirname "$0")/.."
REL=$(ls -dt release/*/ | head -1)
head -c $((KB*1024)) "${REL}LED_Sign_ota.bin" > /tmp/probe.bin
python3 - "$PORT" > /tmp/probe_serial.log 2>&1 <<'PY' &
import serial, sys, time
s = serial.Serial(sys.argv[1], 115200, timeout=0.2, dsrdtr=False, rtscts=False)
t0 = time.time()
while time.time() - t0 < 150:
    l = s.readline().decode(errors="replace").rstrip()
    if l: print("%7.2f %s" % (time.time() - t0, l), flush=True)
PY
SP=$!
sleep 1
curl -s -m 140 --interface "$IF" -F "file=@/tmp/probe.bin" -o /tmp/probe_curl.log http://192.168.4.1/ota
echo "curl exit $?"
sleep 2; kill $SP 2>/dev/null
cat /tmp/probe_curl.log; echo
grep -E "OTA|WEB|RADIO|give up|fail" /tmp/probe_serial.log | head -40
echo "serial lines: $(wc -l < /tmp/probe_serial.log)"
