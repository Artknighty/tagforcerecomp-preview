#!/usr/bin/env python3
"""Acha PISCADAS numa gravacao continua de quadros apresentados (TF_SEQ_AUTO=<arquivo.bin>, ver runtime/capturas.inc).

Uma piscada e um padrao A-B-A: o quadro antes e o quadro depois do bloco B (1 a MAXB saidas) sao quase iguais entre si, mas o bloco difere
de ambos. Funciona para qualquer duracao e em qualquer ponto da sessao, o que a caixa-preta de 1,5 s do F8 nao cobre.

Uso: python tools/piscadas_analisar.py <arquivo.bin | base_da_tela --tela> [--png pasta] [--min 60] [--max-pares 40]
  --min N      pixels (de 160x90) que precisam diferir para contar como 'grande' (padrao 60)
  --png pasta  grava A | B | A de cada piscada (ate --max-pares) para ver a olho
Para gerar o arquivo num replay: TF_SEQ_AUTO=gen/seq.bin TF_SEQ_RING=600 TF_RES=1 PACE=1 sh tools/replay.sh ...
"""
import sys, os, struct
import numpy as np


def ler(path):
    with open(path, 'rb') as f:
        assert f.readline() == b'TFSEQ1\n'
        w, h = struct.unpack('<ii', f.read(8)); n = w * h * 3
        meta, th = [], []
        while True:
            hd = f.read(16)
            if len(hd) < 16: break
            tag, susp = chr(hd[0]), hd[1]; ms, = struct.unpack('<f', hd[4:8]); fr, = struct.unpack('<Q', hd[8:16])
            b = f.read(n)
            if len(b) < n: break
            meta.append((tag, susp, ms, fr)); th.append(np.frombuffer(b, np.uint8).reshape(h, w, 3))
    return meta, np.stack(th)


def ler_tela(base):
    """gravacao do tools/tela_gravar (o que chegou ao MONITOR): miniaturas 480x270 -> 160x90; meta = (tag, acumulados, ms, indice)"""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import tela_analisar as ta
    meta, rec, W, h, sw = ta.ler(base)
    T = np.empty((len(meta), 90, 160, 3), np.uint8); out = []
    for i in range(len(meta)):
        m = np.asarray(rec[i, sw:]).reshape(270, 480, 3).astype(np.uint16)
        T[i] = (m.reshape(90, 3, 160, 3, 3).sum(axis=(1, 3)) // 9).astype(np.uint8)
        out.append(('I', 1 if meta[i][3] > 1 else 0, meta[i][1], meta[i][0]))
    return out, T


def dist(T, i, j):
    """pixels que mudaram (> 24 em algum canal) entre os quadros i e j (vetorizado: i e j sao arrays)"""
    return (np.abs(T[i].astype(np.int16) - T[j].astype(np.int16)).max(axis=3) > 24).sum(axis=(1, 2))


def main():
    a = sys.argv[1]; grande = int(sys.argv[sys.argv.index('--min') + 1]) if '--min' in sys.argv else 60
    pasta = sys.argv[sys.argv.index('--png') + 1] if '--png' in sys.argv else None
    maxp = int(sys.argv[sys.argv.index('--max-pares') + 1]) if '--max-pares' in sys.argv else 40
    meta, T = (ler_tela(a[:-4] if a.endswith('.tsv') else a) if '--tela' in sys.argv else ler(a)); F = len(T); print(f'{F} quadros, {meta[-1][2] / 1000:.1f} s')
    MAXB = 8; achados = {}
    for b in range(1, MAXB + 1):
        i = np.arange(1, F - b)            # primeiro quadro do bloco B
        esq, dir_, fim = i - 1, i + b, i + b - 1
        for lo in range(0, len(i), 2000):
            s = slice(lo, lo + 2000)
            ok = dist(T, esq[s], dir_[s]) <= max(8, grande // 6)            # A_esq ~ A_dir
            if not ok.any(): continue
            ii = i[s][ok]
            d1 = dist(T, ii - 1, ii); d2 = dist(T, ii + b - 1, ii + b)      # B difere dos dois lados
            for q, x, y in zip(ii, d1, d2):
                if x >= grande and y >= grande:
                    k = int(q)
                    if k not in achados or achados[k][0] > b: achados[k] = (b, int(x), int(y))
    # junta achados vizinhos (o mesmo evento aparece com varios b)
    ev = []
    for k in sorted(achados):
        b, x, y = achados[k]
        if ev and k <= ev[-1][0] + ev[-1][1]: continue
        ev.append((k, b, x, y))
    print(f'{len(ev)} piscada(s) (>= {grande} px de 14400, bloco de 1 a {MAXB} saidas)')
    for n, (k, b, x, y) in enumerate(ev):
        tags = ''.join(meta[j][0] for j in range(k, k + b)); fr = meta[k][3]
        print(f'  #{n + 1:3d} saida {k:6d}  t={meta[k][2] / 1000:7.2f}s  quadro {fr}  bloco {b} [{tags}]  difere {x}/{y} px  susp={meta[k][1]}')
        if pasta and n < maxp:
            from PIL import Image
            os.makedirs(pasta, exist_ok=True)
            im = np.concatenate([T[k - 1], T[k], T[k + b - 1] if b > 1 else T[k], T[k + b]], axis=1)
            Image.fromarray(im).resize((im.shape[1] * 3, im.shape[0] * 3), Image.NEAREST).save(os.path.join(pasta, f'{n + 1:03d}_q{fr}_{tags}.png'))


if __name__ == '__main__': main()
