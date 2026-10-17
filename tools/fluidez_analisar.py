"""Fluidez de uma sessao inteira no monitor: cada tranco que o jogador viu, com o quadro do jogo e a causa provavel.

Uso: python tools/fluidez_analisar.py <pasta da reproducao> [--fps 60|120]

A pasta e a de tools/tela_aovivo.sh (de preferencia com SO_TEMPOS=1 e gravacao longa): tela.tsv (hora de cada imagem composta pelo
Windows) + log.txt do jogo com TF_PRESENT_LOG=1 ("[present] inicio fim M|R quadro").

Tranco = intervalo entre duas imagens na tela maior que 1,5x o ideal (16,7 ms a 60 FPS, 8,3 a 120). Cada um e classificado:
  - jogo parado: o proprio jogo nao apresentou nada no intervalo (carregamento, pausa de nota F7/F8, troca de tela);
  - quadro pesado: o log tem "[perf] quadro pesado/lento" naquele quadro (a linha diz se foi textura, PNG, pipelines ou espera da GPU);
  - sem intermediario: so quadros reais naquele trecho (cena trocou ou o par nao pode ser interpolado: o jogo mostra 30 imagens/s ali);
  - ritmo: o jogo apresentou no tempo, mas a imagem perdeu a atualizacao do monitor (o problema que o pacing.cpp ataca).
"""
import re, sys, bisect, collections


def main():
    a = sys.argv[1:]
    if not a: print(__doc__); return
    pasta = a[0].rstrip('/\\')
    t0 = None; tela = []
    for l in open(pasta + '/tela.tsv', encoding='utf-8'):
        if l.startswith('#t0'): t0 = float(l.split()[1]); continue
        if l.startswith('#'): continue
        c = l.rstrip('\n').split('\t'); tela.append(float(c[2]))
    pres = []; pesados = {}; lentos = {}
    for l in open(pasta + '/log.txt', encoding='utf-8', errors='replace'):
        m = re.match(r'\[present\] ([\d.]+) ([\d.]+) ([MR])(?: (\d+))?', l)
        if m: pres.append((float(m.group(1)), float(m.group(2)), m.group(3), int(m.group(4) or 0))); continue
        m = re.match(r'\[perf\] quadro (?:pesado|lento) (\d+): ([\d.]+) ms(.*)', l)
        if m: pesados[int(m.group(1))] = (float(m.group(2)), m.group(3).strip())
        m = re.match(r'\[perf\] jogo pausado (\d+) ms no quadro (\d+)', l)
        if m: lentos[int(m.group(2))] = 'pausa ' + m.group(1) + ' ms'
    if t0 is None or not pres: print('falta #t0 no tela.tsv ou [present] no log (TF_PRESENT_LOG=1)'); return
    T = [x + t0 for x in tela[1:]]
    dt = [b - x for x, b in zip(T, T[1:])]
    fps = int(a[a.index('--fps') + 1]) if '--fps' in a else (120 if sorted(dt)[len(dt) // 2] < 12 else 60)
    ideal = 1000.0 / fps
    ini = [p[0] for p in pres]
    ev = collections.Counter(); lista = []
    for i, d in enumerate(dt):
        if d <= ideal * 1.5: continue
        t1, t2 = T[i], T[i + 1]
        j1 = bisect.bisect_right(ini, t1 - 3.0); j2 = bisect.bisect_right(ini, t2 - 1.0)   # apresentacoes que podiam aparecer no intervalo
        dentro = pres[j1:j2]
        qd = pres[min(j2, len(pres) - 1)][3] if pres else 0
        perto = [q for q in range(qd - 3, qd + 2) if q in pesados or q in lentos]
        if perto:
            q = perto[0]; causa = 'quadro pesado'; det = ('%.0f ms %s' % pesados[q]) if q in pesados else lentos[q]
        elif not dentro or (dentro[-1][0] - (dentro[0][0] if dentro else 0)) > d:
            causa = 'jogo parado'; det = ''
        elif all(p[2] == 'R' for p in pres[max(0, j1 - 2):j2 + 2]):
            causa = 'sem intermediario'; det = ''
        else:
            causa = 'ritmo'; det = ''
        g = [x for x in pres[max(0, j1 - 1):j2 + 1]]
        if causa == 'jogo parado' and g and len(g) >= 2 and max(b[0] - x[0] for x, b in zip(g, g[1:])) < d * 0.6: causa = 'ritmo'
        ev[causa] += 1; lista.append((t1 - T[0], d, qd, causa, det))
    bons = sum(1 for d in dt if abs(d - ideal) <= 1.5)
    print('%d imagens em %.0f s a %d FPS; dentro de +-1,5 ms do ideal: %.2f%%; trancos (> %.1f ms): %d' % (len(T), (T[-1] - T[0]) / 1000, fps, 100.0 * bons / len(dt), ideal * 1.5, len(lista)))
    for k, v in ev.most_common(): print('   %-18s %d' % (k, v))
    print('trancos (s na gravacao, intervalo, quadro do jogo, causa):')
    for s, d, q, c, det in lista:
        if c == 'jogo parado' and d > 200: c += ' (longo: carregamento/pausa)'
        print('  %7.2f s  %6.1f ms  quadro %6d  %s %s' % (s, d, q, c, det))


main()
