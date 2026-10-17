#!/usr/bin/env python3
"""Normalizes the [call] / [hook] lines of a replay log for tools/decomp_calls.sh: drops frame numbers and stack addresses, masks the argument
registers that are not real arguments (gen/arity_<module>.txt) and leaves out the libc-like stubs. Usage: decomp_calls_norm.py <log> <arity> <first frame> <omit stubs, comma separated>"""
import re, sys
log, arity_path, first = sys.argv[1], sys.argv[2], int(sys.argv[3])
override_path = sys.argv[5] if len(sys.argv) > 5 else None
skip = set()
if len(sys.argv) > 6:
    try: skip = set(l.strip().lower() for l in open(sys.argv[6]) if l.strip())
    except OSError: pass
omit = set(sys.argv[4].split(",")) if len(sys.argv) > 4 and sys.argv[4] else set()
arity = {}
for line in open(arity_path):
    a, i, f = line.split()
    arity[a] = (int(i), int(f))
if override_path:
    try:
        for line in open(override_path):   # "<address> <integer args> <float args>": corrections to the Ghidra-derived arity
            f = line.split()
            if len(f) == 3: arity[f[0]] = (int(f[1]), int(f[2]))
    except OSError:
        pass
mask_stack = re.compile(r"^(09f|0a0)[0-9a-f]{5}$")
def val(v): return "SP" if mask_stack.match(v) else v
call = re.compile(r"^\[call\] (\d+) ([0-9a-f]{8})\(([0-9a-f ]+)\) f=([0-9a-f ]+)")
hook = re.compile(r"^\[hook\] quadro (\d+): ([0-9a-f]{8})\(([0-9a-f, ]+)\)")
out = []
for line in open(log, encoding="utf-8", errors="replace"):
    m = call.match(line)
    if m:
        if int(m.group(1)) < first or m.group(2) in omit: continue
        ni, nf = arity.get(m.group(2), (8, 6))
        r = m.group(3).split(); f = m.group(4).split()
        out.append("[call] %s(%s) f=%s" % (m.group(2), " ".join(val(r[i]) if i < ni else "-" for i in range(8)), " ".join(f[i] if i < nf else "-" for i in range(6))))
        continue
    m = hook.match(line)
    if m:
        if int(m.group(1)) < first or m.group(2) in skip: continue
        ni, nf = arity.get(m.group(2), (4, 0))
        r = [v.strip(",") for v in m.group(3).split()]
        out.append("[hook] %s(%s)" % (m.group(2), " ".join(val(r[i]) if i < min(ni, 4) else "-" for i in range(4))))
print("\n".join(out))
