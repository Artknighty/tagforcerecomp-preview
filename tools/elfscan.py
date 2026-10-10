"""Sondagem inicial dos modulos PSP: secoes, relocs e cobertura de opcodes MIPS."""
import struct, sys, collections

def load(path):
    d = open(path, 'rb').read()
    e_type, = struct.unpack_from('<H', d, 16)
    shoff, = struct.unpack_from('<I', d, 32)
    shentsize, shnum, shstrndx = struct.unpack_from('<HHH', d, 46)
    secs = []
    for i in range(shnum):
        n, t, f, a, o, s, l, inf, al, es = struct.unpack_from('<IIIIIIIII I'.replace(' ', ''), d, shoff + i * shentsize)
        secs.append(dict(name=n, type=t, flags=f, addr=a, off=o, size=s, link=l, info=inf))
    stro = secs[shstrndx]['off']
    for s in secs:
        e = d.index(b'\0', stro + s['name'])
        s['name'] = d[stro + s['name']:e].decode()
    return d, e_type, secs

def classify(w):
    op = w >> 26
    return op

if __name__ == '__main__':
    d, et, secs = load(sys.argv[1])
    print('type 0x%x' % et)
    for s in secs:
        print('%-26s type=%08x addr=%08x off=%06x size=%06x' % (s['name'], s['type'], s['addr'], s['off'], s['size']))
    t = next(s for s in secs if s['name'] == '.text')
    words = struct.unpack_from('<%dI' % (t['size'] // 4), d, t['off'])
    c = collections.Counter(w >> 26 for w in words)
    print('primary opcodes:', {hex(k): v for k, v in sorted(c.items())})
    print('VFPU-ish (0x18,0x19,0x1b,0x34,0x35,0x36,0x37,0x3c,0x3e,0x3f... / COP2 0x12):',
          {hex(k): c[k] for k in (0x12, 0x18, 0x19, 0x1a, 0x1b, 0x32, 0x36, 0x3a, 0x3e, 0x34, 0x35, 0x37, 0x3c, 0x3d, 0x3f) if c[k]})
