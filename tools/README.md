# tools

| tool | runs on | purpose |
|---|---|---|
| `fonts/render_patterns.py` | anywhere (PIL) | validate a `character_patterns*.h`, dump ASCII, write preview PNGs |
| `fonts/build_character_patterns.py` | anywhere | regenerate `character_patterns.h` from the string-art source of truth |
| `serial_capture.py [port] [secs] [keys]` | Qwen box | DTR/RTS-reset an ESP32 and capture serial; `keys` are sent 1 s after reset (e.g. `H`/`N` to set the bench role) |
| `ask_local_llm.py -m MODEL -p "..." [-f file ...]` | Qwen box | query the local Ollama server (`localhost:11434`) |

## Local LLM (Qwen box)
Models available via Ollama: `qwen3-coder-next:q4_K_M` (52 GB, spills to CPU), `qwen3.8:27b-q4_K_M` (17.7 GB, fits the R9700 → ~29 tok/s), `qwen3.8:27b-q8_0`, `flint:40b`, `laguna-s-2.1`.

Use it for: first-draft code, code review of a diff, generating glyph string-art, explaining a compiler error, writing test scaffolding.
Rule: **its output is a draft, not a source.** Smoke test 2026-11-08: it asserted ESP-NOW nodes must associate with the hub AP to share a channel; our bench shows an unassociated STA with `esp_wifi_set_channel()` runs at 0 % loss. Verify on hardware before anything it says lands in the plan.
