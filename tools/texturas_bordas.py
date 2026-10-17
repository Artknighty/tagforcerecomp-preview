#!/usr/bin/env python3
"""Corrige o retangulo escuro nas bordas das pecas dos retratos (mikeymk/characters/combinations/*).

As pecas (olhos, boca, topo da cabeca...) sao retangulos opacos que o jogo encaixa no corpo. Na ampliacao por IA os 2 px externos de cada
lado ficaram mais escuros que o interior (a IA nao ve vizinhos fora da imagem); encaixada no corpo, a borda vira um retangulo fino
visivel na testa/cabelo/gola. Complementa texturas_buracos.py (que cuida do buraco do corpo).

Por lado da imagem com maioria opaca: se os 2 texels externos diferem do interior (4..5 px adentro) em mais de LIMIAR, eles sao
substituidos pela linha/coluna imediatamente interna (indice 2), so onde o alfa e opaco.

Uso: python tools/texturas_bordas.py <pasta ou png> [--aplicar] [--backup <pasta>]
"""
import sys, os, glob
import numpy as np
from PIL import Image

LARGURA = 6      # texels da borda contaminados pelo ampliador (os 2 primeiros sempre; os demais so se a cor difere pouco: contorno de verdade fica)
SEMPRE = 2
TOLERA = 150
LIMIAR = 12


def corrigir(a):
    """a: array HxWx4 (int). Devolve (novo array, lados corrigidos)."""
    out = a.copy(); lados = []
    rgb = a[..., :3].sum(2); al = a[..., 3]
    ys, xs = np.nonzero(al >= 250)
    if len(ys) == 0: return out, lados
    # caixa opaca da peca (as pecas comecam no meio da imagem, ex.: olhos a partir da linha 128)
    y0, y1, x0, x1 = ys.min(), ys.max(), xs.min(), xs.max()
    if y1 - y0 < 16 or x1 - x0 < 16: return out, lados
    L = LARGURA
    X = slice(x0, x1 + 1); Y = slice(y0, y1 + 1)
    casos = (   # nome, linha/coluna da borda (k=0), passo para dentro, e se e linha (True) ou coluna
        ('topo', y0, +1, True), ('base', y1, -1, True), ('esq', x0, +1, False), ('dir', x1, -1, False))
    for nome, b, p, linha in casos:
        def sel(k): return (b + p * k, X) if linha else (Y, b + p * k)
        borda = np.mean([rgb[sel(k)] for k in range(L)], axis=0)
        inter = np.mean([rgb[sel(L + 2 + k)] for k in range(L)], axis=0)
        opaco = np.all([al[sel(k)] >= 250 for k in range(L + 1)], axis=0)
        if opaco.mean() < 0.5 or np.abs(borda - inter)[opaco].mean() <= LIMIAR: continue
        lados.append(nome)
        ref = a[sel(L)][..., :3]
        for k in range(L):
            ok = opaco if k < SEMPRE else opaco & (np.abs(a[sel(k)][..., :3] - ref).sum(1) <= TOLERA)
            out[sel(k)][ok, :3] = ref[ok]
    return out, lados


def dilatar_rgb(a, passos=12):
    """RGB dos texels de alfa 0 = cor do vizinho opaco mais proximo (o filtro bilinear mistura o RGB dos texels transparentes; branco/lixo vira franja clara)."""
    out = a.copy(); pronto = a[..., 3] > 0
    for _ in range(passos):
        novo = pronto.copy()
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            viz = np.roll(np.roll(pronto, dy, 0), dx, 1); cor = np.roll(np.roll(out, dy, 0), dx, 1)
            m = ~novo & viz
            out[m, :3] = cor[m, :3]; novo |= m
        pronto = novo
    return out


def main():
    args = [x for x in sys.argv[1:] if not x.startswith('--')]
    aplicar = '--aplicar' in sys.argv
    backup = sys.argv[sys.argv.index('--backup') + 1] if '--backup' in sys.argv else None
    if backup and backup in args: args.remove(backup)
    alvo = args[0]
    arqs = [alvo] if alvo.lower().endswith('.png') else glob.glob(os.path.join(alvo, '**', '*.png'), recursive=True)
    n = 0
    for f in sorted(arqs):
        a = np.array(Image.open(f).convert('RGBA')).astype(int)
        novo, lados = corrigir(a)
        novo = dilatar_rgb(novo)
        if (novo[..., :3] == a[..., :3]).all(): continue
        lados = lados or ['rgb']
        n += 1
        if aplicar:
            if backup:
                dst = os.path.join(backup, os.path.relpath(f, alvo)); os.makedirs(os.path.dirname(dst), exist_ok=True)
                if not os.path.exists(dst): Image.fromarray(a.astype(np.uint8), 'RGBA').save(dst)
            Image.fromarray(novo.astype(np.uint8), 'RGBA').save(f)
        elif n <= 10: print(f, lados)
    print(f'{n} de {len(arqs)} PNGs {"corrigidos" if aplicar else "precisam de correcao"}')


if __name__ == '__main__':
    main()
