#!/usr/bin/env python3
"""Fire N unicast identify commands at pos P through the hub's serial JSON console and
count how many the node logs. Usage: test_identify.py [hub_port] [node_port] [N] [pos]"""
import sys, time, serial
hp = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyUSB0"
np_ = sys.argv[2] if len(sys.argv) > 2 else "/dev/ttyUSB1"
N = int(sys.argv[3]) if len(sys.argv) > 3 else 15
pos = int(sys.argv[4]) if len(sys.argv) > 4 else 2
h = serial.Serial(hp, 115200, timeout=0.2, dsrdtr=False, rtscts=False)
n = serial.Serial(np_, 115200, timeout=0.2, dsrdtr=False, rtscts=False)
time.sleep(0.5); h.reset_input_buffer(); n.reset_input_buffer()
got = 0; replies = []
for i in range(N):
    h.write(('{"cmd":"identify","pos":%d,"ms":300}\n' % pos).encode()); t = time.time()
    while time.time() - t < 1.2:
        l = n.readline().decode(errors="replace")
        if "IDENTIFY" in l: got += 1; break
    while h.in_waiting: replies.append(h.readline().decode(errors="replace").strip())
    time.sleep(0.3)
h.write(b"s\n"); time.sleep(1.5)
st = "".join(h.readline().decode(errors="replace") for _ in range(80))
print("node IDENTIFY received: %d / %d" % (got, N))
print("hub replies ok:", sum(1 for r in replies if '"ok":true' in r), "err:", [r for r in replies if '"ok":false' in r][:2])
print("\n".join(l for l in st.splitlines() if "tx" in l.lower() or "fail" in l.lower())[:400])
