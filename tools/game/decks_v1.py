"""Candidatos v1 para os decks do Zane (RCP037-042) e do Jaden (RCP001-006): 'limpeza' (tira cartas que a CPU nao sabe usar ou que sao fracas) + reforco com cartas boas
que ja aparecem em decks de NPC do jogo (so cartas e copias vistas no proprio jogo = legais). Nao muda a identidade: o arquétipo e as cartas-assinatura ficam.
Uso: python tools/game/decks_v1.py <pasta_mod> [base_zane=201] [base_jaden=211]   (escreve decks/RCPnnn.txt no mod; indice = base + posicao-1, para comparar com o original)"""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from decks_lib import *

ZANE_TIRAR = ['Heavy Mech Support Platform', 'Different Dimension Capsule', 'Dust Tornado', 'Magic Jammer', 'Photon Generator Unit', 'Attack Reflector Unit', 'Mystik Wok',
              'Emergency Provisions', 'Seven Tools of the Bandit', 'Ceasefire', 'The Spell Absorbing Life', 'Magician of Faith']
# (carta, maximo de copias no deck), em ordem de prioridade
ZANE_PUXAR = [('Cyber Dragon', 3), ('Proto-Cyber Dragon', 3), ('Blowback Dragon', 2), ('Cyber Phoenix', 2), ('Cyber Laser Dragon', 1), ('Reflect Bounder', 1), ('Jinzo', 1),
              ('Sakuretsu Armor', 3), ('Bottomless Trap Hole', 2), ('Solemn Judgment', 2), ('Smashing Ground', 2), ('Nobleman of Crossout', 2), ('Torrential Tribute', 1),
              ('Magic Cylinder', 1), ('Cyber Raider', 2), ('Drillago', 2), ('Nanobreaker', 2)]
ZANE_PUXAR_T = [('Sakuretsu Armor', 3), ('Bottomless Trap Hole', 2), ('Solemn Judgment', 2), ('Smashing Ground', 2), ('Torrential Tribute', 1), ('Magic Cylinder', 1), ('Nobleman of Crossout', 2),
                ('Blowback Dragon', 2), ('Cyber Laser Dragon', 1), ('Jinzo', 1), ('Reflect Bounder', 1), ('Cyber Dragon', 3), ('Cyber Raider', 2), ('Drillago', 2), ('Nanobreaker', 2)]   # variante T: armadilhas primeiro
JADEN_TIRAR = ['Winged Kuriboh LV10', 'Transcendent Wings', 'Creature Swap', 'Dark Catapulter', 'Wroughtweiler']
JADEN_TIRAR_T = JADEN_TIRAR + ['Winged Kuriboh', 'The Warrior Returning Alive', 'Pot of Avarice', 'Card Destruction', 'Fake Hero', 'Cyclone Boomerang', 'Dark Factory of Mass Production']
JADEN_PUXAR_T = [('Sakuretsu Armor', 3), ('Bottomless Trap Hole', 2), ('Solemn Judgment', 2), ('Magic Cylinder', 1), ('Torrential Tribute', 1), ('Smashing Ground', 2), ('Hero Signal', 2),
                 ('Elemental Hero Sparkman', 3), ('Elemental Hero Burstinatrix', 3), ('Elemental Hero Avian', 3), ('Elemental Hero Wildheart', 3), ('Elemental Hero Clayman', 3),
                 ('Elemental Hero Bladedge', 1), ('Reinforcement of the Army', 2), ('Skyscraper', 2), ('Polymerization', 3)]
JADEN_PUXAR = [('Elemental Hero Sparkman', 3), ('Elemental Hero Avian', 3), ('Elemental Hero Burstinatrix', 3), ('Elemental Hero Clayman', 3), ('Elemental Hero Wildheart', 3),
               ('Elemental Hero Bladedge', 1), ('Polymerization', 3), ('Hero Signal', 2), ('Skyscraper', 2), ('Reinforcement of the Army', 2), ('Sakuretsu Armor', 3),
               ('Solemn Judgment', 2), ('Bottomless Trap Hole', 2), ('Torrential Tribute', 1), ('Magic Cylinder', 1), ('Smashing Ground', 2), ('Fusion Sage', 1)]


def limpar(deck, tirar, puxar, protegidas=()):
    alvo = len(deck['principal'])
    for nome in tirar:
        i = id_de(nome)
        while i in deck['principal']:
            deck['principal'].remove(i)
    for nome, teto in puxar:
        i = id_de(nome)
        if len(deck['principal']) >= alvo:
            break
        lim = min(teto, 3, MAX_NPC.get(i, 0))
        # nao passar do que o jogo permite no total (principal + extra) nem do teto
        while len(deck['principal']) < alvo and deck['principal'].count(i) < lim and (deck['principal'] + deck['extra']).count(i) < 3:
            deck['principal'].append(i)
    return deck


def main():
    mod = sys.argv[1]; bz = int(sys.argv[2]) if len(sys.argv) > 2 else 201; bj = int(sys.argv[3]) if len(sys.argv) > 3 else 211
    for base_real, base_novo, tirar, puxar, nome in ((37, bz, ZANE_TIRAR, ZANE_PUXAR, 'Zane'), (37, bz + 30, ZANE_TIRAR, ZANE_PUXAR_T, 'Zane-T'), (1, bj, JADEN_TIRAR, JADEN_PUXAR, 'Jaden'), (1, bj + 30, JADEN_TIRAR_T, JADEN_PUXAR_T, 'Jaden-T')):
        for j in range(6):
            d = carregar(base_real + j)
            antes = contar(d['principal'])
            limpar(d, tirar, puxar)
            depois = contar(d['principal'])
            erros = validar(d)
            dif = ['+%d %s' % (depois[i] - antes.get(i, 0), nome_de(i)) for i in depois if depois[i] > antes.get(i, 0)] + ['-%d %s' % (antes[i] - depois.get(i, 0), nome_de(i)) for i in antes if antes[i] > depois.get(i, 0)]
            print('%s %d (RCP%03d -> RCP%03d): %s | %s' % (nome, j + 1, base_real + j, base_novo + j, resumo(d), '; '.join(dif)))
            if erros:
                print('   AVISO:', erros)
            escrever(base_novo + j, d, mod, 'candidato v1 de %s %d (original RCP%03d)' % (nome, j + 1, base_real + j))


if __name__ == "__main__":
    main()
