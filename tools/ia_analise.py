"""Le o log da sonda do mod IA Avancada (replay) e imprime as decisoes da CPU com nomes de cartas.
Uso: python tools/ia_analise.py gen/replay/<sessao>_ia/log.txt"""
import re, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cartas import CARTAS
def nm(i):
    k = CARTAS.get(int(i)); return k.nome if k else '?%s' % i
def board(l):
    return l
for l in open(sys.argv[1], errors='ignore'):
    if '[mod IA Avancada]' not in l: continue
    l = l.split('] ', 1)[1].strip()
    l = re.sub(r'carta (\d+)', lambda m: 'carta ' + nm(m.group(1)), l)
    l = re.sub(r'\[(\d):(\d+) ', lambda m: '[%s:%s ' % (m.group(1), nm(m.group(2))), l)
    l = re.sub(r'mao=(\d+)\(([\d,]*)\)', lambda m: 'mao(%s)' % ', '.join(nm(x) for x in m.group(2).split(',') if x), l)
    print(l[:300])
