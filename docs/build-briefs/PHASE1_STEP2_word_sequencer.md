# Build brief — Phase 1, step 2: word sequencer + synchronized word changes

Builds on verified steps 1 and 3 (`docs/reviews/STEP1_VERIFICATION.md`, `STEP3_VERIFICATION.md`). Behaviour source: `docs/PROJECT_PLAN.md` §2.5 (data flow 3–5), §4.1 (`Show.applyAtHubMs`, `wordIdx/wordCount/wordPeriodMs`), §4.2 (time sync). Algorithm source: `parseMessageIntoWords()` in `firmware/legacy/LED_Sign_WiFi_Symbol_Edition.ino` (lines ~112–192) — port it faithfully, then extend as below.

**Rendering rules that must be quoted, not paraphrased** (lesson from step 3): identify = position digit, purple at 50 %, 2 Hz (already implemented — do not touch). Position digit on join = 3.0 s. LOST_HUB = last char + red corner dot.

## Target
Same sketch, same FQBN/toolchain. Hub carries the sequencer; nodes only gain exact-time application of a pending Show. Web UI gains word-speed + live word-group preview.

## Scope
1. **`sign_state.h/.cpp`** — add `uint16_t wordPeriodMs` (500–10000, default 2000) and `uint8_t mode` (0 = `SEQ_WORDS` default, 1 = `SEQ_RAW` = today's one-char-per-position behaviour, for single-word/manual layouts). Bump `SIGN_BLOB_VER` to 2 **and migrate**: if the stored blob is version 1, load it and fill the new fields with defaults (do not drop the user's saved text/colour).
2. **`sequencer.h/.cpp`** (hub) — `sequencer::rebuild(const SignState&, uint8_t numBoards)`: ports `parseMessageIntoWords`: split on spaces; a `{token}` counts as ONE character (symbol code); a word longer than `numBoards` is split into `numBoards`-sized chunks (the legacy code truncated — **change**: chunk instead, e.g. 4 boards + "PARTY" → "PART","Y"); greedy-pack consecutive words with a single space between them while the group length ≤ `numBoards` (legacy behaviour: "WALK THE DOG" on 8 boards → "WALK THE","DOG"; on 4 → "WALK","THE","DOG"); max 20 groups (`SEQ_MAX_GROUPS`), each ≤ `MAX_BOARDS` chars. API: `groupCount()`, `group(i, char out[MAX_BOARDS]) → len`, `current()`, `advance()`. Empty text → one group of blanks. In `SEQ_RAW` mode: one group = first `numBoards` chars (current behaviour).
3. **Hub timing** (`hub.cpp`): on every change to text/boards/mode/numBoards (incl. a node joining or being forgotten) → `rebuild` + reset to group 0 + Show burst. If `groupCount() > 1`: every `wordPeriodMs` advance the group and send a Show with `applyAtHubMs = hubMillis + WORD_LEAD_MS` (config, 300 ms) in a 3× burst (0/60/250 ms, same `showSeq`); the periodic 500 ms heartbeat keeps carrying the *current* group with `applyAtHubMs = 0`. `Show.wordIdx/wordCount/wordPeriodMs` filled truthfully. Hub renders its own char through the same scheduled-apply path as nodes (so hub and nodes switch in the same frame). `groupCount() == 1` → no timer, behaviour identical to step 3.
4. **Node application** (`node.cpp`): a Show with `applyAtHubMs != 0` and a new `showSeq` becomes *pending*; `render()` swaps it in when `hubNow() >= applyAtHubMs` (if already past on arrival, apply immediately). The periodic heartbeat with the same `showSeq` must not re-trigger. Pending Show must survive a heartbeat carrying the previous group arriving in between (compare `showSeq`, not arrival order). Log `[NODE] apply seq=N word=i/n '<char>' (+lead ms)` once per applied Show, where `lead` = `applyAtHubMs - hubNow()` at arrival (negative = late).
5. **Commands** (`commands.cpp`): `wspeed {ms}` (500–10000 → `bad_arg` outside); `mode {"words"|"raw"}`; `msg` unchanged on input but reply adds `"groups":[...]` (the laid-out groups as display strings with `{tokens}` restored) and `"groupCount"`. `cfg` JSON adds `wspeed`, `mode`, `groups`, `groupCount`; `state` adds `wordIdx`. `boards` change triggers rebuild.
6. **Web UI** (`web/index.html`): Message tab gains a **Word speed** slider (0.5–10 s, 0.1 s steps, label in seconds, debounced `wspeed`) and a live **preview** row of chips showing the groups computed client-side with a JS port of the same algorithm (so it previews before Send) and highlighted current group from `state.wordIdx` after Send; a Words/Raw toggle (`mode`). Chips render `{heart}` etc. as the Unicode glyphs the legacy UI used (❤ ★ ✓ ✗ → ← ↑ ↓ ◆ ● ☺ ☹). Keep ≤ 40 kB. Rebuild `web_page.h` with `tools/build_web.py` and commit it.
7. **Serial**: `t<text>` unchanged (routes through `msg`); add `n` = advance to next group now (hub); `s` on hub prints `groups=N idx=i period=P next in X ms` and the group list.
8. **Drift fix**: `Show.wordPeriodMs` is informational for nodes (unused now) — keep. `docs/PROJECT_PLAN.md` §4.1 says `applyAtHubMs` "0 = apply now" — unchanged.

## Out of scope
Scrolling text (plan open question 8), per-board colour, battery, order/swap, BLE, OTA.

## Acceptance (verification agent, Qwen box; hub 9C:9C:1F:E3:CE:54 on the port that prints it, node 78:E3:6D:19:18:38; join the AP from `wlp3s0` with wpa_supplicant as documented in STEP3_VERIFICATION.md; flash with `UploadSpeed=115200`)
- C1 Build 0 errors/0 warnings; flash ≤ 1.4 MB.
- C2 Migration: boards currently hold a v1 sign blob (text/colour set in step 3). After flashing, hub log shows the migration line and `/state` cfg still has the previous text and colour, `wspeed:2000`, `mode:"words"`.
- C3 Grouping (2 boards): `msg "GO TEAM"` → reply `groups:["GO","TE","AM"]`, `groupCount:3`; `msg "HI"` → `["HI"]` (no timer: hub `s` shows `groups=1`); `msg "A {heart} B"` → `["A","{heart}","B"]`? No — with 2 boards "A {heart}" fits (2 chars incl. the token) → `["A {heart}","B"]`… **the verifier computes the expected output by hand from rule 2 and records both expectation and result**; include a case with a 5-char word (`"PARTY"` → `["PA","RT","Y"]`).
- C4 Sequencing: `msg "GO TEAM"`, `wspeed 1000`: node log shows `apply … word=0/3 'G'`, `1/3 'T'`, `2/3 'A'`, `0/3 'G'` … with ~1000 ms spacing (±50 ms); hub renders 'G','T','A' in step (hub `[SHOW]` lines). Run 30 s: 30 ± 1 applies on the node, no missed index.
- C5 Synchrony: lead value in node apply logs ≥ 150 ms for ≥ 95 % of applies (i.e. the burst arrives well before the switch instant); never negative at bench range. Capture 20 consecutive.
- C6 Heartbeat non-interference: during C4, periodic Shows (every 500 ms) must not cause extra applies or flapping — node apply count == group changes.
- C7 Speed change: `wspeed 500` live → spacing drops to 500 ms within one period; `wspeed 10000` → ~10 s.
- C8 Mode raw: `mode raw` with "GO TEAM" → single group "GO" shown, no timer; `mode words` → sequencing resumes from group 0.
- C9 Boards change: pin `boards 1` → regroups to 1-char groups ("G","O","T","E","A","M"); `boards 0` (auto, 2 online) → back to 3 groups.
- C10 Persistence: set `wspeed 1500`, `mode words`, text "GO TEAM"; wait 3 s; reset hub → after boot sequencing resumes with 1500 ms spacing (node logs).
- C11 Node join mid-sequence: power-cycle the node → it shows "2" for 3 s, then its first apply is the *current* group with correct `wordIdx` (not group 0 forced).
- C12 UI (Playwright in the cloud against the stub): slider posts `wspeed`, preview chips for "WALK THE DOG" with boards=4 show WALK/THE/DOG and with boards=8 show "WALK THE"/"DOG"; Words/Raw toggle posts `mode`. Screenshot `docs/reviews/step2_ui_message.png`.
- C13 Regression: A3 (sticky pos), A5 (reset order + claim), B6 (persistence), identify (digit, purple) still pass. 5-min soak with sequencing at 1 s: heaps flat ±4 kB, node never LOST_HUB, rxDrop 0.

## Output format for the drafting LLM
`=== FILE: <repo-relative path> === … === END FILE ===` blocks, no prose. Emit changed files in full (sign_state.h/.cpp, hub.cpp, node.cpp, commands.cpp, LED_Sign_Mesh.ino if touched, web/index.html) and new `firmware/LED_Sign_Mesh/sequencer.h/.cpp`. Do not emit `web_page.h`.
