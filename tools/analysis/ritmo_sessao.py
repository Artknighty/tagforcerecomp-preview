"""Cadence of a whole replayed session, from the game's own [present] log (TF_PRESENT_LOG=1, set by tela_aovivo.sh).
Usage: python tools/analysis/ritmo_sessao.py gen/replay/<sessao><TAG> [--passo-ms 16.67] [--bloco 300]
For every block of game frames: share of presentations whose spacing is within +-2 ms of the ideal step (the 60 FPS cadence), share over
+-4 ms, and the worst gap. The monitor-side capture (tela.tsv) is not used: it can be polluted by the desktop (~120 images/s)."""
import re, sys, collections
a = sys.argv[1:]
passo = float(a[a.index('--passo-ms') + 1]) if '--passo-ms' in a else 1000 / 60
bloco = int(a[a.index('--bloco') + 1]) if '--bloco' in a else 300
P = []
for l in open(a[0].rstrip('/') + '/log.txt', errors='replace'):
    m = re.match(r'\[present\] ([\d.]+) ([\d.]+) (\w) (\d+)', l)
    if m: P.append((float(m[1]), m[3], int(m[4])))
seg = collections.defaultdict(list)
for i in range(1, len(P)): seg[P[i][2] // bloco].append(P[i][0] - P[i - 1][0])
tot = [0, 0, 0]
print('frames        n   <=2ms%  <=4ms%   worst ms')
for s in sorted(seg):
    v = seg[s]; n = len(v); d = [abs(x - passo) for x in v]
    g2 = sum(x <= 2 for x in d); g4 = sum(x <= 4 for x in d)
    tot[0] += n; tot[1] += g2; tot[2] += g4
    print(f'{s*bloco:5d}-{s*bloco+bloco-1:5d} {n:5d} {g2/n*100:7.1f} {g4/n*100:7.1f} {max(v):10.1f}')
print(f'TOTAL        {tot[0]:5d} {tot[1]/tot[0]*100:7.1f} {tot[2]/tot[0]*100:7.1f}')
