"""Metricas do 60 FPS (quadro do meio, runtime/interp.cpp) sobre capturas de uma reproducao.

Gere as capturas com o replay gravando TODOS os quadros reais e intermediarios do trecho, ex. (1x basta):
  L=$(python -c "print(','.join(map(str,range(1400,2131))))")
  TF_INTERP_SNAP=$L SNAPS=$L ENDF=2131 TF_GPU=2 TF_RES=1 TF_MSAA=1 TAG=_m sh tools/replay.sh <sessao>
  -> gen/replay/<sessao>_m/gen/snap_<vb>.bmp (real) e interp_<vb>.bmp (quadro do meio)

Uso:
  python tools/interp_metrics.py piscadas  <pasta gen> <de> <ate> [topo]   pixel em que os dois quadros reais vizinhos CONCORDAM e o meio
                                                                          difere muito (pedaco que aparece/some: olhos sob os oculos).
                                                                          Robusta a movimento grande. A principal.
  python tools/interp_metrics.py inventados <pasta gen> <de> <ate> [topo]  cor do meio que nao existe (janela 3 px) em nenhum vizinho:
                                                                          acha rasgos/texturas erradas, mas da FALSO POSITIVO com
                                                                          movimento grande (camera de cut-in a 35 px/quadro)
  python tools/interp_metrics.py mostrar   <pasta gen> <vb> <saida.png>     anterior | meio / proximo | pixels de 'piscadas' em magenta
  python tools/interp_metrics.py autoteste <log.txt> [de ate]              resumo do TF_INTERP_SELFTEST=1 TF_INTERP_SELFTEST_V=1 (erro x hold)
Compare sempre A/B (mesma reproducao com e sem a mudanca): os numeros absolutos dependem da cena.
"""
import os, re, sys
import numpy as np
from PIL import Image


def ld(p):
    return np.asarray(Image.open(p).convert('RGB')).astype(np.int16)


def frames(d, a, b):
    for v in range(a, b + 1):
        m, p, n = (os.path.join(d, f) for f in ('interp_%d.bmp' % v, 'snap_%d.bmp' % (v - 1), 'snap_%d.bmp' % (v + 1)))
        if os.path.exists(m) and os.path.exists(p) and os.path.exists(n):
            yield v, ld(p), ld(m), ld(n)


def flick_mask(A, M, B):
    return (np.abs(A - B).max(axis=2) < 24) & (np.abs(M - A).max(axis=2) > 64) & (np.abs(M - B).max(axis=2) > 64)


def near(m, ref, R=3):
    H, W, _ = m.shape; best = np.full((H, W), 999, np.int16); pad = np.pad(ref, ((R, R), (R, R), (0, 0)), mode='edge')
    for dy in range(-R, R + 1):
        for dx in range(-R, R + 1):
            best = np.minimum(best, np.abs(m - pad[R + dy:R + dy + H, R + dx:R + dx + W]).max(axis=2))
    return best


def report(res, top):
    res.sort(reverse=True)
    print('total %d px em %d quadros do meio (%d com >50 px)' % (sum(r[0] for r in res), len(res), sum(1 for r in res if r[0] > 50)))
    for r in res[:top]:
        print('  vb %d: %d px  caixa %s' % (r[1], r[0], r[2]))


def main():
    if len(sys.argv) < 3:
        print(__doc__); return
    cmd = sys.argv[1]
    if cmd in ('piscadas', 'inventados'):
        d, a, b = sys.argv[2], int(sys.argv[3]), int(sys.argv[4]); top = int(sys.argv[5]) if len(sys.argv) > 5 else 15; res = []
        for v, A, M, B in frames(d, a, b):
            bad = flick_mask(A, M, B) if cmd == 'piscadas' else (near(M, A) > 60) & (near(M, B) > 60)
            n = int(bad.sum()); box = '-'
            if n:
                ys, xs = np.nonzero(bad); box = '%d,%d-%d,%d' % (xs.min(), ys.min(), xs.max(), ys.max())
            res.append((n, v, box))
        report(res, top)
    elif cmd == 'mostrar':
        d, v, out = sys.argv[2], int(sys.argv[3]), sys.argv[4]
        A, M, B = ld(os.path.join(d, 'snap_%d.bmp' % (v - 1))), ld(os.path.join(d, 'interp_%d.bmp' % v)), ld(os.path.join(d, 'snap_%d.bmp' % (v + 1)))
        K = (M // 2).copy(); K[flick_mask(A, M, B)] = [255, 0, 255]
        H, W, _ = A.shape; s = np.zeros((H * 2, W * 2, 3), np.uint8); s[:H, :W] = A; s[:H, W:] = M; s[H:, :W] = B; s[H:, W:] = K
        Image.fromarray(s).save(out); print('ok:', out)
    elif cmd == 'autoteste':
        rows = []
        for l in open(sys.argv[2], errors='replace'):
            m = re.match(r'\[selftest\] vb (\d+): erro ([\d.]+)% \(hold ([\d.]+)%\)', l)
            if m: rows.append((int(m[1]), float(m[2]), float(m[3])))
        if len(sys.argv) > 4: rows = [r for r in rows if int(sys.argv[3]) <= r[0] <= int(sys.argv[4])]
        if not rows: print('sem linhas [selftest] vb (use TF_INTERP_SELFTEST=1 TF_INTERP_SELFTEST_V=1)'); return
        e = sum(r[1] for r in rows) / len(rows); h = sum(r[2] for r in rows) / len(rows)
        print('%d quadros: erro %.3f%%, hold %.3f%% (razao %.3f; menor = melhor; hold = mostrar o quadro novo, como no 30 fps)' % (len(rows), e, h, e / max(h, 1e-9)))
    else:
        print(__doc__)


if __name__ == '__main__':
    main()
