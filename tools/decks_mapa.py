"""Mapa completo dos decks: quem usa cada RCPnnn (dos 351) e que papel ele tem. Rodar: python tools/decks_mapa.py
Fontes (tudo lido do jogo instalado em gen/jogo, nada disso vai para o git):
  - script/st0X_t01_brain.ehp  ScrNNNDeck.bin: por personagem (NNN+1 = id do personagem), registros de 16 bytes; o ultimo u16 e o RCP que ele usa
    (os 4 primeiros u16 sao uma faixa de datas mes/dia-mes/dia, 0 = sem data). st01 = 1o ano (nivel 1, 2 e o duelo final de junho),
    st02 = 2o ano (niveis 3, 4 e 5).
  - SYSDIR/BOOT.BIN @0x71cc0: nomes dos personagens (nome completo + nome curto, na ordem dos ids a partir de 2).
  - gen/decks_db/decks.json (tools/decks_extrair.py): nomes e conteudo dos decks.
Saidas:
  gen/decks_db/MAPA_DECKS.md, mapa_decks.json   leitura humana / consulta (dados do jogo: fora do git)
  decomp/duelist_ids.inc                         so ids e mascaras (sem nomes de decks): incluido por decomp/duelists.h
Confere tambem as regras de decomp/duelists.h (bloco de 6 por personagem, 1 deck por personagem a partir do 41, ...) contra os scripts e
imprime as excecoes."""
import sys, os, struct, json, re, collections
RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(RAIZ, 'tools'))
from cartas import ehp

B = open(os.path.join(RAIZ, 'gen', 'jogo', 'PSP_GAME', 'SYSDIR', 'BOOT.BIN'), 'rb').read()
DECKS = {int(k): v for k, v in json.load(open(os.path.join(RAIZ, 'gen', 'decks_db', 'decks.json'), encoding='utf-8')).items()}
PRIMEIRO_ID, ULTIMO_ID = 2, 151


def le_nomes():
    p = B.find('Jaden Yuki'.encode('utf-16le'))
    out = []
    for _ in range(ULTIMO_ID - PRIMEIRO_ID + 1):
        par = []
        for _ in range(2):
            s = ''
            while True:
                c = struct.unpack_from('<H', B, p)[0]
                p += 2
                if c == 0:
                    break
                s += chr(c)
            par.append(s)
        out.append(tuple(par))
    return out


NOMES = le_nomes()   # indice 0 = personagem 2


def nome(pid):
    return NOMES[pid - PRIMEIRO_ID][0] or NOMES[pid - PRIMEIRO_ID][1] or ''


def ident(pid):
    """identificador C++ do personagem: kJaden, kParadoxBrothersPara..., ou kUnnamed47 quando o jogo nao da nome"""
    n = NOMES[pid - PRIMEIRO_ID][1] or NOMES[pid - PRIMEIRO_ID][0]   # nome curto; o do Atticus ('Rhodes') confunde com a Alexis
    if pid == 11:
        n = 'Atticus'
    if pid in (25, 26):
        n = 'Paradox ' + n
    if not n:
        return 'kUnnamed%d' % pid
    n = n.replace('•', ' ').replace("'", '').replace('.', ' ')
    base = 'k' + ''.join(w[:1].upper() + w[1:] for w in re.split(r'[^A-Za-z0-9]+', n) if w)
    return base


def identificadores():
    vistos, out = collections.Counter(), {}
    brutos = {pid: ident(pid) for pid in range(PRIMEIRO_ID, ULTIMO_ID + 1)}
    total = collections.Counter(brutos.values())
    for pid in range(PRIMEIRO_ID, ULTIMO_ID + 1):
        b = brutos[pid]
        if total[b] > 1:
            vistos[b] += 1
            b = '%s%d' % (b, vistos[b])
        out[pid] = b
    return out


IDS = identificadores()


def scripts():
    """{personagem: {'st01': [(datas, rcp), ...], 'st02': [...]}}"""
    res = collections.defaultdict(dict)
    for st in ('st01', 'st02'):
        F = ehp(open(os.path.join(RAIZ, 'gen', 'jogo', 'PSP_GAME', 'USRDIR', 'script', st + '_t01_brain.ehp'), 'rb').read(), 0)
        for n in range(1, ULTIMO_ID):
            k = 'Scr%03dDeck.bin' % n
            if k not in F:
                continue
            d = F[k]
            regs = []
            for i in range(0, len(d) - 15, 16):
                v = struct.unpack_from('<8H', d, i)
                regs.append((v[0:4], v[7]))
            res[n + 1][st] = [r for r in regs if r[1] != 0]
    return res


def regra_dono(deck):
    """a regra de decomp/duelists.h: (personagem, nivel) ou (0, 0)"""
    if 1 <= deck <= 228:
        return 2 + (deck - 1) // 6, (deck - 1) % 6 + 1
    if 240 <= deck <= 341:
        return deck - 199, 1
    return 0, 0


def main():
    sc = scripts()
    usados = collections.defaultdict(set)      # deck -> personagens cujo script o usa
    for pid, por_st in sc.items():
        for st, regs in por_st.items():
            for _, rcp in regs:
                usados[rcp].add(pid)
    modelo = tuple(DECKS[51]['principal']), tuple(DECKS[51]['extra'])
    enchimento = sorted(i for i in DECKS if i and (tuple(DECKS[i]['principal']), tuple(DECKS[i]['extra'])) == modelo)

    # ---- excecoes da regra ----
    exc = []
    for deck, pids in sorted(usados.items()):
        dono, _ = regra_dono(deck)
        for pid in sorted(pids):
            if dono != pid:
                exc.append((pid, deck, dono))
    print('decks usados pelos scripts: %d | enchimento (conteudo do RCP051): %d' % (len(usados), len(enchimento)))
    print('excecoes da regra (personagem do script, deck, dono pela regra):')
    por_deck = collections.defaultdict(list)
    for pid, deck, dono in exc:
        por_deck[deck].append(pid)
    for deck, pids in sorted(por_deck.items()):
        print('  deck %d: usado por %s (regra: %s)' % (deck, [IDS[p] for p in pids][:6] + (['... +%d' % (len(pids) - 6)] if len(pids) > 6 else []),
                                                 IDS.get(regra_dono(deck)[0], '-')))

    # ---- mascara de niveis usados (so personagens 2..39, blocos de 6) ----
    mascaras = {}
    for pid in range(2, 40):
        m = 0
        for deck in range(1 + 6 * (pid - 2), 7 + 6 * (pid - 2)):
            if pid in usados.get(deck, ()):
                m |= 1 << (deck - 1 - 6 * (pid - 2))
        mascaras[pid] = m

    # ---- .inc para o C++ ----
    inc = ['/* GERADO por tools/decks_mapa.py a partir dos scripts do jogo - nao editar. So ids e mascaras (nenhum nome de deck do jogo). */']
    inc.append('#define DUELIST_CHARACTER_IDS(X) \\')
    for pid in range(PRIMEIRO_ID, ULTIMO_ID + 1):
        inc.append('    X(%s, %d) \\' % (IDS[pid], pid))
    inc[-1] = inc[-1].rstrip(' \\')
    inc.append('')
    inc.append('/* levels (bit 0 = level 1) of a block of 6 decks that the story really uses; the others hold the placeholder deck */')
    inc.append('#define DUELIST_LADDER_USED(X) \\')
    for pid in range(2, 40):
        inc.append('    X(%s, 0x%02X) \\' % (IDS[pid], mascaras[pid]))
    inc[-1] = inc[-1].rstrip(' \\')
    inc.append('')
    faixas, ini = [], None
    for i in range(0, 352):
        if i in enchimento and ini is None:
            ini = i
        if (i not in enchimento or i == 351) and ini is not None:
            faixas.append((ini, i - 1))
            ini = None
    inc.append('/* the decks that hold the placeholder deck (same cards as RCP051): inclusive ranges */')
    inc.append('#define DUELIST_PLACEHOLDER_RANGES(X) \\')
    for a, b in faixas:
        inc.append('    X(%d, %d) \\' % (a, b))
    inc[-1] = inc[-1].rstrip(' \\')
    open(os.path.join(RAIZ, 'decomp', 'duelist_ids.inc'), 'w', encoding='utf-8', newline='\n').write('\n'.join(inc) + '\n')

    # ---- mapa humano ----
    md = ['# Mapa dos 351 decks (RCP000..RCP350)', '',
          'Gerado por tools/decks_mapa.py. Dados do jogo: fora do git. Regras de decomp/duelists.h.', '',
          'Legenda do papel: **escada** = bloco de 6 decks do personagem (nivel 1..6); **unico** = 1 deck do personagem; **enchimento** = mesmo conteudo do RCP051 '
          '(deck-reserva de Magos que o jogo repete onde nao ha deck de verdade); **especial** = fora das regras.', '']
    mapa = {}
    for deck in range(0, 351):
        d = DECKS[deck]
        dono, nivel = regra_dono(deck)
        papel = 'pessoal' if deck == 0 else 'enchimento' if deck in enchimento else ('escada' if deck <= 228 else 'unico') if dono else 'especial'
        mapa[deck] = dict(deck=deck, nome=d['nome']['en'], papel=papel, dono_regra=dono, nivel=nivel, usado_por=sorted(usados.get(deck, [])),
                          cartas=len(d['principal']), extra=len(d['extra']))
    json.dump(mapa, open(os.path.join(RAIZ, 'gen', 'decks_db', 'mapa_decks.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)

    def linha(deck, extra=''):
        d, m = DECKS[deck], mapa[deck]
        r = d['resumo']
        arq = '%s' % (', '.join(a for a, _ in r['atributos'][:2]) + ' / ' + ', '.join(t for t, _ in r['tipos'][:2]))
        return '- RCP%03d "%s" [%s] %d+%d cartas, %s%s' % (deck, d['nome']['en'], m['papel'], r['total'], r['extra'], arq, extra)

    md += ['## RCP000: deck pessoal (modelo)', linha(0), '']
    md += ['## Duelistas com escada de 6 decks (personagens 2..39)', '']
    for pid in range(2, 40):
        md.append('### %s (%d) - RCP%03d..%03d' % (nome(pid), pid, 1 + 6 * (pid - 2), 6 + 6 * (pid - 2)))
        fases = {}
        for st, regs in sc.get(pid, {}).items():
            for datas, rcp in regs:
                fases.setdefault(rcp, []).append(st + (' %d/%d-%d/%d' % datas if any(datas) else ''))
        for deck in range(1 + 6 * (pid - 2), 7 + 6 * (pid - 2)):
            extra = ' | script: ' + '; '.join(fases[deck]) if deck in fases else ''
            md.append(linha(deck, extra))
        md.append('')
    md += ['## Duelistas com 1 deck (personagens 41..141, deck = personagem + 199)', '']
    for pid in range(41, 142):
        deck = pid + 199
        md.append(linha(deck, ' | %s (%d)' % (nome(pid) or '(sem nome)', pid)))
    md.append('')
    md += ['## Fora das regras', '']
    for deck in list(range(229, 240)) + list(range(342, 351)):
        quem = ['%s (%d)' % (nome(p) or '(sem nome)', p) for p in sorted(usados.get(deck, []))]
        md.append(linha(deck, ' | script: ' + (', '.join(quem[:5]) + (' +%d' % (len(quem) - 5) if len(quem) > 5 else '') if quem else 'nenhum')))
    open(os.path.join(RAIZ, 'gen', 'decks_db', 'MAPA_DECKS.md'), 'w', encoding='utf-8').write('\n'.join(md) + '\n')
    cont = collections.Counter(m['papel'] for m in mapa.values())
    print('papeis:', dict(cont))
    print('escrito: decomp/duelist_ids.inc, gen/decks_db/MAPA_DECKS.md, gen/decks_db/mapa_decks.json')


main()
