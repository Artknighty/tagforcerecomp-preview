#!/usr/bin/env python3
"""Argument registers set before each call of a generated module, per NID/import name.
The Ghidra pseudo-C often hides arguments, so this reads the generated C instead.
Usage: python tools/decomp_args.py <generated .c file>
For every import called, prints how many call sites set r4..r11 (a0..a7) just before the call."""
import re
import sys
from collections import defaultdict

src = open(sys.argv[1], encoding="utf-8", errors="replace").read()
imports = {}
for m in re.finditer(r'\{0x([0-9a-f]+), 0x([0-9a-f]+), "(\w+)", "(\w+)"\}', src):
    imports[int(m.group(1), 16)] = (m.group(2), m.group(4))

sites = defaultdict(list)
for fn in re.finditer(r"^void (\w+)\(CPU \*c\) \{\n(.*?)^\}\n", src, re.S | re.M):
    pending = set()
    for line in fn.group(2).split("\n"):
        regs = set(int(r) for r in re.findall(r"c->r\[(4|5|6|7|8|9|10|11)\] =", line.split("psp_call")[0]))
        pending |= regs
        call = re.search(r"psp_call\(c, 0x([0-9a-f]+)\)", line)
        if call:
            a = int(call.group(1), 16)
            if a in imports:
                sites[imports[a]].append(sorted(pending))
            pending = set()
        elif re.match(r"^L_[0-9a-f]+:;", line.strip()):
            pending = set()

for (nid, name), lst in sorted(sites.items(), key=lambda kv: kv[0][1]):
    top = max((max(p) if p else 3) for p in lst) - 3
    print(f"{name:34s} {nid}  calls={len(lst):3d}  max arg index set={top}   sets: " + " ".join(sorted({",".join(map(str, p)) for p in lst}))[:90])
