"""Monta o conjunto de treino da FUNCAO DE VALOR ("quem esta ganhando") a partir dos logs dos duelos da arena (gen/replay/*arena*/log.txt, monitor [mon] do mod CPUvsCPU).
Cada duelo tem um quadro do tabuleiro a cada troca de vez (mao, campo, LP dos dois lados) e o vencedor. Cada quadro vira DUAS amostras (uma de cada ponto de vista) com so o que o
jogador SABE no jogo real (a mao do oponente nao entra: so a quantidade de cartas; cartas viradas para baixo = forca desconhecida). Rotulo = o jogador do ponto de vista venceu.
Uso: python tools/ai/ia_valor/dataset.py [saida=gen/ia_valor/valor.npz]
Os mesmos 'features()' sao reimplementados em C++ no plugin (mods_src/IAAprimorada) quando o modelo for integrado: mantenha as duas versoes iguais."""
import re, os, sys, glob, json
import numpy as np
RAIZ = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
sys.path.insert(0, os.path.join(RAIZ, 'tools', 'game'))
from decks_lib import CARTAS

R_SNAP = re.compile(r'\[mon\] turno (\d+) \(vez de J(\d)\) J(\d) LP=(-?\d+) raw\[cf0\.\.d08\]=([\d,]+) mao=\d+\(([\d,]*)\) campo:(.*)')
R_FIM = re.compile(r'\[arena\] FIM quadro.*venceu o jogador (\d)')
R_CAMPO = re.compile(r'(M|ST|C)(\d+)=(\d+)/([0-9a-f]+)')
NF = 35   # sem o numero do turno (no jogo real a plugin nao tem um contador confiavel dentro da simulacao da CPU)


def stat(i):
    c = CARTAS.get(i)
    if not c: return 0, 0, 0, 0   # monstro?, atk, def, nivel
    return (1 if c['monstro'] else 0), (c['atk'] or 0), (c['defe'] or 0), (c['nivel'] or 0)


def lado(lp, raw, mao, campo, conhecido):
    """vetor de um lado. conhecido=True: o proprio jogador (sabe a mao)"""
    r = [int(x) for x in raw.split(',')]
    hand_n = r[2] if len(r) > 2 else len(mao); deck_n = r[3] if len(r) > 3 else 0
    up_atk = up_def = down = st = st_down = campo_mag = 0
    soma_atk = soma_def = max_atk = max_def = poder = 0
    for tipo, slot, cid, fl in R_CAMPO.findall(campo):
        cid = int(cid); fl = int(fl, 16)
        if tipo == 'M':
            vis = bool(fl & 0x80); defesa = bool(fl & 0x08) or not vis
            _, atk, dfn, _ = stat(cid)
            if vis:
                if defesa: up_def += 1; soma_def += dfn; max_def = max(max_def, dfn)
                else: up_atk += 1; soma_atk += atk; max_atk = max(max_atk, atk)
                poder += max(atk, 0.7 * dfn)
            else:
                down += 1; poder += 1200; max_def = max(max_def, 1200)
        elif tipo == 'ST':
            st += 1; st_down += 0 if (fl & 0x80) else 1
        else:
            campo_mag = 1
    v = [lp / 8000.0, hand_n / 8.0, deck_n / 40.0, up_atk / 5.0, up_def / 5.0, down / 5.0, st / 5.0, st_down / 5.0, campo_mag,
         soma_atk / 10000.0, max_atk / 4000.0, soma_def / 10000.0, max_def / 4000.0, poder / 10000.0]
    if conhecido:
        mm = ma = sm = tm = 0; msoma = 0; mmax = 0
        for x in mao:
            m, atk, _, _ = stat(x)
            c = CARTAS.get(x)
            if m: mm += 1; msoma += atk; mmax = max(mmax, atk)
            elif c and c['categoria'] == 'Armadilha': tm += 1
            else: sm += 1
        v += [mm / 6.0, sm / 6.0, tm / 6.0, mmax / 4000.0, msoma / 10000.0]
    return v


def features(me, op, turno, minha_vez):
    """me/op: dict(lp, raw, mao, campo). 14 + 5 (so eu) + 14 + 2 globais = 35"""
    a = lado(me['lp'], me['raw'], me['mao'], me['campo'], True)
    b = lado(op['lp'], op['raw'], [], op['campo'], False)
    g = [1.0 if minha_vez else 0.0, (me['lp'] - op['lp']) / 8000.0]
    f = a + b + g
    assert len(f) == NF, len(f)
    return f


def main():
    saida = sys.argv[1] if len(sys.argv) > 1 else os.path.join(RAIZ, 'gen', 'ia_valor', 'valor.npz')
    os.makedirs(os.path.dirname(saida), exist_ok=True)
    X = []; y = []; duelo = []; turno_l = []
    nd = 0
    for d in sorted(glob.glob(os.path.join(RAIZ, 'gen', 'replay', '*arena*')))[:]:
        f = os.path.join(d, 'log.txt')
        if not os.path.exists(f): continue
        snaps = {}; venc = None
        for l in open(f, errors='ignore'):
            m = R_FIM.search(l)
            if m: venc = int(m.group(1)); continue
            m = R_SNAP.search(l)
            if m:
                t, vez, jog, lp, raw, mao, campo = int(m.group(1)), int(m.group(2)), int(m.group(3)), int(m.group(4)), m.group(5), m.group(6), m.group(7)
                snaps.setdefault(t, {'vez': vez})[jog] = dict(lp=lp, raw=raw, mao=[int(x) for x in mao.split(',') if x], campo=campo)
        if venc is None: continue
        nd += 1
        for t, s in snaps.items():
            if 0 not in s or 1 not in s or t < 2: continue
            for p in (0, 1):
                X.append(features(s[p], s[1 - p], t, s['vez'] == p)); y.append(1 if venc == p else 0); duelo.append(nd); turno_l.append(t)
    X = np.array(X, dtype=np.float32); y = np.array(y, dtype=np.int8)
    np.savez(saida, X=X, y=y, duelo=np.array(duelo, dtype=np.int32), turno=np.array(turno_l, dtype=np.int16))
    print('duelos com vencedor:', nd, '| amostras:', len(y), '| vitorias do ponto de vista: %.1f%%' % (100 * y.mean()), '->', saida)


if __name__ == '__main__':
    main()
