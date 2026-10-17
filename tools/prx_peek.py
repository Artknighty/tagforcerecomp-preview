#!/usr/bin/env python3
"""Show the initial bytes of a module's data: python tools/prx_peek.py <module> <hex address> [count] [mode]
module = name of a gmodule/*.prx (libfield); the address is absolute (the module's base from tools/modules.tsv is subtracted).
mode: w (32-bit words, default; values that look like module offsets are shown with the base added as @address), h (16-bit), b (bytes), s (a C string)."""
import os, struct, sys
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
mod, addr = sys.argv[1], int(sys.argv[2], 16)
count = int(sys.argv[3], 0) if len(sys.argv) > 3 else 16
mode = sys.argv[4] if len(sys.argv) > 4 else "w"
base = None
for line in open(os.path.join(root, "tools", "modules.tsv"), encoding="utf-8"):
    f = line.split("\t")
    if f and f[0] == mod: base = int(f[1], 16)
if base is None: sys.exit("module not in tools/modules.tsv")
data = open(os.path.join(root, "gen", "ferramentas_decomp", "modulos", "gmodule", mod + ".prx"), "rb").read()
phoff, = struct.unpack_from("<I", data, 0x1C); phentsize, phnum = struct.unpack_from("<HH", data, 0x2A)
segs = []
for i in range(phnum):
    t, off, va, pa, fsz, msz, fl, al = struct.unpack_from("<8I", data, phoff + i * phentsize)
    if t == 1: segs.append((va, off, fsz, msz))
memsz = max(va + msz for va, off, fsz, msz in segs)
def byte(a):
    rel = a - base
    for va, off, fsz, msz in segs:
        if va <= rel < va + msz: return data[off + rel - va] if rel - va < fsz else 0
    raise SystemExit("address outside the module image: %08x" % a)
def read(a, n): return bytes(byte(a + i) for i in range(n))
if mode == "s":
    out = bytearray(); a = addr
    while byte(a) != 0 and len(out) < 4096: out.append(byte(a)); a += 1
    print(out.decode("latin-1")); sys.exit()
size = {"w": 4, "h": 2, "b": 1}[mode]
fmt = {"w": "<I", "h": "<H", "b": "<B"}[mode]
per = {"w": 4, "h": 8, "b": 16}[mode]
for i in range(0, count, per):
    row = []
    for j in range(i, min(i + per, count)):
        v, = struct.unpack(fmt, read(addr + j * size, size))
        row.append(("%0*x" % (size * 2, v)) + ("@%08x" % (v + base) if mode == "w" and 0 < v < memsz else ""))
    print("%08x: %s" % (addr + i * size, " ".join(row)))
