#!/usr/bin/env python3
"""Send JSON commands (or single-letter console keys) to the hub over serial without resetting it,
print the JSON replies, then optionally the `s` status board table.
Usage: hub_cmd.py [--port /dev/ttyUSB0] [--status] 'JSON' ['JSON' ...]"""
import argparse, time, serial
ap = argparse.ArgumentParser()
ap.add_argument("--port", default="/dev/ttyUSB0")
ap.add_argument("--status", action="store_true")
ap.add_argument("cmds", nargs="*")
a = ap.parse_args()
s = serial.Serial(a.port, 115200, timeout=0.3, dsrdtr=False, rtscts=False)
time.sleep(0.3); s.reset_input_buffer()
for c in a.cmds:
    s.write((c + "\n").encode()); time.sleep(0.7)
    for l in s.read(8000).decode(errors="replace").splitlines():
        if l.startswith("{"): print(l[:400])
if a.status:
    s.write(b"s\n"); time.sleep(2)
    for l in s.read(10000).decode(errors="replace").splitlines():
        if "role=" in l or " -> pos " in l or l.startswith("fw ") or "groups=" in l or "text=" in l:
            print(l[:130])
