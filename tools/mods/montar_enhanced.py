#!/usr/bin/env python3
"""Monta o mod `gen/mods/EnhancedTextures` (texturas melhoradas por IA) a partir do pacote `gen/mods/TexturasPC`.

Cada PNG do pacote e ampliado com o Real-ESRGAN (`realesrgan-x4plus-anime`, ncnn-vulkan em gen/ferramentas_ai/realesrgan) e reduzido ao
fator pedido (padrao 2x o tamanho do pacote, que e o que um monitor 4K precisa: o pacote ja tem ~4x as texturas do PSP, 8x = 3840x2176).
O canal alfa nao passa pela IA (Lanczos); a cor sob a transparencia recebe a cor vizinha antes, para nao aparecer halo.
Texturas com lado < 32 px (setas 8x8...) a IA nao aceita: saem por Lanczos.

O mod sai completo (indice `textures.ini` igual ao do pacote, mesmos nomes, mais o filme HD por hardlink), para o jogo poder usar
`Textures = Original | Enhanced` (um OU outro). Nao altera o TexturasPC nem os mods de origem. Fica com o arquivo `disabled` ate o usuario validar.

Retoma de onde parou (pula o que ja existe na saida). Uso:
  python tools/mods/montar_enhanced.py [--fator 2] [--max-segundos 540] [--lote-mpx 12] [--so-amostra N] [--saida pasta]
"""
import argparse
import concurrent.futures as cf
import os
import shutil
import subprocess
import sys
import time

import numpy as np
from PIL import Image, ImageFilter

RAIZ = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
PACOTE = os.path.join(RAIZ, "gen", "mods", "TexturasPC")
FERR = os.path.join(RAIZ, "gen", "ferramentas_ai", "realesrgan")
TRAB = os.path.join(RAIZ, "gen", "upscale_trabalho")
MODELO = "realesrgan-x4plus-anime"
NATIVO = 4


def listar(base):
    res = []
    for dp, _, fs in os.walk(base):
        for f in fs:
            if f.lower().endswith(".png"):
                res.append(os.path.relpath(os.path.join(dp, f), base).replace(os.sep, "/"))
    return sorted(res)


def preparar(rel, destino_in, base):
    """Le o PNG, devolve (largura, altura, tem_alfa) e grava a versao RGB com a cor espalhada sob a transparencia."""
    im = Image.open(os.path.join(base, rel))
    tem_alfa = im.mode in ("RGBA", "LA") or (im.mode == "P" and "transparency" in im.info)
    im = im.convert("RGBA")
    w, h = im.size
    arr = np.array(im)
    rgb = arr[:, :, :3].copy()
    a = arr[:, :, 3]
    if tem_alfa and (a < 255).any():
        solido = a > 0
        mascara = Image.fromarray((solido * 255).astype(np.uint8))
        pre = Image.fromarray(rgb)
        for _ in range(6):
            borrado = np.array(pre.filter(ImageFilter.MaxFilter(3)))
            rgb = np.where(solido[:, :, None], rgb, borrado)
            mascara = mascara.filter(ImageFilter.MaxFilter(3))
            solido = np.array(mascara) > 0
            pre = Image.fromarray(rgb)
        tem_alfa = True
    else:
        tem_alfa = False
    Image.fromarray(rgb).save(destino_in, compress_level=1)
    return w, h, tem_alfa


def finalizar(rel, arquivo_out, w, h, tem_alfa, fator, base, saida):
    alvo = (w * fator, h * fator)
    if arquivo_out:
        up = Image.open(arquivo_out).convert("RGB")
        if up.size != alvo:
            up = up.resize(alvo, Image.LANCZOS)
        img = up.convert("RGBA")
    else:  # pequena demais para a IA
        img = Image.open(os.path.join(base, rel)).convert("RGBA").resize(alvo, Image.LANCZOS)
        tem_alfa = False
    if tem_alfa:
        a = Image.open(os.path.join(base, rel)).convert("RGBA").getchannel("A")
        img.putalpha(a.resize(alvo, Image.LANCZOS))
    destino = os.path.join(saida, "textures", rel)
    os.makedirs(os.path.dirname(destino), exist_ok=True)
    tmp = destino + ".tmp"
    img.save(tmp, format="PNG", compress_level=6)
    os.replace(tmp, destino)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fator", type=int, default=2)
    ap.add_argument("--max-segundos", type=float, default=540)
    ap.add_argument("--lote-mpx", type=float, default=12.0)
    ap.add_argument("--so-amostra", type=int, default=0, help="processa so N arquivos (teste)")
    ap.add_argument("--saida", default=os.path.join(RAIZ, "gen", "mods", "EnhancedTextures"))
    a = ap.parse_args()
    base = os.path.join(PACOTE, "textures")
    saida = a.saida
    t_ini = time.time()
    todos = listar(base)
    if a.so_amostra:
        passo = max(1, len(todos) // a.so_amostra)
        todos = todos[::passo][:a.so_amostra]
    pend = [r for r in todos if not os.path.exists(os.path.join(saida, "textures", r))]
    print(f"{len(todos)} texturas, {len(pend)} pendentes", flush=True)
    os.makedirs(saida, exist_ok=True)
    nfeitos = 0
    pool = cf.ThreadPoolExecutor(max_workers=max(2, (os.cpu_count() or 4) - 2))
    i = 0
    while i < len(pend) and time.time() - t_ini < a.max_segundos:
        # junta um lote por orcamento de megapixels de entrada
        lote, mpx = [], 0.0
        while i < len(pend) and (not lote or mpx < a.lote_mpx) and len(lote) < 300:
            w, h = Image.open(os.path.join(base, pend[i])).size
            lote.append((pend[i], w, h))
            mpx += w * h / 1e6
            i += 1
        shutil.rmtree(TRAB, ignore_errors=True)
        din, dout = os.path.join(TRAB, "in"), os.path.join(TRAB, "out")
        os.makedirs(din)
        os.makedirs(dout)
        grandes = [(n, r, w, h) for n, (r, w, h) in enumerate(lote) if min(w, h) >= 32]
        futs = {n: pool.submit(preparar, r, os.path.join(din, f"{n}.png"), base) for n, r, w, h in grandes}
        info = {n: f.result() for n, f in futs.items()}
        if grandes:
            r = subprocess.run([os.path.join(FERR, "realesrgan-ncnn-vulkan.exe"), "-i", din, "-o", dout, "-n", MODELO, "-s", str(NATIVO),
                                "-m", os.path.join(FERR, "models"), "-t", "0", "-j", "2:2:2", "-f", "png"],
                               capture_output=True, text=True, creationflags=getattr(subprocess, "BELOW_NORMAL_PRIORITY_CLASS", 0))
            if r.returncode != 0:
                print("ERRO ncnn:", r.stderr[-300:], file=sys.stderr)
        fin = []
        for n, (rel, w, h) in enumerate(lote):
            saida_n = os.path.join(dout, f"{n}.png")
            if n in info and os.path.exists(saida_n):
                fin.append(pool.submit(finalizar, rel, saida_n, info[n][0], info[n][1], info[n][2], a.fator, base, saida))
            elif min(w, h) < 32:
                fin.append(pool.submit(finalizar, rel, None, w, h, False, a.fator, base, saida))
            else:
                print("sem saida da IA (usa Lanczos):", rel, file=sys.stderr)
                fin.append(pool.submit(finalizar, rel, None, w, h, False, a.fator, base, saida))
        for f in fin:
            f.result()
        nfeitos += len(lote)
        dt = time.time() - t_ini
        print(f"  {nfeitos}/{len(pend)} em {dt:.0f}s ({dt / nfeitos:.2f}s/arquivo)", flush=True)
    shutil.rmtree(TRAB, ignore_errors=True)
    restantes = [r for r in todos if not os.path.exists(os.path.join(saida, "textures", r))]
    if restantes:
        print(f"FALTAM {len(restantes)}: rode de novo para continuar")
        return
    # arquivos de apoio: indice igual ao do pacote, origem, filme HD por hardlink, mod.txt
    for nome in ("textures.ini",):
        shutil.copy2(os.path.join(base, nome), os.path.join(saida, "textures", nome))
    if os.path.exists(os.path.join(PACOTE, "origem.tsv")):
        shutil.copy2(os.path.join(PACOTE, "origem.tsv"), os.path.join(saida, "origem.tsv"))
    mv = os.path.join(PACOTE, "movie")
    if os.path.isdir(mv):
        os.makedirs(os.path.join(saida, "movie"), exist_ok=True)
        for f in os.listdir(mv):
            d = os.path.join(saida, "movie", f)
            if not os.path.exists(d):
                try:
                    os.link(os.path.join(mv, f), d)
                except OSError:
                    shutil.copy2(os.path.join(mv, f), d)
    with open(os.path.join(saida, "mod.txt"), "w", encoding="utf-8") as f:
        f.write("name=Enhanced Textures\nauthor=Tag Force Recomp; MikeyMK, Filipe Ramos (PT-BR); ampliado com Real-ESRGAN\n"
                "version=0.1\ndescription=Texturas do pacote base ampliadas %dx por IA (pensado para 4K) + abertura em HD\n" % a.fator)
    with open(os.path.join(saida, "LEIA-ME.txt"), "w", encoding="utf-8") as f:
        f.write("Gerado por tools/mods/montar_enhanced.py a partir de gen/mods/TexturasPC (modelo realesrgan-x4plus-anime, fator %dx).\n"
                "Use este mod OU o TexturasPC (Original), nunca os dois: o indice e o mesmo.\n" % a.fator)
    if not os.path.exists(os.path.join(saida, "disabled")):
        open(os.path.join(saida, "disabled"), "w").close()
    print("pronto:", saida)


if __name__ == "__main__":
    main()
