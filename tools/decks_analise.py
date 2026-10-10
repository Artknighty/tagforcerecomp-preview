"""Analisa os duelos do torneio de decks (gen/replay/*_arena_<rotulo>_<a>x<b>_<semente>/log.txt + res).
Uso: python tools/decks_analise.py <rotulo>
Saidas: taxa de vitoria de cada deck; por carta, quantas vezes foi COMPRADA e quantas JOGADA (grosso modo: compras = mudancas na mao entre inicios de turno; jogadas = 'joga da mao' +
'seleciona carta zona 11' do monitor [mon]); cartas que a CPU quase nunca joga quando compra ('mortas'). Salva gen/decks_db/analise_<rotulo>.json"""
import re, sys, os, glob, json, collections
SESSAO = open(os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'gen', 'arena', 'sessao.txt')).read().strip() if os.path.exists(os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'gen', 'arena', 'sessao.txt')) else 'sessao'
RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(RAIZ, 'tools'))
from decks_lib import CARTAS, nome_de

R_MAO = re.compile(r'\[mon\] turno (\d+) \(vez de J(\d)\) J(\d) .* mao=\d+\(([\d,]*)\)')
R_JOGA = re.compile(r'\[mon\] J(\d) joga da mao: carta (\d+)')
R_SEL = re.compile(r'\[mon\] J(\d) seleciona carta (\d+) \(zona 11')


def duelo(log):
    """devolve {jog: (compras Counter, jogadas Counter)} lendo o log de um duelo"""
    snaps = {0: [], 1: []}; plays = {0: collections.Counter(), 1: collections.Counter()}; ev = {0: [], 1: []}
    for l in open(log, errors='ignore'):
        m = R_MAO.search(l)
        if m:
            t, vez, jog, ids = int(m.group(1)), int(m.group(2)), int(m.group(3)), m.group(4)
            if vez == jog:
                snaps[jog].append((t, collections.Counter(int(x) for x in ids.split(',') if x)))
                ev[jog].append(('snap', len(snaps[jog]) - 1))
            continue
        m = R_JOGA.search(l) or R_SEL.search(l)
        if m:
            j = int(m.group(1)) & 1; c = int(m.group(2)); plays[j][c] += 1; ev[j].append(('play', c))
    out = {}
    for j in (0, 1):
        comp = collections.Counter(); prev = None; jog_c = collections.Counter()
        pend = collections.Counter()
        for tipo, v in ev[j]:
            if tipo == 'play':
                pend[v] += 1; jog_c[v] += 1
            else:
                cur = snaps[j][v][1]
                if prev is None:
                    for c, q in cur.items(): comp[c] += q
                else:
                    esperado = prev - pend if False else collections.Counter({c: prev[c] - pend[c] for c in prev if prev[c] - pend[c] > 0})
                    for c, q in cur.items():
                        d = q - esperado.get(c, 0)
                        if d > 0: comp[c] += d
                prev = cur; pend = collections.Counter()
        out[j] = (comp, jog_c)
    return out


def main():
    rot = sys.argv[1]
    vit = collections.defaultdict(lambda: [0, 0]); carta = collections.defaultdict(lambda: [0, 0]); n = 0
    por_deck = collections.defaultdict(lambda: collections.defaultdict(lambda: [0, 0]))
    for res in glob.glob(os.path.join(RAIZ, 'gen', 'arena', 'res_%s_*x*_*.txt' % rot)):
        m = re.search(r'res_%s_(\d+)x(\d+)_(\d+)[^.]*\.txt' % re.escape(rot), os.path.basename(res))
        if not m: continue
        a, b, s = int(m.group(1)), int(m.group(2)), int(m.group(3))
        w = re.search(r'venceu o jogador (\d)', open(res).read())
        cand = glob.glob(os.path.join(RAIZ, 'gen', 'replay', SESSAO + '_arena_%s_%dx%d_%d*' % (rot, a, b, s), 'log.txt'))
        if not w or not cand: continue
        log = cand[0]
        wj = int(w.group(1)); n += 1
        for j, d in ((0, a), (1, b)):
            vit[d][1] += 1; vit[d][0] += (wj == j)
        r = duelo(log)
        for j, d in ((0, a), (1, b)):
            comp, jog = r[j]
            for c in set(comp) | set(jog):
                carta[c][0] += comp[c]; carta[c][1] += jog[c]
                por_deck[d][c][0] += comp[c]; por_deck[d][c][1] += jog[c]
    print('duelos analisados:', n)
    print('VITORIAS por deck (deck: vitorias/duelos):')
    print('; '.join('RCP%03d %d/%d' % (d, v[0], v[1]) for d, v in sorted(vit.items())))
    mortas = sorted([(v[0], v[1], c) for c, v in carta.items() if v[0] >= 15 and v[1] / v[0] < 0.25], reverse=True)
    print('\nCARTAS QUE A CPU QUASE NUNCA JOGA (compradas>=15, jogadas/compradas<25%):')
    for comp, jog, c in mortas[:70]:
        print('  %-38s compradas %3d jogadas %3d (%.0f%%)' % (nome_de(c)[:38], comp, jog, 100 * jog / comp))
    json.dump({'vitorias': {str(d): v for d, v in vit.items()}, 'cartas': {str(c): v for c, v in carta.items()},
               'por_deck': {str(d): {str(c): v for c, v in cc.items()} for d, cc in por_deck.items()}}, open(os.path.join(RAIZ, 'gen', 'decks_db', 'analise_%s.json' % rot), 'w'))


main()
