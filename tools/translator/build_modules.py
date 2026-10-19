"""Recompila todos os modulos de tools/translator/modules.tsv -> gen/<nome>.c e gen/module_index.c
Uso: python tools/translator/build_modules.py [nome ...]   (sem nomes: todos)
Le os PRX de <EXTRAIDOS>/modulos (copias extraidas localmente do ISO do usuario)."""
import os, sys, subprocess
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
VER = os.environ.get("TF_VER", "")   # versao do jogo (ex. ULUS10136); vazio = a europeia ULES00600 (gen/, gen/jogo, tools/translator/modules.tsv)
GEN = os.path.join(ROOT, "gen", VER) if VER else os.path.join(ROOT, "gen")   # codigo C gerado desta versao
M = os.environ.get("TF_TOOLS") or os.path.join(ROOT, "gen", "ferramentas_decomp")   # copia local dos modulos extraidos + exportacao (gen/ fica fora do git)

def rows():
    """(nome_C, modulo, base, caminho_no_iso, dir_export). Um modulo pode aparecer em varias linhas com bases diferentes
    (variantes para contextos de memoria diferentes); a 1a linha usa o nome do modulo, as outras <modulo>_<base>."""
    seen = set()
    for ln in open(os.path.join(ROOT, 'tools', 'translator', 'modules_%s.tsv' % VER if VER else 'modules.tsv'), encoding='utf-8'):
        if ln.startswith('#') or not ln.strip(): continue
        n, base, iso, exp = ln.rstrip('\n').split('\t')[:4]
        b = int(base, 16)
        c = n if n not in seen else '%s_%08x' % (n, b)
        seen.add(n)
        yield c, n, b, iso, exp

JOGO = os.environ.get("TF_JOGO") or os.path.join(ROOT, "gen", "jogo_" + VER if VER else "jogo")   # dados instalados (run.exe --instalar ou tools/game/extrair_jogo.py): fonte preferida dos modulos
ISO_DE = {}   # modulo -> caminho dentro do disco (modules.tsv)
def src_path(n, iso=''):
    cand = os.path.join(JOGO, 'PSP_GAME', *(iso or ISO_DE.get(n, '')).split('/'))
    if (iso or ISO_DE.get(n)) and os.path.isfile(cand): return cand
    return os.path.join(M, 'modulos', 'BOOT.BIN') if n == 'BOOT' else os.path.join(M, 'modulos', 'gmodule', n + '.prx')   # copia antiga (ferramentas do Ghidra)

if __name__ == '__main__':
    want = set(sys.argv[1:])
    os.makedirs(GEN, exist_ok=True)
    idx = ['#include "hle.h"', '', 'extern "C" {']
    tab = []
    boot_base = next(b for c, n, b, i, e in rows() if n == 'BOOT')
    # base com que cada modulo foi importado no Ghidra (dados/bases.tsv; ausente = importado com base 0)
    first_base = {}
    for c, n, b, i, e in rows(): first_base[n] = 0; ISO_DE[n] = i
    bases_tsv = os.path.join(M, 'dados', 'bases.tsv')   # so existe com a exportacao do Ghidra; sem ela as funcoes vem de recomp.discover_starts
    for ln in (open(bases_tsv, encoding='utf-8') if os.path.exists(bases_tsv) else []):
        p = ln.split()
        if len(p) >= 2 and not ln.startswith('#') and p[0] in first_base: first_base[p[0]] = int(p[1], 16)
    for c, mod, base, iso, exp in rows():
        n = c   # prefixo C / nome do arquivo gerado
        idx.append('extern const FnEntry %s_fns[]; extern const unsigned %s_nfns; extern const ImportEntry %s_imports[]; extern const unsigned %s_nimports;' % (n, n, n, n))
        idx.append('extern const ExportEntry %s_exports[]; extern const unsigned %s_nexports; extern const uint32_t %s_gp, %s_base, %s_memsz, %s_textsz, %s_datasz, %s_bsssz, %s_entry;' % ((n,) * 9))
        tab.append('  {"%s", "%s", %s_base, %s_gp, %s_memsz, %s_textsz, %s_datasz, %s_bsssz, %s_entry, %s_fns, %s_nfns, %s_imports, %s_nimports, %s_exports, %s_nexports},' % ((n, iso) + (n,) * 13))
        if want and n not in want: continue
        tsv = os.path.join(M, 'export', exp, '_funcoes.tsv')
        out = os.path.join(GEN, n + '.c')
        print('recomp', n, flush=True)
        extra = [] if mod == 'BOOT' else ['%s@%08x' % (src_path('BOOT', ''), boot_base)]   # variaveis importadas do BOOT
        # o _funcoes.tsv do Ghidra esta na base da 1a linha do modulo (a de bases.tsv); variantes precisam rebasear
        tsv_arg = ('%s@%08x' % (tsv, first_base[mod])) if os.path.exists(tsv) and os.environ.get('TF_USAR_GHIDRA') else '-'   # padrao: SEM o Ghidra (recomp.discover_starts; identico nos testes). TF_USAR_GHIDRA=1 usa a exportacao de gen/ferramentas_decomp
        subprocess.check_call([sys.executable, os.path.join(ROOT, 'tools', 'translator', 'recomp.py'), src_path(mod, iso), '%08x' % base, tsv_arg, out, n] + extra)
    open(os.path.join(GEN, 'modulos.tsv'), 'w', newline='\n').write(''.join('%s\t%s\n' % (c, i) for c, n, b, i, e in rows()))   # lista para o JIT (runtime/core/jit.cpp)
    idx.append('}')
    idx.append('extern "C" const GenModule g_gen_modules[] = {')
    idx += tab
    idx.append('};')
    idx.append('extern "C" const unsigned g_gen_nmodules = %d;' % len(tab))
    open(os.path.join(GEN, 'module_index.cpp'), 'w', newline='\n').write('\n'.join(idx) + '\n')
