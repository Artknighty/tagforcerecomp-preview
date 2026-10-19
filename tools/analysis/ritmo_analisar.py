"""Ritmo do 60/120 FPS no monitor: cruza o que o jogo apresentou com o que chegou a tela.

Uso: python tools/analysis/ritmo_analisar.py <pasta da reproducao> [--fps 60|120] [--ciclos]

A pasta e a de tools/analysis/tela_aovivo.sh (gen/replay/<sessao><TAG>): tela.tsv (tools/analysis/tela_gravar.cpp: hora de cada imagem composta, com a
linha #t0 = relogio QPC do inicio) e log.txt do jogo rodado com TF_PRESENT_LOG=1 (uma linha "[present] inicio fim M|R" por
apresentacao, no mesmo relogio). Sem o log de apresentacoes so sai o resumo dos intervalos.

Imprime:
  - intervalos entre imagens na tela: histograma (ms inteiros), desvio e % dentro de +-1,5 ms do ideal (16,7 ms ou 8,3 ms) - a nota
    principal; "perdidas" = AccumulatedFrames > 1 (apresentacao que nunca apareceu);
  - atraso tela - inicio da apresentacao por fase da atualizacao do monitor (fase 0 = atualizacao, na grade dos tempos da tela):
    serve para escolher a fase alvo do pacing.cpp (TF_RITMO_FASE). Atraso bimodal (0,8 ms ou ~5 ms na mesma fase) = a imagem ora
    entra na hora, ora espera a proxima atualizacao, e cada troca vira um par de intervalos desiguais (12/21 ms a 60 FPS);
  - --ciclos (120 FPS): fase de cada uma das quatro apresentacoes do quadro (Q1 Q2 Q3 = intermediarios, R = real) e os espacamentos;
    fase que anda de um ciclo para o outro = a malha de fase nao segura (saidas atrasando, ver display_present).
Achados que levaram a isto: notas internas, secao 8, "Ritmo do 60/120 FPS (2026-10-14)".
"""
import re, sys, bisect, collections, statistics
import numpy as np

P_MON = 1000.0 / 120.0   # periodo do monitor do jogador (120 Hz); a fase e medida nessa grade


def ler(pasta):
    t0 = None; tela = []
    for l in open(pasta + '/tela.tsv', encoding='utf-8'):
        if l.startswith('#t0'): t0 = float(l.split()[1]); continue
        if l.startswith('#'): continue
        i, ta, tp, acc = l.rstrip('\n').split('\t'); tela.append((float(tp), int(acc)))
    pres = []
    try:
        for l in open(pasta + '/log.txt', encoding='utf-8', errors='replace'):
            m = re.match(r'\[present\] ([\d.]+) ([\d.]+) ([MR])', l)
            if m: pres.append((float(m.group(1)), float(m.group(2)), m.group(3)))
    except OSError: pass
    return t0, tela, pres


def main():
    a = sys.argv[1:]
    if not a: print(__doc__); return
    pasta = a[0].rstrip('/\\'); fps = int(a[a.index('--fps') + 1]) if '--fps' in a else None
    t0, tela, pres = ler(pasta)
    tela = tela[1:]   # a 1a imagem traz o acumulado de antes da gravacao
    ap = [t for t, _ in tela]; dt = [b - x for x, b in zip(ap, ap[1:])]
    if not dt: print('nada gravado'); return
    if fps is None: fps = 120 if statistics.median(dt) < 12 else 60
    ideal = 1000.0 / fps
    bons = sum(1 for d in dt if abs(d - ideal) <= 1.5)
    print('intervalos (ms):', dict(sorted(collections.Counter(int(d + 0.5) for d in dt).items())))
    print('imagens %d, desvio %.2f ms; dentro de +-1,5 ms de %.1f: %.1f%%; perdidas (acumulados > 1): %d'
          % (len(dt) + 1, statistics.pstdev(dt), ideal, 100.0 * bons / len(dt), sum(1 for _, c in tela[1:] if c > 1)))
    if not pres or t0 is None:
        print('(sem [present] no log.txt ou sem #t0 no tela.tsv: rode o jogo com TF_PRESENT_LOG=1 e o tela_gravar atual)'); return
    T = np.array(ap) + t0
    ph0 = np.angle(np.exp(2j * np.pi * T / P_MON).mean())
    fase = lambda x: (x / P_MON - ph0 / (2 * np.pi)) % 1.0
    print('concentracao das imagens na grade de %.2f ms: %.2f (1 = todas na atualizacao)' % (P_MON, abs(np.exp(2j * np.pi * T / P_MON).mean())))
    fins = [p[1] for p in pres]; lat = collections.defaultdict(list)
    for t in T:
        j = bisect.bisect_right(fins, t) - 1
        if j >= 0: lat[int(fase(pres[j][0]) * 10)].append(t - pres[j][0])
    print('atraso tela - inicio da apresentacao, pela fase do inicio:')
    for k in sorted(lat):
        v = np.array(lat[k])
        if len(v) >= 3: print('   fase %.1f-%.1f: n=%4d  p10 %.2f  mediana %.2f  p90 %.2f ms' % (k / 10, k / 10 + 0.1, len(v), np.percentile(v, 10), np.median(v), np.percentile(v, 90)))
    dur = np.array([p[1] - p[0] for p in pres])
    print('duracao das apresentacoes: mediana %.2f, p90 %.2f, max %.2f ms' % (np.median(dur), np.percentile(dur, 90), dur.max()))
    if '--ciclos' in a:
        st = [p for p in pres if T[0] <= p[0] <= T[-1]]; c = []
        for i in range(3, len(st) - 1):
            if st[i][2] == 'R' and all(st[i - k][2] == 'M' for k in (1, 2, 3)):
                q = st[i - 3:i + 1]
                c.append([fase(x[0]) for x in q] + [q[k + 1][0] - q[k][0] for k in range(3)] + [st[i + 1][0] - st[i][0]])
        if not c: print('sem ciclos de 4 apresentacoes (120 FPS desligado ou caindo para o ritmo de 60)'); return
        c = np.array(c); print('ciclos de 120 FPS: %d' % len(c))
        for k, n in enumerate(['Q1', 'Q2', 'Q3', 'R']): print('   %s fase: p10 %.2f mediana %.2f p90 %.2f' % (n, np.percentile(c[:, k], 10), np.median(c[:, k]), np.percentile(c[:, k], 90)))
        for k, n in enumerate(['Q1->Q2', 'Q2->Q3', 'Q3->R', 'R->Q1']): print('   %s: p10 %.2f mediana %.2f p90 %.2f ms' % (n, np.percentile(c[:, 4 + k], 10), np.median(c[:, 4 + k]), np.percentile(c[:, 4 + k], 90)))


main()
