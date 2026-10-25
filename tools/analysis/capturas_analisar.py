"""Prepara as capturas do modo de diagnostico grafico (runtime/gpu/capturas.inc) para analise.
Uso: python tools/analysis/capturas_analisar.py [pasta_das_capturas | ultima] [--por-folha 12]
  - 'ultima' (padrao) = a pasta *_capturas mais recente em logs/ ou gen/logs (player sessions) ou gen/replay/**/logs (testes)
  - monta FOLHAS DE CONTATO (grade 4x3 de miniaturas legendadas com numero, etiqueta e tempo) em <pasta>/folhas/folha_NN.png: uma imagem mostra 12 quadros
  - calcula sinais de problema por quadro (preto/branco quase total, magenta = textura ausente, bloco de cor unica, quadro igual ao anterior por muito tempo) e grava suspeitas.tsv
  - imprime o resumo: quantidades por etiqueta, intervalo de tempo, suspeitas e os caminhos das folhas e das marcas (F8)
Marcas F8: tambem le do log da sessao (ao lado da pasta) as linhas "[marca-graf]" (config grafica, texturas desenhadas, nota digitada pelo jogador)
e grava gen/marcas/<sessao>_graf.md.
Depois: abrir as folhas (e os quadros 'marca' em tamanho cheio) e olhar. Marcas (F8) are what the player flagged as wrong."""
import subprocess
import sys, os, re, glob, collections
import numpy as np
from PIL import Image, ImageDraw, ImageFont

RAIZ = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def achar_pasta(arg):
    if arg and arg != 'ultima':
        return arg
    cands = (glob.glob(os.path.join(RAIZ, 'logs', '*_capturas')) + glob.glob(os.path.join(RAIZ, 'gen', 'logs', '*_capturas'))   # logs/ = the root install the player uses; gen/logs = older sessions
             + glob.glob(os.path.join(RAIZ, 'gen', 'replay', '*', 'logs', '*_capturas')))
    if not cands:
        sys.exit('nenhuma pasta *_capturas encontrada (ligue o modo com TF_CAPTURAS=1 ou F9 no jogo)')
    return max(cands, key=os.path.getmtime)


def ler_indice(pasta):
    out = []
    p = os.path.join(pasta, 'indice.tsv')
    if not os.path.exists(p):
        return out
    for l in open(p, encoding='utf-8', errors='ignore'):
        c = l.rstrip('\r\n').split('\t')
        if len(c) >= 5 and os.path.exists(os.path.join(pasta, c[0])):
            out.append(dict(arq=c[0], quadro=int(c[1]), ms=int(c[2]), tag=c[3], dim=c[4]))
    return out


def sinais(img):
    a = np.asarray(img.convert('RGB').resize((160, 90)), dtype=np.int16)
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    n = r.size
    f = {}
    f['preto'] = float(((r < 12) & (g < 12) & (b < 12)).sum()) / n
    f['branco'] = float(((r > 243) & (g > 243) & (b > 243)).sum()) / n
    f['magenta'] = float(((r > 200) & (g < 70) & (b > 200)).sum()) / n
    # bloco de cor unica: a cor mais comum (quantizada) ocupa quase tudo
    q = (a // 32).reshape(-1, 3); cod = q[:, 0] * 64 + q[:, 1] * 8 + q[:, 2]
    f['cor_unica'] = float(np.bincount(cod).max()) / n
    f['contraste'] = float(a.mean(axis=2).std())
    return f


def suspeito(f):
    m = []
    if f['preto'] > 0.97: m.append('quase tudo preto')
    if f['branco'] > 0.9: m.append('quase tudo branco')
    if f['magenta'] > 0.02: m.append('magenta (textura ausente?)')
    if f['cor_unica'] > 0.85 and f['preto'] <= 0.97: m.append('uma cor ocupa quase tudo')
    if f['contraste'] < 4 and f['preto'] <= 0.97: m.append('imagem quase sem contraste')
    return m


def folhas(pasta, itens, por=12):
    saida = os.path.join(pasta, 'folhas'); os.makedirs(saida, exist_ok=True)
    cols, tw, th = 4, 320, 180
    try:
        fonte = ImageFont.truetype('arial.ttf', 14)
    except OSError:
        fonte = ImageFont.load_default()
    caminhos = []
    for k in range(0, len(itens), por):
        grupo = itens[k:k + por]
        lin = (len(grupo) + cols - 1) // cols
        folha = Image.new('RGB', (cols * tw, lin * (th + 20)), (20, 20, 20))
        d = ImageDraw.Draw(folha)
        for i, it in enumerate(grupo):
            im = Image.open(os.path.join(pasta, it['arq'])).convert('RGB').resize((tw, th))
            x, y = (i % cols) * tw, (i // cols) * (th + 20)
            folha.paste(im, (x, y + 20))
            t = it['ms'] // 1000
            d.text((x + 3, y + 2), '#%s %s %d:%02d q%d%s' % (it['arq'][:5], it['tag'], t // 60, t % 60, it['quadro'], ' !' if it.get('suspeita') else ''), fill=(255, 200, 0) if it['tag'] == 'marca' else (220, 220, 220), font=fonte)
        p = os.path.join(saida, 'folha_%02d.png' % (k // por + 1)); folha.save(p); caminhos.append(p)
    return caminhos


def marcas_graf(pasta):
    """Relatorio das marcas F8 da sessao: nota do jogador, configuracao, texturas usadas (substituicao pendente / sem hash) e as capturas da marca."""
    base = pasta.rstrip('/' + chr(92))
    sessao = os.path.basename(base)[:-len('_capturas')]
    log = os.path.join(os.path.dirname(base), sessao + '.log')
    if not os.path.exists(log): return
    L = open(log, errors='ignore').read().splitlines()
    marcas = {}
    for l in L:
        m = re.search(r'\[marca-graf(-nota)?\] #(\d+)(:| )(.*)', l)
        if m: marcas.setdefault(int(m.group(2)), []).append((bool(m.group(1)), m.group(4)))
    if not marcas: return
    out = ['# Marcas F8 (problema grafico) - %s' % sessao, '']
    for n, ls in sorted(marcas.items()):
        nota = next((t.lstrip() for nt, t in ls if nt), '')
        quadro = next((re.search(r'quadro (\d+)', t).group(1) for nt, t in ls if not nt and t.startswith('quadro')), '?')
        out += ['## Marca #%d (quadro %s)' % (n, quadro), '', '**Anotacao do jogador:** ' + (nota or '_(sem anotacao)_'), '']
        for nt, t in ls:
            if not nt and (t.startswith('config') or t.startswith('saida') or t.startswith('texturas usadas')): out.append('- ' + t)
        tex = [t for nt, t in ls if not nt and t.lstrip().startswith('tex ')]
        carr = [t for t in tex if 'CARREGANDO' in t]; semh = [t for t in tex if 'sem hash' in t]
        out += ['- texturas listadas: %d; com substituta ainda carregando: %d; sem hash: %d' % (len(tex), len(carr), len(semh)), '']
        if carr: out += ['Substitutas carregando (mostrando a original):'] + ['    ' + t.strip() for t in carr] + ['']
        out += ['Texturas com hash (o hash e o nome do PNG no pacote de texturas):'] + ['    ' + t.strip() for t in tex if 'com hash' in t or 'ampliada' in t] + ['']
        if quadro != '?':
            # a captura principal tem o numero do quadro da marca (logs ate 2026-10-08: o quadro seguinte) e as outras 2 vem 10 e 20 quadros depois
            q = int(quadro)
            caps = sorted(set(c for d in (0, 1, 10, 20) for c in glob.glob(os.path.join(base, '*_q%d_marca.png' % (q + d)))))
            out += ['Capturas (inteiras; a 2a e a 3a sao 10 e 20 quadros depois, para piscadas):'] + ['    ' + c for c in caps] + ['']
            # caixa-preta: os ~1,5 s de quadros apresentados ANTES do F8 (runtime/gpu/capturas.inc), analisada por tools/analysis/seq_analisar.py
            # formato atual "(F8 #n, quadro N)"; o antigo "(F8, quadro N+1)" so trazia o quadro
            seqs = [int(m.group(1)) for l in L for m in [re.search(r'\[captura\] sequencia (\d+) \(F8(?: #(\d+))?, quadro (\d+)\)', l)]
                    if m and (int(m.group(2)) == n if m.group(2) else int(m.group(3)) in (q, q + 1))]
            for sid in seqs:
                sp = os.path.join(base, 'seq_%d' % sid)
                if not os.path.isdir(sp): continue
                r = subprocess.run([sys.executable, os.path.join(RAIZ, 'tools', 'analysis', 'seq_analisar.py'), sp], capture_output=True, text=True, errors='ignore')
                out += ['Caixa-preta (quadros apresentados antes do F8): ' + sp, '```'] + (r.stdout.strip() or r.stderr.strip()).splitlines() + ['```', '']
    os.makedirs(os.path.join(RAIZ, 'gen', 'marcas'), exist_ok=True)
    dst = os.path.join(RAIZ, 'gen', 'marcas', sessao + '_graf.md')
    txt = chr(10).join(out)
    open(dst, 'w', encoding='utf-8').write(txt)
    print(txt); print('(relatorio gravado em %s)' % dst)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    por = 12
    if '--por-folha' in sys.argv:
        por = int(sys.argv[sys.argv.index('--por-folha') + 1])
        args = [a for a in args if a != str(por)]
    pasta = achar_pasta(args[0] if args else 'ultima')
    itens = ler_indice(pasta)
    if not itens:
        sys.exit('pasta sem capturas: %s' % pasta)
    conta = collections.Counter(i['tag'] for i in itens)
    sus = []
    for it in itens:
        f = sinais(Image.open(os.path.join(pasta, it['arq'])))
        m = suspeito(f); it['suspeita'] = m
        if m: sus.append((it, m))
    with open(os.path.join(pasta, 'suspeitas.tsv'), 'w', encoding='utf-8') as fo:
        for it, m in sus: fo.write('%s\t%s\t%s\n' % (it['arq'], it['tag'], '; '.join(m)))
    cam = folhas(pasta, itens, por)
    t0, t1 = itens[0]['ms'] // 1000, itens[-1]['ms'] // 1000
    print('pasta:', pasta)
    print('%d capturas (%s) de %d:%02d a %d:%02d' % (len(itens), ', '.join('%s %d' % kv for kv in conta.items()), t0 // 60, t0 % 60, t1 // 60, t1 % 60))
    print('MARCAS (F8, quadro inteiro):', [i['arq'] for i in itens if i['tag'] == 'marca'] or 'nenhuma')
    print('SUSPEITAS automaticas (%d):' % len(sus))
    for it, m in sus[:20]: print('  %s [%s] %s' % (it['arq'], it['tag'], '; '.join(m)))
    print('FOLHAS:', *cam, sep='\n  ')
    marcas_graf(pasta)


main()
