"""Mede, nos logs dos duelos da arena (CPU x CPU, gen/replay/*arena*/log.txt com o monitor [mon]), quais cartas a CPU REALMENTE joga.
Para cada carta: vezes que apareceu na mao no inicio de um turno (turno-mao) e vezes que foi jogada/ativada (joga da mao + seleciona carta na zona 11).
Carta com muitas aparicoes e quase nenhum uso = 'morta' para a CPU (nao entra em deck feito para ela). Saida: gen/decks_db/uso_cartas.json"""
import re, sys, os, glob, json, collections
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'game'))
from cartas import CARTAS
mao_n = collections.Counter(); jogou = collections.Counter(); duelos = 0
R_MAO = re.compile(r'\[mon\] turno (\d+) \(vez de J(\d)\) J(\d) .* mao=\d+\(([\d,]*)\)')
R_JOGA = re.compile(r'\[mon\] J(\d) joga da mao: carta (\d+)')
R_SEL = re.compile(r'\[mon\] J(\d) seleciona carta (\d+) \(zona 11')
for f in glob.glob('gen/replay/*arena*/log.txt'):
    vistos = set(); n = 0
    for l in open(f, errors='ignore'):
        m = R_MAO.search(l)
        if m:
            turno, vez, jog, ids = int(m.group(1)), m.group(2), m.group(3), m.group(4)
            if vez == jog and (turno, jog) not in vistos:   # a mao do jogador da vez, uma vez por turno
                vistos.add((turno, jog)); n += 1
                for x in ids.split(','):
                    if x: mao_n[int(x)] += 1
            continue
        m = R_JOGA.search(l) or R_SEL.search(l)
        if m: jogou[int(m.group(2))] += 1
    duelos += 1 if n else 0
res = {}
for i in set(mao_n) | set(jogou):
    if i in CARTAS: res[i] = dict(nome=CARTAS[i].nome, mao=mao_n[i], jogou=jogou[i], taxa=round(jogou[i] / mao_n[i], 3) if mao_n[i] else None)
json.dump(res, open('gen/decks_db/uso_cartas.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=0)
print('duelos lidos:', duelos, '| cartas:', len(res))
mortas = sorted([(v['mao'], v['taxa'], v['nome']) for v in res.values() if v['mao'] >= 25 and v['taxa'] is not None and v['taxa'] < 0.08], reverse=True)
print('MORTAS (>=25 turnos na mao e <8% de uso):'); print('; '.join('%s (%d, %.0f%%)' % (n, m, t * 100) for m, t, n in mortas[:60]))
vivas = sorted([(v['taxa'], v['mao'], v['nome']) for v in res.values() if v['mao'] >= 25 and v['taxa'] >= 0.35], reverse=True)
print('VIVAS (>=35% de uso):'); print('; '.join('%s (%.0f%%)' % (n, t * 100) for t, m, n in vivas[:60]))
