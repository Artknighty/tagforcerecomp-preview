#!/usr/bin/env python3
"""Print the generated C body of one function: python tools/gen_fn.py gen/<module>.c <module_prefix> <hex address> [more addresses]
(the generated file names functions <prefix>_f_<address> and the bodies <prefix>_b_<address> when a function has secondary entries)."""
import re, sys
path, prefix = sys.argv[1], sys.argv[2]
src = open(path, encoding="utf-8", errors="replace").read()
for a in sys.argv[3:]:
    a = a.lower().replace("0x", "").zfill(8)
    m = re.search(r"^(?:static )?void %s_[fb]_%s\(CPU \*c(?:, uint32_t e_)?\) \{\n(.*?)^\}\n" % (re.escape(prefix), a), src, re.S | re.M)
    print("=" * 8, a)
    print(m.group(0) if m else "(not found)")
