#!/usr/bin/env python3
"""Split an LLM draft containing `=== FILE: path ===` ... `=== END FILE ===` blocks into files.
Usage: split_files.py draft.md [repo_root]   (paths in the draft are relative to repo_root, default '.')
Strips a leading/trailing ``` fence inside a block if the model added one. Refuses paths that escape the root.
"""
import os, re, sys

src = sys.argv[1]
root = os.path.abspath(sys.argv[2] if len(sys.argv) > 2 else ".")
text = open(src, errors="replace").read()
blocks = re.findall(r"=== FILE: (.+?) ===\n(.*?)\n=== END FILE ===", text, re.S)
if not blocks:
    sys.exit("no FILE blocks found")
for rel, body in blocks:
    rel = rel.strip()
    dst = os.path.abspath(os.path.join(root, rel))
    if not dst.startswith(root + os.sep):
        sys.exit(f"refusing path outside root: {rel}")
    lines = body.split("\n")
    if lines and lines[0].startswith("```"): lines = lines[1:]
    if lines and lines[-1].strip() == "```": lines = lines[:-1]
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with open(dst, "w") as fh:
        fh.write("\n".join(lines).rstrip("\n") + "\n")
    print(f"wrote {rel} ({len(lines)} lines)")
