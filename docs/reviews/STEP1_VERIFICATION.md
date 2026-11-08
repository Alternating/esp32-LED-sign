# Verification — Phase 1 step 1 (`firmware/LED_Sign_Mesh/`) on hardware

Verification agent, 2026-11-08. Acceptance tests A1–A7 from `docs/build-briefs/PHASE1_STEP1_radio_and_numbering.md`, run on the Qwen build box against two LOLIN32 boards. Firmware source was not modified.

## Environment

| Item | Value |
|---|---|
| Repo commit | `d767d56` (review: STEP1_REVIEW.md refactor disposition) |
| Core / libs | esp32:esp32 3.3.12, FastLED 3.10.9, ArduinoJson 7.4.3 (unused) |
| FQBN | `esp32:esp32:lolin32:PartitionScheme=no_ota` |
| HUB_PORT | `/dev/ttyUSB0` = MAC **9C:9C:1F:E3:CE:54** (role HUB, saved via serial `H`) |
| NODE_PORT | `/dev/ttyUSB1` = MAC **78:E3:6D:19:18:38** (role NODE, saved via serial `N`) |
| Note | The Phase 0 hub board 9C:9C:1F:E3:C6:8C is **not** attached; USB0 carries a different board (…CE:54). Both boards booted with `Role in NVS: NODE` before roles were set. |

Tooling: `tools/serial_capture.py` for reset+keys; a throw-away `/tmp/cap.py` on the box (same reset pulse, adds relative timestamps, optional no-reset mode) for timing and runtime commands. Opening a port with `dsrdtr=False, rtscts=False` was verified **not** to reset the board (hub `s` after open showed `uptime=29s`, no boot banner). Timestamps below are seconds since the start of each capture; captures on the two ports were started from separate processes, so cross-port offsets of a few seconds are capture-start skew, not device latency.

## Results

| Test | Result | Evidence (key log lines) |
|---|---|---|
| A1 Build | **PASS** | `arduino-cli compile --warnings all` → `grep -ciE "error\|warning"` = 0. `Sketch uses 983199 bytes (46%) of program storage space. Maximum is 2097152 bytes.` / `Global variables use 51912 bytes (15%) of dynamic memory, leaving 275768 bytes`. |
| A2 Numbering from cold | **PASS** | Hub (`H` at boot): `Role set to HUB (saved)` → `[RADIO] SoftAP LEDSIGN-HUB at 192.168.4.1` → `[RADIO] Wi-Fi channel: 6 (want 6) OK` → `[HUB] table loaded: 0 boards, nextPos=2` → `[HUB] BOOT -> SHOW_POSITION` → `[HUB] showing position 1 for 3000 ms` → `[RADIO] addPeer 78:E3:6D:19:18:38 OK` → `[HUB] ASSIGN 78:E3:6D:19:18:38 -> pos 2 (new)` → `[HUB] SHOW_POSITION -> RUNNING`. Node, cold (NVS pos cleared with `f`, hub table cleared with `r`, then reset): `6.19 [NODE] NVS pos=0` → `6.19 [NODE] BOOT -> DISCOVER pos=0` → `6.23 [RADIO] addPeer 9C:9C:1F:E3:CE:54 OK` → `6.23 [NODE] hub learned 9C:9C:1F:E3:CE:54` → `6.23 [NODE] DISCOVER -> SHOW_POSITION pos=2` → `9.23 [NODE] SHOW_POSITION -> RUNNING pos=2`; hub concurrently: `[HUB] ASSIGN 78:E3:6D:19:18:38 -> pos 2 (new)`. ASSIGN arrived 40 ms after DISCOVER entry. |
| A3 Sticky position | **PASS** | Node reset (no keys): `6.19 [NODE] NVS pos=2` → `6.19 [NODE] BOOT -> DISCOVER pos=2` → `6.21 [NODE] DISCOVER -> SHOW_POSITION pos=2` → `9.22 [NODE] SHOW_POSITION -> RUNNING pos=2`. Hub concurrently: `[HUB] ASSIGN 78:E3:6D:19:18:38 -> pos 2 (known)` (no `addPeer` line: peer already present). Hub `s` afterwards: `role=HUB pos=1 state=RUNNING nextPos=3 numBoards=2 showSeq=1` / `78:E3:6D:19:18:38 -> pos 2  online  age=0s rssi=-32 vbat=0`. |
| A4 Hub loss / recovery | **PASS** | Hub reset (no keys): `3.10 Role in NVS: HUB` → `6.45 [RADIO] SoftAP LEDSIGN-HUB` → `6.46 [HUB] table loaded: 1 boards, nextPos=3` → `6.47 [HUB] BOOT -> SHOW_POSITION` → `9.47 [HUB] SHOW_POSITION -> RUNNING`. Node concurrently: `10.97 [NODE] RUNNING -> LOST_HUB pos=2` → `12.61 [NODE] LOST_HUB -> RUNNING pos=2`. LOST_HUB fired 5.0 s after the last heartbeat (by construction); RUNNING resumed 1.64 s later, i.e. on the hub's first heartbeat after its radio came up (hub radio up at +6.45 s after reset, LOST_HUB at +5.0 s → first Show reaches the node ≈ +6.5 s; 12.61 − 10.97 = 1.64 ✓). Node did **not** re-enter SHOW_POSITION, and the hub logged no re-ASSIGN. |
| A5 Reset order + claim | **PASS** | Hub `r`: `[HUB] RESET ORDER: table cleared, nextPos=2, broadcasting RESETORD x5`. Node: `[NODE] RESETORD received` → `[NODE] RUNNING -> UNASSIGNED pos=0`. Hub watched 15 s after `r`: no ASSIGN line (node stopped HELLO). Node `c`: `[NODE] CLAIM sent` → `[NODE] UNASSIGNED -> DISCOVER pos=0` → `[NODE] DISCOVER -> SHOW_POSITION pos=2` (20 ms later) → `[NODE] SHOW_POSITION -> RUNNING pos=2` (+3.0 s). Hub: `[RADIO] addPeer 78:E3:6D:19:18:38 OK` → `[HUB] ASSIGN 78:E3:6D:19:18:38 -> pos 2 (new) [claim]`. Hub `s`: `nextPos=3 numBoards=2` / `78:E3:6D:19:18:38 -> pos 2  online  age=2s rssi=-31 vbat=0`. |
| A6 Test string | **PASS** | Hub `tAB`: `[HUB] test string "AB" showSeq=2` / `[HUB] render 'A'`. Node: `[NODE] render 'B'`. Hub `tHI`: `[HUB] test string "HI" showSeq=3` / `[HUB] render 'H'`. Node: `[NODE] render 'I'`. Hub `s`: `test="HI"` `showSeq=3`; node `s`: `pos=2 state=RUNNING hub=9C:9C:1F:E3:CE:54 haveShow=1 showSeq=3 lastShow=499ms ago rssi=-31`. |
| A7 10-min soak | **PASS** | Continuous capture of both ports for 610 s produced **0 lines** on each (no boot banner, no state transition, no `[RADIO]` message). Hub `s` start → end: `heap=203840 minHeap=202408 … uptime=221s tx=441 txFail=0 rx=44 rxDrop=0 netStackFree=6320` → `heap=203840 minHeap=202408 … uptime=873s tx=1715 txFail=0 rx=225 rxDrop=0 netStackFree=6320` (Δheap = 0 B, Δuptime = 652 s, Δtx = 1274 ≈ 1.95 Show/s). Node `s` start → end: `heap=210772 minHeap=210512 … uptime=82s tx=22 rx=157 rxDrop=0 netStackFree=6380` → `heap=210772 minHeap=210460 … uptime=734s tx=203 txFail=0 rx=1430 rxDrop=0 netStackFree=6380` (Δheap = 0 B, minHeap −52 B transient, Δrx = 1273 = every heartbeat received, Δtx = 181 HBs = hub Δrx 181). Node stayed `state=RUNNING pos=2`, `lastShow` ≤ 500 ms at both samples. |

### Hub `s` output in full (during A6/A7, uptime 221 s)

```
---- status ----
role=HUB pos=1 state=RUNNING nextPos=3 numBoards=2 showSeq=3
test="HI" mirror=0
  78:E3:6D:19:18:38 -> pos 2  online  age=1s rssi=-31 vbat=0
heap=203840 minHeap=202408 channel=6(OK) uptime=221s tx=441 txFail=0 rx=44 rxDrop=0 netStackFree=6320
----------------
```

Node `s` at the same moment:

```
---- status ----
role=NODE pos=2 state=RUNNING hub=9C:9C:1F:E3:CE:54 haveShow=1 showSeq=3 lastShow=85ms ago rssi=-33 mirror=0
heap=210772 minHeap=210512 channel=6(OK) uptime=82s tx=22 txFail=0 rx=157 rxDrop=0 netStackFree=6380
----------------
```

## Observed timings

| Measurement | Value |
|---|---|
| Reset → `=== LED_Sign_Mesh fw 1 ===` banner | 1.15 s (ROM boot + 800 ms POWER_STABILIZE) |
| Banner → role window open (`Role in NVS:` line) | 1.95 s (boot animation) |
| Role window | 3.0 s (exits early when `H`/`N` arrives; keys sent at +1 s are buffered and consumed when the window opens) |
| Radio up → DISCOVER → ASSIGN received (hub already RUNNING) | 10–40 ms |
| SHOW_POSITION dwell (hub and node) | 3.00 s exactly (3.01 s observed) |
| Node reset → RUNNING, hub already up | 9.2 s with no key (1.15 + 1.95 + 3.0 window + 3.0 SHOW_POSITION); 6.2 s when `N` is sent during boot (the buffered key ends the window the instant it opens: `3.10 Role set to NODE (saved)` → `6.22 SHOW_POSITION -> RUNNING`) |
| Hub reset → first heartbeat on air | ≈ 6.5 s (window ran full 3 s with no key) |
| Node LOST_HUB after hub reset | 5.0 s after last Show |
| Node LOST_HUB → RUNNING after hub returns | ≤ 0.5 s after the hub's first Show (1.64 s after LOST_HUB entry) |
| `tXY` → hub render / node render | same 500 ms heartbeat tick; node render line appeared within the same capture second |
| Heartbeat | holds: node `lastShow` always ≤ 500 ms in every `s`; zero LOST_HUB flaps during the 10-min soak; node received 1273 of the hub's 1274 Shows (the one-frame difference is sampling skew between the two `s` commands), `rxDrop=0` on both. |

## Anomalies (tests still pass)

1. **Board swap.** USB0 is MAC 9C:9C:1F:E3:CE:54, not the Phase 0 hub 9C:9C:1F:E3:C6:8C. Recorded here so later runs don't assume C6:8C.
2. **Node `s` does not print the character it is rendering.** The brief's A6 fallback ("`s` on node should show the current char") is not available; the `[NODE] render 'X'` transition line is the only evidence, and it is emitted only when the character changes. `renderedChar` is already tracked in node.cpp (it is sent in HB as `shownChar`), so adding `shown='%c'` to `node::printStatus()` is a one-liner. Not a failure of any acceptance item.
3. **No log line when the hub ignores a repeated same-pos ASSIGN / when the node ACKs.** Fine for noise, but during A4 the only evidence that no re-ASSIGN happened is the absence of a hub ASSIGN line. Acceptable.
4. **Hub `r` does not report how many nodes were notified**, and the node logs `RESETORD received` only once (subsequent 4 broadcasts are suppressed because it is already UNASSIGNED). Expected per design; noted so nobody reads the single line as a lost broadcast.
5. **Role window always costs 3 s on the hub** even when the role is already stored (the hub reset in A4 spent 3 s in the window plus 3 s in SHOW_POSITION before RUNNING; heartbeat starts at radio-up, so recovery is bounded by the window, not SHOW_POSITION). This is the brief's design (item 9); recorded as a latency fact for A4 — "RUNNING within 1 s of the hub's first Show" is met, "first Show" itself is ≈ 6.5 s after hub reset.
6. **Hub heap dipped 152 B across the r/c cycle** (203992 → 203840) and `minHeap` moved 203000 → 202408 during `r`/`c` (NVS writes + peer re-add). Stable afterwards (see A7). Not a leak signature; worth re-checking after many RESETORD/CLAIM cycles in a later step.

## Defects for fix agent

None. All seven acceptance tests pass on hardware at commit `d767d56`. Optional, non-blocking follow-ups from the anomalies above:

- (nice-to-have) `node::printStatus()`: print `shown='%c'` from `renderedChar` so A6 can be confirmed from `s` alone, as the brief assumes.
- (nice-to-have) `hub::printStatus()`: print `renderedChar` too (currently inferable from `test="…"[0]`).

## Raw logs

Kept on the Qwen box under `/tmp/`: `a2_hub.log`, `a2cold_node.log`, `a2cold_hub.log`, `a3_node.log`, `a3_hub.log`, `a3_hub_s.log`, `a4_hub.log`, `a4_node.log`, `a5_hub_r.log`, `a5_node.log`, `a5_node_c.log`, `a5_hub_c.log`, `a5_hub_s.log`, `a6_hub1.log`, `a6_node.log`, `a6_node_s.log`, `a7_*_start.log`, `a7_*_soak.log`, `a7_*_end.log`, `v_build.log`. Both boards were left running the step-1 firmware (USB0 hub, USB1 node, test string "HI").
