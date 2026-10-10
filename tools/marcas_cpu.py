"""Relatorio das marcas "a CPU errou" (F7 ou L3+R3 durante o jogo) de uma sessao.
Uso: python tools/marcas_cpu.py [sessao|caminho_do_log|ultima] [--turnos N]
Para cada marca "[marca-cpu] #n quadro F" do log da sessao mostra, com nomes de cartas:
  - o estado do duelo na hora da marca (LP, mao, campo dos dois lados);
  - as ULTIMAS ACOES DA CPU antes da marca (numeradas; o jogador pode apertar um pouco depois do erro: a jogada errada continua na lista), com o turno;
  - as ultimas decisoes do plugin (o que a CPU via, as opcoes e a escolha);
  - alertas automaticos simples (monstro em ataque diante de um atacante maior, etc.);
  - o caminho da captura de tela (PNG inteiro) tirada na marca.
Grava o relatorio em gen/marcas/<sessao>.md. Os dados sao os do log da sessao (gen/logs/sessao_*.log); nao precisa reproduzir o duelo."""
import re, sys, os, glob
RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(RAIZ, 'tools'))
from decks_lib import CARTAS

R_MARCA = re.compile(r'\[marca-cpu\] #(\d+) quadro (\d+)')
R_SNAP = re.compile(r'\[mon\] (turno (\d+) \(vez de J(\d)\)|MARCA #(\d+)) J(\d) LP=(-?\d+) raw\[[^\]]*\]=([\d,]+) mao=\d+\(([\d,]*)\) campo:(.*)')
R_JOGA = re.compile(r'\[mon\] J(\d) joga da mao: carta (\d+) \(indice (-?\d+), modo (\d)\) quadro (\d+)')
R_SEL = re.compile(r'\[mon\] J(\d) seleciona carta (\d+) \(zona (\d+), idx (\d+), tipo (\d+)\) quadro (\d+)')
R_LP = re.compile(r'\[mon\] LP J(\d): (-?\d+) -> (-?\d+) \(quadro (\d+)\)')
R_IA = re.compile(r'\[mod [^\]]*\] \[ia\] (.*)')
R_DEC = re.compile(r'\[marca-cpu\] (decisao|   situacao|#\d+: ultimas).*')
R_NOTA = re.compile(r'\[marca-cpu-nota\] #(\d+): (.*)')
R_CAMPO = re.compile(r'(M|ST|C)(\d+)=(\d+)/([0-9a-f]+)')


def nome(i):
    c = CARTAS.get(int(i))
    return c['nome'] if c else '?%s' % i


def stats(i):
    c = CARTAS.get(int(i))
    return (c['atk'] or 0, c['defe'] or 0) if c and c['monstro'] else (0, 0)


def campo_txt(campo):
    out = []
    for tipo, slot, cid, fl in R_CAMPO.findall(campo):
        fl = int(fl, 16); vis = bool(fl & 0x80); defesa = bool(fl & 0x08) or not vis
        if tipo == 'M':
            a, d = stats(cid)
            out.append('%s [%s, %s]' % (nome(cid) if vis else 'monstro virado (%s)' % nome(cid), '%d/%d' % (a, d), 'defesa' if defesa else 'ATAQUE'))
        elif tipo == 'ST':
            out.append('%s (%s)' % (nome(cid) if vis else 'carta virada (%s)' % nome(cid), 'aberta' if vis else 'virada'))
        else:
            out.append('campo: %s' % nome(cid))
    return out


def achar_log(arg):
    if arg and arg != 'ultima':
        if os.path.exists(arg): return arg
        c = glob.glob(os.path.join(RAIZ, 'gen', 'logs', '%s*.log' % arg.replace('.log', '')))
        if c: return c[0]
        sys.exit('log nao encontrado: %s' % arg)
    c = glob.glob(os.path.join(RAIZ, 'gen', 'logs', 'sessao_*.log')) + glob.glob(os.path.join(RAIZ, 'gen', 'replay', '*', 'logs', 'sessao_*.log'))
    c = [f for f in c if R_MARCA.search(open(f, errors='ignore').read())]   # marcas gravadas (as descartadas com Esc nao contam)
    if not c: sys.exit('nenhuma sessao com marcas "a CPU errou" (aperte F7 ou L3+R3 durante o duelo)')
    return max(c, key=os.path.getmtime)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    nturnos = int(sys.argv[sys.argv.index('--turnos') + 1]) if '--turnos' in sys.argv else 3
    if '--turnos' in sys.argv: args = [a for a in args if a != str(nturnos)]
    log = achar_log(args[0] if args else 'ultima')
    L = open(log, errors='ignore').read().split('\n')
    sessao = os.path.basename(log).replace('.log', '')
    marcas = [(i, int(m.group(1)), int(m.group(2))) for i, l in enumerate(L) for m in [R_MARCA.search(l)] if m]
    if not marcas: sys.exit('sem marcas neste log: %s' % log)
    out = ['# Marcas "a CPU errou" - %s' % sessao, '']
    capdir = os.path.join(os.path.dirname(log), sessao + '_capturas')
    for (i, n, quadro) in marcas:
        out += ['## Marca #%d (quadro %d)' % (n, quadro), '']
        nota = next((m.group(2) for l in L[i:i + 200] for m in [R_NOTA.search(l)] if m and int(m.group(1)) == n), '')
        out += ['**Anotacao do jogador:** ' + (nota or '_(sem anotacao)_'), '']
        png = sorted(glob.glob(os.path.join(capdir, '*_q%d_marca.png' % quadro)) + glob.glob(os.path.join(capdir, '*_q%d_marca.png' % (quadro + 1))))
        if png: out.append('Captura da tela: `%s`' % png[0]); out.append('')
        # estado na marca
        est = {}
        for l in L[i:i + 12]:
            m = R_SNAP.search(l)
            if m and m.group(4) == str(n): est[int(m.group(5))] = (int(m.group(6)), m.group(8), m.group(9))
        out.append('**Estado na marca**'); out.append('')
        for j in sorted(est):
            lp, mao, campo = est[j]
            out.append('- J%d: LP %d | mao: %s | campo: %s' % (j, lp, ', '.join(nome(x) for x in mao.split(',') if x) or '(vazia)', '; '.join(campo_txt(campo)) or '(vazio)'))
        out.append('')
        # acoes anteriores: desde o N-esimo inicio de turno antes da marca
        turnos = [k for k in range(i) if R_SNAP.search(L[k]) and 'turno' in L[k] and 'MARCA' not in L[k]]
        # cada turno tem 2 linhas (J0 e J1): conta por numero de turno distinto
        tn = []
        for k in turnos:
            m = R_SNAP.search(L[k]); t = int(m.group(2))
            if not tn or tn[-1][0] != t: tn.append((t, k))
        ini = tn[-nturnos][1] if len(tn) >= nturnos else 0
        out.append('**Ultimas acoes (da mais antiga para a mais recente; o erro pode estar em qualquer uma)**'); out.append('')
        k_acao = 0; turno_atual = None
        for k in range(ini, i):
            l = L[k]; m = R_SNAP.search(l)
            if m and 'turno' in l and 'MARCA' not in l:
                t, vez, j = int(m.group(2)), int(m.group(3)), int(m.group(5))
                if turno_atual != t:
                    turno_atual = t; out.append('- **Turno %d (vez de J%d)**' % (t, vez))
                if j == vez:
                    out.append('    - tabuleiro de J%d no inicio do turno: LP %s, mao: %s' % (j, m.group(6), ', '.join(nome(x) for x in m.group(8).split(',') if x) or '(vazia)'))
                    out.append('      campo de J%d: %s' % (j, '; '.join(campo_txt(m.group(9))) or '(vazio)'))
                else:
                    out.append('      campo de J%d: %s' % (j, '; '.join(campo_txt(m.group(9))) or '(vazio)'))
                continue
            m = R_JOGA.search(l)
            if m:
                k_acao += 1; modo = {'0': 'em ATAQUE (Invocar)', '1': 'em DEFESA virada (baixar)'}.get(m.group(4), 'modo %s' % m.group(4))
                out.append('    - **[%d]** J%s joga %s %s (quadro %s)' % (k_acao, m.group(1), nome(m.group(2)), modo, m.group(5))); continue
            m = R_SEL.search(l)
            if m:
                k_acao += 1; zona = 'da mao' if m.group(3) == '11' else 'zona %s' % m.group(3)
                out.append('    - **[%d]** J%s seleciona/ativa %s (%s, tipo %s) (quadro %s)' % (k_acao, m.group(1), nome(m.group(2)), zona, m.group(5), m.group(6))); continue
            m = R_LP.search(l)
            if m: out.append('    - LP de J%s: %s -> %s (quadro %s)' % (m.group(1), m.group(2), m.group(3), m.group(4))); continue
            m = R_IA.search(l)
            if m: out.append('    - [regra do plugin] %s' % re.sub(r'carta (\d+)', lambda x: 'carta ' + nome(x.group(1)), m.group(1))[:260])
        out.append('')
        # decisoes do plugin despejadas na marca
        dec = [l for l in L[i:i + 200] if R_DEC.search(l)]
        if dec:
            out.append('**O que o plugin via e escolheu nas ultimas decisoes**'); out.append('')
            for l in dec: out.append('    ' + l.split('] ', 1)[-1][:900])
            out.append('')
        # alertas automaticos simples (estado na marca)
        alertas = []
        for j in est:
            outro = est.get(1 - j)
            if not outro: continue
            minhas = [(cid, int(fl, 16)) for tipo, s, cid, fl in R_CAMPO.findall(est[j][2]) if tipo == 'M']
            deles = [stats(cid)[0] for tipo, s, cid, fl in R_CAMPO.findall(outro[2]) if tipo == 'M' and (int(fl, 16) & 0x80) and not (int(fl, 16) & 0x08)]
            amea = max(deles or [0])
            for cid, fl in minhas:
                if (fl & 0x80) and not (fl & 0x08) and stats(cid)[0] < amea:
                    alertas.append('J%d tem %s (ATK %d) em ATAQUE diante de um atacante de ATK %d do oponente: pode ser destruido numa batalha' % (j, nome(cid), stats(cid)[0], amea))
            if outro[0] <= 2500 and any((fl & 0x80) and not (fl & 0x08) for _, fl in minhas) and not deles:
                alertas.append('J%d tem monstro em ataque e o oponente (LP %d) esta sem monstros: possivel ataque direto forte' % (j, outro[0]))
        if alertas:
            out.append('**Alertas automaticos (heuristicas simples)**'); out.append('')
            out += ['- ' + a for a in alertas]; out.append('')
    os.makedirs(os.path.join(RAIZ, 'gen', 'marcas'), exist_ok=True)
    dst = os.path.join(RAIZ, 'gen', 'marcas', sessao + '.md')
    open(dst, 'w', encoding='utf-8').write('\n'.join(out))
    print('\n'.join(out)); print('\n(relatorio gravado em %s)' % dst)


main()
