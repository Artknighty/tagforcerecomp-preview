"""Tradutor estatico Allegrex -> C.

Uso: python tools/recomp.py <modulo.prx> <base_hex> <funcoes.tsv|-> <saida.c> [nome]
Uma funcao C por funcao (particao do .text pelos enderecos do Ghidra + alvos de jal).
"""
import os, sys, struct, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pspmod import load_module
from allegrex import decode

CMP = {'c.f': '0', 'c.un': 'isunordered(a,b)', 'c.eq': 'a==b', 'c.ueq': 'a==b||isunordered(a,b)',
       'c.olt': 'a<b', 'c.ult': 'a<b||isunordered(a,b)', 'c.ole': 'a<=b', 'c.ule': 'a<=b||isunordered(a,b)',
       'c.sf': '0', 'c.ngle': 'isunordered(a,b)', 'c.seq': 'a==b', 'c.ngl': 'a==b||isunordered(a,b)',
       'c.lt': 'a<b', 'c.nge': 'a<b||isunordered(a,b)', 'c.le': 'a<=b', 'c.ngt': 'a<=b||isunordered(a,b)'}

def vec_regs(n, reg):
    mtx = (reg >> 2) & 7; col = reg & 3; transpose = (reg >> 5) & 1
    row = {1: (reg >> 5) & 3, 2: (reg >> 5) & 2, 3: (reg >> 6) & 1, 4: (reg >> 5) & 2}[n]
    if n == 1: transpose = 0
    out = []
    for i in range(n):
        idx = mtx * 4
        idx += (((row + i) & 3) + col * 32) if transpose else (col + ((row + i) & 3) * 32)
        out.append(idx)
    return out

def Rr(n): return '0u' if n == 0 else 'c->r[%d]' % n
def W(n, e): return '' if n == 0 else 'c->r[%d] = %s;' % (n, e)

BR_COND = {  # mnemonico -> (expr, likely)
    'beq': ('a == b', 0), 'bne': ('a != b', 0),
    'blez': ('(int32_t)a <= 0', 0), 'bgtz': ('(int32_t)a > 0', 0),
    'bltz': ('(int32_t)a < 0', 0), 'bgez': ('(int32_t)a >= 0', 0),
    'beql': ('a == b', 1), 'bnel': ('a != b', 1),
    'blezl': ('(int32_t)a <= 0', 1), 'bgtzl': ('(int32_t)a > 0', 1),
    'bltzl': ('(int32_t)a < 0', 1), 'bgezl': ('(int32_t)a >= 0', 1),
    'bc1t': ('c->cc', 0), 'bc1f': ('!c->cc', 0), 'bc1tl': ('c->cc', 1), 'bc1fl': ('!c->cc', 1),
    'bvt': ('VCC', 0), 'bvf': ('!VCC', 0), 'bvtl': ('VCC', 1), 'bvfl': ('!VCC', 1)}
JUMPS = set(BR_COND) | {'j', 'jal', 'jr', 'jalr', 'bltzal', 'bgezal', 'bltzall', 'bgezall'}
BINOP = {'addu': '%s + %s', 'add': '%s + %s', 'subu': '%s - %s', 'sub': '%s - %s', 'and': '%s & %s',
         'or': '%s | %s', 'xor': '%s ^ %s', 'nor': '~(%s | %s)'}
HILO = ('c->lo = (uint32_t)p; c->hi = (uint32_t)(p >> 32); }')

def simple(m, f, w, pc):
    """Instrucao sem fluxo de controle -> statement C (string)."""
    rs, rt, rd, sa, imm, u = f['rs'], f['rt'], f['rd'], f['sa'], f['imm'], f['uimm']
    a, b = Rr(rs), Rr(rt)
    S = lambda e: W(rd, e)
    T = lambda e: W(rt, e)
    if m == 'nop': return ''
    if m in BINOP: return S(BINOP[m] % (a, b))
    if m == 'slt': return S('(int32_t)%s < (int32_t)%s' % (a, b))
    if m == 'sltu': return S('%s < %s' % (a, b))
    if m == 'sll': return S('%s << %d' % (b, sa))
    if m == 'srl': return S('%s >> %d' % (b, sa))
    if m == 'sra': return S('(uint32_t)((int32_t)%s >> %d)' % (b, sa))
    if m == 'rotr': return S('(%s >> %d) | (%s << %d)' % (b, sa, b, (32 - sa) & 31)) if sa else S(b)
    if m == 'sllv': return S('%s << (%s & 31)' % (b, a))
    if m == 'srlv': return S('%s >> (%s & 31)' % (b, a))
    if m == 'srav': return S('(uint32_t)((int32_t)%s >> (%s & 31))' % (b, a))
    if m == 'rotrv': return S('(%s >> (%s & 31)) | (%s << ((32 - (%s & 31)) & 31))' % (b, a, b, a))
    if m == 'movz': return ('if (%s == 0) { %s }' % (b, S(a))) if rd else ''
    if m == 'movn': return ('if (%s != 0) { %s }' % (b, S(a))) if rd else ''
    if m == 'max': return S('(int32_t)%s > (int32_t)%s ? %s : %s' % (a, b, a, b))
    if m == 'min': return S('(int32_t)%s < (int32_t)%s ? %s : %s' % (a, b, a, b))
    if m == 'clz': return S('clz_(%s)' % a)
    if m == 'clo': return S('clz_(~%s)' % a)
    if m == 'mfhi': return S('c->hi')
    if m == 'mflo': return S('c->lo')
    if m == 'mthi': return 'c->hi = %s;' % a
    if m == 'mtlo': return 'c->lo = %s;' % a
    acc = '(int64_t)(((uint64_t)c->hi << 32) | c->lo)'
    uacc = '(((uint64_t)c->hi << 32) | c->lo)'
    if m == 'mult': return '{ int64_t p = (int64_t)(int32_t)%s * (int32_t)%s; %s' % (a, b, HILO)
    if m == 'multu': return '{ uint64_t p = (uint64_t)%s * %s; %s' % (a, b, HILO)
    if m == 'madd': return '{ int64_t p = %s + (int64_t)(int32_t)%s * (int32_t)%s; %s' % (acc, a, b, HILO)
    if m == 'maddu': return '{ uint64_t p = %s + (uint64_t)%s * %s; %s' % (uacc, a, b, HILO)
    if m == 'msub': return '{ int64_t p = %s - (int64_t)(int32_t)%s * (int32_t)%s; %s' % (acc, a, b, HILO)
    if m == 'msubu': return '{ uint64_t p = %s - (uint64_t)%s * %s; %s' % (uacc, a, b, HILO)
    if m == 'div': return 'div_(c, (int32_t)%s, (int32_t)%s);' % (a, b)
    if m == 'divu': return 'divu_(c, %s, %s);' % (a, b)
    if m in ('addi', 'addiu'): return T('%s + %du' % (a, imm & 0xffffffff))
    if m == 'slti': return T('(int32_t)%s < %d' % (a, imm))
    if m == 'sltiu': return T('%s < %du' % (a, imm & 0xffffffff))
    if m == 'andi': return T('%s & 0x%xu' % (a, u))
    if m == 'ori': return T('%s | 0x%xu' % (a, u))
    if m == 'xori': return T('%s ^ 0x%xu' % (a, u))
    if m == 'lui': return T('0x%xu' % (u << 16))
    if m == 'ext': return T('(%s >> %d) & 0x%xu' % (a, sa, ((1 << (rd + 1)) - 1) & 0xffffffff))
    if m == 'ins':
        size = rd - sa + 1
        mask = ((1 << size) - 1) & 0xffffffff
        return T('(%s & ~0x%xu) | ((%s & 0x%xu) << %d)' % (b, (mask << sa) & 0xffffffff, a, mask, sa))
    if m == 'seb': return S('(uint32_t)(int32_t)(int8_t)%s' % b)
    if m == 'seh': return S('(uint32_t)(int32_t)(int16_t)%s' % b)
    if m == 'wsbh': return S('((%s & 0xff00ff00u) >> 8) | ((%s & 0x00ff00ffu) << 8)' % (b, b))
    if m == 'wsbw': return S('__builtin_bswap32(%s)' % b)
    if m == 'bitrev': return S('bitrev_(%s)' % b)
    addr = '(%s + %du)' % (a, imm & 0xffffffff)
    if m in ('lb', 'lbu', 'lh', 'lhu', 'lw'): return T('%s%s' % (m.upper(), addr))
    if m == 'll': return T('LW%s' % addr)
    if m == 'lwl': return T('lwl_(c, %s, %s)' % (addr, b))
    if m == 'lwr': return T('lwr_(c, %s, %s)' % (addr, b))
    if m in ('sb', 'sh', 'sw'): return '%s(%s, %s);' % (m.upper(), addr, b)
    if m == 'swl': return 'swl_(c, %s, %s);' % (addr, b)
    if m == 'swr': return 'swr_(c, %s, %s);' % (addr, b)
    if m == 'sc': return 'SW(%s, %s); %s' % (addr, b, T('1u'))
    if m in ('sync', 'cache', 'synci'): return ''
    if m == 'lwc1': return 'c->f[%d].u = LW%s;' % (rt, addr)
    if m == 'swc1': return 'SW(%s, c->f[%d].u);' % (addr, rt)
    if m == 'syscall': return 'psp_syscall(c, 0x%08x, 0x%x);' % (pc, (w >> 6) & 0xfffff)
    if m == 'break': return 'psp_break(c, 0x%08x);' % pc
    if m == 'vfpu.lv.s' or m == 'vfpu.sv.s':
        vr = ((w >> 16) & 0x1f) | ((w & 3) << 5)
        ad = '(%s + %du)' % (a, (w & 0xfffc) - (0x10000 if w & 0x8000 else 0) & 0xffffffff)
        return ('c->v[%d].u = LW%s;' % (vr, ad)) if m == 'vfpu.lv.s' else ('SW(%s, c->v[%d].u);' % (ad, vr))
    if m == 'vfpu.lv.q' or m == 'vfpu.sv.q':
        vr = ((w >> 16) & 0x1f) | ((w & 1) << 5)
        regs = vec_regs(4, vr)
        ad = '(%s + %du)' % (a, (w & 0xfffc) - (0x10000 if w & 0x8000 else 0) & 0xffffffff)
        if m == 'vfpu.lv.q': return '{ uint32_t ad_ = %s; %s }' % (ad, ' '.join('c->v[%d].u = LW(ad_ + %d);' % (r, 4 * i) for i, r in enumerate(regs)))
        return '{ uint32_t ad_ = %s; %s }' % (ad, ' '.join('SW(ad_ + %d, c->v[%d].u);' % (4 * i, r) for i, r in enumerate(regs)))
    if m.startswith('vfpu.') and m not in ('vfpu.lvl/lvr.q', 'vfpu.svl/svr.q'):
        return 'vfpu_exec(c, 0x%08x);' % w
    # ---- FPU (ft=rt, fs=rd, fd=sa)
    ft, fs, fd = rt, rd, sa
    if m == 'mfc1': return T('c->f[%d].u' % fs)
    if m == 'mtc1': return 'c->f[%d].u = %s;' % (fs, b)
    if m == 'cfc1':
        if rd == 31: return T('(c->fcr31 & ~(1u << 23)) | (c->cc << 23)')
        return T('0x00003351u' if rd == 0 else '0u')
    if m == 'ctc1': return ('c->fcr31 = %s; c->cc = (%s >> 23) & 1;' % (b, b)) if rd == 31 else ''
    if m in ('add.s', 'sub.s', 'mul.s', 'div.s'):
        op = {'add.s': '+', 'sub.s': '-', 'mul.s': '*', 'div.s': '/'}[m]
        return 'c->f[%d].f = c->f[%d].f %s c->f[%d].f;' % (fd, fs, op, ft)
    if m == 'sqrt.s': return 'c->f[%d].f = sqrtf(c->f[%d].f);' % (fd, fs)
    if m == 'abs.s': return 'c->f[%d].u = c->f[%d].u & 0x7fffffffu;' % (fd, fs)
    if m == 'neg.s': return 'c->f[%d].u = c->f[%d].u ^ 0x80000000u;' % (fd, fs)
    if m == 'mov.s': return 'c->f[%d].u = c->f[%d].u;' % (fd, fs)
    if m in ('round.w.s', 'trunc.w.s', 'ceil.w.s', 'floor.w.s'):
        mode = {'round.w.s': 0, 'trunc.w.s': 1, 'ceil.w.s': 2, 'floor.w.s': 3}[m]
        return 'c->f[%d].i = f2i_(c->f[%d].f, %d);' % (fd, fs, mode)
    if m == 'cvt.w.s': return 'c->f[%d].i = f2i_(c->f[%d].f, 0);' % (fd, fs)
    if m == 'cvt.s.w': return 'c->f[%d].f = (float)c->f[%d].i;' % (fd, fs)
    if m in CMP:
        return '{ float a = c->f[%d].f, b = c->f[%d].f; c->cc = %s; }' % (fs, ft, CMP[m])
    return 'psp_unimpl(c, 0x%08x, 0x%08x);' % (pc, w)

def is_unimpl(m):
    return m is None or m in ('vfpu.lvl/lvr.q', 'vfpu.svl/svr.q') or m in ('halt/mfic', 'mfc0', 'mtc0')

def find_starts(img, base, tsize, tsv):
    tend = base + tsize
    word = lambda pc: struct.unpack_from('<I', img, pc - base)[0]
    starts = {base}
    if tsv != '-':
        # "<arquivo>@<base_hex>": o export do Ghidra foi feito com o modulo nessa base; rebaseia para a base desta variante
        tsv_path, tsv_base = (tsv.rsplit('@', 1)[0], int(tsv.rsplit('@', 1)[1], 16)) if '@' in tsv else (tsv, base)
        for ln in open(tsv_path, encoding='utf-8'):
            p = ln.split('\t')
            if len(p) >= 3 and p[0] != 'endereco':
                a = int(p[0], 16) - tsv_base + base
                if base <= a < tend: starts.add(a)
    from pspmod import HILO_ADDRS
    for a in HILO_ADDRS:
        if base <= a < tend and a % 4 == 0: starts.add(a)
    for pc in range(base, tend, 4):
        m, f = decode(word(pc), pc)
        if m == 'jal' and base <= f['target'] < tend: starts.add(f['target'])
    if tsv == '-': starts |= discover_starts(img, base, tsize)   # sem exportacao do Ghidra: descobre as funcoes por estrutura (ver abaixo)
    return sorted(starts)

def discover_starts(img, base, tsize):
    """Inicios de funcao SEM o Ghidra (dados 100% do proprio modulo). jal e pares HI/LO ja entram em find_starts; aqui entram os que so
    sao chamados por ponteiro (tabelas de funcoes, callbacks) ou por desvio de cauda. Candidatos: alvos de relocacoes R_MIPS_32 e de `j`; um
    candidato vale se (a) comeca com o prologo `addiu sp,sp,-N` (nas 3 primeiras instrucoes) logo depois do fim de um bloco sem retorno
    (jr ra, j, b incondicional; ignorando nops de alinhamento) ou (b) vem logo depois de `jr ra` + delay slot. Alem disso, varredura linear:
    prologo logo depois de `jr ra` + delay slot e inicio de funcao mesmo sem nenhuma referencia. Rotulos de switch (alvos de R_MIPS_32 DENTRO
    de funcao) nao tem esse formato e ficam de fora. Medido contra o Ghidra nos 23 modulos: acha 99,5% (10467 de 10524) e ~330 a mais."""
    from pspmod import PTR32_ADDRS
    tend = base + tsize
    word = lambda pc: struct.unpack_from('<I', img, pc - base)[0] if base <= pc < base + len(img) else 0
    JRRA = 0x03e00008
    def ends_block(w):
        op = w >> 26
        return w == JRRA or op == 2 or (op == 4 and ((w >> 21) & 31) == ((w >> 16) & 31))   # jr ra, j, beq r,r
    def prologue(pc):
        for i in range(3):
            w = word(pc + 4 * i)
            if (w & 0xffff0000) == 0x27bd0000 and (w & 0x8000): return True   # addiu sp,sp,-N
            if i == 0 and ((w >> 26) in (1, 2, 3, 4, 5, 6, 7) or (w & 0xfc00003f) in (8, 9)): break   # desvio na 1a instrucao
        return False
    def after_end(pc):
        k = pc - 4
        while k >= base and word(k) == 0: k -= 4   # nops de alinhamento
        if k != pc - 4 and (ends_block(word(k - 4)) or ends_block(word(k))): return True
        return ends_block(word(pc - 8))
    cand = {a for a in PTR32_ADDRS if base <= a < tend and a % 4 == 0}
    for pc in range(base, tend, 4):
        m, f = decode(word(pc), pc)
        if m == 'j' and base <= f['target'] < tend: cand.add(f['target'])
    out = {a for a in cand if (prologue(a) and after_end(a)) or (after_end(a) and word(a - 8) == JRRA)}
    for pc in range(base + 8, tend, 4):
        if prologue(pc) and ends_block(word(pc - 8)) and after_end(pc): out.add(pc)
    return out

def load_merges(name):
    """tools/merge.tsv: regioes [ini, fim) de assembly escrito a mao que viram UMA funcao C.
    Dentro dela, jal/bltzal para alvos internos sao 'chamadas locais' (goto + ra) e jr ra despacha
    pelos pontos de retorno conhecidos (sub-rotinas que saltam de volta ao laco principal sem retornar)."""
    out = []
    fn = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'merge.tsv')
    if os.path.exists(fn):
        for ln in open(fn, encoding='utf-8'):
            p = ln.split('#')[0].split()
            if len(p) >= 3 and p[0] == name: out.append((int(p[1], 16), int(p[2], 16)))
    return out

VARS_FROM = None   # (caminho, base) do modulo que exporta as variaveis importadas (o BOOT)

def build(path, base, tsv, name):
    img, secs, _ = load_module(path, base)
    if VARS_FROM:
        from imports import parse as _p0, exports as _e0, apply_var_imports
        vimg, _vs, vinfo, _vi = _p0(VARS_FROM[0], VARS_FROM[1])
        vexp = {(l, n): a for l, n, a in _e0(vimg, VARS_FROM[1], vinfo)}
        _i0, _s0, info0, _m0 = _p0(path, base)
        img = bytearray(img)
        nv = apply_var_imports(img, base, info0, lambda lib, nid: vexp.get((lib, nid)))
        if nv: print('%d refs de variaveis importadas aplicadas' % nv)
        img = bytes(img)
    text = next(s for s in secs if s['name'] == '.text')
    tsize = text['size']; tend = base + tsize
    word = lambda pc: struct.unpack_from('<I', img, pc - base)[0]
    starts = find_starts(img, base, tsize, tsv)
    from imports import parse as _parse, exports as _exports
    _i, _s, _info, _imps = _parse(path, base)
    extra = {a for l, n, a in _exports(_i, base, _info) if base <= a < base + tsize and a % 4 == 0}
    # ponto de entrada do ELF (sceKernelStartModule chama isso quando nao ha module_start exportado)
    e_entry = base + struct.unpack_from('<I', open(path, 'rb').read(32), 24)[0]
    if base <= e_entry < base + tsize: extra.add(e_entry)
    starts = set(starts) | extra
    merges = load_merges(name)
    merged_inner = set()
    for ms, me in merges:
        merged_inner |= {a for a in starts if ms < a < me}
        starts |= {ms, me}
    starts = sorted(starts - merged_inner)
    local_fns = {ms for ms, me in merges}
    sset = set(starts)
    # ponteiros em dados (tabelas de salto) -> alvos possiveis de jr indireto
    dptrs = set()
    for s in secs:
        if s['addr'] >= tsize and s['type'] == 1 and s['name'] != '.sceStub.text':
            for off in range(s['addr'], s['addr'] + s['size'] - 3, 4):
                v = struct.unpack_from('<I', img, off)[0]
                if base <= v < tend and v % 4 == 0: dptrs.add(v)
    # Entradas secundarias: enderecos no meio de uma funcao que podem ser alvo vindo de fora dela
    # (ponteiros em dados - callbacks/vtables que o Ghidra nao listou - e branches/j que cruzam fronteiras).
    # Cada uma vira um wrapper NAME_e_<end> que entra no corpo da funcao por um switch.
    owner = {}
    def owner_of(a):
        import bisect
        k = bisect.bisect_right(starts, a) - 1
        return starts[k] if k >= 0 else None
    xtargets = set(dptrs) | merged_inner
    for i, fa in enumerate(starts):
        fe = starts[i + 1] if i + 1 < len(starts) else tend
        for pc in range(fa, fe, 4):
            m, f = decode(word(pc), pc)
            if (m in BR_COND or m == 'j') and not (fa <= f['target'] < fe) and base <= f['target'] < tend:
                xtargets.add(f['target'])
    entries = collections.defaultdict(list)
    for t in sorted(xtargets):
        if t in sset or t % 4: continue
        fo = owner_of(t)
        if fo is not None: entries[fo].append(t); owner[t] = fo
    fname = lambda a: '%s_f_%08x' % (name, a)
    ename = lambda a: '%s_e_%08x' % (name, a)
    o = ['/* gerado por tools/recomp.py - NAO EDITAR */', '#include "psp_rt.h"',
         '#pragma GCC diagnostic ignored "-Wunused-label"', '']
    o += ['void %s(CPU *c);' % fname(a) for a in starts]
    o += ['void %s(CPU *c);' % ename(a) for a in sorted(owner)]
    o.append('')
    stats = collections.Counter()
    for i, fa in enumerate(starts):
        fe = starts[i + 1] if i + 1 < len(starts) else tend
        insns = {pc: decode(word(pc), pc) for pc in range(fa, fe, 4)}
        labels = set()
        has_ind = False
        for pc, (m, f) in insns.items():
            if (m in BR_COND or m == 'j') and fa <= f['target'] < fe: labels.add(f['target'])
            if m == 'jr' and f['rs'] != 31: has_ind = True
        table = sorted(p for p in dptrs if fa <= p < fe) if has_ind else []
        labels |= set(table)
        ents = entries.get(fa, [])
        labels |= set(ents)
        # alvo caindo em delay slot: a entrada lateral executa a instrucao e segue em pc+8
        local = fa in local_fns
        retsites = []
        if local:
            for pc, (m, f) in insns.items():
                if m in ('jal', 'bltzal', 'bgezal', 'bltzall', 'bgezall') and fa <= f['target'] < fe:
                    labels.add(f['target']); labels.add(pc + 8); retsites.append(pc + 8)
        for pc, (m, f) in insns.items():
            if m in JUMPS and (pc + 4) in labels and pc + 8 < fe: labels.add(pc + 8)

        def tail(t):
            if t in sset: return '{ %s(c); return; }' % fname(t)
            if t in owner: return '{ %s(c); return; }' % ename(t)
            return '{ psp_call(c, 0x%08x); return; }' % t

        def goto(t):
            return ('goto L_%08x;' % t) if fa <= t < fe else tail(t)

        def delay(pc):
            d = pc + 4
            if d >= tend: return ''
            dm, df = decode(word(d), d)
            if dm in JUMPS or is_unimpl(dm):
                stats['bad_delay'] += 1
                return 'psp_unimpl(c, 0x%08x, 0x%08x);' % (d, word(d))
            return simple(dm, df, word(d), d)

        if ents:
            bname = '%s_b_%08x' % (name, fa)
            o.append('static void %s(CPU *c, uint32_t e_);' % bname)
            o.append('void %s(CPU *c) { %s(c, 0x%08x); }' % (fname(fa), bname, fa))
            o += ['void %s(CPU *c) { %s(c, 0x%08x); }' % (ename(e), bname, e) for e in ents]
            o.append('static void %s(CPU *c, uint32_t e_) {' % bname)
            o.append('  TRC(0x%08x);' % fa)
            o.append('  if (e_ == 0x%08x) HOOK(0x%08x);' % (fa, fa))
            o.append('  switch (e_) { %s default: break; }' % ' '.join('case 0x%08x: goto L_%08x;' % (e, e) for e in ents))
            stats['entries'] += len(ents)
        else:
            o.append('void %s(CPU *c) {' % fname(fa))
            o.append('  TRC(0x%08x);' % fa)
            o.append('  HOOK(0x%08x);' % fa)
        pc = fa
        while pc < fe:
            m, f = insns[pc]; w = word(pc)
            if pc in labels: o.append('L_%08x:;' % pc)
            if m in JUMPS and (pc + 4) in labels:
                # alvo de salto caindo no delay slot: entrada lateral que executa so o delay slot
                o.append('  if (0) { L_%08x:; %s %s }' % (pc + 4, delay(pc), goto(pc + 8)))
                stats['delay_label'] += 1
            if m in BR_COND:
                cond, likely = BR_COND[m]
                if 'VCC' in cond: cond = cond.replace('VCC', '((c->vctrl[3] >> %d) & 1)' % f['ccbit'])
                o.append('  { uint32_t a = %s, b = %s; (void)a; (void)b; int t_ = %s;' % (Rr(f['rs']), Rr(f['rt']), cond))
                if likely: o.append('    if (t_) { %s %s }' % (delay(pc), goto(f['target'])))
                else: o.append('    %s if (t_) %s' % (delay(pc), goto(f['target'])))
                o.append('  }')
                stats['branch'] += 1; pc += 8; continue
            if m == 'j':
                o.append('  %s %s' % (delay(pc), goto(f['target']))); stats['j'] += 1; pc += 8; continue
            if local and m in ('jal', 'bltzal', 'bgezal', 'bltzall', 'bgezall') and fa <= f['target'] < fe:
                t = f['target']
                if m == 'jal': o.append('  c->r[31] = 0x%08x; %s goto L_%08x;' % (pc + 8, delay(pc), t))
                else:
                    cond = '(int32_t)%s %s 0' % (Rr(f['rs']), '<' if m.startswith('bltz') else '>=')
                    if m.endswith('ll'): o.append('  { int t_ = %s; c->r[31] = 0x%08x; if (t_) { %s goto L_%08x; } }' % (cond, pc + 8, delay(pc), t))
                    else: o.append('  { int t_ = %s; c->r[31] = 0x%08x; %s if (t_) goto L_%08x; }' % (cond, pc + 8, delay(pc), t))
                stats['local_call'] += 1; pc += 8; continue
            if m == 'jal':
                t = f['target']
                call = ('%s(c);' % fname(t)) if t in sset else ('%s(c);' % ename(t)) if t in owner else 'psp_call(c, 0x%08x);' % t
                o.append('  c->r[31] = 0x%08x; %s %s' % (pc + 8, delay(pc), call)); stats['jal'] += 1; pc += 8; continue
            if m in ('bltzal', 'bgezal', 'bltzall', 'bgezall'):
                # chamada condicional (usada como "call" local no inflate escrito a mao); ra e escrito sempre
                t = f['target']
                call = ('%s(c);' % fname(t)) if t in sset else ('%s(c);' % ename(t)) if t in owner else 'psp_call(c, 0x%08x);' % t
                cond = '(int32_t)%s %s 0' % (Rr(f['rs']), '<' if m.startswith('bltz') else '>=')
                if m.endswith('ll'):
                    o.append('  { int t_ = %s; c->r[31] = 0x%08x; if (t_) { %s %s } }' % (cond, pc + 8, delay(pc), call))
                else:
                    o.append('  { int t_ = %s; c->r[31] = 0x%08x; %s if (t_) %s }' % (cond, pc + 8, delay(pc), call))
                stats['branch_link'] += 1; pc += 8; continue
            if m == 'jr':
                if f['rs'] == 31 and local:
                    o.append('  { uint32_t t_ = c->r[31]; %s switch (t_) { %s default: return; } }' % (
                        delay(pc), ' '.join('case 0x%08x: goto L_%08x;' % (r, r) for r in sorted(set(retsites)))))
                    stats['local_ret'] += 1
                elif f['rs'] == 31:
                    o.append('  %s return;' % delay(pc)); stats['ret'] += 1
                else:
                    o.append('  { uint32_t t_ = %s; %s' % (Rr(f['rs']), delay(pc)))
                    if table:
                        o.append('    switch (t_) {')
                        o += ['    case 0x%08x: goto L_%08x;' % (p, p) for p in table]
                        o.append('    default: psp_call(c, t_); return; }')
                    else:
                        o.append('    psp_call(c, t_); return;')
                    o.append('  }'); stats['jr_ind'] += 1
                pc += 8; continue
            if m == 'jalr':
                link = ('c->r[%d] = 0x%08x;' % (f['rd'], pc + 8)) if f['rd'] else ''
                o.append('  { uint32_t t_ = %s; %s %s psp_call(c, t_); }' % (Rr(f['rs']), link, delay(pc)))
                stats['jalr'] += 1; pc += 8; continue
            if is_unimpl(m):
                o.append('  psp_unimpl(c, 0x%08x, 0x%08x);' % (pc, w)); stats['unimpl_' + str(m)] += 1; pc += 4; continue
            s = simple(m, f, w, pc)
            if s: o.append('  ' + s)
            stats['insn'] += 1; pc += 4
        # queda para a proxima funcao (sem jr ra no fim)
        last = insns.get(fe - 8)
        if fe in sset and not (last and last[0] in ('jr', 'j') ):
            o.append('  %s(c);' % fname(fe))
        o.append('}')
        o.append('')
    o.append('typedef struct { uint32_t addr; void (*fn)(CPU *); } FnEntry;')
    o.append('const FnEntry %s_fns[] = {' % name)
    allf = sorted([(a, fname(a)) for a in starts] + [(a, ename(a)) for a in owner])
    o += ['  {0x%08x, %s},' % af for af in allf]
    o.append('};')
    o.append('const unsigned %s_nfns = %d;' % (name, len(allf)))
    return o, starts, stats

def main():
    path, base, tsv, out = sys.argv[1], int(sys.argv[2], 16), sys.argv[3], sys.argv[4]
    name = sys.argv[5] if len(sys.argv) > 5 else 'mod'
    global VARS_FROM
    if len(sys.argv) > 6:   # <caminho>@<base_hex> do modulo exportador (BOOT)
        vp, vb = sys.argv[6].rsplit('@', 1); VARS_FROM = (vp, int(vb, 16))
    lines, starts, stats = build(path, base, tsv, name)
    from imports import parse
    _img, _secs, info, imps = parse(path, base)
    lines.append('typedef struct { uint32_t stub, nid; const char *lib, *name; } ImportEntry;')
    lines.append('const ImportEntry %s_imports[] = {' % name)
    lines += ['  {0x%08x, 0x%08x, "%s", "%s"},' % (a, n, l, nm or ('nid_%08x' % n)) for l, n, a, nm in imps]
    lines.append('};')
    lines.append('const unsigned %s_nimports = %d;' % (name, len(imps)))
    lines.append('const uint32_t %s_gp = 0x%08x, %s_base = 0x%08x;' % (name, info['gp'], name, base))
    sd = {x['name']: x for x in _secs}
    lines.append('const uint32_t %s_memsz = 0x%x, %s_textsz = 0x%x, %s_datasz = 0x%x, %s_bsssz = 0x%x;' % (
        name, len(_img), name, sd['.text']['size'], name, sd['.data']['size'], name, sd['.bss']['size'] if '.bss' in sd else 0))
    lines.append('const char %s_name[] = "%s";' % (name, info['name']))
    from imports import exports
    exps = exports(_img, base, info)
    lines.append('typedef struct { uint32_t nid, addr; const char *lib; } ExportEntry;')
    lines.append('const ExportEntry %s_exports[] = {' % name)
    lines += ['  {0x%08x, 0x%08x, "%s"},' % (n, a, l) for l, n, a in exps]
    lines.append('  {0, 0, 0}')
    lines.append('};')
    lines.append('const unsigned %s_nexports = %d;' % (name, len(exps)))
    import struct as _st
    e_entry = _st.unpack_from('<I', open(path, 'rb').read(32), 24)[0]
    entry = next((a for l, n, a in exps if n == 0xd632acdb), base + e_entry)
    lines.append('const uint32_t %s_entry = 0x%08x;' % (name, entry))
    open(out, 'w', newline='\n').write('\n'.join(lines) + '\n')
    print('%d funcoes; %s' % (len(starts), dict(stats)))

if __name__ == '__main__':
    main()
