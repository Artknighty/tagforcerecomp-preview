"""Situacoes SINTETICAS de ataque/invocacao no mesmo formato do plugin (nomes de cartas e numeros sorteados): ensina a comparacao de ATK/DEF/LP alem dos 2 decks da arena.
Os rotulos vem do professor (dataset.py). Uso: python tools/ai/ia_treino/synth.py [n=3000] [dados_reais.jsonl] [saida=gen/ia_treino/sintetico.jsonl]"""
import sys, os, re, json, random
sys.path.insert(0, os.path.dirname(__file__))
from prompt import montar_prompt
from dataset import professor
N = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
real = sys.argv[2] if len(sys.argv) > 2 else 'gen/ia_treino/dados_d2b.jsonl'
saida = sys.argv[3] if len(sys.argv) > 3 else 'gen/ia_treino/sintetico.jsonl'
R = random.Random(7)
nomes = set()
for l in open(real, encoding='utf-8'):
    for m in re.finditer(r'(?:^|; |: )([A-Z][^;:\n]*?) ATK \d+/DEF', json.loads(l)['prompt'], re.M):
        nomes.add(m.group(1).strip())
nomes = sorted(n for n in nomes if len(n) < 40) or ['Monster A', 'Monster B', 'Monster C', 'Monster D']
NL = chr(10)


def atk():
    return R.choice([0, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1100, 1200, 1300, 1400, 1500, 1600, 1700, 1800, 1900, 2000, 2100, 2200, 2400, 2500, 2600, 2800, 3000])


def dfn():
    return R.choice([0, 200, 400, 600, 800, 1000, 1200, 1400, 1600, 1800, 2000, 2200, 2400, 2600, 3000])


def lp():
    return R.choice([8000, 8000, 7500, 7000, 6200, 5400, 4800, 4000, 3200, 2500, 1800, 1200, 800, 400, 150])


def mon():
    return dict(n=R.choice(nomes), a=atk(), d=dfn(), defesa=R.random() < 0.35, vis=True)


def txt_mon(m):
    return '%s ATK %d/DEF %d, %s' % (m['n'], m['a'], m['d'], 'defense position' if m['defesa'] else 'attack position')


def sit_texto(me, opp, lp_me, lp_op, st_me, st_op, mao_op, ameaca, ataque):
    t = "You have %d LP and the opponent has %d LP. Cards left in decks: yours %d, opponent's %d.%s" % (lp_me, lp_op, R.randint(10, 38), R.randint(10, 38), NL)
    t += "Your hand: " + ('; '.join(R.choice(nomes) + ' (monster, ATK 1200/DEF 900, level 4)' for _ in range(R.randint(0, 3))) or 'none') + NL
    t += "Your monsters: " + ('; '.join(txt_mon(m) for m in me) or 'none') + NL
    t += "Your spell/trap zone: %d card(s), %d face-down.%s" % (st_me[0], st_me[1], NL)
    vis = [m for m in opp if m['vis']]
    oc = len(opp) - len(vis)
    partes = [txt_mon(m) for m in vis] + (["%d face-down monster(s) of unknown strength" % oc] if oc else [])
    t += "Opponent's monsters: " + ('; '.join(partes) or 'none') + NL
    t += "Opponent's spell/trap zone: %d card(s), %d face-down. Opponent's hand: %d card(s).%s" % (st_op[0], st_op[1], mao_op, NL)
    if ataque and ameaca:
        t += "The opponent's strongest face-up attacker has ATK %d.%s" % (ameaca, NL)
    return t


def texto_ataque(m, alvo, direto, lp_op):
    t = "Attack with %s (ATK %d) " % (m['n'], m['a'])
    if direto or alvo is None:
        return t + "directly: the opponent (%d LP) would lose %d LP%s." % (lp_op, m['a'], ' - that is LETHAL, you win the duel' if m['a'] >= lp_op else '')
    o = alvo
    if not o['vis']:
        return t + "into a face-down monster of unknown strength: it may be a weak monster that gets destroyed, or a strong one that survives and hurts you."
    if not o['defesa']:
        if m['a'] > o['a']:
            return t + "into %s (ATK %d, attack position): it is destroyed and the opponent loses %d LP; yours survives." % (o['n'], o['a'], m['a'] - o['a'])
        if m['a'] == o['a']:
            return t + "into %s (ATK %d, attack position): both monsters are destroyed." % (o['n'], o['a'])
        return t + "into %s (ATK %d, attack position): yours is destroyed and you lose %d LP." % (o['n'], o['a'], o['a'] - m['a'])
    if m['a'] > o['d']:
        return t + "into %s (DEF %d, defense position): it is destroyed, no LP loss for anyone; yours survives." % (o['n'], o['d'])
    if m['a'] == o['d']:
        return t + "into %s (DEF %d, defense position): nothing is destroyed." % (o['n'], o['d'])
    return t + "into %s (DEF %d, defense position): nothing is destroyed and you lose %d LP." % (o['n'], o['d'], o['d'] - m['a'])


def nota_combate(a, opp):
    t = []
    for o in opp:
        if not o['vis'] or o['defesa']:
            continue
        if o['a'] > a:
            t.append("%s (ATK %d) could attack it: it would be destroyed and you would lose %d LP." % (o['n'], o['a'], o['a'] - a))
        elif o['a'] == a:
            t.append("%s (ATK %d) attacking it: both are destroyed." % (o['n'], o['a']))
        else:
            t.append("%s (ATK %d) could not attack it profitably." % (o['n'], o['a']))
    s = ' '.join(t) or "The opponent has no face-up attacker, so it is safe from battle until the opponent summons one."
    if any(not o['vis'] for o in opp):
        s += " The opponent also has face-down monsters of unknown strength."
    return s


out = []
while len(out) < N:
    me = [mon() for _ in range(R.randint(1, 4))]
    opp = [mon() for _ in range(R.randint(0, 4))]
    for m in me:
        m['defesa'] = False
    for o in opp:
        o['vis'] = R.random() > 0.3
    lp_me, lp_op = lp(), lp()
    n_me = R.randint(0, 2)
    st_me = (n_me, R.randint(0, n_me))
    n_op = R.randint(0, 3)
    st_op = (n_op, R.randint(0, n_op))
    ameaca = max([o['a'] for o in opp if o['vis'] and not o['defesa']] or [0])
    if R.random() < 0.65:   # decisao de ataque
        m = R.choice(me)
        direto = not opp or (R.random() < 0.15 and not any(o['vis'] and not o['defesa'] for o in opp))
        alvo = None if direto else R.choice(opp)
        sit = sit_texto(me, opp, lp_me, lp_op, st_me, st_op, R.randint(0, 5), ameaca, True)
        o0 = texto_ataque(m, alvo, direto, lp_op) + ((" Beware: the opponent has %d face-down spell/trap card(s) that may punish an attack." % st_op[1]) if st_op[1] else '')
        d = dict(tipo='attack with this monster now?', situacao=sit, opcoes=[o0, "Do not attack: end the battle phase and keep the monster(s) as they are."])
    else:                  # invocar em ataque?
        c = mon()
        d = dict(tipo='summon this monster in attack position?', situacao=sit_texto(me, opp, lp_me, lp_op, st_me, st_op, R.randint(0, 5), ameaca, False),
                 opcoes=["Summon %s (monster, ATK %d/DEF %d, level %d) face-up in ATTACK position (the game's attack simulation likes this). %s" % (c['n'], c['a'], c['d'], R.randint(1, 8), nota_combate(c['a'], opp)),
                         "Do not summon it in attack position (the game will then set a monster face-down in defense, or do something else)."])
    lab = professor(d)
    if lab is None:
        continue
    out.append({'prompt': montar_prompt(d['tipo'], d['situacao'], d['opcoes']), 'answer': str(lab), 'fonte': 'sintetico', 'tipo': ' '.join(d['tipo'].split(' ')[:2]), 'n': 2})
with open(saida, 'w', encoding='utf-8') as f:
    for r in out:
        f.write(json.dumps(r, ensure_ascii=False) + NL)
c = {}
for r in out:
    c[(r['tipo'], r['answer'])] = c.get((r['tipo'], r['answer']), 0) + 1
print(len(out), 'exemplos sinteticos', c, '->', saida)
