"""Puts the art of Tag Force 3 cards into a Tag Force 1 .cip file (card/cardm_*.cip or card/cardh_*.cip) WITHOUT changing the file size.

Format (checked in both games): 0x30-byte header ("CPM\\x1a" 64x64 thumbnail / "CIP\\x1a" 128x64 large art), u32 size at +4,
+0x14/+0x18 = lowest/highest id; from 0x30 one 8-byte entry per id (u32 position in 0x800 units, 0 = no art; bit 31 = the entry points to a
list of alternative arts, 8 bytes each, bit 31 marks the last); then the lists, and from unit 0xc the art blocks (1 unit for thumbnails,
2 for large art).

Limits found on 2026-10-26 (the game hangs when the duel starts): the file cannot grow (an identical copy of the original loads; +1 block, or
a bigger index, hangs; the game probably loads the whole file into a fixed-size buffer) and the id must be a gap inside [lo, hi]. So the new
art OVERWRITES the block of an existing alternative art (the last one of each list with more than one art) and the new id's entry points to
it; the card that loses that alternative art keeps its main one.

Usage:  python -I tools/mods/tf3_porte_arte.py <tf1.cip> <tf3.cip> <out.cip> <id> [<id> ...]   (the type, m or h, comes from the header)
"""
import struct
import sys

UNIDADE = 0x800


def _lista(d, p):
    """(item position, unit) of each art in the alternatives list at p"""
    out = []
    while True:
        w, = struct.unpack_from("<I", d, p)
        out.append((p, w & 0x7fffffff))
        p += 8
        if w & 0x80000000:
            return out


def porta(tf1, tf3, ids):
    d = bytearray(tf1)
    blocos = 1 if bytes(d[:4]) == b"CPM\x1a" else 2
    assert bytes(tf3[:4]) == bytes(d[:4]), "different types"
    lo, hi = struct.unpack_from("<II", d, 0x14)
    lo3, _ = struct.unpack_from("<II", tf3, 0x14)
    doadores = []   # last art of each alternatives list (never the main one)
    for cid in range(lo, hi + 1):
        v, = struct.unpack_from("<I", d, 0x30 + (cid - lo) * 8)
        if v & 0x80000000:
            alt = _lista(d, v & 0x7fffffff)
            if len(alt) > 1:
                doadores.append(alt[-1][1])
    for cid in ids:
        assert lo <= cid <= hi, "id %d outside the file range %d..%d: a bigger index does not load in the game" % (cid, lo, hi)
        pos = 0x30 + (cid - lo) * 8
        assert struct.unpack_from("<I", d, pos)[0] == 0, "id %d already has art in TF1" % cid
        v3, = struct.unpack_from("<I", tf3, 0x30 + (cid - lo3) * 8)
        assert v3 and not v3 & 0x80000000, "TF3 has no plain art for %d" % cid
        assert doadores, "no more alternative arts left to overwrite"
        u = doadores.pop()
        d[u * UNIDADE:(u + blocos) * UNIDADE] = tf3[v3 * UNIDADE:(v3 + blocos) * UNIDADE]
        struct.pack_into("<I", d, pos, u)
    assert len(d) == len(tf1)
    return bytes(d)


if __name__ == "__main__":
    if len(sys.argv) < 5:
        sys.exit(__doc__)
    saida = porta(open(sys.argv[1], "rb").read(), open(sys.argv[2], "rb").read(), [int(a) for a in sys.argv[4:]])
    open(sys.argv[3], "wb").write(saida)
    print("wrote", sys.argv[3], len(saida), "bytes")
