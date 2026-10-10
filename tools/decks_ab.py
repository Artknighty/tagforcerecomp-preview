"""A/B de decks na arena (CPU x CPU, regras do plugin, nos dois lados).
Uso: python tools/decks_ab.py <rotulo> <deckA> <deckB> <n_sementes> <pasta_mods> [paralelo=6]
Roda n duelos com A no J0 e n com B no J0 (cancela a vantagem de lado) e imprime as vitorias de A. Os indices de deck sao os do jogo (RCPnnn); candidatos ficam em indices
de teste (200..299) escritos no mod Decks Aprimorados da pasta de mods. Resultado bruto em gen/arena/res_<rotulo>_<a>x<b>_<semente>.txt"""
import sys, os, re, glob, subprocess, math
RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def rodar(rotulo, a, b, n, mods, paralelo=6, base_semente=5000):
    pares = []
    for k in range(n):
        pares.append((a, b, base_semente + k)); pares.append((b, a, base_semente + k))
    arq = os.path.join(RAIZ, 'gen', 'pares_%s.txt' % rotulo)
    open(arq, 'w').write('\n'.join('%d %d %d' % p for p in pares) + '\n')
    flag = os.path.join(RAIZ, 'gen', 'arena', 'torneio_%s_fim.txt' % rotulo)
    if os.path.exists(flag):
        os.remove(flag)
    subprocess.run(['sh', 'tools/torneio.sh', rotulo, arq, mods, str(paralelo)], cwd=RAIZ)
    return resultados(rotulo, a, b)


def resultados(rotulo, a, b):
    va = vb = sem = 0
    for f in glob.glob(os.path.join(RAIZ, 'gen', 'arena', 'res_%s_*x*_*.txt' % rotulo)):
        m = re.search(r'res_%s_(\d+)x(\d+)_(\d+)[^.]*\.txt' % re.escape(rotulo), os.path.basename(f))
        if not m:
            continue
        j0, j1 = int(m.group(1)), int(m.group(2))
        t = open(f).read(); w = re.search(r'venceu o jogador (\d)', t)
        if not w:
            sem += 1; continue
        vencedor = j0 if int(w.group(1)) == 0 else j1
        if vencedor == a: va += 1
        elif vencedor == b: vb += 1
    return va, vb, sem


def texto(rotulo, a, b, va, vb, sem):
    n = va + vb
    p = va / n if n else 0
    ic = 1.96 * math.sqrt(p * (1 - p) / n) if n else 0
    return '%s: deck %d venceu %d x %d contra o deck %d (%d sem fim) = %.0f%% (+-%.0f%%)' % (rotulo, a, va, vb, b, sem, 100 * p, 100 * ic)


if __name__ == '__main__':
    rot, a, b, n, mods = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]), sys.argv[5]
    par = int(sys.argv[6]) if len(sys.argv) > 6 else 6
    va, vb, sem = rodar(rot, a, b, n, mods, par)
    print(texto(rot, a, b, va, vb, sem))
