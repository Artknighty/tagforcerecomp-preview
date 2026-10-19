"""Decodificador do Allegrex (MIPS32r2-ish do PSP): inteiro, FPU (COP1) e Allegrex extras.

VFPU e classificado apenas por familia (mnemonico 'vfpu.<fam>'); a decodificacao fina vem depois.
decode(word, pc) -> (mnemonic, fields) ; mnemonic None = palavra invalida/dado.
"""
import struct, sys, collections

def sx16(v): return v - 0x10000 if v & 0x8000 else v

SPECIAL = {0:'sll',2:'srl',3:'sra',4:'sllv',6:'srlv',7:'srav',8:'jr',9:'jalr',10:'movz',11:'movn',
    12:'syscall',13:'break',15:'sync',16:'mfhi',17:'mthi',18:'mflo',19:'mtlo',22:'clz',23:'clo',
    24:'mult',25:'multu',26:'div',27:'divu',28:'madd',29:'maddu',32:'add',33:'addu',34:'sub',35:'subu',
    36:'and',37:'or',38:'xor',39:'nor',42:'slt',43:'sltu',44:'max',45:'min',46:'msub',47:'msubu'}
REGIMM = {0:'bltz',1:'bgez',2:'bltzl',3:'bgezl',16:'bltzal',17:'bgezal',18:'bltzall',19:'bgezall',31:'synci'}
IMM = {4:'beq',5:'bne',6:'blez',7:'bgtz',20:'beql',21:'bnel',22:'blezl',23:'bgtzl',
    8:'addi',9:'addiu',10:'slti',11:'sltiu',12:'andi',13:'ori',14:'xori',15:'lui'}
MEM = {32:'lb',33:'lh',34:'lwl',35:'lw',36:'lbu',37:'lhu',38:'lwr',40:'sb',41:'sh',42:'swl',43:'sw',
    46:'swr',47:'cache',48:'ll',49:'lwc1',56:'sc',57:'swc1'}
VFPU_MEM = {50:'lv.s',53:'lvl/lvr.q',54:'lv.q',58:'sv.s',61:'svl/svr.q',62:'sv.q'}
VFPU_ARITH = {24:'vfpu0',25:'vfpu1',27:'vfpu3',52:'vfpu4',55:'vfpu5',60:'vfpu6',63:'vfpu7'}
FPU_S = {0:'add.s',1:'sub.s',2:'mul.s',3:'div.s',4:'sqrt.s',5:'abs.s',6:'mov.s',7:'neg.s',
    12:'round.w.s',13:'trunc.w.s',14:'ceil.w.s',15:'floor.w.s',36:'cvt.w.s'}
FPU_CMP = ['c.f','c.un','c.eq','c.ueq','c.olt','c.ult','c.ole','c.ule','c.sf','c.ngle','c.seq','c.ngl','c.lt','c.nge','c.le','c.ngt']

def decode(w, pc):
    op = w >> 26; rs = (w>>21)&31; rt = (w>>16)&31; rd = (w>>11)&31; sa = (w>>6)&31; fn = w & 63
    f = dict(rs=rs, rt=rt, rd=rd, sa=sa, imm=sx16(w&0xffff), uimm=w&0xffff)
    if op == 0:
        if w == 0: return 'nop', f
        m = SPECIAL.get(fn)
        if m == 'srl' and rs == 1: m = 'rotr'
        if m == 'srlv' and sa == 1: m = 'rotrv'
        return m, f
    if op == 1:
        m = REGIMM.get(rt); f['target'] = pc + 4 + (f['imm'] << 2); return m, f
    if op in (2, 3):
        f['target'] = ((pc + 4) & 0xf0000000) | ((w & 0x3ffffff) << 2); return ('j' if op == 2 else 'jal'), f
    if op in IMM:
        if op in (4,5,6,7,20,21,22,23): f['target'] = pc + 4 + (f['imm'] << 2)
        return IMM[op], f
    if op == 16:  # COP0 (so mfc0/mtc0 basicos)
        return {0:'mfc0',4:'mtc0'}.get(rs), f
    if op == 17:
        if rs == 0: return 'mfc1', f
        if rs == 2: return 'cfc1', f
        if rs == 4: return 'mtc1', f
        if rs == 6: return 'ctc1', f
        if rs == 8:
            f['target'] = pc + 4 + (f['imm'] << 2)
            return {0:'bc1f',1:'bc1t',2:'bc1fl',3:'bc1tl'}.get(rt), f
        if rs == 16:
            if fn >= 48: return FPU_CMP[fn-48], f
            return FPU_S.get(fn), f
        if rs == 20 and fn == 32: return 'cvt.s.w', f
        return None, f
    if op == 18:
        if rs == 8:   # bvf/bvt (branch no codigo de condicao do VFPU)
            f['target'] = pc + 4 + (f['imm'] << 2); f['ccbit'] = (w >> 18) & 7
            return {0: 'bvf', 1: 'bvt', 2: 'bvfl', 3: 'bvtl'}[rt & 3], f
        return 'vfpu.cop2', f
    if op == 28: return {36:'mfic',38:'mtic'}.get(fn) if False else 'halt/mfic', f
    if op == 31:
        if fn == 0: return 'ext', f
        if fn == 4: return 'ins', f
        if fn == 32: return {2:'wsbh',3:'wsbw',16:'seb',20:'bitrev',24:'seh'}.get(sa), f
        return None, f
    if op in MEM: return MEM[op], f
    if op in VFPU_MEM: return 'vfpu.' + VFPU_MEM[op], f
    if op in VFPU_ARITH: return 'vfpu.' + VFPU_ARITH[op], f
    return None, f

if __name__ == '__main__':
    import os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from pspmod import load_module
    path, base = sys.argv[1], int(sys.argv[2], 16)
    img, secs, _ = load_module(path, base)
    t = next(s for s in secs if s['name'] == '.text')
    # .text no ELF comeca em file off 0x60 == vaddr 0 (segmento unico)
    cnt = collections.Counter(); bad = []
    for i in range(0, t['size'], 4):
        w, = struct.unpack_from('<I', img, i)
        m, _f = decode(w, base + i)
        cnt[m] += 1
        if m is None: bad.append((base + i, w))
    total = sum(cnt.values()); vf = sum(v for k, v in cnt.items() if k and k.startswith('vfpu'))
    print('%d palavras, %d VFPU (%.1f%%), %d nao decodificadas' % (total, vf, 100*vf/total, len(bad)))
    for a, w in bad[:25]: print('  %08x: %08x' % (a, w))
    for k, v in sorted(((k, v) for k, v in cnt.items() if k and k.startswith('vfpu')), key=lambda x: -x[1]): print('  ', k, v)
