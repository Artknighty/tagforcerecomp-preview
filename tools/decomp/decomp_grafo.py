#!/usr/bin/env python3
"""Call graph of a Module's own functions from the Ghidra pseudo-C export.
Usage: python tools/decomp/decomp_grafo.py <module> [export_dir]
Prints each own function with the own functions, ehsys, sce* and libc calls it makes, and a leaf-first swap order."""
import re
import sys
from pathlib import Path

mod = sys.argv[1]
root = Path(sys.argv[2]) if len(sys.argv) > 2 else Path("gen/ferramentas_decomp/export")
src = (root / mod / f"{mod}.c").read_text(encoding="utf-8", errors="replace")

parts = re.split(r"^// ==== (\S+) @ ([0-9a-f]+) ====$", src, flags=re.M)
funcs = {}
for i in range(1, len(parts), 3):
    name, addr, body = parts[i], parts[i + 1], parts[i + 2]
    if name.startswith("FUN_") or name.startswith(mod + "_"):
        funcs[name] = (addr, body)

call_re = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(")
KW = {"if", "while", "for", "switch", "return", "sizeof"}
info = {}
for n, (a, body) in funcs.items():
    own, ehsys, sce, other = set(), set(), set(), set()
    for c in call_re.findall(body):
        if c in KW or c == n:
            continue
        if c in funcs:
            own.add(c)
        elif c.startswith("libehsys_"):
            ehsys.add(c[9:])
        elif c.startswith("sce"):
            sce.add(c)
        elif c.startswith("(") or c in ("memset", "memcpy", "sprintf", "strcpy", "strlen", "memcmp", "strcmp", "printf"):
            other.add(c)
    info[n] = (a, own, ehsys, sce, other, body.count("\n"))

# leaf-first order
done, order = set(), []
def visit(n, stack=()):
    if n in done or n in stack:
        return
    for c in sorted(info[n][1]):
        visit(c, stack + (n,))
    done.add(n)
    order.append(n)
for n in sorted(info, key=lambda k: info[k][0]):
    visit(n)

for n in sorted(info, key=lambda k: info[k][0]):
    a, own, e, s, o, ln = info[n]
    print(f"{a} {n} ({ln} lines)")
    if own: print("   own:  ", ", ".join(sorted(own)))
    if e: print("   ehsys:", ", ".join(sorted(e)))
    if s: print("   sce:  ", ", ".join(sorted(s)))
    if o: print("   libc: ", ", ".join(sorted(o)))
print("\nleaf-first order:")
for n in order:
    print(" ", info[n][0], n)
cnt = {}
for n in info:
    for x in info[n][2]:
        cnt[x] = cnt.get(x, 0) + 1
print("\nehsys calls used (functions using it):", len(cnt))
for x, c in sorted(cnt.items(), key=lambda kv: -kv[1]):
    print(f"  {x}: {c}")
