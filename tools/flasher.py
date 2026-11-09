#!/usr/bin/env python3
"""
LED Sign flasher — guided, one board at a time.

  python tools/flasher.py                 # interactive (hub first if not yet assigned, then add-on boards)
  python tools/flasher.py --images release/120f6ca   # use a specific release folder (default: newest under release/)

Flow:
  "Is the hub already assigned?"  n -> the first board flashed gets LED_Sign_HUB_install.bin
                                  y -> every board gets LED_Sign_NODE_install.bin
  after each board: "Add another board?"  y -> unplug it, plug in the next, press Enter -> flash
Each flash is verified ("Hash of data verified"), then the boot banner is read to confirm role + MAC,
and a line is appended to release/<ver>/boards_flashed.csv.

Wired flashing is the one-time full install. Every update after that is over the air from the phone:
Settings -> Firmware -> upload release/<ver>/LED_Sign_ota.bin to the hub -> "Update all boards".

Requires: pip install esptool pyserial      (115200 baud throughout — the sign's USB adapters reject faster)
"""
import argparse, csv, glob, os, subprocess, sys, time
from datetime import datetime

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("pyserial missing:  pip install pyserial esptool")

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, ".."))
IMAGES = {"hub": ("LED_Sign_HUB_install.bin", "0x0"),
          "node": ("LED_Sign_NODE_install.bin", "0x0")}
OTA_NOTE = ("Updates after install are done over the air from the phone (Settings -> Firmware): "
            "upload LED_Sign_ota.bin to the hub, then press 'Update all boards'.")

def say(msg): print(msg, flush=True)

def ask(prompt, choices=("y", "n"), default=None):
    hint = "/".join(c.upper() if c == default else c for c in choices)
    while True:
        try:
            a = input(f"{prompt} [{hint}] ").strip().lower()
        except EOFError:
            say(""); return default or choices[-1]
        if not a and default: return default
        if a in choices: return a
        say(f"  please answer one of: {', '.join(choices)}")

def newest_release():
    dirs = [d for d in glob.glob(os.path.join(REPO, "release", "*")) if os.path.isdir(d)]
    if not dirs: sys.exit("no release/ folder found — run tools/make_images.sh on the build box, or pass --images DIR")
    return max(dirs, key=os.path.getmtime)

def esptool_cmd():
    # prefer the esptool bundled with the Arduino ESP32 core (Linux build box), else the pip module
    for c in glob.glob(os.path.expanduser("~/.arduino15/packages/esp32/tools/esptool_py/*/esptool")):
        if os.access(c, os.X_OK): return [c]
    return [sys.executable, "-m", "esptool"]

def list_serial():
    """USB serial adapters only (CH340/CP210x…): skip the legacy ttyS* / onboard ports."""
    ports = set()
    for p in list_ports.comports():
        usb = p.vid is not None or "USB" in p.device.upper() or "ACM" in p.device.upper() or p.device.upper().startswith("COM")
        if usb: ports.add(p.device)
    for d in glob.glob("/dev/ttyUSB*") + glob.glob("/dev/ttyACM*") + glob.glob("/dev/cu.usbserial*") + glob.glob("/dev/cu.wchusbserial*"):
        ports.add(d)
    return sorted(ports)

def pick_port(known_before):
    """Auto-pick the port that appeared since `known_before`; otherwise ask."""
    now = list_serial()
    new = [p for p in now if p not in known_before]
    if len(new) == 1:
        say(f"  detected new board on {new[0]}"); return new[0]
    if len(now) == 1:
        return now[0]
    if not now:
        say("  no serial ports found — plug the board in"); return None
    say("  serial ports: " + ", ".join(f"[{i+1}] {p}" for i, p in enumerate(now)))
    while True:
        a = input("  which one? (number, or Enter to rescan) ").strip()
        if not a: return None
        if a.isdigit() and 1 <= int(a) <= len(now): return now[int(a) - 1]
        say("  not a valid number")

def flash(port, image, offset):
    cmd = esptool_cmd() + ["--chip", "esp32", "--port", port, "--baud", "115200",
                           "--before", "default-reset", "--after", "hard-reset",
                           "write-flash", "-z", "--flash-mode", "dio", "--flash-freq", "80m", "--flash-size", "4MB",
                           offset, image]
    say(f"  flashing {os.path.basename(image)} -> {port} @ {offset}  (~80 s, do NOT unplug)")
    t0 = time.time()
    r = subprocess.run(cmd, capture_output=True, text=True)
    out = r.stdout + r.stderr
    ok = r.returncode == 0 and "Hash of data verified" in out
    say(f"  {'verified OK' if ok else 'FAILED'} in {time.time()-t0:.0f} s")
    if not ok:
        tail = "\n".join(out.strip().splitlines()[-6:])
        say("  esptool said:\n    " + tail.replace("\n", "\n    "))
    return ok

def read_banner(port, secs=9.0):
    """Reset via RTS and read the boot banner; return dict(role, mac, pos, lines)."""
    info = {"role": "?", "mac": "?", "pos": "?", "lines": []}
    try:
        s = serial.Serial(port, 115200, timeout=0.3)
        s.setDTR(False); s.setRTS(True); time.sleep(0.1); s.setRTS(False)
        t0 = time.time()
        while time.time() - t0 < secs:
            l = s.readline().decode(errors="replace").strip()
            if not l: continue
            info["lines"].append(l)
            if l.startswith("Role in NVS:"): info["role"] = l.split(":")[1].split(".")[0].strip()
            if "STA MAC" in l: info["mac"] = l.split("MAC")[-1].strip()
            if "SHOW_POSITION pos=" in l or "-> RUNNING pos=" in l: info["pos"] = l.split("pos=")[-1].strip()
            if "SoftAP" in l: info["pos"] = "1 (hub)"
        s.close()
    except Exception as e:
        info["lines"].append(f"(serial read failed: {e})")
    return info

def main():
    ap = argparse.ArgumentParser(description="Guided flasher for the LED sign boards")
    ap.add_argument("--images", help="release folder with the three .bin files (default: newest under release/)")
    ap.add_argument("--update", action="store_true", help=argparse.SUPPRESS)   # retired in Phase 4 (OTA)
    a = ap.parse_args()

    rel = os.path.abspath(a.images) if a.images else newest_release()
    for kind, (fn, _) in IMAGES.items():
        if not os.path.exists(os.path.join(rel, fn)): sys.exit(f"missing {fn} in {rel}")
    roster = os.path.join(rel, "boards_flashed.csv")
    say(f"LED Sign flasher — images: {rel}")
    if a.update:
        sys.exit("--update is retired: the boards have two app slots now, so wired app-only writes would hit the wrong one.\n"
                 + OTA_NOTE + f"\n  image: {os.path.join(rel, 'LED_Sign_ota.bin')}")
    say("Rule: keep the board plugged in until it says 'verified OK'. Interrupted flashes erase the app.")
    say(OTA_NOTE + "\n")

    hub_done = ask("Is the hub already assigned (a board already flashed as HUB)?", default="n") == "y"
    mode_first = "node" if hub_done else "hub"
    mode_rest = "node"
    if not hub_done:
        say("  -> the FIRST board you plug in now becomes the HUB. Label it 'HUB / 1'.")
    say("  -> every board after that is an add-on NODE; they number themselves by power-on order later.")

    mode = mode_first
    count = 0
    known = list_serial()
    while True:
        say("")
        try:
            input(f"Plug in the {'HUB' if mode == 'hub' else 'next'} board over USB, then press Enter to flash it as {mode.upper()} ... ")
        except EOFError:
            break
        port = None
        while port is None:
            time.sleep(1.0)
            port = pick_port(known)
            if port is None:
                try: input("  press Enter to rescan (Ctrl-C to quit) ... ")
                except EOFError: sys.exit("no board found")
        fn, off = IMAGES[mode]
        if not flash(port, os.path.join(rel, fn), off):
            if ask("  retry this board?", default="y") == "y": continue
            if ask("  skip it and continue?", default="n") == "n": break
            continue
        info = read_banner(port)
        say(f"  boot: role={info['role']}  MAC={info['mac']}  pos={info['pos']}")
        if mode == "hub" and info["role"] != "HUB": say("  !! expected HUB — check the banner lines below")
        if mode == "node" and info["role"] != "NODE": say("  !! expected NODE — check the banner lines below")
        if info["role"] == "?":
            say("  (banner not parsed; raw lines)"); [say("    " + l) for l in info["lines"][:12]]
        newfile = not os.path.exists(roster)
        with open(roster, "a", newline="") as fh:
            w = csv.writer(fh)
            if newfile: w.writerow(["time", "port", "image", "role", "mac", "pos"])
            w.writerow([datetime.now().isoformat(timespec="seconds"), port, fn, info["role"], info["mac"], info["pos"]])
        count += 1
        mode = mode_rest
        if ask(f"Board #{count} done. Add another board?", default="y") == "n": break
        say("  Unplug this board and plug in the next one.")
        known = list_serial()  # whatever is attached right now is 'old'; the next new port is the next board

    say(f"\nFlashed {count} board(s). Roster: {roster}")
    if mode_first == "hub" and count >= 1:
        say("Power-up order for the sign: HUB first, then nodes in the order you want them numbered (1 = hub).")
    say(OTA_NOTE)

if __name__ == "__main__":
    main()
