"""Le a tabela de imports (.lib.stub) e o sceModuleInfo de um modulo PSP relocado."""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pspmod import load_module

NIDS_TSV = os.path.join(os.environ.get("TF_TOOLS") or os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "gen", "ferramentas_decomp"), "dados", "nids.tsv")

NIDS_HLE = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "gen", "nids_hle.tsv")   # gerada por tools/translator/gerar_nids.py dos nomes HLE(...) do runtime

def load_nid_names():
    names = {}
    tsv = NIDS_TSV if os.path.exists(NIDS_TSV) else NIDS_HLE   # sem a tabela do Ghidra, vale a derivada dos HLEs do runtime (NID = SHA-1 do nome)
    if not os.path.exists(tsv):
        import gerar_nids; gerar_nids.main()
    for ln in open(tsv, encoding='utf-8'):
        p = ln.rstrip('\n').split('\t')
        if len(p) >= 2 and p[0][:1] != '#':
            try: names[int(p[0], 16)] = p[1]
            except ValueError: pass
    return names

def cstr(img, base, addr):
    o = addr - base
    return img[o:img.index(b'\0', o)].decode('ascii', 'replace')

def parse(path, base):
    img, secs, _ = load_module(path, base)
    sd = {s['name']: s for s in secs}
    mi = sd['.rodata.sceModuleInfo']['addr']
    attr, ver = struct.unpack_from('<HH', img, mi)
    name = img[mi + 4:mi + 4 + 27].split(b'\0')[0].decode()
    gp, ent_top, ent_end, stub_top, stub_end = struct.unpack_from('<5I', img, mi + 0x20)
    info = dict(name=name, gp=gp, ent_top=ent_top, ent_end=ent_end, stub_top=stub_top, stub_end=stub_end)
    nids = load_nid_names()
    imports = []   # (lib, nid, stub_addr, nome)
    o = stub_top - base
    while o < stub_end - base:
        namep, ver, flags, size, nvar, nfunc = struct.unpack_from('<IHHBBH', img, o)
        nidp, stubp = struct.unpack_from('<II', img, o + 12)
        lib = cstr(img, base, namep)
        for i in range(nfunc):
            nid, = struct.unpack_from('<I', img, nidp - base + 4 * i)
            imports.append((lib, nid, stubp + 8 * i, nids.get(nid, '')))
        o += size * 4
    return img, secs, info, imports

def exports(img, base, info):
    """[(lib, nid, addr)] das exportacoes (.lib.ent). lib '' = exportacoes do proprio modulo (module_start...)."""
    out = []
    o = info['ent_top'] - base
    while o < info['ent_end'] - base:
        namep, ver, attr, size, nvar, nfunc, _r = struct.unpack_from('<IHHBBHI', img, o)
        nidp, = struct.unpack_from('<I', img, o + 12)   # 'resident': NIDs seguidos dos enderecos
        entp = nidp + 4 * (nfunc + nvar)
        lib = cstr(img, base, namep) if namep else ''
        for i in range(nfunc + nvar):
            nid, = struct.unpack_from('<I', img, nidp - base + 4 * i)
            addr, = struct.unpack_from('<I', img, entp - base + 4 * i)
            out.append((lib, nid, addr))
        o += size * 4
    return out

if __name__ == '__main__':
    img, secs, info, imps = parse(sys.argv[1], int(sys.argv[2], 16))
    print({k: (hex(v) if isinstance(v, int) else v) for k, v in info.items()})
    import collections
    per = collections.Counter(i[0] for i in imps)
    print(len(imps), 'imports;', dict(per))
    unk = sum(1 for i in imps if not i[3]); print(unk, 'sem nome no nids.tsv')
    if len(sys.argv) > 3:
        for l, n, a, nm in imps: print('%08x %-20s %08x %s' % (a, l, n, nm))

def apply_var_imports(img, base, info, resolve):
    """Importacoes de VARIAVEIS (sceVstub): cada entrada de .lib.stub com nvar > 0 aponta (em +20) para pares
    {lista_de_refs, nid}; cada ref = tipo << 26 | endereco >> 2 (ja relocado). O loader do PSP soma o endereco da
    variavel exportada (resolve(lib, nid)) nessas instrucoes/palavras: 2 = R_MIPS_32, 5/6 = par HI16/LO16.
    img: bytearray (modificado no lugar). Devolve o numero de refs aplicadas."""
    n = 0
    o = info['stub_top'] - base
    while o < info['stub_end'] - base:
        namep, ver, flags, size, nvar, nfunc = struct.unpack_from('<IHHBBH', img, o)
        if nvar and size >= 6:
            lib = cstr(img, base, namep)
            varp, = struct.unpack_from('<I', img, o + 20)
            for i in range(nvar):
                refp, nid = struct.unpack_from('<II', img, varp - base + 8 * i)
                addr = resolve(lib, nid)
                if addr is None: raise SystemExit('variavel importada %s::%08x sem export' % (lib, nid))
                his = []
                r = refp - base
                while True:
                    w, = struct.unpack_from('<I', img, r); r += 4
                    if w == 0: break
                    typ, at = w >> 26, ((w & 0x03ffffff) << 2) - base
                    cur, = struct.unpack_from('<I', img, at)
                    if typ == 2: struct.pack_into('<I', img, at, (cur + addr) & 0xffffffff)
                    elif typ == 5: his.append(at); continue
                    elif typ == 6:
                        lo = cur & 0xffff
                        if lo & 0x8000: lo -= 0x10000
                        for h in his:
                            hw, = struct.unpack_from('<I', img, h)
                            val = (((hw & 0xffff) << 16) + lo + addr) & 0xffffffff
                            struct.pack_into('<I', img, h, (hw & 0xffff0000) | (((val >> 16) + (1 if val & 0x8000 else 0)) & 0xffff))
                        his = []
                        struct.pack_into('<I', img, at, (cur & 0xffff0000) | ((lo + addr) & 0xffff))
                    else: raise SystemExit('tipo de ref de variavel %d nao suportado' % typ)
                    n += 1
        o += size * 4
    return n
