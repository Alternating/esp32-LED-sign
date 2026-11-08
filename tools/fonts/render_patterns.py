#!/usr/bin/env python3
"""
render_patterns.py - validate and preview 20x20 LED glyphs in character_patterns*.h

Usage:
    python3 render_patterns.py <header.h> [--png OUT.png] [--cell N]
                               [--only NAME,NAME,...] [--quiet]

What it does
  * Extracts every `const bool PATTERN_X[400] = { ... };` array.
  * Validates: exactly 400 entries, every entry 0 or 1.
  * Prints each glyph as a 20x20 ASCII block (█ = on, · = off), row-major
    (index = row*20 + col), which is exactly how the sketches index it.
  * Cross-checks getCharacterPattern(): which PATTERN_ names it references,
    which cases it handles, and which arrays are defined but never returned.
  * Writes a labelled PNG contact sheet (10 glyphs per row) with Pillow.

Exit status is 1 if any array fails validation, so it can be used as a
pre-flight check before flashing.
"""
import argparse
import re
import sys

ARRAY_RE = re.compile(
    r"const\s+bool\s+(PATTERN_\w+)\s*\[\s*(\d+)\s*\]\s*=\s*\{(.*?)\}\s*;",
    re.S,
)
GRID = 20
EXPECTED = GRID * GRID


def strip_comments(src: str) -> str:
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    src = re.sub(r"//[^\n]*", "", src)
    return src


def parse_arrays(src: str):
    """Return list of (name, declared_len, [ints], raw_body) in file order."""
    out = []
    for m in ARRAY_RE.finditer(strip_comments(src)):
        name, declared, body = m.group(1), int(m.group(2)), m.group(3)
        tokens = [t.strip() for t in body.split(",") if t.strip()]
        values = []
        bad = []
        for t in tokens:
            if t in ("0", "1"):
                values.append(int(t))
            else:
                bad.append(t)
        out.append({"name": name, "declared": declared, "values": values,
                    "bad_tokens": bad})
    return out


def validate(arrays):
    problems = []
    for a in arrays:
        n = len(a["values"])
        if a["declared"] != EXPECTED:
            problems.append(f"{a['name']}: declared [{a['declared']}], expected [{EXPECTED}]")
        if n != EXPECTED:
            problems.append(f"{a['name']}: {n} entries (expected {EXPECTED})")
        if a["bad_tokens"]:
            problems.append(f"{a['name']}: non-0/1 tokens {a['bad_tokens'][:5]}")
    return problems


def ascii_block(values):
    rows = []
    for r in range(GRID):
        row = values[r * GRID:(r + 1) * GRID]
        rows.append("".join("█" if v else "·" for v in row))
    return "\n".join(rows)


def analyse_dispatch(src: str, arrays):
    """Report what getCharacterPattern() handles and which arrays are orphaned."""
    m = re.search(r"getCharacterPattern\s*\(\s*char\s+\w+\s*\)\s*\{(.*)\n\}", strip_comments(src), re.S)
    if not m:
        return None
    body = m.group(1)
    lit = r"'(?:\\.|[^'\\])'"  # C char literal incl. escapes like '\''
    referenced = set(re.findall(r"return\s+(PATTERN_\w+)\s*;", body))
    cases = re.findall(r"case\s+(" + lit + r"|\d+)\s*:\s*return\s+(PATTERN_\w+)", body)
    ifs = re.findall(r"if\s*\(\s*c\s*==\s*(" + lit + r"|\d+)\s*\)\s*return\s+(PATTERN_\w+)", body)
    # the last bare `return X;` in the function is the fall-through default
    default = re.findall(r"return\s+(PATTERN_\w+|nullptr|NULL)\s*;", body)[-1:]
    defined = {a["name"] for a in arrays}
    return {
        "cases": cases,
        "ifs": ifs,
        "referenced": referenced,
        "undefined_refs": sorted(referenced - defined),
        "orphans": sorted(defined - referenced),
        "default": default[0] if default else "?",
    }


def write_png(arrays, path, cell=6, per_row=10, label_h=14):
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        print("Pillow not installed; skipping PNG (pip install pillow)", file=sys.stderr)
        return False
    pad = 6
    tile_w = GRID * cell + pad
    tile_h = GRID * cell + label_h + pad
    n = len(arrays)
    rows = (n + per_row - 1) // per_row
    img = Image.new("RGB", (per_row * tile_w + pad, rows * tile_h + pad), (18, 18, 18))
    d = ImageDraw.Draw(img)
    for i, a in enumerate(arrays):
        tx = pad + (i % per_row) * tile_w
        ty = pad + (i // per_row) * tile_h
        label = a["name"].replace("PATTERN_", "")
        ok = len(a["values"]) == EXPECTED and not a["bad_tokens"]
        d.text((tx, ty), label if ok else label + " !!", fill=(230, 230, 230) if ok else (255, 80, 80))
        gy = ty + label_h
        # board background
        d.rectangle([tx, gy, tx + GRID * cell - 1, gy + GRID * cell - 1], fill=(35, 35, 35))
        vals = a["values"][:EXPECTED]
        for idx, v in enumerate(vals):
            r, c = divmod(idx, GRID)
            x0, y0 = tx + c * cell, gy + r * cell
            if v:
                d.rectangle([x0, y0, x0 + cell - 2, y0 + cell - 2], fill=(255, 60, 40))
            else:
                # faint dot so the grid reads like an LED board
                d.point((x0 + cell // 2, y0 + cell // 2), fill=(60, 60, 60))
    img.save(path)
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("header")
    ap.add_argument("--png", default="preview_sheet.png", help="output PNG (default preview_sheet.png)")
    ap.add_argument("--cell", type=int, default=6, help="pixel size of one LED in the PNG")
    ap.add_argument("--per-row", type=int, default=10)
    ap.add_argument("--only", help="comma list of pattern names (with or without PATTERN_) to include")
    ap.add_argument("--quiet", action="store_true", help="skip ASCII dump")
    args = ap.parse_args()

    with open(args.header, encoding="utf-8") as f:
        src = f.read()

    arrays = parse_arrays(src)
    if args.only:
        wanted = {("PATTERN_" + n if not n.startswith("PATTERN_") else n) for n in args.only.split(",")}
        arrays = [a for a in arrays if a["name"] in wanted]

    problems = validate(arrays)

    if not args.quiet:
        for a in arrays:
            n = len(a["values"])
            flag = "" if (n == EXPECTED and not a["bad_tokens"]) else "   <-- INVALID"
            print(f"\n=== {a['name']}  ({n} entries){flag}")
            print(ascii_block(a["values"] + [0] * max(0, EXPECTED - n)))

    print(f"\n{len(arrays)} arrays parsed from {args.header}")
    if problems:
        print("VALIDATION PROBLEMS:")
        for p in problems:
            print("  -", p)
    else:
        print(f"All arrays have exactly {EXPECTED} entries of 0/1.")

    info = analyse_dispatch(src, parse_arrays(src))
    if info:
        print("\ngetCharacterPattern() dispatch:")
        for key, pat in info["cases"]:
            print(f"  case {key:>5} -> {pat}")
        for key, pat in info["ifs"]:
            print(f"  if c=={key:>5} -> {pat}")
        print(f"  default      -> {info['default']}")
        print(f"  referenced patterns: {len(info['referenced'])}")
        if info["undefined_refs"]:
            print(f"  REFERENCED BUT NOT DEFINED: {info['undefined_refs']}")
        print(f"  defined but never returned: {info['orphans'] or 'none'}")
    else:
        print("\n(no getCharacterPattern() found)")

    if write_png(arrays, args.png, cell=args.cell, per_row=args.per_row):
        print(f"\nWrote {args.png}")

    sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()
