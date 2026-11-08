#!/usr/bin/env python3
"""Send a prompt to the local Ollama server and print the reply.

Usage:
  ask_local_llm.py -m MODEL -p "prompt text"
  ask_local_llm.py -m MODEL -f prompt.md [-f more_context.ino ...] [-s "system prompt"] [-o out.md]
Files given with -f are concatenated (each wrapped in a fenced block with its filename) and appended to -p.
Default model: qwen3-coder-next:q4_K_M. Default host: http://localhost:11434.
"""
import argparse, json, sys, time, urllib.request

ap = argparse.ArgumentParser()
ap.add_argument("-m", "--model", default="qwen3-coder-next:q4_K_M")
ap.add_argument("-p", "--prompt", default="")
ap.add_argument("-f", "--file", action="append", default=[])
ap.add_argument("-s", "--system", default="You are a senior embedded C++ engineer working on an ESP32 (Arduino core 3.x, FastLED, ESP-NOW) LED sign project. Be concise and concrete; output code in fenced blocks.")
ap.add_argument("-o", "--out", default="")
ap.add_argument("--host", default="http://localhost:11434")
ap.add_argument("--temp", type=float, default=0.2)
ap.add_argument("--ctx", type=int, default=32768)
a = ap.parse_args()

prompt = a.prompt
for fn in a.file:
    with open(fn, errors="replace") as fh:
        prompt += f"\n\n```{fn}\n{fh.read()}\n```"
if not prompt.strip():
    sys.exit("no prompt")

body = {"model": a.model, "stream": False, "options": {"temperature": a.temp, "num_ctx": a.ctx},
        "messages": [{"role": "system", "content": a.system}, {"role": "user", "content": prompt}]}
req = urllib.request.Request(a.host + "/api/chat", data=json.dumps(body).encode(), headers={"Content-Type": "application/json"})
t0 = time.time()
with urllib.request.urlopen(req, timeout=3600) as r:
    res = json.load(r)
text = res["message"]["content"]
if a.out:
    open(a.out, "w").write(text)
print(text)
ev = res.get("eval_count", 0); ed = res.get("eval_duration", 1) / 1e9
sys.stderr.write(f"\n[{a.model}: {ev} tok in {time.time()-t0:.0f}s, {ev/ed if ed else 0:.1f} tok/s]\n")
