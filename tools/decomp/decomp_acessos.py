#!/usr/bin/env python3
"""Lista os acessos a memoria (LB/LH/LW/SB/SH/SW e o tamanho) do codigo GERADO de uma funcao, com o deslocamento literal.
O pseudo-C do Ghidra erra o passo do ponteiro (short* x byte*): o deslocamento e a largura certos estao aqui.
Uso: python tools/decomp/decomp_acessos.py <modulo> <endereco hex>"""
import re
import sys

modulo, addr = sys.argv[1], sys.argv[2].lower().replace("0x", "").rjust(8, "0")
linhas = open(f"gen/{modulo}.c", encoding="utf-8", errors="replace").read().split("\n")
ini = next(i for i, l in enumerate(linhas) if re.match(rf"static void {modulo}_b_{addr}\(CPU \*c, uint32_t e_\) \{{", l) or re.match(rf"void {modulo}_f_{addr}\(CPU \*c\) \{{", l) and "_b_" not in l)
fim = next(i for i in range(ini + 1, len(linhas)) if re.match(rf"^(static )?void {modulo}_[bfe]_[0-9a-f]{{8}}\(CPU \*c(, uint32_t e_)?\) \{{", linhas[i]) and f"_{addr}(" not in linhas[i])
op = re.compile(r"\b(LB|LBU|LH|LHU|LW|SB|SH|SW)\((?:c->r\[(\d+)\]\s*\+\s*(\d+)u|[^)]*)\)")
for i in range(ini, fim):
    l = linhas[i]
    if "0x" in l and "c->r[2] = 0x" in l: print(f"{i+1}: {l.strip()}")
    for m in op.finditer(l):
        print(f"{i+1}: {l.strip()[:150]}")
        break
