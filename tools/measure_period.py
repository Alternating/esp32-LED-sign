#!/usr/bin/env python3
"""Set wspeed on the hub, then measure spacing of node `apply` lines.
Usage: measure_period.py [hub_port] [node_port] [period_ms] [seconds]"""
import sys, time, re, statistics, serial
hp = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyUSB0"
np_ = sys.argv[2] if len(sys.argv) > 2 else "/dev/ttyUSB1"
period = int(sys.argv[3]) if len(sys.argv) > 3 else 1000
secs = float(sys.argv[4]) if len(sys.argv) > 4 else 30
h = serial.Serial(hp, 115200, timeout=0.2, dsrdtr=False, rtscts=False)
n = serial.Serial(np_, 115200, timeout=0.2, dsrdtr=False, rtscts=False)
time.sleep(0.4)
h.write(('{"cmd":"wspeed","ms":%d}\n' % period).encode()); time.sleep(1.0)
n.reset_input_buffer()
t0 = time.time(); times = []; leads = []; idxs = []
while time.time() - t0 < secs:
    l = n.readline().decode(errors="replace")
    if "[NODE] apply" in l:
        times.append(time.time())
        m = re.search(r"word=(\d+)/(\d+)", l); idxs.append(m.group(1) if m else "?")
        m = re.search(r"\(([+-]\d+) ms\)", l); leads.append(int(m.group(1)) if m else None)
gaps = [round((b - a) * 1000) for a, b in zip(times, times[1:])]
gaps = [g for g in gaps if g > 50]  # ignore same-word restyle applies
print("period set %d ms, %d applies in %.0f s, idx seq: %s" % (period, len(times), secs, "".join(idxs)))
if gaps: print("spacing ms: mean %.1f  sd %.1f  min %d  max %d" % (statistics.mean(gaps), statistics.pstdev(gaps), min(gaps), max(gaps)))
ld = [x for x in leads if x is not None]
if ld: print("lead ms: min %d  median %d  max %d" % (min(ld), statistics.median(ld), max(ld)))
