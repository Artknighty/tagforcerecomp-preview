"""Analisa uma gravacao do tools/analysis/tela_gravar.cpp (o que chegou ao monitor).

Uso: python tools/analysis/tela_analisar.py <saida_sem_extensao> [--png pasta] [--de i --ate j]

Para cada imagem composta pelo Windows mede, na faixa gravada (canal verde, largura total), o deslocamento (dx, dy) em relacao a
imagem anterior por correlacao de fase com precisao de subpixel, e o resto depois de alinhar (piscada = conteudo que nao e
so o fundo andando). Junta com a hora da apresentacao e AccumulatedFrames (> 1 = quadro do jogo que nunca apareceu).
Resumo: passos por imagem (ideal com 60 FPS: o mesmo passo sempre), passos parados/dobrados/para tras, intervalos entre
imagens e as piores piscadas. --png grava as miniaturas (480x270) das imagens suspeitas e vizinhas.
"""
import sys, os
import numpy as np

def ler(base):
    meta = []; W = H = y0 = y1 = 0
    for ln in open(base + '.tsv', encoding='utf-8'):
        if ln.startswith('#monitor'):
            p = ln.split(); W, H = map(int, p[1].split('x')); y0, y1 = int(p[4]), int(p[5]); continue
        if ln.startswith('#'): continue
        i, ta, tp, acc = ln.split('\t'); meta.append((int(i), float(ta), float(tp), int(acc)))
    sw = W * (y1 - y0); tw = 480 * 270 * 3
    raw = np.memmap(base + '.bin', dtype=np.uint8, mode='r')
    n = min(len(meta), raw.size // (sw + tw))
    rec = raw[:n * (sw + tw)].reshape(n, sw + tw)
    return meta[:n], rec, W, y1 - y0, sw

def faixa(rec, i, W, h, sw):
    return rec[i, :sw].reshape(h, W).astype(np.float32)

def mini(rec, i, sw):
    return rec[i, sw:].reshape(270, 480, 3)

def fase(a, b, win):
    """deslocamento de b em relacao a a (b = a deslocado de (dx, dy))"""
    A = np.fft.rfft2((a - a.mean()) * win); B = np.fft.rfft2((b - b.mean()) * win)
    R = B * np.conj(A); R /= np.abs(R) + 1e-6
    c = np.fft.irfft2(R, s=a.shape)
    py, px = np.unravel_index(np.argmax(c), c.shape)
    def sub(c0, cm, cp):
        d = cm - 2 * c0 + cp
        return 0.0 if abs(d) < 1e-9 else 0.5 * (cm - cp) / d
    h, w = c.shape
    dy = py + sub(c[py, px], c[(py - 1) % h, px], c[(py + 1) % h, px])
    dx = px + sub(c[py, px], c[py, (px - 1) % w], c[py, (px + 1) % w])
    if dy > h / 2: dy -= h
    if dx > w / 2: dx -= w
    return dx, dy, float(c[py, px])

def resto(a, b, dx, dy):
    """diferenca media depois de alinhar b sobre a pelo deslocamento inteiro mais proximo (bordas fora)"""
    ix, iy = int(round(dx)), int(round(dy)); h, w = a.shape; m = 24
    y0, y1, x0, x1 = m + max(0, -iy), h - m - max(0, iy), m + max(0, -ix), w - m - max(0, ix)
    if y1 - y0 < 8 or x1 - x0 < 8: return 999.0
    return float(np.abs(a[y0:y1, x0:x1] - b[y0 + iy:y1 + iy, x0 + ix:x1 + ix]).mean())

def main():
    base = sys.argv[1]; png = None; de = 0; ate = None
    if '--png' in sys.argv: png = sys.argv[sys.argv.index('--png') + 1]
    if '--de' in sys.argv: de = int(sys.argv[sys.argv.index('--de') + 1])
    if '--ate' in sys.argv: ate = int(sys.argv[sys.argv.index('--ate') + 1])
    meta, rec, W, h, sw = ler(base)
    n = len(meta) if ate is None else min(ate, len(meta))
    win = np.outer(np.hanning(h), np.hanning(W)).astype(np.float32)
    linhas = []; prev = faixa(rec, de, W, h, sw)
    for i in range(de + 1, n):
        cur = faixa(rec, i, W, h, sw)
        dx, dy, pk = fase(prev, cur, win)
        r = resto(prev, cur, dx, dy)
        dt = meta[i][2] - meta[i - 1][2]
        linhas.append((i, meta[i][2], dt, meta[i][3], dx, dy, pk, r))
        prev = cur
    out = base + '_analise.tsv'
    with open(out, 'w', encoding='utf-8') as f:
        f.write('img\tms_apres\tintervalo\tacumulados\tdx\tdy\tpico\tresto\n')
        for l in linhas: f.write('%d\t%.2f\t%.2f\t%d\t%.2f\t%.2f\t%.3f\t%.2f\n' % l)
    if not linhas: print('nada'); return
    a = np.array([[l[2], l[3], l[4], l[5], l[7]] for l in linhas])
    passo = np.hypot(a[:, 2], a[:, 3])
    med = float(np.median(passo[passo > 0.3])) if (passo > 0.3).any() else 0.0
    print('imagens %d, intervalo medio %.2f ms (mediana %.2f, min %.2f, max %.2f)' % (len(linhas) + 1, a[:, 0].mean(), np.median(a[:, 0]), a[:, 0].min(), a[:, 0].max()))
    print('acumulados >1 (quadros do jogo que nao apareceram): %d imagens, %d quadros' % ((a[:, 1] > 1).sum(), int((a[:, 1] - 1)[a[:, 1] > 1].sum())))
    print('passo mediano %.2f px; parados (<0.3*med) %d; dobrados (>1.5*med) %d; para tras %d' % (med, (passo < 0.3 * med).sum(), (passo > 1.5 * med).sum(),
          ((a[:, 2] * np.sign(np.median(a[:, 2])) < -0.3 * med) | (a[:, 3] * np.sign(np.median(a[:, 3])) < -0.3 * med)).sum() if med else 0))
    hist = {}
    for p in passo: k = round(p / med * 2) / 2 if med else 0; hist[k] = hist.get(k, 0) + 1
    print('passos em multiplos do mediano:', dict(sorted(hist.items())))
    rs = a[:, 4]; lim = max(4.0, float(np.median(rs)) * 4)
    sus = [l for l in linhas if l[7] > lim]
    print('resto mediano %.2f; piscadas (resto > %.1f): %d' % (float(np.median(rs)), lim, len(sus)))
    for l in sorted(sus, key=lambda l: -l[7])[:15]: print('  img %d t=%.1f dt=%.1f acc=%d dx=%.2f dy=%.2f resto=%.1f' % (l[0], l[1], l[2], l[3], l[4], l[5], l[7]))
    print('detalhe em', out)
    if png:
        from PIL import Image
        os.makedirs(png, exist_ok=True)
        idx = set()
        for l in sus[:20]:
            for k in (l[0] - 1, l[0], l[0] + 1):
                if 0 <= k < len(meta): idx.add(k)
        for k in sorted(idx): Image.fromarray(np.ascontiguousarray(mini(rec, k, sw))).save(os.path.join(png, 'img_%05d.png' % k))
        print('miniaturas em', png)

main()
