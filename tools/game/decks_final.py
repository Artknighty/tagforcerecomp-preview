"""Gera os decks FINAIS do mod Decks Aprimorados nos indices reais do jogo (decks/RCPnnn.txt). Versao atual: Zane (RCP037-042) = variante T (armadilhas primeiro; 62% de vitorias contra
os originais em 72 duelos de arena). Uso: python tools/game/decks_final.py <pasta_do_mod>"""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from decks_lib import *
from decks_v1 import limpar, ZANE_TIRAR, ZANE_PUXAR_T, JADEN_TIRAR_T, JADEN_PUXAR_T

mod = sys.argv[1]
os.makedirs(os.path.join(mod, 'decks'), exist_ok=True)
for f in os.listdir(os.path.join(mod, 'decks')):
    if f.startswith('RCP') and f.endswith('.txt'):
        os.remove(os.path.join(mod, 'decks', f))
for j in range(6):
    d = carregar(37 + j)
    limpar(d, ZANE_TIRAR, ZANE_PUXAR_T)
    erros = validar(d)
    print('Zane %d (RCP%03d): %s %s' % (j + 1, 37 + j, resumo(d), erros or ''))
    escrever(37 + j, d, mod, 'Zane %d melhorado (variante T: tira cartas que a CPU nao usa, reforca com armadilhas e monstros fortes do proprio jogo)' % (j + 1))
# Jaden (RCP001-006): variante T so nos decks 1, 2, 3 e 6 (nos 4 e 5 ela nao muda nada). 25 x 19 (57 %) sobre os originais em 44 duelos: evidencia fraca, confirmar com mais duelos.
for j in (0, 1, 5):   # deck 3 fora: 10 x 20 (33%) na confirmacao de 2026-10-06
    d = carregar(1 + j)
    limpar(d, JADEN_TIRAR_T, JADEN_PUXAR_T)
    erros = validar(d)
    print('Jaden %d (RCP%03d): %s %s' % (j + 1, 1 + j, resumo(d), erros or ''))
    escrever(1 + j, d, mod, 'Jaden %d melhorado (variante T; confianca baixa: 57%% em 44 duelos)' % (j + 1))
