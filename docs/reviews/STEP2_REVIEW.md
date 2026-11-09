# Review — Phase 1 step 2 draft (word sequencer + synchronized word changes, Qwen draft 1)

Reviewed against: `docs/build-briefs/PHASE1_STEP2_word_sequencer.md` (items 1–8, C1–C13), `docs/PROJECT_PLAN.md` §2.5 (data flow 3–5), §4.1 (`Show.applyAtHubMs`, `wordIdx/wordCount/wordPeriodMs`), §4.2 (time sync, "word changes take effect at `applyAtHubMs`"), the legacy `parseMessageIntoWords()` (`firmware/legacy/LED_Sign_WiFi_Symbol_Edition.ino` 112–192) and the verified step-1/step-3 baseline in `firmware/LED_Sign_Mesh/` + `web/index.html`. Draft read from the Qwen box at `~/esp32-led-sign/drafts/step2_qwen_v1/`; compile log `build1.log`.

What the draft changed vs the baseline (by `cmp`/`diff`): **new** `sequencer.h/.cpp`; **modified** `sign_state.h/.cpp` (+2 fields, +enum, +defaults), `hub.cpp` (+~90 lines), `node.cpp` (+~45 lines), `commands.cpp` (+~60 lines), `web/index.html` (+~85 lines), `web_page.h` (regenerated — the brief said not to emit it). **Byte-identical to the baseline**: `storage.h/.cpp` (so there is **no migration**), `LED_Sign_Mesh.ino` (so there is **no `n` serial key**), `hub.h`, `node.h`, `protocol.h`, `config.h`, `display.*`, `radio_espnow.*`, `transport*`.

## Verdict

**NOT MERGEABLE — 11 blocking, 12 major, 9 minor. Items 1 (migration), 3 (scheduled apply / hub rendering), 4 (node pending semantics) and 7 (serial) are not implemented or implemented so that the headline feature — synchronized word changes — cannot work; the C++ grouping algorithm hangs on the brief's own test case and never emits a symbol.**

Three defects decide the verdict on their own:

1. **The hub never sets `Show.applyAtHubMs`** (it stays `0` from the baseline `sendShow()`), so there is no scheduled apply anywhere in the system. Every word change is applied by each node on arrival of whichever burst frame it hears first (0/60/250 ms jitter), which is exactly the behaviour step 2 exists to replace. C5 (lead ≥ 150 ms) cannot be measured because `lead` is never non-zero; the node's `(+lead ms)` log line is unreachable.
2. **The hub's own display never changes word.** `hub::render()` is untouched and still draws `liveChars[0]` (the first char of the whole text). On 2 boards with "GO TEAM" the hub shows "G" forever while the node steps through T/E/A/M… C4 ("hub renders 'G','T','A' in step") fails.
3. **`Sequencer::rebuild()` loops forever (bounded only by `SEQ_MAX_GROUPS`) on any word longer than `numBoards`**: the inner packer `break`s without consuming the word, the outer loop emits an empty group and tries the same word again, 20 times. `msg "GO TEAM"` on 2 boards → `groups:["GO","","","",…]` (20 entries), `groupCount:20`, and the sign cycles GO / blank ×19. Chunking (brief item 2's explicit change from the legacy truncation) is not implemented at all. Independently, `{token}` handling is broken (`Word.len` is a logical char count but is used as a byte length for `memchr`, so `{heart}` always becomes `'?'`), and the `std::string` call does not compile.

Plus: no storage migration (a v1 blob is 4 bytes shorter (73 vs 77 B) and has version 1 → `loadSign()` falls to defaults → the user's text/colour are dropped, C2 fails); `groupStr` is `strlen`'d uninitialised in two places (UB, likely garbage/overflow in the `/state` and `msg` replies); the node's pending-Show logic applies the heartbeat copy of a scheduled Show immediately (bypassing the schedule the moment the hub starts setting `applyAtHubMs`), compares `showSeq` unsigned (wrap), writes `pendingShow` on core 0 without the mutex, and indexes `chars[myPos-1]` with `myPos == 0`; any colour/brightness/speed change resets the sequence to group 0; the UI's chips restore `{tokens}` **at random** (`Math.random()` over the symbol list) and never render Unicode glyphs; the preview does not update while typing.

The good parts, to keep: the `SignState` extension (`wordPeriodMs`, `mode`, `SIGN_BLOB_VER 2`, `SIGN_DEFAULT`), the `wspeed`/`mode` command handlers (ranges, `bad_arg`, reply shapes), the `cfg.wspeed/mode/groups/groupCount` and `state.wordIdx` fields, the `[HUB] advance group i/n` and `[HUB] sequencer rebuilt` log lines, the node `printStatus()` additions, the JS `groupsPreview()` **words-mode** packer (which is actually correct on all five trace cases — see the trace table — and should become the reference the C++ is written to), the slider (range/step/label/debounce are right), and the mode button plumbing.

## Findings

Severity: **blocking** = will not compile, regresses verified step-1/3 behaviour, or defeats C1–C6 outright; **major** = a brief item or C7–C13 fails; **minor** = quality/robustness.

### A. Compile blockers and narrowing (C1)

| ID | Sev | File:line | Problem | Required fix |
|---|---|---|---|---|
| S2-01 | blocking | sequencer.cpp:88 | `symbolCode(std::string(p + 1, e).c_str())` — no `<string>` include (`error: 'string' is not a member of 'std'`); `std::string` also heap-allocates per token on every rebuild and is not used anywhere else on this target. | Copy the token into a `char tok[16]` and null-terminate (exactly as `signLayout()` already does in sign_state.cpp:49-51). No `std::string` anywhere in the firmware. |
| S2-02 | blocking | node.cpp:169 | `s.b` on `const Show* s` (`error: request for member 'b' in 's', which is of pointer type`). | `s->b`. (The whole log line is replaced in R5 anyway.) |
| S2-03 | blocking | sequencer.h:11 | `constexpr uint8_t WORD_LEAD_MS = 300;` → stored as **44** (`-Woverflow` warning, 0-warnings rule fails). Had the hub used it, every scheduled apply would be 44 ms ahead instead of 300 — the burst's 60/250 ms repeats would land *after* the switch instant, so the first-frame-lost case would apply late by up to 200 ms. | `#define WORD_LEAD_MS 300` in `config.h` next to `SHOW_BURST_GAP*` (it is a timing constant like the others), or `constexpr uint16_t`. Never `uint8_t` for a millisecond value. |
| S2-04 | major | sequencer.cpp:70 (`uint8_t needed = w.len + …`), 103 (`uint8_t groupIdx`), hub.cpp:433/commands.cpp:68 (`for (int i …) sq.group(i, …)`) | `size_t → uint8_t` narrowing of `w.len` (a 60-char word is fine today but the pattern is fragile); `int → uint8_t` implicit on `group(i, …)`. No warning at `-Wall` but `--warnings all` is the C1 gate; keep the TU clean. | Use `uint8_t` consistently for lengths ≤ `SIGN_TEXT_MAX`, cast explicitly where `size_t` enters. |
| S2-05 | minor | sequencer.cpp:65, 96, 131 | `Word words[SEQ_MAX_GROUPS * 2]` (40 × 8 B on the stack per rebuild, also from the core-0 web task in `buildState`/`cmdMsg`); `uint8_t used` is computed by a nonsense expression (`used += groupPos - (groupPos ? groupPos - used : 0)`) and never read. | Remove `used`. Size the word table at `(SIGN_TEXT_MAX + 1) / 2 = 31` (max words in 60 chars) or, better, stream words straight into the packer (R3 design) so there is no word table at all. |

### B. Algorithm (item 2) vs the brief and the legacy code

| ID | Sev | File:line | Problem | Required fix |
|---|---|---|---|---|
| S2-06 | blocking | sequencer.cpp:98-141 | **Infinite/degenerate loop on a word longer than `numBoards`.** Inner `while`: `needed = w.len + (groupPos ? 1 : 0)`; if `groupPos + needed > numBoards` → `break` **without `wi++`**. Outer `while (wi < wordCount && g < SEQ_MAX_GROUPS)` then stores `groupLens_[g] = 0`, `g++`, and retries the same word. Result for "GO TEAM" @2: `GO` + 19 empty groups, `groupCount_ = 20`. For "PARTY" @2: 20 empty groups. **No chunking exists** — the brief's one explicit algorithm change vs legacy ("4 boards + PARTY → PART, Y") is missing, and the legacy truncation is missing too, so the draft is strictly worse than both. C3 fails on 2 of its 4 cases. | Implement chunking **before** packing: a word of logical length L > numBoards is cut into ⌈L/numBoards⌉ pseudo-words of ≤ numBoards units each; packing then treats pseudo-words like words (so the tail chunk may pack with the next word if it fits — see R3 for the exact rule and the trace). Also assert the invariant: every iteration of the outer loop must consume ≥ 1 word (`if (groupPos == 0) { /* cannot happen after chunking */ wi++; }` as a guard). |
| S2-07 | blocking | sequencer.cpp:64-93, 114-125 | **`{token}` handling is wrong twice.** (a) `Word.len` counts a token as **1** (logical) but is later used as the **byte** span for `memchr(p, '}', w.len - (p - w.start))` → for "{heart}" the search window is 1 byte, `}` is not found, `'?'` is emitted and `p++` leaves `remaining` miscounted; "A{heart}B" (one word) emits `A ? h`. Every symbol in every message is lost. (b) The tokenizer's first pass (`memchr(text + i, '}', len - i)`) is right, so the word *count* is right; only the copy pass is wrong. | Store **both** `byteLen` and `unitLen` per word (or, per R3, tokenise each word into a small `char units[SIGN_TEXT_MAX]` array of codes once and only ever work in units). Unknown `{xyz}` → the legacy and `signLayout()` pass the characters through literally (`'{','X','Y','Z','}'` = 5 units); the draft emits one `'?'`. Follow `signLayout()`: literal pass-through, so the preview, `chars` count and the groups agree. |
| S2-08 | major | sequencer.cpp:55-60 | Empty text → group filled with `' '` (0x20, len = numBoards). The protocol defines **0 = blank**; `' '` goes through `getCharacterPattern(' ')` (a real glyph lookup each frame) and the node logs `render ' '`. Also the hub's `liveChars` for empty text is all 0, so hub (`wordCount == 1` → `liveChars` path) and the sequencer disagree about what "blank" is. | Empty text → `groupCount_ = 1`, `len_[0] = 0`, chars all `0`. |
| S2-09 | major | sequencer.cpp:43-49 | `SEQ_RAW`: `signLayout(s, (char*)groups_[0])` writes **MAX_BOARDS** bytes, then `len` is clamped to `numBoards` but the bytes beyond `numBoards` stay in `groups_[0]`; `group()` copies only `len` so the result is right by accident. Also the whole raw branch is only reached when `s.mode == MODE_RAW` — fine — but `hub::sendShow()` ignores the sequencer when `wordCount == 1` (see S2-12) so raw mode's trimming never reaches the air. | Keep `signLayout` + `memset(groups_[0] + len, 0, …)` and make the hub **always** take `chars` from the sequencer (R4), so `SEQ_RAW` and `SEQ_WORDS` share one path. |
| S2-10 | major | hub.cpp:420-445, commands.cpp:62-92 (duplicated verbatim) | **`groupStr` is never initialised** (`char groupStr[MAX_BOARDS + 1];` then `strlen(groupStr)`/`strcat`) → UB: whatever is on the stack decides the output, and `strcat` can run off the end. Also the size is wrong by design: a group of 10 symbols restores to 10 × up to 9 chars (`{diamond}`) = 90 bytes; the `< MAX_BOARDS` checks silently drop tokens after the first one or two. Non-printable non-symbol bytes (`< 32`, e.g. a stray code 15) become `'?'`, inconsistent with `putChar()` in the same file which emits `""`. The 30-line block is copy-pasted into two files. | One function in `sequencer`: `size_t groupString(uint8_t i, char* out, size_t n)` — `out[0] = 0`, append `{name}` for codes 1–14 via `symbolName()`, the char for `>= 32`, skip anything else; `n = MAX_BOARDS * 9 + 1 = 91`. Call it from `buildState()` and `cmdMsg()`. |
| S2-11 | minor | sequencer.h:19 | `advance() { current_ = (current_ + 1) % groupCount_; }` divides by zero when `groupCount_ == 0` (true until the first `rebuild`; hub.cpp never calls `advance()` anyway and keeps its own `currentGroup`, so the class's `current()/advance()` are dead API). | Either the hub uses `seq.current()/advance()` (preferred, one source of truth — R4) or drop them. Guard `if (groupCount_) …`. |

### C. Hub timing (item 3)

| ID | Sev | File:line | Problem | Required fix |
|---|---|---|---|---|
| S2-12 | blocking | hub.cpp:172-199 (`sendShow`) | **`s.applyAtHubMs` is still always `0`** (the baseline line `s.applyAtHubMs = 0;` is unchanged). Group-advance bursts are therefore "apply now" frames with 0/60/250 ms arrival jitter — no synchrony at all. Brief item 3 / plan §2.5 step 3 / §4.2 all require `now + WORD_LEAD_MS` on a word change. Also `chars` are taken from the sequencer **only when `wordCount > 1`**, otherwise from `liveChars` (the full-text layout) — so in `SEQ_RAW`, or for a one-group message on fewer pinned boards than chars, the air frame carries characters the sequencer trimmed. | R4: `sendShow(periodic)` fills `chars`/`wordIdx` from the **displayed** group with `applyAtHubMs = 0` when `periodic`, and from the **scheduled** group with `applyAtHubMs = scheduledAt` when it is a word-advance burst. Always from the sequencer, never from `liveChars`. |
| S2-13 | blocking | hub.cpp (render, unchanged 490-501) | **Hub never renders the current group.** `render()` draws `liveChars[0]`. The brief: "Hub renders its own char through the same scheduled-apply path as nodes". C4's "hub renders 'G','T','A' in step" fails; a 2-board sign shows G/T, G/E, G/A, G/M… | R4: hub keeps `dispChars[]`/`dispIdx` (what it and the heartbeat show) and `nextChars[]`/`nextIdx`/`nextSeq`/`applyAt`; `tick()` swaps `next → disp` when `(int32_t)(millis() - applyAt) >= 0`; `render()` draws `dispChars[0]`. Log `[HUB] apply seq=N word=i/n '<c>'` at the swap (mirrors the node line so C4 can diff the two logs). |
| S2-14 | blocking | hub.cpp:545-555 | The advance bumps `showSeq` and schedules a burst **immediately**, and the 500 ms heartbeat (same `showSeq`, `applyAt 0`) may fire inside the lead window. Once S2-12 is fixed the heartbeat would carry the *new* group with `applyAt 0` → the node's immediate path (S2-19) applies it early → flapping between "scheduled" and "now". The brief is explicit: the heartbeat "keeps carrying the *current* group with `applyAtHubMs = 0`" — "current" = displayed, not scheduled. | Two-seq design (R4): `dispSeq` (heartbeat) and `nextSeq = dispSeq + 1` (burst). Heartbeats during the lead window carry `dispSeq`/`dispChars`; nodes ignore them (seq already applied). After the swap, heartbeats carry `nextSeq` — a node that missed all three burst frames applies it then (≤ 500 ms late, as plan §4.1 allows). |
| S2-15 | major | hub.cpp:520 (`tick`: `rebuildSequencer()` on every pending swap) | **Any** applied change — colour, `bri`, `fx`, `wspeed` — rebuilds and resets to group 0. C7 says "`wspeed 500` live → spacing drops within one period", not "restarts". Tapping a swatch while a message cycles jumps it back to the first word every time; brightness slider (debounced 300 ms) resets per step. The brief lists the rebuild triggers exhaustively: text / boards / mode / numBoards. | In `tick()`, before `live = pending`, compute `bool relayout = strcmp(live.text, pending.text) || live.boards != pending.boards || live.mode != pending.mode;` and `rebuild` + reset only then. For other changes: keep `currentGroup`, bump `nextSeq`, burst the **current** group with the new style and `applyAt 0` (step-3 behaviour). `wordPeriodMs` change: re-arm `nextAdvanceAt = lastApplyAt + newPeriod` (so the next switch moves, not the index). |
| S2-16 | major | hub.cpp:160-168, 214-222 (`onHello`), 327-347 (`forgetPos`) | **No rebuild on node join or forget.** Brief item 3: "every change to … numBoards (incl. a node joining or being forgotten)". `onHello()` → `scheduleBurst()` only; `forgetPos()` → `scheduleBurst()` only. With auto boards and "GO TEAM": a 2nd node joining mid-run keeps the 1-board grouping (G/O/T/E/A/M) until the next text change (C9's `boards 0` case and C11 both depend on this). Note these run on core 0 (`onHello` from `handlePacket`) and any core (`forgetPos` from web/serial) — the sequencer **must not** be rebuilt there (S2-18). | `tick()` (core 1) caches `uint8_t lastNumBoards`; each tick `uint8_t nb = live.boards ? live.boards : highestOwned();` (reads `table[]` under `tMux`, as `highestOwned()` already does without the lock — take the lock); if `nb != lastNumBoards` → rebuild + reset + burst. That single check covers join, forget, reset-order and `boards` pinning, all from core 1. Drop the `scheduleBurst()` calls in `onHello()`/`forgetPos()`? **No** — keep them: a late joiner needs a frame immediately (step-1 A5 behaviour); it will just carry the group of the moment. |
| S2-17 | major | hub.cpp:545-555 | Timer semantics: `groupSwitchAt` is armed from `now` **inside tick after the first RUNNING tick** (`groupSwitchAt == 0 → now + period`) and re-armed from `now` at each advance. Combined with the (missing) lead this means period = spacing between *sends*, and the burst start can drift by the tick latency (≤ 1 ms, fine) — acceptable, but after `rebuildSequencer()` sets `groupSwitchAt = 0` the **first** word is shown for `period` + up to one tick, while every later word is shown for `period`; after C8's `mode words` the index restarts at 0 but the display shows group 0 immediately (applyAt 0 burst) — correct. Also `live.wordPeriodMs > 0` is dead (range 500–10000 enforced). | R4: `nextAdvanceAt = applyAt + wordPeriodMs` where `applyAt` is the instant the *displayed* group changed (for a content change: the burst time). Spacing between apply instants is then exactly `wordPeriodMs` (C4's ±50 ms). Remove the `> 0` test. |
| S2-18 | major | hub.cpp:160-168 (`rebuildSequencer` uses `sign()`), 420-445 (`buildState` builds a `Sequencer` on core 0), commands.cpp:62 (same on core 0) | Concurrency: `rebuildSequencer()` reads `sign()` (pending-or-live) instead of `live` — called from `tick()` right after the swap it is equivalent, but called from `begin()` it reads `pending = live` (fine) — it should simply take `live` to make the core-1 ownership obvious. `buildState()`/`cmdMsg()` each run a **second** `Sequencer` (220 B + 91 B string buffer) on the 12 kB net-task stack per request — acceptable, but `cmdMsg()` computes the groups from the *candidate* state `s` (good: the reply predicts) while `buildState()` recomputes from `sign()` instead of reporting the hub's actual groups; `state.wordIdx = currentGroup` is read **unlocked** from core 0 (a `uint8_t`, atomic — fine, but document it `volatile`). | `rebuildSequencer()` takes `const SignState&` (= `live`) and `numBoards`; `buildState()` reports the hub's real groups by snapshotting `dispIdx`/`groupCount` (volatile uint8_t) and the group strings via a small `portMUX`-guarded copy of the sequencer's `groups_/len_` (220 B) — or keep the recompute from `sign()` but say so in a comment; both are acceptable, the recompute is simpler. `cmdMsg()` recomputing from the candidate is correct — keep. |
| S2-19 | major | hub.cpp (`s` output, 592-593), `LED_Sign_Mesh.ino` (unchanged) | Item 7: `s` must print `groups=N idx=i period=P next in X ms` **and the group list**; the draft prints `wspeed= mode= groups= current=` and no list. `n` (advance now) is absent — the .ino is byte-identical. | `hub::nextGroup()` (core 1, called from `runCommand('n')`): `nextAdvanceAt = millis()` so the normal tick path schedules it with the lead. `printStatus()`: `groups=%u idx=%u period=%u next in %ld ms` then one line per group `  [i] "<groupString>"` (via `sequencer::groupString`). Add `'n'` to the help string. |

### D. Node application (item 4)

| ID | Sev | File:line | Problem | Required fix |
|---|---|---|---|---|
| S2-20 | blocking | node.cpp:147-165 | **Immediate path bypasses the schedule.** `seqChanged` compares against `latestShow.showSeq` (the *applied* seq). Sequence: burst frame (seq 7, applyAt T+300) → pending; heartbeat (seq 7, applyAt 0) arrives at T+200 (500 ms cadence, so in 60 % of periods) → `seqChanged` is still true (7 ≠ applied 6) and `applyAtHubMs == 0` → **`applyNow`** at T+200 while other nodes wait for T+300. Brief: "the periodic heartbeat with the same `showSeq` must not re-trigger". With the hub fix S2-14 the heartbeat carries seq 6 inside the window, which masks this — but the node must be correct on its own (a node that missed the burst and receives the post-swap heartbeat seq 7/applyAt 0 must apply immediately, which this path does right; the bug is only the ordering test). | Track `int16_t` signed differences against **the newest seq the node knows** (`newestSeq = havePending ? pending.showSeq : applied.showSeq`): `int16_t d = (int16_t)(s->showSeq - newestSeq); if (d <= 0) { feed clock, return; }` (duplicate or older — includes the heartbeat with the pending seq, and an old-group heartbeat arriving after the pending one). Only `d > 0` proceeds: `applyAt == 0 || (int32_t)(hubNow() - applyAt) >= 0` → apply now (log with lead = 0 or negative), else → pending (replacing any older pending). |
| S2-21 | blocking | node.cpp:151, 323-335 | `s->showSeq > pendingShow.showSeq` — **unsigned** compare; `showSeq` is `uint16_t` and wraps after ~18 h at 1 s/word (3600 advances/h). After the wrap every new Show is "older" than the pending one and is dropped until the pending one is applied… which never happens if the pending was superseded (S2-22). Same in `render()`: `now >= ps.applyAtHubMs` is an unsigned `uint32_t` compare (49-day wrap; cheap to do right). | `(int16_t)(a - b) > 0` for seqs; `(int32_t)(hubNow() - applyAt) >= 0` for times, as every other timer in the codebase already does. |
| S2-22 | blocking | node.cpp:147-165, 320-335 | **Stale pending clobbers a newer applied Show.** Pending seq 7 (applyAt T+300); a content change burst seq 8 with `applyAt 0` arrives at T+100 → `applyNow` (latest = 8) but `havePending` stays true; at T+300 `render()` sees `ps.showSeq (7) != lastAppliedSeq (8)` → applies **7 over 8**: the sign reverts to the previous word/colour until the next heartbeat (≤ 500 ms) re-applies 8 — and that heartbeat is then "seqChanged" again → `apply` log spam, HB `lastShowSeq` flaps. | When applying any Show, `havePending = false` if `(int16_t)(pending.showSeq - applied.showSeq) <= 0`. With S2-20's "newest known seq" rule this falls out naturally: applying 8 makes 7 stale. |
| S2-23 | blocking | node.cpp:147-152 vs 320-324 | `pendingShow` is **written on core 0 without `showMux`** (`memcpy(&pendingShow, s, 40)` outside the critical section) while `render()` (core 1) copies it **under** the mutex — the mutex protects nothing on the writer side; a torn 40-byte read can apply a mixed frame (old `chars`, new `applyAtHubMs`). `havePending` is set after the copy, but there is no barrier that orders the two on the other core. The baseline's `latestShow` discipline (every access under `showMux`) must be kept. | Write `pendingShow`/`havePending` inside `portENTER_CRITICAL(&showMux)`. Better: keep a single `struct { Show show; bool valid; } slot[2]` (applied, pending) under one mutex and swap in `render()`. |
| S2-24 | major | node.cpp:333 | `ps.chars[myPos - 1]` with `myPos == 0` (UNASSIGNED/DISCOVER with a Show in flight, or after RESETORD/FORGET) → index −1 (reads `wordCount`). Symbol codes print as `'?'`; the brief's format `'<char>'` is for printable chars — codes need `code N` like the baseline `render` lines. | `uint8_t c = (myPos >= 1 && myPos <= MAX_BOARDS) ? ps.chars[myPos-1] : 0;` then `c >= 32 ? "'%c'" : "code %d"`. |
| S2-25 | major | node.cpp:147-165 | The baseline's `apply = seqChanged || st != RUNNING` (refresh `latestShow` in any non-RUNNING state so SHOW_POSITION → RUNNING starts from a fresh frame) was dropped; `lastShowMs` still feeds LOST_HUB from every Show (good). A node in LOST_HUB that hears a scheduled burst (seq new, applyAt future) goes to RUNNING (line 154 `setState(RUNNING)`) **before** anything is applied: `render()` RUNNING with the old `latestShow` for ≤ 300 ms, then the apply — correct, and C11 holds because the post-ASSIGN burst and the heartbeats are `applyAt 0` frames of the *current* group (hub side, S2-12/14), so the first apply is the current group, not group 0. Just keep that property explicit in the hub design (a join burst is **not** an advance). | Keep: any Show → `lastShowMs`, LOST_HUB → RUNNING. Add: on `SETPOS`/`RESETORD`/`FORGET`(to us) clear `havePending` (a pending frame for the old position must not apply after the position changes). |
| S2-26 | minor | node.cpp:153-154, 331-333 | Two different apply log formats: the immediate path prints the step-3 line (`color=… bri=… fx=… period=… numBoards=… word=i/n`), the scheduled path prints the brief's line. C4/C5/C6 grep for `apply … word=i/n '<c>' (+lead ms)` and count them — immediate applies (content changes, late heartbeat catch-ups) would be invisible to the lead statistic and the count. | One `logApply(const Show&, int32_t lead)` used by both paths: `[NODE] apply seq=%u word=%u/%u '%c' (%+ld ms) color=%02X%02X%02X bri=%u fx=%u` — the brief's prefix, with the style tail appended so step-3 verification greps still match. Immediate applies print `(+0 ms)`; late ones negative. |

### E. Commands / JSON (item 5)

| ID | Sev | File:line | Problem | Required fix |
|---|---|---|---|---|
| S2-27 | major | commands.cpp:58-93 | `cmdMsg()` groups: uninitialised `groupStr` (S2-10) → garbage in the `groups` array of **the** reply C3 checks; `boards` fallback `s.boards ? s.boards : hub::numBoardsEffective()` is fine. `reply["chars"] = used` still reports `signLayout()`'s one-char-per-position count, now meaningless in words mode (the UI's char counter uses it). | Use `sequencer::groupString()`. Keep `chars` (UI compatibility) but add nothing else; `groups`/`groupCount` are the new truth. |
| S2-28 | minor | commands.cpp:159-182 | `wspeed` and `mode` are correct (range 500–10000 → `bad_arg`; `"words"|"raw"`; echo). `mode` string compare is case-sensitive while every other name lookup (`fxFromName`, `symbolCode`) is `strcasecmp`. Mode-name mapping (`== MODE_WORDS ? "words" : "raw"`) is written out five times across hub.cpp/commands.cpp. | `modeName(uint8_t)` / `modeFromName(const char*)` in sign_state (as `fxName/fxFromName`), `strcasecmp`. |

### F. Storage migration (item 1)

| ID | Sev | File:line | Problem | Required fix |
|---|---|---|---|---|
| S2-29 | blocking | storage.cpp (unchanged), sign_state.h:11 | `SIGN_BLOB_VER` is 2 and `sizeof(SignState)` grew by 4 (two new fields + alignment: v1 = 61+3+1+1+1+pad1+2+1 = 71 → 72 B with trailing pad; v2 = 76 B), so a v1 blob fails **both** the size and the version test in `loadSign()` and falls back to `SIGN_DEFAULT` — "HI", green, boards auto. The user's text/colour from step 3 are silently dropped; the only log is the baseline's "size %u / version mismatch, using defaults". C2 fails outright. No `[STORAGE]` migration line exists. | R2: `struct SignStateV1` (verbatim copy of the step-3 struct), `loadSign()`: `got == 1 + sizeof(SignStateV1) && buf[0] == 1` → copy field-by-field, `wordPeriodMs = 2000`, `mode = MODE_WORDS`, run the same clamps, log `[STORAGE] sign blob v1 -> v2 migrated (text="…")`, and `hub::begin()` arms `dirtyAt = 1` so the v2 blob is written on the first tick (the next boot is then a clean v2 load). Add `static_assert(sizeof(SignStateV1) == 72)` and `static_assert(sizeof(SignState) == 76)` so a future field addition cannot silently change the on-flash layout again. Clamp the new fields on load (`wordPeriodMs` 500–10000 else 2000; `mode > 1` → 0). |

### G. Web UI (item 6)

| ID | Sev | File:line | Problem | Required fix |
|---|---|---|---|---|
| S2-30 | blocking | index.html:313-316 | **Tokens are restored at random.** Every `{token}` is replaced by `'#'` for packing and then `g.replace(/#/g, …syms[Math.floor(Math.random()*12)])` — a `{heart}` chip shows `{sad}` or `{up}` on each poll, changing every 2 s. Also a literal `#` typed by the user becomes a random token. Nothing renders Unicode glyphs (brief: ❤ ★ ✓ ✗ → ← ↑ ↓ ◆ ● ☺ ☹). | R8: tokenise into an array of *units* (`{ t: 'heart' }` or `'A'`) exactly like the C++, pack units, and render units: token → glyph from a fixed map `{heart:'❤', star:'★', check:'✓', cross:'✗', right:'→', left:'←', up:'↑', down:'↓', diamond:'◆', circle:'●', happy:'☺', sad:'☹'}` (use `\uXXXX` escapes so the file stays ASCII for `build_web.py`). Unknown `{xyz}` → literal characters, as the C++. |
| S2-31 | major | index.html:247, 303-320 | **Preview does not update while typing.** `renderGroupsPreview()` runs only from `applyState()` (every 2 s poll, which also overwrites it from `cfg.text` unless the textarea is focused — and polling is *paused* while it is focused), from the Send callback and from the mode button. Brief: "live preview … so it previews before Send". C12 (type "WALK THE DOG", read the chips before Send) fails. The chips also ignore the boards select on the Boards tab (uses `numBoards()` = `state.numBoards`, which is right for the live value — but a pinned `boards` change is only reflected after the next poll). | `$('msgText').addEventListener('input', renderGroupsPreview)` next to `updateCharCount` (or call it from `updateCharCount`). Highlight from `S.state.wordIdx` only when the textarea text equals `lastCfgText` (otherwise the index refers to the old message — show no highlight). |
| S2-32 | major | index.html:270-300 (`groupsPreview`) | Raw mode preview chunks the **whole** text into `boards`-sized pieces (several chips) — the firmware's `SEQ_RAW` is **one** group of the first `numBoards` chars (brief item 2). Words-mode packer is otherwise correct (see trace table), but: it works on `'#'`-substituted strings (S2-30), `split(/\s+/)` also splits on tabs/newlines (the C++ splits on `' '` only — align the C++ to treat any `isspace` as a separator, or the JS to `' '`; pick one — recommend **space only**, like the legacy); `text.replace(/\s+$/,'')` trims only trailing whitespace while the C++ skips leading spaces too (harmless, both ignore empties). The chunk tail does not pack with the following word; the C++ (R3) must match whichever rule is chosen — see trace note. | Raw: one chip of the first `boards` units. Share one unit-tokeniser between words/raw. Apply the chunk rule from R3 (chunks become pseudo-words). |
| S2-33 | minor | index.html:101-104, 134-136, 310-312 | Chips use inline `style.cssText` with hard-coded `#444`/`#007bff` (the page uses CSS variables and the dark palette; `#007bff` is Bootstrap blue, not the page's accent); the **Mode: Words/Raw** toggle sits on the **Look** tab while the brief puts it on the Message tab next to the preview it affects; the hint text "Groups will appear here" is static until the first poll; slider `step="100"` is right (0.1 s) but `min=500` shows "0.5 s" and the label is "Word speed" with the value *inside* the label (matches the other sliders — fine). Size 26 477 B ≤ 40 kB. No modern-syntax regressions found (ES5 only: `forEach`, `filter`, `function` closures; no `=>`, `?.`, template literals). Step-3 handlers untouched (diff confirms). | Move the toggle under the preview as a 2-segment control (`.segment` class already exists for fx) that POSTs `{"cmd":"mode","mode":…}`; style chips with a `.chip`/`.chip.on` class using the page's variables; hint = `"N groups · boards B"`. |
| S2-34 | minor | web_page.h (regenerated), brief "Output format" | The draft emitted `web_page.h` (brief: "Do not emit `web_page.h`"); it will be regenerated by `tools/build_web.py` from the final page anyway. | Regenerate after R8; commit the regenerated file. |

### H. Regressions vs steps 1/3

| ID | Sev | File:line | Problem | Required fix |
|---|---|---|---|---|
| S2-35 | major | hub.cpp:520 (see S2-15) | Colour/brightness/fx change now resets the word index (behavioural regression of the step-3 Look tab feel; B5-style "swatch → immediate colour change" still works but the text jumps). | S2-15. |
| S2-36 | minor | node.cpp:147-165 | In SHOW_POSITION/LOST_HUB the node no longer refreshes `latestShow` on a non-seq-changing Show (baseline `|| st != RUNNING`). Harmless today (style is carried by every frame and seqs change on every style edit), noted so the drop is deliberate. Identify (purple 50 %, 2 Hz) untouched — `display.*` byte-identical. ACK paths, HB telemetry (`info[]`), auto-save (`dirtyAt`), serial letters: untouched by diff. `FastLED.show()` still only in `display::show()` from `loop()`. | None beyond R5. |

## Refactor plan (ordered)

Refactor against the verified baseline (do not patch the draft files); carry over the pieces listed under "Keep as-is".

### R1. `sign_state.h/.cpp`
Keep the draft's `SignMode` enum, `wordPeriodMs`/`mode` fields (appended **after** `boards`, as drafted — the v1 prefix stays byte-identical, which is what makes R2 a plain prefix copy), `SIGN_BLOB_VER 2`, `SIGN_DEFAULT = {…, 0, 2000, MODE_WORDS}`. Add `const char* modeName(uint8_t)` ("words"/"raw") and `int modeFromName(const char*)` (strcasecmp, −1 unknown). Add `static_assert(sizeof(SignState) == 76, "SignState layout is the NVS blob layout; add a SignStateVn migration if this changes")`.

### R2. `storage.cpp` — migration (S2-29)
```cpp
struct SignStateV1 {                         // step-3 layout, frozen
  char text[SIGN_TEXT_MAX + 1]; uint8_t r, g, b; bool rainbow; uint8_t bri; uint8_t effect;
  uint16_t effectPeriodMs; uint8_t boards;
};
static_assert(sizeof(SignStateV1) == 72, "frozen v1 layout");
void loadSign(SignState& s) {
  uint8_t buf[1 + sizeof(SignState)];
  Preferences p; p.begin(NS_SIGN, false);
  size_t got = p.getBytesLength("sign");
  if (got && got <= sizeof(buf)) p.getBytes("sign", buf, got);
  p.end();
  bool ok = false, migrated = false;
  if (got == 1 + sizeof(SignState) && buf[0] == SIGN_BLOB_VER) { memcpy(&s, buf + 1, sizeof(SignState)); ok = true; }
  else if (got == 1 + sizeof(SignStateV1) && buf[0] == 1) {
    SignStateV1 v1; memcpy(&v1, buf + 1, sizeof(v1));
    s = SIGN_DEFAULT;                                   // new fields = defaults
    memcpy(s.text, v1.text, sizeof(s.text)); s.r = v1.r; s.g = v1.g; s.b = v1.b; s.rainbow = v1.rainbow;
    s.bri = v1.bri; s.effect = v1.effect; s.effectPeriodMs = v1.effectPeriodMs; s.boards = v1.boards;
    ok = migrated = true;
  }
  if (!ok) { if (got) Serial.printf("[STORAGE] sign blob size %u / version mismatch, using defaults\n", (unsigned)got); s = SIGN_DEFAULT; return; }
  /* existing clamps */ ; if (s.wordPeriodMs < 500 || s.wordPeriodMs > 10000) s.wordPeriodMs = 2000; if (s.mode > MODE_RAW) s.mode = MODE_WORDS;
  if (migrated) Serial.printf("[STORAGE] sign blob v1 -> v2 migrated (text=\"%s\" color=%02X%02X%02X), rewriting\n", s.text, s.r, s.g, s.b);
}
```
`storage::loadSign` returns `bool migrated` (or an out-param) so `hub::begin()` can set `dirtyAt = 1` → `[HUB] sign saved` on the first tick writes the v2 blob (C2: the log shows both lines, `/state` shows the old text/colour, `wspeed:2000`, `mode:"words"`).

### R3. `sequencer.h/.cpp` (hub, core 1 only; also instantiated as a local in `cmdMsg`/`buildState`)
```cpp
namespace sequencer {
constexpr uint8_t SEQ_MAX_GROUPS = 20;
constexpr size_t  GROUP_STR_MAX  = MAX_BOARDS * 9 + 1;        // "{diamond}" x10 + NUL
class Sequencer {
public:
  void     rebuild(const SignState& s, uint8_t numBoards);    // numBoards clamped 1..MAX_BOARDS
  uint8_t  groupCount() const { return count_; }
  uint8_t  group(uint8_t i, char out[MAX_BOARDS]) const;      // zero-filled, returns len (0 = blank group)
  size_t   groupString(uint8_t i, char* out, size_t n) const; // "{heart}" restored, codes < 32 skipped
  uint8_t  current() const { return cur_; }
  uint8_t  advance() { if (count_) cur_ = (uint8_t)((cur_ + 1) % count_); return cur_; }
  void     reset() { cur_ = 0; }
private:
  void     push(const char* units, uint8_t n);               // appends a group (no-op when full)
  char     groups_[SEQ_MAX_GROUPS][MAX_BOARDS] = {};
  uint8_t  len_[SEQ_MAX_GROUPS] = {};
  uint8_t  count_ = 0, cur_ = 0;
};
}
```
`rebuild()` algorithm (**units** = one byte per display position: symbol code 1–14 or uppercased char; unknown `{xyz}` passes through as literal units like `signLayout()`):
1. `count_ = cur_ = 0`. `SEQ_RAW`: `len_[0] = min(signLayout(s, groups_[0]), numBoards)`, zero the rest, `count_ = 1`, return.
2. Tokenise `s.text` into units with a cursor (`{name}` → `symbolCode(name)` if known, else the literal bytes), splitting words on `' '` (runs of spaces = one separator, leading/trailing ignored). Each word is a `char w[SIGN_TEXT_MAX]` of units + `uint8_t wl`. Words are produced one at a time and fed to step 3 — no word table.
3. **Chunk**: if `wl > numBoards`, emit pseudo-words of `numBoards` units each, the last one `wl % numBoards` (if non-zero). Each pseudo-word then goes through step 4 like a normal word. Consequence: the final short chunk **may pack with the next word** if it fits ("TEAM GO" @3 → `TEA` / `M GO`). This is the simplest rule that both C++ and JS can implement identically; record it in the review's trace table and the brief's C3 note. (The alternative — tail chunk always alone — is also acceptable, but then the JS and C++ must both do it; the draft's JS does the alternative, the draft's C++ does neither.)
4. **Pack** (legacy rule): `cur[]`/`cl`. `need = wl + (cl ? 1 : 0)`; if `cl + need <= numBoards` → append `' '` (if `cl`) and the word; else `push(cur, cl)`, `cur = word`. At end `push(cur, cl)` if `cl`. `push()` ignores groups beyond `SEQ_MAX_GROUPS` (text is ≤ 60 chars, so ≥ 30 groups is impossible with chunking into ≥ 1-unit groups — but keep the guard).
5. If `count_ == 0` (empty/space-only text) → `count_ = 1`, `len_[0] = 0` (blank).
Loop invariant to assert in a comment: every word/pseudo-word is consumed exactly once; after chunking `wl <= numBoards` so a word always fits an empty group.

`WORD_LEAD_MS` → `config.h`: `#define WORD_LEAD_MS 300`.

### R4. `hub.cpp` — scheduled apply, two-seq model, rebuild triggers
State (all core 1 unless noted):
```cpp
static sequencer::Sequencer seq;
static uint8_t  lastNumBoards = 0;
static char     dispChars[MAX_BOARDS]; static volatile uint8_t dispIdx = 0; static volatile uint16_t dispSeq = 1;   // what the hub shows and the heartbeat carries
static char     nextChars[MAX_BOARDS]; static uint8_t nextIdx = 0; static uint16_t nextSeq = 1; static uint32_t applyAt = 0; static bool nextArmed = false;
static uint32_t nextAdvanceAt = 0;
```
- `relayout(numBoards)`: `seq.rebuild(live, nb)`, `seq.reset()`, `loadGroup(0, dispChars)`, `dispIdx = 0`, `dispSeq++`, `nextArmed = false`, `nextAdvanceAt = millis() + live.wordPeriodMs`, `scheduleBurst()` (burst = `applyAt 0`, immediate — same as step 3), log `[HUB] sequencer rebuilt: groups=N mode=… boards=B period=P`.
- `tick()`: (a) pending swap: compute `relayoutNeeded` (text/boards/mode differ) **before** `live = pending`; if yes → `relayout`; else (style/wspeed only) → `dispSeq++`, `scheduleBurst()` (current group, new style, applyAt 0); if `wordPeriodMs` changed → `nextAdvanceAt = lastApplyAt + newPeriod`. (b) `nb = live.boards ? live.boards : highestOwned()` (under `tMux`); if `nb != lastNumBoards` → `lastNumBoards = nb; relayout(nb)` — covers join/forget/reset-order. (c) Advance: `if (st == RUNNING && seq.groupCount() > 1 && !nextArmed && (int32_t)(now - nextAdvanceAt) >= 0)` → `nextIdx = seq.advance()`, `loadGroup(nextIdx, nextChars)`, `nextSeq = dispSeq + 1`, `applyAt = now + WORD_LEAD_MS`, `nextArmed = true`, `scheduleBurst()`, log `[HUB] advance group i/n seq=S applyAt=+300`. (d) Swap: `if (nextArmed && (int32_t)(now - applyAt) >= 0)` → `memcpy(dispChars, nextChars)`, `dispIdx = nextIdx`, `dispSeq = nextSeq`, `nextArmed = false`, `lastApplyAt = applyAt`, `nextAdvanceAt = applyAt + live.wordPeriodMs`, log `[HUB] apply seq=S word=i/n '<c>'`. (e) Burst/heartbeat as today.
- `sendShow(periodic)`: `applyAtHubMs = (!periodic && nextArmed) ? applyAt : 0`; `showSeq = (!periodic && nextArmed) ? nextSeq : dispSeq`; `chars` likewise; `wordIdx`/`wordCount` truthful (`nextIdx` or `dispIdx`, `seq.groupCount()`); `wordPeriodMs = live.wordPeriodMs`; `numBoards = lastNumBoards`. A burst scheduled for a *content* change (`!nextArmed`) is an immediate frame of `dispSeq` — so a late joiner's post-ASSIGN burst is always "current group, apply now" (C11).
- Edge: a content change while `nextArmed` → `relayout` clears `nextArmed`; the already-sent burst frames with `nextSeq` are superseded because `dispSeq` is bumped past them (`dispSeq = max(dispSeq, nextSeq) + 1`).
- `render()`: draw `dispChars[0]` (replaces `liveChars[0]`); keep identify/SHOW_POSITION precedence.
- `nextGroup()` (for `'n'`): `nextAdvanceAt = millis()`. `printStatus()`: `groups=%u idx=%u period=%u next in %ld ms` (negative/`-` when `groupCount() <= 1`), then `  [i] "…"` per group via `groupString`. `buildState()`: `cfg.wspeed/mode/groups/groupCount` (recompute from `sign()` with a local Sequencer — acceptable on core 0, document it) and `state.wordIdx = dispIdx`.
- Delete `liveChars`/`signLayout` from hub.cpp except inside the sequencer's raw branch.

### R5. `node.cpp` — pending Show
```cpp
static Show applied, pending;                 // both under showMux
static volatile bool haveApplied = false, havePending = false;
static void logApply(const Show& s, int32_t lead);   // one format: [NODE] apply seq=%u word=%u/%u '%c' (%+ld ms) color=… bri=… fx=…
onShow(): learnHub; hubRssi; lastShowMs; feed clock FIRST (so the lead below uses the fresh offset);
  uint16_t newest = havePending ? pending.showSeq : (haveApplied ? applied.showSeq : s->showSeq - 1);
  if (haveApplied || havePending) { if ((int16_t)(s->showSeq - newest) <= 0) { if (st==LOST_HUB) setState(RUNNING); return; } }
  int32_t lead = s->applyAtHubMs ? (int32_t)(s->applyAtHubMs - hubNow()) : 0;
  if (s->applyAtHubMs == 0 || lead <= 0) { lock; applied = *s; haveApplied = true; havePending = false; unlock; logApply(*s, lead); }
  else { lock; pending = *s; havePending = true; unlock; Serial.printf("[NODE] pending seq=%u word=%u/%u in %ld ms\n", …); }
  if (st == LOST_HUB) setState(RUNNING);
render(): if (havePending) { lock; copy; unlock; if ((int32_t)(hubNow() - p.applyAtHubMs) >= 0) { lock; applied = p; haveApplied = true; havePending = false; unlock; logApply(p, 0 /* on time */); } }
```
Store `leadAtArrival` with the pending copy so the scheduled-path log prints the arrival lead (C5 measures it), not 0. `haveShow` in `tick()`'s SHOW_POSITION → RUNNING test becomes `haveApplied || havePending` — no: keep `haveApplied` (a node must not go RUNNING with nothing to draw; the pending applies within 300 ms anyway). Clear `havePending` in `SETPOS`, `RESETORD`, `FORGET`-to-us. `sendHb().lastShowSeq = applied.showSeq`. Guard `chars[myPos-1]` (S2-24). `printStatus()` additions from the draft are fine.

### R6. `commands.cpp`
Keep the draft's `cmdWspeed`/`cmdMode` (use `modeFromName`). `cmdMsg()`: after `applySign`, local `Sequencer sq; sq.rebuild(s, boards)`; `groups` via `groupString` into `char buf[GROUP_STR_MAX]`; `groupCount`. `cmdBoards()`/`cmdMode()` need no extra work — the tick detects the change.

### R7. `LED_Sign_Mesh.ino`
`case 'n': if (role == HUB) hub::nextGroup(); else "? (hub only)"`; help string `(r i n t<text> {json} s m w | c f s m)`. Header comment: step 2.

### R8. `web/index.html`
- `tokenize(text)` → array of units (`{sym:'heart'}` or `'A'`), splitting words on `' '`; `groupsFor(units, boards, mode)` implementing R3 steps 3–5 exactly (raw = first `boards` units, one chip).
- `renderGroupsPreview()` on `input`, on poll, after Send, after mode change; chips via `createElement`, class `chip` (+ `on` for `S.state.wordIdx` when the textarea equals `lastCfgText`); unit → text: glyph map (S2-30) or the char; hint `N groups · B boards`.
- Words/Raw 2-segment control on the Message tab under the preview; `applyState()` marks it from `cfg.mode`.
- Slider: keep the draft's (min 500, max 10000, step 100, `(v/1000).toFixed(1) + ' s'`, 300 ms debounce, `wspeedTouched` guard).
- Re-run `tools/build_web.py`, confirm ≤ 40 kB, commit `web_page.h`.

## Keep as-is
- `sign_state.h/.cpp` field additions, enum, `SIGN_DEFAULT`, `SIGN_BLOB_VER 2`.
- `cmdWspeed()`/`cmdMode()` bodies and their reply shapes; dispatch lines in `handle()`.
- `cfg.wspeed`, `cfg.mode`, `cfg.groups`, `cfg.groupCount`, `state.wordIdx` field names; `[SHOW] … group=i/n` log suffix; `[HUB] advance group`, `[HUB] sequencer rebuilt` lines (adjust fields per R4).
- node `printStatus()` additions (`havePending`, `word=i/n`).
- JS words-mode packer structure (it is the correct reference for the five cases); slider markup and handler; mode button → `{"cmd":"mode","mode":…}` request shape.
- Everything byte-identical to the baseline stays byte-identical except the files named in R1–R8.

## Algorithm trace table

Rules (brief item 2): split on spaces; `{token}` = 1 unit; word > numBoards → chunked into numBoards-unit pieces; greedy pack consecutive (pseudo-)words with one space while ≤ numBoards; ≤ 20 groups; empty → one blank group; `SEQ_RAW` → first numBoards units. **Brief C3's "A {heart}" waffle resolved**: `"A"` + space + `{heart}` = **3 units > 2**, so the correct expectation on 2 boards is `["A","{heart}","B"]`; `["A {heart}","B"]` would need 3 boards.

| Case (boards) | Expected per rules | C++ draft (by reading the code) | JS draft (by reading the code) |
|---|---|---|---|
| "WALK THE DOG" (4) | `WALK` / `THE` / `DOG` | `WALK` / `THE` / `DOG` ✓ | `WALK` / `THE` / `DOG` ✓ |
| "WALK THE DOG" (8) | `WALK THE` / `DOG` | `WALK THE` / `DOG` ✓ | `WALK THE` / `DOG` ✓ |
| "GO TEAM" (2) | `GO` / `TE` / `AM` | `GO` + **19 empty groups**, `groupCount 20` (S2-06) ✗ | `GO` / `TE` / `AM` ✓ |
| "PARTY" (2) | `PA` / `RT` / `Y` | **20 empty groups** (S2-06) ✗ | `PA` / `RT` / `Y` ✓ |
| "A {heart} B" (2) | `A` / `{heart}` / `B` | `A` / **`?`** / `B` — grouping right, token lost (S2-07; and does not compile, S2-01) ✗ | `A` / `#`→**random token** / `B` — grouping right, glyph wrong (S2-30) ✗ |
| extra: "" (any) | 1 blank group (len 0) | 1 group of `' '` × numBoards (S2-08) ~ | `['']` ✓ |
| extra (chunk-tail rule, R3 step 3): "TEAM GO" (3) | `TEA` / `M GO` (pseudo-word rule) | — (hangs) | `TEA` / `M` / `GO` (tail alone) — must be aligned with the C++ either way |
| extra, SEQ_RAW: "GO TEAM" (2) | `GO` (one group) | `GO` ✓ (but never sent — S2-12) | `GO` / ` T` / `EA` / `M` (S2-32) ✗ |

## Refactor disposition
(to be filled by the refactor agent, STEP3_REVIEW.md format: ID → fixed/kept/deferred + note; then compile line, flash size, and the C12 Playwright run against the stub.)
