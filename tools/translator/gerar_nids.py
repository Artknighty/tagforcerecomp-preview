"""Gera gen/nids_hle.tsv (NID <tab> nome) a partir dos nomes das chamadas de sistema que o PROPRIO runtime implementa (`HLE(nome)` em runtime/*.cpp).
O NID de uma funcao da biblioteca do PSP e os 4 primeiros bytes do SHA-1 do nome (little-endian), entao nao precisamos de nenhuma tabela externa:
as importacoes dos modulos que o runtime nao implementa ficam sem nome (aparecem como NID se o jogo as chamar). Conferido contra a tabela do
Ghidra: os 241 nomes HLE dao exatamente os mesmos NIDs. Uso: python tools/translator/gerar_nids.py"""
import glob, hashlib, os, re

def nid(nome):
    h = hashlib.sha1(nome.encode()).digest()
    return '%02X%02X%02X%02X' % (h[3], h[2], h[1], h[0])

def main():
    raiz = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    nomes = set()
    for f in glob.glob(os.path.join(raiz, 'runtime', '*.cpp')):
        for m in re.finditer(r'^HLE\((\w+)\)', open(f, encoding='utf-8', errors='replace').read(), re.M): nomes.add(m.group(1))
    # Also the imports the decomp calls by name (SwapCtx::callImport("sprintf", ...)). The runtime has no HLE for libc functions such as
    # sprintf/strlen, so without these names the packaged translator leaves them as nid_XXXXXXXX and callImport finds nothing (crash on the public build).
    for f in glob.glob(os.path.join(raiz, 'decomp', '**', '*.cpp'), recursive=True) + glob.glob(os.path.join(raiz, 'decomp', '**', '*.h'), recursive=True):
        for m in re.finditer(r'callImport\("(\w+)"', open(f, encoding='utf-8', errors='replace').read()): nomes.add(m.group(1))
    os.makedirs(os.path.join(raiz, 'gen'), exist_ok=True)
    out = os.path.join(raiz, 'gen', 'nids_hle.tsv')
    open(out, 'w', encoding='utf-8', newline='\n').write(''.join('%s\t%s\n' % (nid(n), n) for n in sorted(nomes)))
    print('%d nomes HLE -> %s' % (len(nomes), out))

if __name__ == '__main__':
    main()
