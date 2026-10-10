"""Confirma a variante T de Jaden: candidato (RCP21x) x original (RCP00x) na arena. Uso: python tools/decks_jaden_t.py <pasta_mods> <sementes> [paralelo]"""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from decks_lib import *
from decks_v1 import limpar, JADEN_TIRAR_T, JADEN_PUXAR_T
import decks_ab
mods, n = sys.argv[1], int(sys.argv[2]); par = int(sys.argv[3]) if len(sys.argv) > 3 else 4
tot_t = tot_o = 0
for j in (1, 2, 3, 6):
    d = carregar(j); limpar(d, JADEN_TIRAR_T, JADEN_PUXAR_T)
    escrever(210 + j, d, mods, 'Jaden %d variante T (candidato)' % j)
for j in (1, 2, 3, 6):
    va, vb, sem = decks_ab.rodar('jt%d' % j, 210 + j, j, n, mods, par, base_semente=7000)
    print(decks_ab.texto('jt%d' % j, 210 + j, j, va, vb, sem), flush=True)
    tot_t += va; tot_o += vb
print('TOTAL variante T %d x %d original (%.0f%%)' % (tot_t, tot_o, 100 * tot_t / max(1, tot_t + tot_o)))
