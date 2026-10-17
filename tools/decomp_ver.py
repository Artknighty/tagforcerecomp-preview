#!/usr/bin/env python3
"""Mostra o pseudo-C do Ghidra de uma ou mais funcoes de um modulo. Uso: python tools/decomp_ver.py <modulo> <endereco> [<endereco>...]"""
import re
import sys
from pathlib import Path

modulo = sys.argv[1]
src = (Path("gen/ferramentas_decomp/export") / modulo / f"{modulo}.c").read_text(encoding="utf-8", errors="replace")
partes = re.split(r"^// ==== (\S+) @ ([0-9a-f]+) ====$", src, flags=re.M)
funcs = {partes[i + 1]: (partes[i], partes[i + 2]) for i in range(1, len(partes), 3)}
for a in sys.argv[2:]:
    nome, corpo = funcs[a.lower().lstrip("0x").rjust(8, "0")] if a.lower().startswith("0x") else funcs[a.rjust(8, "0")]
    print(f"// ==== {nome} @ {a} ====")
    print(corpo.strip())
