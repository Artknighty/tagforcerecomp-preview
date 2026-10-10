"""Analisa a caixa-preta do F8 (quadros APRESENTADOS antes da marca: runtime/capturas.inc, pasta <sessao>_capturas/seq_<n>/).

Uso: python tools/seq_analisar.py [pasta seq_<n> | ultima]      (ultima = a seq mais recente de gen/logs)

Para cada trio de quadros apresentados consecutivos a, b, c (reais R e intermediarios M do 60 FPS se alternam, cada um 1/60 s):
  - mede o movimento global a->c (correlacao de fase, subpixel) e preve b = a andado METADE disso;
  - 'resto' = diferenca media entre b e a previsao (e entre b e c andado metade para tras; vale a menor);
  - PISCADA = b destoa dos dois vizinhos (resto alto) enquanto a e c concordam entre si (a->c explicado por um deslocamento).
    Ex.: o xadrez do Duel Result invertendo por 1 quadro, um objeto que aparece/some num quadro, textura que troca e volta.
Tambem lista passos parados/dobrados, intervalos irregulares entre saidas e quadros com o 60 FPS suspenso.
Saida: tabela no terminal, <pasta>/analise.tsv e <pasta>/suspeitos.png (a | b | c dos piores casos).
"""
import sys, os, glob
import numpy as np
from PIL import Image


def achar_pasta(arg):
    if arg and arg != 'ultima': return arg
    cands = glob.glob('gen/logs/*_capturas/seq_*') + glob.glob('gen/replay/*/logs/*_capturas/seq_*')
    if not cands: sys.exit('nenhuma pasta seq_* encontrada')
    return max(cands, key=os.path.getmtime)


def ler(pasta):
    rows = []
    for ln in open(os.path.join(pasta, 'indice.tsv'), encoding='utf-8'):
        if ln.startswith('#') or not ln.strip(): continue
        p = ln.rstrip('\n').split('\t')
        rows.append({'i': int(p[0]), 'tag': p[1], 'ms': float(p[2]), 'arq': p[3], 'susp': int(p[4]) if len(p) > 4 else 0, 'quadro': int(p[5]) if len(p) > 5 else -1})
    return rows


def cinza(img, f=2):
    a = np.asarray(img.convert('RGB')).astype(np.float32)
    g = a[..., 0] * 0.3 + a[..., 1] * 0.59 + a[..., 2] * 0.11
    h, w = g.shape; h -= h % f; w -= w % f
    return g[:h, :w].reshape(h // f, f, w // f, f).mean(axis=(1, 3))


def fase(a, b, win):
    A = np.fft.rfft2((a - a.mean()) * win); B = np.fft.rfft2((b - b.mean()) * win)
    R = B * np.conj(A); R /= np.abs(R) + 1e-6
    c = np.fft.irfft2(R, s=a.shape); py, px = np.unravel_index(np.argmax(c), c.shape); h, w = c.shape
    def sub(c0, cm, cp):
        d = cm - 2 * c0 + cp
        return 0.0 if abs(d) < 1e-9 else 0.5 * (cm - cp) / d
    dy = py + sub(c[py, px], c[(py - 1) % h, px], c[(py + 1) % h, px]); dx = px + sub(c[py, px], c[py, (px - 1) % w], c[py, (px + 1) % w])
    if dy > h / 2: dy -= h
    if dx > w / 2: dx -= w
    return dx, dy


def dif(a, b, dx, dy, m=8):
    """media |a(x) - b(x + d)| na area comum (d inteiro)"""
    ix, iy = int(round(dx)), int(round(dy)); h, w = a.shape
    y0, y1, x0, x1 = m + max(0, -iy), h - m - max(0, iy), m + max(0, -ix), w - m - max(0, ix)
    if y1 - y0 < 8 or x1 - x0 < 8: return 999.0
    return float(np.abs(a[y0:y1, x0:x1] - b[y0 + iy:y1 + iy, x0 + ix:x1 + ix]).mean())


def main():
    pasta = achar_pasta(sys.argv[1] if len(sys.argv) > 1 else 'ultima')
    rows = ler(pasta)
    if len(rows) < 3: sys.exit('poucos quadros em ' + pasta)
    imgs = [Image.open(os.path.join(pasta, r['arq'])) for r in rows]
    g = [cinza(im) for im in imgs]
    win = np.outer(np.hanning(g[0].shape[0]), np.hanning(g[0].shape[1])).astype(np.float32)
    n = len(rows); passo = [(0.0, 0.0)] * n
    for k in range(1, n): passo[k] = fase(g[k - 1], g[k], win)
    res = []
    for k in range(1, n - 1):
        dx2, dy2 = fase(g[k - 1], g[k + 1], win)
        rac = dif(g[k - 1], g[k + 1], dx2, dy2)                       # a e c concordam (andando)?
        rb = min(dif(g[k - 1], g[k], dx2 / 2, dy2 / 2), dif(g[k + 1], g[k], -dx2 / 2, -dy2 / 2))   # b no meio do caminho?
        res.append((k, rb, rac, dx2, dy2))
    rbs = np.array([r[1] for r in res]); base = float(np.median(rbs)) if len(rbs) else 0.0
    lim = max(4.0, base * 4)
    pisc = [r for r in res if r[1] > lim and r[1] > 2.5 * r[2]]
    F = 2   # cinza() reduz 2x: passos convertidos para px da imagem gravada
    ints = np.diff([r['ms'] for r in rows])
    print('pasta: %s' % pasta)
    print('quadros: %d (%s), %.0f ms; quadros do jogo %s..%s' % (n, ''.join(r['tag'] for r in rows), rows[-1]['ms'], rows[0]['quadro'], rows[-1]['quadro']))
    print('intervalo entre saidas: mediana %.1f ms, min %.1f, max %.1f; > 25 ms: %d; 60 FPS suspenso em %d quadros' % (np.median(ints), ints.min(), ints.max(), int((ints > 25).sum()), sum(r['susp'] for r in rows)))
    st = np.array([np.hypot(*p) for p in passo[1:]]) * F
    print('movimento global por quadro (px da imagem; 0 e normal quando a interface parada domina a tela): mediana %.2f, maiores: %s' % (float(np.median(st)), ', '.join('%.1f' % v for v in sorted(st)[-3:])))
    print('resto tipico %.2f; PISCADAS (quadro que destoa dos dois vizinhos): %d' % (base, len(pisc)))
    for k, rb, rac, dx2, dy2 in sorted(pisc, key=lambda r: -r[1])[:12]:
        r = rows[k]; print('  #%d %s quadro %d t=%.1f ms: resto %.1f (vizinhos concordam: %.1f; movimento a->c %.1f,%.1f)' % (k, r['tag'], r['quadro'], r['ms'], rb, rac, dx2 * F, dy2 * F))
    with open(os.path.join(pasta, 'analise.tsv'), 'w', encoding='utf-8') as f:
        f.write('indice\ttipo\tquadro\tms\tpasso_dx\tpasso_dy\tresto\tvizinhos\tpiscada\n')
        pk = {r[0] for r in pisc}; rd = {r[0]: r for r in res}
        for k in range(n):
            rr = rd.get(k); f.write('%d\t%s\t%d\t%.2f\t%.2f\t%.2f\t%s\t%s\t%d\n' % (k, rows[k]['tag'], rows[k]['quadro'], rows[k]['ms'], passo[k][0] * F, passo[k][1] * F,
                                     '%.2f' % rr[1] if rr else '', '%.2f' % rr[2] if rr else '', 1 if k in pk else 0))
    if pisc:
        top = sorted(pisc, key=lambda r: -r[1])[:6]; W = 480; H = int(imgs[0].size[1] * W / imgs[0].size[0])
        sh = Image.new('RGB', (W * 3 + 20, (H + 10) * len(top)), (255, 0, 255))
        for i, (k, *_r) in enumerate(top):
            for j, kk in enumerate((k - 1, k, k + 1)): sh.paste(imgs[kk].convert('RGB').resize((W, H)), (j * (W + 10), i * (H + 10)))
        sh.save(os.path.join(pasta, 'suspeitos.png')); print('suspeitos (anterior | quadro | proximo):', os.path.join(pasta, 'suspeitos.png'))


main()
