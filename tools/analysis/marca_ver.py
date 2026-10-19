#!/usr/bin/env python3
"""Converte os quadros grandes de uma marca (tela_continua: <pasta>/marca_N.raw, 960x540, os ultimos ~6 s ANTES da marca + 1 s depois)
em PNGs para ver a olho: <pasta>/marca_N/NNNN_<saida>.png e <pasta>/marca_N_folha_<k>.png (folhas de 5x4 quadros, numero da saida
em amarelo). Apaga o .raw no fim (--manter para guardar).

Uso: python tools/analysis/marca_ver.py <pasta da gravacao> [--manter] [--passo 3]
  --passo n  grava 1 PNG a cada n quadros (padrao 1 = todos; as folhas usam sempre 1 a cada 6)"""
import sys, os, glob, struct
import numpy as np
from PIL import Image, ImageDraw


def main():
    pasta = sys.argv[1]; manter = '--manter' in sys.argv
    passo = int(sys.argv[sys.argv.index('--passo') + 1]) if '--passo' in sys.argv else 1
    for raw in sorted(glob.glob(os.path.join(pasta, 'marca_*.raw'))):
        base = raw[:-4]
        with open(raw, 'rb') as f:
            assert f.readline() == b'TFHI1\n'
            w, h, n = struct.unpack('<iii', f.read(12))
            os.makedirs(base, exist_ok=True)
            quadros = []
            for k in range(n):
                hd = f.read(8)
                if len(hd) < 8: break
                ms, ix = struct.unpack('<fI', hd)
                buf = f.read(w * h * 4)
                if len(buf) < w * h * 4: break
                a = np.frombuffer(buf, np.uint8).reshape(h, w, 4)[:, :, [2, 1, 0]]   # BGRA -> RGB
                quadros.append((ix, ms, a))
                if k % passo == 0: Image.fromarray(a).save(os.path.join(base, '%04d_%d.png' % (k, ix)))
        print('%s: %d quadros %dx%d, saidas %d a %d' % (os.path.basename(raw), len(quadros), w, h, quadros[0][0], quadros[-1][0]))
        sel = quadros[::6]; tw, th = 384, 216
        for fo in range(0, len(sel), 20):
            folha = Image.new('RGB', (tw * 5, th * 4))
            for j, (ix, ms, a) in enumerate(sel[fo:fo + 20]):
                im = Image.fromarray(a).resize((tw, th), Image.BILINEAR); d = ImageDraw.Draw(im); d.rectangle((0, 0, 50, 12), fill=(0, 0, 0)); d.text((2, 0), str(ix), fill=(255, 255, 0))
                folha.paste(im, ((j % 5) * tw, (j // 5) * th))
            folha.save(base + '_folha_%d.png' % (fo // 20))
        if not manter: os.remove(raw)


if __name__ == '__main__': main()
