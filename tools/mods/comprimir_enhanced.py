#!/usr/bin/env python3
"""Reduz o mod `gen/mods/EnhancedTextures` para caber em menos disco: limita o maior lado a 2048 px (Lanczos) e comprime cada PNG.

Compressao: paleta de 256 cores so quando o resultado fica praticamente igual (PSNR ponderado pelo alfa >= --psnr, padrao 38 dB);
senao PNG RGBA otimizado, sem perda. Escreve em uma pasta nova (padrao `gen/mods/EnhancedTextures_2k`), sem tocar na original;
o filme e os arquivos de texto vao por hardlink/copia. Retoma de onde parou. Uso:
  python tools/mods/comprimir_enhanced.py [--lado 2048] [--psnr 38] [--saida pasta] [--jobs N]
"""
import argparse
import concurrent.futures as cf
import io
import os
import shutil
import sys

import numpy as np
from PIL import Image

RAIZ = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
ORIGEM = os.path.join(RAIZ, "gen", "mods", "EnhancedTextures")


def psnr(a, b):
    w = (a[..., 3:4].astype(np.float32) + b[..., 3:4]) / 510
    d = (a.astype(np.float32) - b) ** 2 * np.concatenate([w, w, w, np.ones_like(w)], 2)
    m = float(d.mean())
    return 99.0 if m == 0 else 10 * np.log10(255.0 ** 2 / m)


def processa(args):
    src, dst, lado, lim = args
    if os.path.exists(dst):
        return os.path.getsize(src), os.path.getsize(dst), "pula"
    im = Image.open(src).convert("RGBA")
    if max(im.size) > lado:
        r = lado / max(im.size)
        im = im.resize((max(1, round(im.width * r)), max(1, round(im.height * r))), Image.LANCZOS)
    modo = "sem perda"
    saida = io.BytesIO()
    if min(im.size) >= 4:
        q = im.quantize(256, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE)
        if psnr(np.asarray(im), np.asarray(q.convert("RGBA"))) >= lim:
            q.save(saida, "PNG", optimize=True)
            modo = "paleta"
    if modo == "sem perda":
        im.save(saida, "PNG", optimize=True)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    tmp = dst + ".tmp"
    with open(tmp, "wb") as f:
        f.write(saida.getvalue())
    os.replace(tmp, dst)
    return os.path.getsize(src), len(saida.getvalue()), modo


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lado", type=int, default=2048)
    ap.add_argument("--psnr", type=float, default=38.0)
    ap.add_argument("--saida", default=os.path.join(RAIZ, "gen", "mods", "EnhancedTextures_2k"))
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 4) - 2))
    a = ap.parse_args()
    tarefas = []
    for dp, _, fs in os.walk(ORIGEM):
        for f in fs:
            s = os.path.join(dp, f)
            rel = os.path.relpath(s, ORIGEM)
            d = os.path.join(a.saida, rel)
            if f.lower().endswith(".png"):
                tarefas.append((s, d, a.lado, a.psnr))
            elif not os.path.exists(d):
                os.makedirs(os.path.dirname(d), exist_ok=True)
                try:
                    os.link(s, d)
                except OSError:
                    shutil.copy2(s, d)
    tot = [0, 0]
    cont = {}
    with cf.ProcessPoolExecutor(a.jobs) as ex:
        for i, (o, n, modo) in enumerate(ex.map(processa, tarefas, chunksize=8), 1):
            tot[0] += o
            tot[1] += n
            cont[modo] = cont.get(modo, 0) + 1
            if i % 500 == 0:
                print(f"{i}/{len(tarefas)}  {tot[0] / 2**20:.0f} MB -> {tot[1] / 2**20:.0f} MB  {cont}", flush=True)
    print(f"FIM {len(tarefas)} PNGs  {tot[0] / 2**20:.0f} MB -> {tot[1] / 2**20:.0f} MB  {cont}")


if __name__ == "__main__":
    sys.exit(main())
