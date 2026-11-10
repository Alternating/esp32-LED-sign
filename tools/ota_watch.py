#!/usr/bin/env python3
"""Follow a node OTA rollout from the hub's serial log (no reset). Prints [OTA]/queue lines with timestamps
until the queue reports idle or the timeout passes. Usage: ota_watch.py [port] [timeout_s]"""
import sys, time, serial
port = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyUSB0"
tmo = float(sys.argv[2]) if len(sys.argv) > 2 else 400
s = serial.Serial(port, 115200, timeout=0.3, dsrdtr=False, rtscts=False)
t0 = time.time()
while time.time() - t0 < tmo:
    l = s.readline().decode(errors="replace").rstrip()
    if not l: continue
    if "OTA" in l or "ota" in l or "adopted" in l or "give up" in l:
        print("%6.1f %s" % (time.time() - t0, l[:150]), flush=True)
        if "queue idle" in l or "queue done" in l or "queue empty" in l: break
