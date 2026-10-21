"""Extrai os decks de NPC do Tag Force (RCPnnn.ydc no BOOT.BIN) para um banco de dados em gen/decks_db/ (fora do git: sao dados do jogo).
Uso: python tools/game/decks_extrair.py        (needs the installed data in gen/jogo: tools/game/extrair_jogo.py)
Saidas em gen/decks_db/:
  cartas.json        id -> nome, categoria, atributo, tipo, nivel, atk, def, texto
  decks.json         indice RCP -> nomes (6 idiomas), principal/extra/side (ids), cabecalho (hex), resumo (contagens, atributos, tipos)
  protagonistas.json grupos de 6 decks por protagonista (a tabela do jogo marca o inicio de cada grupo: 1,7,13,19,25,31,37)
  DECKS.md           leitura humana dos protagonistas
Formato do .ydc (libehsys_85A292D7 do BOOT): 8 bytes de cabecalho (u32 + u32, checksum/flags), u16 n_principal (<=80) + n ids, u16 n_extra (<31) + ids,
u16 n_side (<16) + ids. Cada copia da carta aparece repetida. Os nomes dos decks estao em recipe_name_{e,f,g,i,j,s}.bin (indice = numero do RCP)."""
import sys, os, struct, json, collections
RAIZ = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(RAIZ, 'tools', 'game'))
from cartas import ehp, CARTAS

SAIDA = os.path.join(RAIZ, 'gen', 'decks_db')
os.makedirs(SAIDA, exist_ok=True)
B = open(os.path.join(RAIZ, 'gen', 'jogo', 'PSP_GAME', 'SYSDIR', 'BOOT.BIN'), 'rb').read()
F = ehp(B, B.find(b'EHP', 0x93298))

# protagonistas: 6 decks cada, em grupos consecutivos (tabela 1,7,13,19,25,31,37 em libtitle.prx); a ordem/nome vem do conteudo dos decks
PROTAGONISTAS = [('Jaden', 1), ('Syrus', 7), ('Chumley', 13), ('Alexis', 19), ('Chazz', 25), ('Bastion', 31), ('Zane', 37)]


def nomes_idioma(arq):
    b = F[arq]
    n, tab = struct.unpack_from('<II', b, 0)
    base = tab + 4 * n
    out = []
    for off in struct.unpack_from('<%dI' % n, b, tab):
        e, s = base + off, ''
        while e + 1 < len(b):
            c = struct.unpack_from('<H', b, e)[0]
            if c == 0:
                break
            s += chr(c)
            e += 2
        out.append(s)
    return out


IDIOMAS = {'en': 'recipe_name_e.bin', 'fr': 'recipe_name_f.bin', 'de': 'recipe_name_g.bin', 'it': 'recipe_name_i.bin', 'ja': 'recipe_name_j.bin', 'es': 'recipe_name_s.bin'}
NOMES = {k: nomes_idioma(v) for k, v in IDIOMAS.items()}


def lista(b, p):
    n = struct.unpack_from('<H', b, p)[0]
    return [struct.unpack_from('<H', b, p + 2 + 2 * i)[0] for i in range(n)], p + 2 + 2 * n


def ler_deck(i):
    b = F['RCP%03d.ydc' % i]
    principal, p = lista(b, 8)
    extra, p = lista(b, p)
    side, p = lista(b, p)
    return dict(id=i, nome={k: (v[i] if i < len(v) else '') for k, v in NOMES.items()}, cabecalho=b[:8].hex(), tamanho=len(b),
                principal=principal, extra=extra, side=side, sobra=b[p:].hex())


def resumo(d):
    cs = [CARTAS.get(x) for x in d['principal']]
    mon = [c for c in cs if c and c.monstro]
    mag = [c for c in cs if c and not c.monstro and c.icone != 'Resposta' and c.f0 == 13]
    arm = [c for c in cs if c and not c.monstro and c.f0 == 14]
    cnt = lambda it: collections.Counter(it).most_common(4)
    return dict(total=len(d['principal']), monstros=len(mon), magias=len(mag), armadilhas=len(arm), extra=len(d['extra']),
                atributos=cnt(c.atributo for c in mon), tipos=cnt(c.tipo for c in mon),
                atk_medio=round(sum((c.atk or 0) for c in mon) / max(1, len(mon))), niveis_altos=sum(1 for c in mon if (c.nivel or 0) >= 7),
                mais_copias=collections.Counter(c.nome for c in cs if c).most_common(5))


def main():
    cartas = {}
    for i, c in CARTAS.items():
        cartas[i] = dict(nome=c.nome, categoria=str(c.categoria), atributo=str(c.atributo), tipo=str(c.tipo), nivel=c.nivel, atk=c.atk, defe=c.defe, monstro=bool(c.monstro), texto=c.desc)
    json.dump(cartas, open(os.path.join(SAIDA, 'cartas.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=0)
    decks = {}
    for k in F:
        if k.startswith('RCP') and k.endswith('.ydc'):
            i = int(k[3:6])
            d = ler_deck(i)
            d['resumo'] = resumo(d)
            decks[i] = d
    json.dump(decks, open(os.path.join(SAIDA, 'decks.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=0)
    prot = {}
    md = ['# Decks dos protagonistas (Tag Force 1 EU)', '', 'Gerado por tools/game/decks_extrair.py. 6 decks por protagonista, em escadinha (o 1o e o mais fraco).', '']
    for nome, ini in PROTAGONISTAS:
        prot[nome] = [ini + j for j in range(6)]
        md += ['## %s (RCP%03d-%03d)' % (nome, ini, ini + 5), '']
        for j in range(6):
            d = decks[ini + j]
            r = d['resumo']
            md.append('### %d. RCP%03d "%s" - %d cartas (%d monstros, %d magias, %d armadilhas), extra %d, ATK medio %d' % (j + 1, ini + j, d['nome']['en'], r['total'], r['monstros'], r['magias'], r['armadilhas'], r['extra'], r['atk_medio']))
            md.append('Atributos: %s | Tipos: %s' % (', '.join('%s x%d' % x for x in r['atributos']), ', '.join('%s x%d' % x for x in r['tipos'])))
            cont = collections.Counter(CARTAS[x].nome if x in CARTAS else '?%d' % x for x in d['principal'])
            md.append('Cartas: ' + '; '.join('%s x%d' % (n, q) for n, q in sorted(cont.items(), key=lambda t: (-t[1], t[0]))))
            if d['extra']:
                ce = collections.Counter(CARTAS[x].nome if x in CARTAS else '?%d' % x for x in d['extra'])
                md.append('Extra: ' + '; '.join('%s x%d' % (n, q) for n, q in ce.items()))
            md.append('')
    json.dump(prot, open(os.path.join(SAIDA, 'protagonistas.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    open(os.path.join(SAIDA, 'DECKS.md'), 'w', encoding='utf-8').write('\n'.join(md))
    print('decks:', len(decks), '| cartas:', len(cartas), '| saida:', SAIDA)


main()
