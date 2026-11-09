# How-to: the build pipeline (draft → review → refactor → verify)

Used for Phase 1 step 1 on 2026-11-08; repeat per build brief.

1. **Brief** — write `docs/build-briefs/<PHASE>_<STEP>_<topic>.md`: scope, out-of-scope, exact acceptance tests (A1…An) runnable on the two bench boards, output format for the drafting LLM.
2. **Draft (local LLM, Qwen box)** —
   `python3 tools/ask_local_llm.py -m qwen3-coder-next:q4_K_M --ctx 49152 --temp 0.15 -p "Implement EVERYTHING in the brief…" -f <brief> -f docs/PROJECT_PLAN.md -f <proven reference .ino> -o drafts/<step>_qwen.md`
   then `python3 tools/split_files.py drafts/<step>_qwen.md .` and a first `arduino-cli compile` for the error list. (~4 min draft at ~50 tok/s; expect it to not compile.)
3. **Review agent** (read-only) — reads brief + plan + draft (+ compile log), writes `docs/reviews/<STEP>_REVIEW.md`: verdict, numbered findings with severity and concrete fix, refactor plan, keep-as-is list.
4. **Refactor agent** — rewrites `firmware/<sketch>/` from the review, commits/pushes from the cloud, pulls + compiles on the box in a loop until `--warnings all` is clean; appends "Refactor disposition" to the review.
5. **Verification agent** — flashes both boards, runs A1…An via `tools/serial_capture.py` (reset + role key) and non-resetting pyserial one-liners for runtime commands, writes `docs/reviews/<STEP>_VERIFICATION.md` with log evidence, leaves boards running.
6. Orchestrator updates `Progress.md` (log, decisions, drift) and closes the step.

Rules: one agent active at a time; drift between brief/plan/reality is logged as a Framework Improvement Trigger, never patched silently; nothing from the local LLM lands in the plan until verified on hardware.

Box commands (Qwen box, `~/esp32-led-sign`):
```bash
cd firmware && ~/.local/bin/arduino-cli compile --fqbn esp32:esp32:lolin32:PartitionScheme=min_spiffs LED_Sign_Mesh
~/.local/bin/arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:lolin32:PartitionScheme=min_spiffs LED_Sign_Mesh
python3 tools/serial_capture.py /dev/ttyUSB0 12 H     # reset, set role HUB, capture 12 s
python3 tools/serial_capture.py /dev/ttyUSB1 12 N
```
Runtime console: hub `r` reset order, `i` identify all, `t<text>` test string, `s` status, `m` mirror; node `c` claim, `f` forget, `s`.
