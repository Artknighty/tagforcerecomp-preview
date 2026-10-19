#!/usr/bin/env python3
"""Corrige a costura dos retratos montados em pecas (mikeymk/characters/combinations/*).

O jogo monta o retrato com varias pecas: o corpo (body.png) tem um BURACO retangular onde encaixa a peca da cabeca.
Na arte ampliada a borda do buraco ficou suavizada (alfa parcial) e com a cor contaminada pelo preenchimento branco do buraco;
como a peca comeca exatamente na borda, o suavizado que cai FORA dela deixa o fundo aparecer: uma linha clara fina ao redor.

Detecta as bordas RETAS e LONGAS do buraco (alinhadas a grade de texels do PSP; contornos de silhueta sao curvos): em cada linha/coluna
a rampa de alfa entre arte opaca e transparencia total. Os texels da rampa que ficam do lado da arte viram opacos com a cor do texel
limpo vizinho; os do lado do buraco viram transparentes.

Uso: python tools/mods/texturas_buracos.py <pasta ou png> [--aplicar]   (sem --aplicar so relata; com --aplicar grava o PNG)
"""
import sys, os, glob
import numpy as np
from PIL import Image

MIN_RUN = 48     # linhas seguidas com a mesma borda para contar como borda reta de buraco
FAIXA = 9        # texels junto do buraco com cor escurecida pelo ampliador (a referencia limpa fica FAIXA texels adentro)
COBRIR = False   # alfa 255 nos 4 texels do buraco junto da borda (a peca desenha por cima)
MIN_HOLE = 32    # largura minima da transparencia depois da rampa


def rampas(a):
    """Para cada linha de a: lista (y, i, j, lado) com a rampa [i, j) entre arte opaca e transparencia; lado = +1 buraco a direita, -1 a esquerda."""
    h, w = a.shape; out = []
    op = a >= 250; zero = a == 0
    for y in range(h):
        row = a[y]
        d = np.flatnonzero(op[y, :-1] & ~op[y, 1:]) + 1      # i = primeiro texel nao opaco
        for i in d:
            j = i
            while j < w and 0 < row[j] < 250: j += 1
            if j - i > 4 or j >= w - MIN_HOLE: continue
            if zero[y, j:j + MIN_HOLE].all(): out.append((y, int(i), int(j), +1))
        e = np.flatnonzero(~op[y, :-1] & op[y, 1:]) + 1      # e = primeiro texel opaco depois da rampa
        for k in e:
            i = k
            while i > 0 and 0 < row[i - 1] < 250: i -= 1
            if k - i > 4 or i < MIN_HOLE: continue
            if zero[y, i - MIN_HOLE:i].all(): out.append((y, int(i), int(k), -1))
    return out


def corrige_eixo(arr):
    """Corrige as bordas verticais (rampas por linha). Devolve quantos texels mudou."""
    a = arr[:, :, 3].astype(int); n = 0
    # encadeia as rampas de linhas vizinhas com a borda no mesmo lugar (+-1 texel): uma borda reta e longa de buraco
    ativos = []; runs = []
    for (y, i, j, lado) in rampas(a):
        for r in ativos:
            if r['lado'] == lado and r['ult'] == y - 1 and abs(r['li'] - i) <= 1 and abs(r['lj'] - j) <= 1:
                r['lst'].append((y, i, j)); r['ult'] = y; r['li'] = i; r['lj'] = j; break
        else:
            r = {'lado': lado, 'ult': y, 'li': i, 'lj': j, 'lst': [(y, i, j)]}; ativos.append(r); runs.append(r)
        ativos = [r for r in ativos if r['ult'] >= y - 1]
    for r in runs:
        if len(r['lst']) < MIN_RUN: continue
        lado = r['lado']
        for (y, i, j) in r['lst']:
            passo = -1 if lado == +1 else 1                          # de dentro da rampa para dentro da arte
            ini = i - 1 if lado == +1 else j                         # primeiro texel opaco do lado da arte
            ref = arr[y, ini + FAIXA * passo, :3].astype(int) if 0 <= ini + FAIXA * passo < arr.shape[1] else None
            if ref is not None:                                      # os 3 texels junto da rampa tem a cor 'tremida' do ampliador: usa a de dentro quando difere pouco (contorno de verdade nao muda)
                for m in range(FAIXA - 1):
                    x = ini + m * passo
                    if abs(arr[y, x, :3].astype(int) - ref).sum() <= 150 and arr[y, x, 3] >= 250: arr[y, x, :3] = ref
            cor = arr[y, ini, :3].copy()                             # cor limpa do lado da arte
            for x in range(i, j):
                if a[y, x] >= 128: arr[y, x, :3] = cor; arr[y, x, 3] = 255
                else: arr[y, x, :3] = cor; arr[y, x, 3] = 255 if COBRIR else 0
                n += 1
            # cor da arte tambem nos primeiros texels transparentes (o filtro bilinear mistura o RGB dos vizinhos de alfa 0)
            for m in range(4):
                x = j + m if lado == +1 else i - 1 - m
                if 0 <= x < arr.shape[1] and arr[y, x, 3] == 0:
                    arr[y, x, :3] = cor; n += 1
                    if COBRIR: arr[y, x, 3] = 255                    # o corpo invade o buraco: se a peca encaixar com uma fresta de fracao de pixel, aparece a cor do corpo e nao o fundo
    return n


def corrige(im):
    arr = np.asarray(im.convert('RGBA')).copy()
    n = corrige_eixo(arr)
    t = np.ascontiguousarray(arr.transpose(1, 0, 2))      # bordas horizontais: transpoe, corrige, volta
    n += corrige_eixo(t)
    arr = np.ascontiguousarray(t.transpose(1, 0, 2))
    return (Image.fromarray(arr, 'RGBA') if n else None), n


def main():
    alvo = sys.argv[1]; aplicar = '--aplicar' in sys.argv
    arqs = [alvo] if os.path.isfile(alvo) else sorted(glob.glob(os.path.join(alvo, '**', '*.png'), recursive=True))
    tot = 0
    for f in arqs:
        novo, n = corrige(Image.open(f))
        if novo is None: continue
        tot += 1; print(f'{f}: {n} texel(s) corrigido(s)')
        if aplicar: novo.save(f, optimize=True)
    print('arquivos corrigidos:', tot)


if __name__ == '__main__': main()
