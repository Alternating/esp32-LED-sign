# character_patterns.h — changelog (2026-11-08)

New header: `character_patterns.h` (replaces `character_patterns_ORIGINAL.h`).
Drop-in: same include guard (`CHARACTER_PATTERNS_20X20_H`), same
`const bool* getCharacterPattern(char c)` signature, same lower→upper folding,
same `PATTERN_SPACE` fallback. `LED_Sign_WiFi_Symbol_Edition.ino`,
`LED_Sign_v4_4Button_Backwards.ino` and `Led_standalone_lolin32.ino` compile unchanged.

## What was verified

| Check | Result |
|---|---|
| Arrays in original | 47 (`A`–`Z`, `0`–`9`, HEART, STAR, CHECK, CROSS, 4 arrows, DIAMOND, CIRCLE, SPACE) |
| Length / content | all 47 are exactly 400 entries of 0/1 |
| Visual inspection (`preview_original.png`, ASCII dump at 20 wide) | no broken art. HEART, STAR, `8`, `O`, `0` all correct. The 10-values-per-line layout is purely cosmetic; the data is row-major 20x20 |
| Style finding | every original glyph is a 10x10 design pixel-doubled: 4 px strokes, full-bleed to the 20 px edge, no margin |
| `getCharacterPattern()` in original | codes 1–10, A–Z, 0–9, `' '`, `'*'`→STAR, `'>'`/`'<'`/`'^'` arrows, fallback `PATTERN_SPACE` (never `nullptr`). Every defined array is referenced. One dead branch: `if (c == 'v')` can never fire because `'v'` is folded to `'V'` first |
| Codes used by Symbol Edition but missing in original | `'?'`, `'!'`, 13 (happy), 14 (sad) — these silently rendered as blanks |
| Original arrays in new file | all 47 byte-identical (checked by script); the block from `#ifndef` to the dispatch function is copied verbatim |
| New file | 68 arrays, all exactly 400 entries; compiles with `g++ -Wall -Wextra` via `compile_test.cpp`; `getCharacterPattern()` returns non-null for all 256 byte values |

## Fixes to existing art

None. No glyph was changed.

The only code change to legacy behaviour is removing the unreachable
`if (c == 'v') return PATTERN_ARROW_DOWN;` line (dead code, behaviour identical:
`'v'` still returns `PATTERN_V`; the down arrow is code 8). A comment marks the spot.

## What was added

Design weight: new glyphs were drawn to match the *actual* existing weight
(4 px strokes, full-bleed) rather than a nominal 3 px / 2 px margin, so they do
not look thin next to the letters. Punctuation that fits a 10-grid was drawn at
10x10 and pixel-doubled exactly like the originals; dots, faces and `10` were
drawn natively at 20x20. New arrays are written 20 values per line (one matrix
row per line) so they can be read in the source.

| Char / code | Pattern | Notes |
|---|---|---|
| `?` | `PATTERN_QUESTION` | required by Symbol Edition "question" button |
| `!` | `PATTERN_EXCLAIM` | required by Symbol Edition "exclaim" button |
| `"` | `PATTERN_QUOTE` | |
| `:` | `PATTERN_COLON` | two 6x6 dots |
| `&` | `PATTERN_AMPERSAND` | |
| `$` | `PATTERN_DOLLAR` | |
| `#` | `PATTERN_HASH` | |
| `+` | `PATTERN_PLUS` | |
| `.` | `PATTERN_PERIOD` | 6x6 dot, bottom centre |
| `,` | `PATTERN_COMMA` | |
| `'` | `PATTERN_APOSTROPHE` | |
| `-` | `PATTERN_MINUS` | |
| `/` | `PATTERN_SLASH` | |
| `@` | `PATTERN_AT` | |
| `%` | `PATTERN_PERCENT` | |
| `(` | `PATTERN_PAREN_L` | |
| `)` | `PATTERN_PAREN_R` | mirror of `(` |
| `=` | `PATTERN_EQUALS` | |
| code 13 | `PATTERN_HAPPY` | round face, 2 eyes, smile. Symbol Edition web UI shows `:)` and sends `\x0D` |
| code 14 | `PATTERN_SAD` | same face, frown. Web UI shows `:(` and sends `\x0E` |
| code 15 | `PATTERN_TEN` | digits `1` and `0` side by side (cols 1–8 and 11–18, 2 px gap, 3 px strokes) for board position 10 |

Not added / deliberately unchanged:

* `*` — already mapped to `PATTERN_STAR` in the original. Kept as-is for
  compatibility (a 5-point star is the natural LED-sign asterisk anyway); no
  separate asterisk glyph.
* `'>'`, `'<'`, `'^'` ASCII aliases for arrows kept exactly as before.
* No code 11/12/16. `?` and `!` are reached by their ASCII characters only.
* Unknown characters still return `PATTERN_SPACE` (blank board), never `nullptr`,
  matching the original. The `if (pattern == nullptr)` guards in the sketches
  remain harmless.

Full supported set (68 glyphs):
`A–Z` (a–z folded), `0–9`, space, `? ! " : & $ # + . , ' - / @ % * ( ) =`,
`> < ^` (arrow aliases), control codes 1–10, 13, 14, 15.

## RAM note

Patterns are `const bool PATTERN_X[400]`, i.e. 400 bytes each in RAM
(`sizeof(bool) == 1`; on ESP32 `const` globals are not automatically placed in
flash the way `PROGMEM` would on AVR — in practice the ESP32 toolchain *does*
usually put `const` data in the `.rodata` flash segment, but treat it as RAM
budget to be safe).

* Original: 47 x 400 = 18,800 bytes
* New:      68 x 400 = 27,200 bytes (+8,400)

Bit-packing (400 bits = 50 bytes per glyph, 3.4 KB total) or `PROGMEM`/`pgm_read_byte`
would cut this by 8x but requires changing every consumer's
`if (pattern[bitmapIndex])` read. Deferred; not needed on an ESP32 with ~320 KB SRAM.

## Files

| File | Purpose |
|---|---|
| `character_patterns.h` | the deliverable header |
| `character_patterns_ORIGINAL.h` | untouched input |
| `build_character_patterns.py` | source of truth for the new art (string art); regenerates `character_patterns.h` from the ORIGINAL |
| `render_patterns.py` | validator / previewer (see below) |
| `compile_test.cpp` | host-side compile and dispatch sanity test (`g++ -std=gnu++11 -Wall -Wextra compile_test.cpp`) |
| `preview_sheet.png` | all 68 glyphs, 10 per row |
| `preview_new.png` | the 21 new glyphs, large cells |
| `preview_original.png` | the 47 original glyphs |

## Running the render tool

```bash
cd patterns/
# validate + ASCII dump + contact sheet (exit code 1 if any array is not 400 x 0/1)
python3 render_patterns.py character_patterns.h --png preview_sheet.png

# only some glyphs, bigger cells, no ASCII dump
python3 render_patterns.py character_patterns.h --only HAPPY,SAD,TEN --png preview_new.png --cell 12 --per-row 7 --quiet

# regenerate the header after editing art in build_character_patterns.py
python3 build_character_patterns.py character_patterns_ORIGINAL.h character_patterns.h
```

The tool also prints every `case`/`if` handled by `getCharacterPattern()`, the
fall-through default, any pattern referenced but not defined, and any pattern
defined but never returned. Requires Python 3 and Pillow (`pip install pillow`)
for the PNG; the ASCII dump and validation work without Pillow.
