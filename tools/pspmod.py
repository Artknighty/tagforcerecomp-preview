"""Carregador de modulos PSP (PRX relocavel ja descriptografado) com relocacoes aplicadas.

Uso: python tools/pspmod.py <modulo.prx> <base_hex> [savestate.ppst]
Com savestate, compara a imagem relocada com a RAM do estado (oraculo).
"""
import struct, sys
from elfscan import load

R_MIPS_32, R_MIPS_26, R_MIPS_HI16, R_MIPS_LO16 = 2, 4, 5, 6

HILO_ADDRS = set()   # enderecos formados por pares HI16/LO16 (constantes de ponteiro)
PTR32_ADDRS = set()  # alvos das relocacoes R_MIPS_32 (ponteiros em dados: tabelas de funcoes, callbacks, rotulos de switch)

def load_module(path, base):
    d, et, secs = load(path)
    # segmento unico: ELF program header 0
    phoff, = struct.unpack_from('<I', d, 28)
    p_type, p_off, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align = struct.unpack_from('<8I', d, phoff)
    img = bytearray(p_memsz)
    img[:p_filesz] = d[p_off:p_off + p_filesz]
    hi_pending = []
    HILO_ADDRS.clear(); PTR32_ADDRS.clear()
    nrel = 0
    for s in secs:
        if s['type'] != 0x700000a0:
            continue
        for i in range(s['size'] // 8):
            r_off, r_info = struct.unpack_from('<II', d, s['off'] + i * 8)
            typ = r_info & 0xff
            ofs_base = (r_info >> 8) & 0xff
            addr_base = (r_info >> 16) & 0xff
            assert ofs_base == 0 and addr_base == 0, 'modulo multi-segmento nao suportado'
            w, = struct.unpack_from('<I', img, r_off)
            A = base  # so ha um segmento, com vaddr 0
            if typ == R_MIPS_32:
                w = (w + A) & 0xffffffff
                PTR32_ADDRS.add(w)
            elif typ == R_MIPS_26:
                w = (w & 0xfc000000) | ((w + (A >> 2)) & 0x03ffffff)
            elif typ == R_MIPS_HI16:
                hi_pending.append(r_off)
                continue
            elif typ == R_MIPS_LO16:
                lo = w & 0xffff
                if lo & 0x8000: lo -= 0x10000
                for ho in hi_pending:
                    hw, = struct.unpack_from('<I', img, ho)
                    val = ((hw & 0xffff) << 16) + lo + A
                    HILO_ADDRS.add(val & 0xffffffff)
                    hi = ((val >> 16) + (1 if val & 0x8000 else 0)) & 0xffff
                    struct.pack_into('<I', img, ho, (hw & 0xffff0000) | hi)
                hi_pending.clear()
                lo_new = (lo + A) & 0xffff
                w = (w & 0xffff0000) | lo_new
            else:
                raise SystemExit('reloc tipo %d desconhecido' % typ)
            struct.pack_into('<I', img, r_off, w)
            nrel += 1
    return bytes(img), secs, nrel

if __name__ == '__main__':
    if sys.argv[1] == '--dump':   # --dump <prx> <base> <saida.img>: imagem relocada crua (uso local, nao distribuir)
        open(sys.argv[4], 'wb').write(load_module(sys.argv[2], int(sys.argv[3], 16))[0]); sys.exit(0)
    path, base = sys.argv[1], int(sys.argv[2], 16)
    img, secs, nrel = load_module(path, base)
    print('imagem %d bytes, %d relocs' % (len(img), nrel))
    if len(sys.argv) > 3:
        sys.path.insert(0, os.path.join(os.environ.get("TF_TOOLS") or os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "gen", "ferramentas_decomp"), "scripts"))
        from achar_bases import ler_state
        raw = ler_state(sys.argv[3])
        o = 0x48 + base - 0x08000000
        ram = raw[o:o + len(img)]
        text = next(s for s in secs if s['name'] == '.text')
        ok = sum(1 for i in range(0, text['size'], 4) if ram[i:i+4] == img[i:i+4])
        print('.text: %d/%d palavras iguais a RAM do savestate' % (ok, text['size'] // 4))
