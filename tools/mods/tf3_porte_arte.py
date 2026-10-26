"""Puts the art of Tag Force 3 cards into a Tag Force 1 .cip file (card/cardm_*.cip or card/cardh_*.cip), in place.

Format (checked in both games): 0x30-byte header ("CPM\x1a" 64x64 thumbnail / "CIP\x1a" 128x64 large art), u32 file size at +4,
+0x10 = end of the index, +0x14/+0x18 = lowest/highest id; from 0x30 one 8-byte entry per id (u32 position in 0x800 units, 0 = no art;
bit 31 = the entry points to a list of alternative arts); then the lists, a {0xffffffff, 0xffffffff} terminator (the stream reader stops at it:
never remove or shift it) and from unit 0xc the art blocks (1 unit for thumbnails, 2 for large art).

A ported card plays under the id of a Tag Force 1 card (its slot, see tf3_porte_carta.py): the new art is written over the art block of that id.
The file keeps its size and layout, so nothing else changes (ids above the file's range or new blocks are not needed).

Usage:  python -I tools/mods/tf3_porte_arte.py <tf1.cip> <tf3.cip> <out.cip> <tf3 id>:<slot id> [...]   (the type, m or h, comes from the header)
"""
import struct
import sys

UNIT = 0x800


def porta(tf1, tf3, slots):
    """slots: {TF3 id: TF1 id whose art block is replaced}"""
    d = bytearray(tf1)
    blocks = 1 if bytes(d[:4]) == b"CPM"+bytes([0x1a]) else 2
    assert bytes(tf3[:4]) == bytes(d[:4]), "different file types"
    lo = struct.unpack_from("<I", d, 0x14)[0]
    lo3 = struct.unpack_from("<I", tf3, 0x14)[0]
    for cid, slot in slots.items():
        v1, = struct.unpack_from("<I", d, 0x30 + (slot - lo) * 8)
        v3, = struct.unpack_from("<I", tf3, 0x30 + (cid - lo3) * 8)
        assert v1 and not v1 & 0x80000000, "slot %d has no plain art" % slot
        assert v3 and not v3 & 0x80000000, "TF3 has no plain art for %d" % cid
        d[v1 * UNIT:(v1 + blocks) * UNIT] = tf3[v3 * UNIT:(v3 + blocks) * UNIT]
    return bytes(d)


def slots_ok(tf1, slot):
    """True if the file has a plain art block for the id"""
    lo, hi = struct.unpack_from("<II", tf1, 0x14)
    if not lo <= slot <= hi:
        return False
    v, = struct.unpack_from("<I", tf1, 0x30 + (slot - lo) * 8)
    return bool(v) and not v & 0x80000000


def extend(tf1, tf3, gids):
    """adds art for NEW ids (above the id range of the file): gids {TF3 id: new game id}. The index grows, so the lists and the art blocks move
    to later units (every unit number of the file is shifted); the new blocks go at the end. Returns the bytes of the new file."""
    end = struct.unpack_from("<I", tf1, 0x10)[0]
    lo, hi = struct.unpack_from("<II", tf1, 0x14)
    blocks = 1 if bytes(tf1[:4]) == b"CPM" + bytes([0x1a]) else 2
    lo3 = struct.unpack_from("<I", tf3, 0x14)[0]
    new_hi = max(gids.values())
    assert min(gids.values()) > hi, "new ids must be above the ids of the file"
    n_old, n_new = hi - lo + 1, new_hi - lo + 1
    old_list, new_list = 0x30 + n_old * 8, 0x30 + n_new * 8
    lists = tf1[old_list:end - 8]
    assert tf1[end - 8:end] == bytes([255]) * 8, "no terminator where the header says the index ends"
    new_end = new_list + len(lists) + 8
    u0 = max(-(-new_end // UNIT), 0xc)
    shift = u0 - 0xc

    def unit(u):
        return u + shift if u else 0
    out = bytearray(tf1[:0x30])
    for i in range(n_old):
        e = struct.unpack_from("<I", tf1, 0x30 + i * 8)[0]
        e = 0x80000000 | (e & 0x7fffffff) - old_list + new_list if e & 0x80000000 else unit(e)
        out += struct.pack("<II", e, 0)
    data = bytes(tf1[0xc * UNIT:])
    assert len(data) % UNIT == 0
    cursor = u0 + len(data) // UNIT
    extra = bytearray()
    entries = {}
    for tf3id, gid in sorted(gids.items(), key=lambda x: x[1]):
        v3 = struct.unpack_from("<I", tf3, 0x30 + (tf3id - lo3) * 8)[0]
        assert v3 and not v3 & 0x80000000, "TF3 has no plain art for %d" % tf3id
        extra += tf3[v3 * UNIT:(v3 + blocks) * UNIT]
        entries[gid] = cursor
        cursor += blocks
    for gid in range(hi + 1, new_hi + 1):
        out += struct.pack("<II", entries.get(gid, 0), 0)
    for i in range(0, len(lists), 8):
        u, f = struct.unpack_from("<II", lists, i)
        out += struct.pack("<II", (u & 0x80000000) | unit(u & 0x7fffffff), f)
    out += bytes([255]) * 8
    assert len(out) == new_end
    out += bytes(u0 * UNIT - len(out))
    out += data + extra
    struct.pack_into("<I", out, 4, len(out))
    struct.pack_into("<I", out, 0x10, new_end)
    struct.pack_into("<I", out, 0x18, new_hi)
    out += bytes(UNIT)                                   # the last block's data runs a little past its units
    return bytes(out)


if __name__ == "__main__":
    if len(sys.argv) < 5:
        sys.exit(__doc__)
    pares = {int(a.split(":")[0]): int(a.split(":")[1]) for a in sys.argv[4:]}
    result = porta(open(sys.argv[1], "rb").read(), open(sys.argv[2], "rb").read(), pares)
    open(sys.argv[3], "wb").write(result)
    print("wrote", sys.argv[3], len(result), "bytes")
