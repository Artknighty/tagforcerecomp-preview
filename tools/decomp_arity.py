#!/usr/bin/env python3
"""How many arguments each function a generated module calls takes, for tools/decomp_calls.sh (registers beyond the real arguments hold stale values
and must not be compared). Usage: python tools/decomp_arity.py <module>   (writes gen/arity_<module>.txt: "<address> <integer args> <float args>")
- the module's own functions: the parameters of their Ghidra pseudo-C declaration (gen/ferramentas_decomp/export/<module>/<module>.c).
- the imports (stubs of the module): for the engine (libehsys) the parameters of the Ghidra declaration of the engine function in BOOT.c, which Ghidra
  derives from what the function itself reads; four integer arguments for the others."""
import re, sys, os
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
mod = sys.argv[1]
exp = os.path.join(root, "gen", "ferramentas_decomp", "export")

def declared(name, text):
    """(integer args, float args) from the Ghidra declaration of a function in the text, None when it is not there"""
    m = re.search(r"^// ==== %s @ [0-9a-f]{8} ====\n(.*?)^\{" % re.escape(name), text, re.S | re.M)
    if not m: return None
    d = m.group(1)
    pm = re.search(re.escape(name) + r"\s*\((.*?)\)\s*$", d[d.rfind(name):], re.S)
    if not pm or pm.group(1).strip() in ("", "void"): return (0, 0)
    ints = floats = 0
    for part in pm.group(1).split(","):
        part = part.strip()
        if not part: continue
        if re.match(r"^(float|double)\b", part): floats += 1
        else: ints += 1
    return (ints, floats)

out = {}
ghidra = open(os.path.join(exp, mod, mod + ".c"), encoding="utf-8", errors="replace").read()
for m in re.finditer(r"^// ==== (\S+) @ ([0-9a-f]{8}) ====", ghidra, re.M):
    d = declared(m.group(1), ghidra)
    out[m.group(2)] = d if d else (4, 0)
gen = open(os.path.join(root, "gen", mod + ".c"), encoding="utf-8", errors="replace").read()
boot = open(os.path.join(exp, "BOOT", "BOOT.c"), encoding="utf-8", errors="replace").read()
for m in re.finditer(r'\{0x([0-9a-f]+), 0x([0-9a-f]+), "(\w+)", "(\w+)"\}', gen):
    stub, nid, lib = m.group(1), m.group(2), m.group(3)
    key = "%08x" % int(stub, 16)
    d = declared("libehsys_" + nid.upper(), boot) if lib == "libehsys" else None
    out[key] = d if d else (4, 0)
with open(os.path.join(root, "gen", "arity_%s.txt" % mod), "w") as f:
    for k in sorted(out): f.write("%s %d %d\n" % (k, out[k][0], out[k][1]))
print("%d functions" % len(out))
