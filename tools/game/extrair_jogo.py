"""Instala os dados do jogo (versao em Python da instalacao do run.exe: o resultado e o MESMO formato de pasta, ver runtime/core/isofolder.cpp).
Serve para a primeira compilacao (o run.exe ainda nao existe) e para scripts. Uso: python tools/game/extrair_jogo.py <iso> [pasta]   (padrao: gen/jogo)
Cria: arquivos soltos com os nomes originais, _setores.bin (setores fora de arquivos e nao-nulos) e, por ultimo, _disco.tsv (instalacao completa).
Nada vai para o git (a pasta fica em gen/ ou jogo/, ambos ignorados)."""
import os, struct, sys
sys.path.insert(0, os.path.dirname(__file__))
import isofs

def main():
    if len(sys.argv) < 2: raise SystemExit(__doc__)
    iso = isofs.Iso(sys.argv[1])
    dest = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(__file__), '..', '..', 'gen', 'jogo')
    os.makedirs(dest, exist_ok=True)
    itens = []   # (tipo, lba, tamanho, caminho)
    def anda(lba, tam, pref):
        for nome, el, es, d in iso._entradas(lba, tam):
            p = pref + nome
            itens.append(('D' if d else 'F', el, es, p))
            if d: anda(el, es, p + '/')
    anda(*iso.raiz, '')
    tamanho_iso = os.path.getsize(sys.argv[1]); nsect = tamanho_iso // 2048
    cobertos = []; vistos = {}; feito = 0; total = sum(i[2] for i in itens if i[0] == 'F')
    for tipo, lba, tam, p in itens:
        alvo = os.path.join(dest, *p.split('/'))
        if tipo == 'D': os.makedirs(alvo, exist_ok=True); continue
        if lba in vistos: continue   # o mesmo arquivo com dois nomes: so uma copia
        vistos[lba] = p; cobertos.append((lba, (tam + 2047) // 2048))
        os.makedirs(os.path.dirname(alvo), exist_ok=True)
        with open(alvo, 'wb') as f:
            off = 0
            while off < tam:
                n = min(4 << 20, tam - off)
                iso.f.seek(lba * 2048 + off); f.write(iso.f.read(n)); off += n; feito += n
        print('\r[extrair] %3d%% %s' % (feito * 100 // max(total, 1), p[-60:]), end='', flush=True)
    print()
    # setores fora dos arquivos que nao sao zeros
    cobertos.sort(); runs = []; meta = bytearray(); cur = 0
    def lacuna(a, b):
        s = a
        while s < b:
            n = min(b - s, 512); iso.f.seek(s * 2048); t = iso.f.read(n * 2048)
            for q in range(n):
                sec = t[q * 2048:(q + 1) * 2048]
                if sec.count(0) == 2048: continue
                if runs and runs[-1][0] + runs[-1][1] == s + q: runs[-1][1] += 1
                else: runs.append([s + q, 1])
                meta.extend(sec)
            s += n
    for lba, ns in cobertos:
        if lba > cur: lacuna(cur, lba)
        cur = max(cur, lba + ns)
    if cur < nsect: lacuna(cur, nsect)
    with open(os.path.join(dest, '_setores.bin'), 'wb') as f:
        f.write(struct.pack('<II', 0x45534654, len(runs)))
        for r in runs: f.write(struct.pack('<II', r[0], r[1]))
        f.write(bytes(meta))
    linhas = ['#tagforce-disco 1', '#setores %d' % nsect, 'D %d %d ' % iso.raiz]
    linhas += ['%s %d %d %s' % (t[0], t[1], t[2], t[3]) for t in itens]
    tmp = os.path.join(dest, '_disco.tsv.tmp')
    open(tmp, 'w', encoding='utf-8', newline='\n').write('\n'.join(linhas) + '\n')
    os.replace(tmp, os.path.join(dest, '_disco.tsv'))   # o indice por ultimo = instalacao completa
    print('[extrair] pronto:', os.path.abspath(dest))

if __name__ == '__main__':
    main()
