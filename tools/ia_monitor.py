"""Mostra as jogadas registradas pelo mod CPU contra CPU ([mon] ...) e pelo mod IA Aprimorada ([ia] ...) com nomes de cartas.
Uso: python tools/ia_monitor.py gen/replay/<sessao>/log.txt [--so-mon]"""
import re, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cartas import CARTAS
def nm(i):
    k = CARTAS.get(int(i)); return k.nome if k else '?%s' % i
for l in open(sys.argv[1], errors='ignore'):
    m = re.search(r'\[mod [^\]]+\] (.*)', l)
    if not m: continue
    t = m.group(1).strip()
    if not (t.startswith('[mon]') or t.startswith('[ia]') or t.startswith('[arena]')): continue
    if '--so-mon' in sys.argv and not t.startswith('[mon]'): continue
    t = re.sub(r'carta (\d+)', lambda x: 'carta ' + nm(x.group(1)), t)
    t = re.sub(r'(M\d|ST\d|C\d)=(\d+)/', lambda x: '%s=%s/' % (x.group(1), nm(x.group(2))), t)
    t = re.sub(r'mao=(\d+)\(([\d,]*)\)', lambda x: 'mao(%s)' % ', '.join(nm(i) for i in x.group(2).split(',') if i), t)
    print(t[:400])
