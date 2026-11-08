#!/usr/bin/env python3
"""Reset the ESP32 via DTR/RTS and capture serial output for N seconds.
Usage: serial_capture.py [port] [seconds] [keys-to-send-after-1s]
"""
import sys, time
import serial

port = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyUSB0"
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 20
keys = sys.argv[3] if len(sys.argv) > 3 else ""

s = serial.Serial(port, 115200, timeout=0.5)
# Reset pulse (EN low via RTS), keep IO0 high (DTR) so it boots normally
s.setDTR(False); s.setRTS(True); time.sleep(0.1); s.setRTS(False)
t0 = time.time(); sent = False
while time.time() - t0 < secs:
    line = s.readline()
    if line:
        print(line.decode(errors="replace").rstrip(), flush=True)
    if keys and not sent and time.time() - t0 > 1.0:
        s.write(keys.encode()); sent = True
s.close()
