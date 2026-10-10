"""Biblioteca para editar decks do Tag Force (usa o banco gerado por tools/decks_extrair.py em gen/decks_db).
Um deck e um dict {'principal': [ids], 'extra': [ids], 'side': [ids]}. Funcoes: carregar, trocar cartas por NOME, validar (copias <= maximo visto em decks de NPC, tamanho),
escrever o arquivo do mod (decks/RCPnnn.txt, formato do plugin DecksAprimorados) e resumo legivel."""
import json, os, collections, sys
RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DB = os.path.join(RAIZ, 'gen', 'decks_db')
CARTAS = {int(k): v for k, v in json.load(open(os.path.join(DB, 'cartas.json'), encoding='utf-8')).items()}
DECKS = {int(k): v for k, v in json.load(open(os.path.join(DB, 'decks.json'), encoding='utf-8')).items()}
MAX_NPC = {int(k): v for k, v in json.load(open(os.path.join(DB, 'max_copias_npc.json'))).items()}
POR_NOME = {}
for i, c in CARTAS.items():
    POR_NOME.setdefault(c['nome'].lower(), i)


def id_de(nome):
    if isinstance(nome, int):
        return nome
    i = POR_NOME.get(nome.lower())
    if i is None:
        raise KeyError('carta nao existe no jogo: %s' % nome)
    return i


def nome_de(i):
    return CARTAS[i]['nome'] if i in CARTAS else '?%d' % i


def carregar(idx):
    d = DECKS[idx]
    return {'principal': list(d['principal']), 'extra': list(d['extra']), 'side': list(d['side'])}


def contar(lista):
    return collections.Counter(lista)


def tirar(deck, parte, nome, n=1):
    i = id_de(nome); l = deck[parte]
    for _ in range(n):
        if i not in l:
            raise ValueError('%s nao esta em %s' % (nome_de(i), parte))
        l.remove(i)


def por_(deck, parte, nome, n=1):
    deck[parte].extend([id_de(nome)] * n)


def trocar(deck, de, para, n=1, parte='principal'):
    """tira n copias de 'de' e poe n copias de 'para'"""
    tirar(deck, parte, de, n); por_(deck, parte, para, n)


def validar(deck, permitir_extra_copias=()):
    erros = []
    n = len(deck['principal'])
    if not 40 <= n <= 60:
        erros.append('principal com %d cartas (40-60)' % n)
    if len(deck['extra']) > 30 or len(deck['side']) > 15:
        erros.append('extra/side grande demais')
    for parte in ('principal', 'extra'):
        for i, q in contar(deck[parte]).items():
            lim = MAX_NPC.get(i)
            if i not in CARTAS:
                erros.append('id %d nao existe' % i)
            elif lim is None:
                erros.append('%s nunca aparece em deck de NPC (pode ser proibida/limitada): evitar' % nome_de(i))
            elif q > max(lim, 1) and i not in permitir_extra_copias:
                erros.append('%s x%d (maximo visto em deck de NPC: %d)' % (nome_de(i), q, lim))
    tot = contar(deck['principal'] + deck['extra'])
    for i, q in tot.items():
        if q > 3 and i in CARTAS:
            erros.append('%s x%d (maximo 3)' % (nome_de(i), q))
    return erros


def escrever(idx, deck, pasta_mod, comentario=''):
    os.makedirs(os.path.join(pasta_mod, 'decks'), exist_ok=True)
    def lin(parte):
        return ' '.join(str(i) for i in deck[parte])
    cont = contar(deck['principal'])
    with open(os.path.join(pasta_mod, 'decks', 'RCP%03d.txt' % idx), 'w', encoding='utf-8') as f:
        if comentario:
            f.write('# ' + comentario.replace('\n', ' ') + '\n')
        f.write('# ' + '; '.join('%s x%d' % (nome_de(i), q) for i, q in sorted(cont.items(), key=lambda t: (-t[1], nome_de(t[0])))) + '\n')
        f.write('principal: ' + lin('principal') + '\n')
        f.write('extra: ' + lin('extra') + '\n')
        f.write('side: ' + lin('side') + '\n')


def resumo(deck):
    mon = [i for i in deck['principal'] if CARTAS[i]['monstro']]
    mag = [i for i in deck['principal'] if not CARTAS[i]['monstro'] and CARTAS[i]['categoria'] != 'Armadilha']
    arm = [i for i in deck['principal'] if CARTAS[i]['categoria'] == 'Armadilha']
    atk = sum((CARTAS[i]['atk'] or 0) for i in mon) / max(1, len(mon))
    return '%d cartas: %d monstros (ATK medio %d), %d magias, %d armadilhas, extra %d' % (len(deck['principal']), len(mon), atk, len(mag), len(arm), len(deck['extra']))
