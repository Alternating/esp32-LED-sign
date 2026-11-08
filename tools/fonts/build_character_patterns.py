#!/usr/bin/env python3
"""
build_character_patterns.py - assemble character_patterns.h from the ORIGINAL
header (all existing arrays copied verbatim) plus the new glyphs defined here.

    python3 build_character_patterns.py character_patterns_ORIGINAL.h character_patterns.h

Glyph art is written as strings: '#' = LED on, '.' = off.
  * 10x10 art is pixel-doubled to 20x20 (this is how every ORIGINAL glyph was
    made: 4px strokes, full-bleed).  Use it when the shape fits a 10-grid.
  * 20x20 art is used as-is (faces, "10", punctuation that needs odd offsets).
"""
import re
import sys
from math import hypot

GRID = 20

# ----------------------------------------------------------------------------
# helpers
# ----------------------------------------------------------------------------
def blank():
    return [[0] * GRID for _ in range(GRID)]


def from_art(rows):
    rows = [r for r in rows.strip("\n").split("\n")]
    rows = [r.strip() for r in rows if r.strip()]
    n = len(rows)
    assert n in (10, 20), f"art must be 10 or 20 rows, got {n}"
    for r in rows:
        assert len(r) == n, f"row '{r}' is {len(r)} wide, expected {n}"
    g = [[1 if ch == "#" else 0 for ch in r] for r in rows]
    if n == 10:  # pixel-double
        g = [[v for v in row for _ in (0, 1)] for row in g for _ in (0, 1)]
    return g


def fill_rect(g, r0, r1, c0, c1):
    for r in range(r0, r1 + 1):
        for c in range(c0, c1 + 1):
            g[r][c] = 1


def ring(g, cx, cy, r_out, r_in, rows=None, cols=None):
    for r in range(GRID):
        for c in range(GRID):
            if rows and not (rows[0] <= r <= rows[1]):
                continue
            if cols and not (cols[0] <= c <= cols[1]):
                continue
            d = hypot(c - cx, r - cy)
            if r_in < d <= r_out:
                g[r][c] = 1


def mirror_h(g):
    return [list(reversed(row)) for row in g]


# ----------------------------------------------------------------------------
# new glyphs
# ----------------------------------------------------------------------------
NEW = {}  # name -> (grid, comment)

NEW["QUESTION"] = (from_art("""
..######..
.########.
##......##
........##
.....####.
...####...
....##....
..........
....##....
....##....
"""), "'?'")

NEW["EXCLAIM"] = (from_art("""
...####...
...####...
...####...
...####...
...####...
....##....
....##....
..........
...####...
...####...
"""), "'!'")

# -- dots / quotes (native 20x20: 6x6 dots read better from a distance) -----
g = blank(); fill_rect(g, 14, 19, 7, 12)
NEW["PERIOD"] = (g, "'.'")

g = blank(); fill_rect(g, 11, 16, 7, 12); fill_rect(g, 17, 17, 6, 11); fill_rect(g, 18, 18, 5, 10); fill_rect(g, 19, 19, 4, 8)
NEW["COMMA"] = (g, "','")

g = blank(); fill_rect(g, 3, 8, 7, 12); fill_rect(g, 12, 17, 7, 12)
NEW["COLON"] = (g, "':'")

g = blank(); fill_rect(g, 0, 5, 7, 12); fill_rect(g, 6, 6, 6, 11); fill_rect(g, 7, 7, 5, 10); fill_rect(g, 8, 8, 4, 8)
NEW["APOSTROPHE"] = (g, "'\\''")

g = blank(); fill_rect(g, 0, 6, 4, 7); fill_rect(g, 7, 7, 3, 6); fill_rect(g, 0, 6, 12, 15); fill_rect(g, 7, 7, 11, 14)
NEW["QUOTE"] = (g, "'\"'")

# -- math / punctuation ------------------------------------------------------
g = blank(); fill_rect(g, 8, 11, 2, 17)
NEW["MINUS"] = (g, "'-'")

g = blank(); fill_rect(g, 8, 11, 2, 17); fill_rect(g, 2, 17, 8, 11)
NEW["PLUS"] = (g, "'+'")

g = blank(); fill_rect(g, 5, 8, 2, 17); fill_rect(g, 11, 14, 2, 17)
NEW["EQUALS"] = (g, "'='")

g = blank()
for r in range(GRID):  # 4px-wide diagonal, bottom-left to top-right
    start = round(14 - 12 * r / 19)
    fill_rect(g, r, r, start, start + 3)
NEW["SLASH"] = (g, "'/'")

g = blank()
for (r0, r1), (c0, c1) in [((0, 1), (11, 14)), ((2, 3), (9, 12)), ((4, 5), (7, 10)),
                           ((6, 13), (6, 9)), ((14, 15), (7, 10)), ((16, 17), (9, 12)),
                           ((18, 19), (11, 14))]:
    fill_rect(g, r0, r1, c0, c1)
NEW["PAREN_L"] = (g, "'('")
NEW["PAREN_R"] = (mirror_h(g), "')'")

NEW["HASH"] = (from_art("""
..##..##..
..##..##..
##########
##########
..##..##..
..##..##..
##########
##########
..##..##..
..##..##..
"""), "'#'")

NEW["DOLLAR"] = (from_art("""
....##....
.########.
##..##..##
##..##....
.#######..
..#######.
....##..##
##..##..##
.########.
....##....
"""), "'$'")

NEW["AMPERSAND"] = (from_art("""
..####....
.##..##...
.##..##...
..####....
.####..##.
##..##.##.
##...###..
##...###..
.##..####.
..###..###
"""), "'&'")

NEW["AT"] = (from_art("""
..######..
.##....##.
##......##
##..###.##
##.##.#.##
##.##.#.##
##..######
##........
.##....##.
..######..
"""), "'@'")

NEW["PERCENT"] = (from_art("""
###.....##
#.#....##.
###...##..
.....##...
....##....
...##.....
..##......
.##...###.
##....#.#.
......###.
"""), "'%'")

# -- faces (native 20x20) ----------------------------------------------------
NEW["HAPPY"] = (from_art("""
........####........
.....##########.....
....############....
...####......####...
..###..........###..
.###.###....###.###.
.###.###....###.###.
.##..###....###..##.
###..............###
###..............###
###..............###
###..............###
.##.##........##.##.
.###.##......##.###.
.###..########..###.
..###..######..###..
...####......####...
....############....
.....##########.....
........####........
"""), "code 13  :)")

NEW["SAD"] = (from_art("""
........####........
.....##########.....
....############....
...####......####...
..###..........###..
.###.###....###.###.
.###.###....###.###.
.##..###....###..##.
###..............###
###..............###
###..............###
###..............###
.##....######....##.
.###.###....###.###.
.###.##......##.###.
..###..........###..
...####......####...
....############....
.....##########.....
........####........
"""), "code 14  :(")

# -- "10" for board position ten (native 20x20) -----------------------------
g = blank()
# '1' in cols 1-8
fill_rect(g, 0, 16, 4, 6)           # stem
fill_rect(g, 1, 1, 3, 3)            # flag
fill_rect(g, 2, 3, 1, 3)
fill_rect(g, 17, 19, 1, 8)          # base
# '0' in cols 11-18
fill_rect(g, 0, 0, 13, 16); fill_rect(g, 1, 1, 12, 17); fill_rect(g, 2, 2, 11, 18)
fill_rect(g, 3, 16, 11, 13); fill_rect(g, 3, 16, 16, 18)
fill_rect(g, 17, 17, 11, 18); fill_rect(g, 18, 18, 12, 17); fill_rect(g, 19, 19, 13, 16)
NEW["TEN"] = (g, "code 15  \"10\"")

# ----------------------------------------------------------------------------
# emit
# ----------------------------------------------------------------------------
HEADER_COMMENT = """/*
 * Character Patterns Library - 20x20
 * 20x20 pixel patterns for letters, numbers, and symbols
 *
 * Save this file as: character_patterns.h
 * Place in same folder as your main .ino file
 *
 * Each pattern is a const bool[400], row-major: index = row*20 + col,
 * row 0 = top, col 0 = left (the sketch mirrors columns for its wiring).
 * 1 = LED on.  Every array below is exactly 400 entries.
 *
 * getCharacterPattern(char) lookup table
 * ---------------------------------------------------------------
 *  Control codes (symbol buttons / Symbol Edition web UI)
 *    1  PATTERN_HEART         6  PATTERN_ARROW_LEFT
 *    2  PATTERN_STAR          7  PATTERN_ARROW_UP
 *    3  PATTERN_CHECK         8  PATTERN_ARROW_DOWN
 *    4  PATTERN_CROSS         9  PATTERN_DIAMOND
 *    5  PATTERN_ARROW_RIGHT  10  PATTERN_CIRCLE
 *   13  PATTERN_HAPPY  :)    14  PATTERN_SAD  :(
 *   15  PATTERN_TEN    "10" (board position 10)
 *
 *  Letters   A-Z (a-z are folded to upper case)     PATTERN_A .. PATTERN_Z
 *  Digits    0-9                                    PATTERN_0 .. PATTERN_9
 *
 *  ASCII punctuation
 *    ' '  PATTERN_SPACE       '?'  PATTERN_QUESTION    '!'  PATTERN_EXCLAIM
 *    '.'  PATTERN_PERIOD      ','  PATTERN_COMMA       ':'  PATTERN_COLON
 *    '\\'' PATTERN_APOSTROPHE  '"'  PATTERN_QUOTE       '-'  PATTERN_MINUS
 *    '+'  PATTERN_PLUS        '='  PATTERN_EQUALS      '/'  PATTERN_SLASH
 *    '('  PATTERN_PAREN_L     ')'  PATTERN_PAREN_R     '#'  PATTERN_HASH
 *    '$'  PATTERN_DOLLAR      '&'  PATTERN_AMPERSAND   '@'  PATTERN_AT
 *    '%'  PATTERN_PERCENT
 *
 *  ASCII aliases for symbols (legacy, unchanged)
 *    '*' PATTERN_STAR   '>' PATTERN_ARROW_RIGHT   '<' PATTERN_ARROW_LEFT
 *    '^' PATTERN_ARROW_UP
 *
 *  Anything else returns PATTERN_SPACE (blank board), never nullptr.
 *
 * RAM: each pattern is 400 bytes (const bool, not PROGMEM).
 */
"""

ORDER = ["QUESTION", "EXCLAIM", "PERIOD", "COMMA", "COLON", "APOSTROPHE", "QUOTE",
         "MINUS", "PLUS", "EQUALS", "SLASH", "PAREN_L", "PAREN_R", "HASH", "DOLLAR",
         "AMPERSAND", "AT", "PERCENT", "HAPPY", "SAD", "TEN"]

ASCII_MAP = [  # (C char literal, pattern)
    ("'?'", "QUESTION"), ("'!'", "EXCLAIM"), ("'.'", "PERIOD"), ("','", "COMMA"),
    ("':'", "COLON"), ("'\\''", "APOSTROPHE"), ("'\"'", "QUOTE"), ("'-'", "MINUS"),
    ("'+'", "PLUS"), ("'='", "EQUALS"), ("'/'", "SLASH"), ("'('", "PAREN_L"),
    ("')'", "PAREN_R"), ("'#'", "HASH"), ("'$'", "DOLLAR"), ("'&'", "AMPERSAND"),
    ("'@'", "AT"), ("'%'", "PERCENT"),
]


def emit_array(name, grid, comment):
    lines = [f"// {comment}", f"const bool PATTERN_{name}[400] = {{"]
    for r, row in enumerate(grid):
        assert len(row) == GRID
        lines.append("  " + ",".join(str(v) for v in row) + ("," if r < GRID - 1 else ""))
    lines.append("};\n")
    return "\n".join(lines)


def emit_function():
    out = []
    out.append("// Function to get character pattern")
    out.append("const bool* getCharacterPattern(char c) {")
    out.append("  // Handle special symbol codes (1-10, 13-15)")
    out.append("  switch((uint8_t)c) {")
    for code, name in [(1, "HEART"), (2, "STAR"), (3, "CHECK"), (4, "CROSS"), (5, "ARROW_RIGHT"),
                       (6, "ARROW_LEFT"), (7, "ARROW_UP"), (8, "ARROW_DOWN"), (9, "DIAMOND"),
                       (10, "CIRCLE"), (13, "HAPPY"), (14, "SAD"), (15, "TEN")]:
        out.append(f"    case {code}: return PATTERN_{name};")
    out.append("  }")
    out.append("")
    out.append("  // Convert to uppercase for letters")
    out.append("  if (c >= 'a' && c <= 'z') {")
    out.append("    c = c - 32; // Convert to uppercase")
    out.append("  }")
    out.append("")
    out.append("  // Letters A-Z")
    out.append("  if (c >= 'A' && c <= 'Z') {")
    out.append("    switch(c) {")
    for ch in "ABCDEFGHIJKLMNOPQRSTUVWXYZ":
        out.append(f"      case '{ch}': return PATTERN_{ch};")
    out.append("    }")
    out.append("  }")
    out.append("")
    out.append("  // Numbers 0-9")
    out.append("  if (c >= '0' && c <= '9') {")
    out.append("    switch(c) {")
    for ch in "0123456789":
        out.append(f"      case '{ch}': return PATTERN_{ch};")
    out.append("    }")
    out.append("  }")
    out.append("")
    out.append("  // ASCII punctuation")
    out.append("  switch(c) {")
    for lit, name in ASCII_MAP:
        out.append(f"    case {lit}: return PATTERN_{name};")
    out.append("  }")
    out.append("")
    out.append("  // Special ASCII symbols (legacy aliases, unchanged)")
    out.append("  if (c == ' ') return PATTERN_SPACE;")
    out.append("  if (c == '*') return PATTERN_STAR;")
    out.append("  if (c == '>') return PATTERN_ARROW_RIGHT;")
    out.append("  if (c == '<') return PATTERN_ARROW_LEFT;")
    out.append("  if (c == '^') return PATTERN_ARROW_UP;")
    out.append("  // NOTE: the original also had `if (c == 'v') return PATTERN_ARROW_DOWN;`")
    out.append("  // which was unreachable ('v' is folded to 'V' above). Dropped as dead code;")
    out.append("  // behaviour is identical. Use code 8 for the down arrow.")
    out.append("")
    out.append("  // Default: return space if character not found")
    out.append("  return PATTERN_SPACE;")
    out.append("}")
    return "\n".join(out)


def main():
    src_path, dst_path = sys.argv[1], sys.argv[2]
    src = open(src_path, encoding="utf-8").read()

    # keep everything from the include guard up to (not including) the dispatch
    start = src.index("#ifndef CHARACTER_PATTERNS_20X20_H")
    end = src.index("// Function to get character pattern")
    body = src[start:end].rstrip() + "\n\n"

    parts = [HEADER_COMMENT, "\n", body]
    parts.append("// ===== Added glyphs (20 values per line) =====\n\n")
    for name in ORDER:
        grid, comment = NEW[name]
        parts.append(emit_array(name, grid, comment))
        parts.append("\n")
    parts.append(emit_function())
    parts.append("\n\n#endif // CHARACTER_PATTERNS_20X20_H\n")

    with open(dst_path, "w", encoding="utf-8") as f:
        f.write("".join(parts))
    print(f"wrote {dst_path}: {len(ORDER)} new glyphs")


if __name__ == "__main__":
    main()
