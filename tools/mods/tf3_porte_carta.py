"""Builds the data mod (cardinfo_eng.ehp) that adds Tag Force 3 cards to the Tag Force 1 card database.

Each new card becomes the last entry of the database (index = n of Prop); its TF3 id must be a GAP of TF1 (CARD_IntID = 0, inside
4007..6957; there are 472, 150 of them are TF3 cards): ids above 6957 need bigger tables and the game hangs (see tf3_porte_arte.py).
Prop ends up out of id order; the game finds the card through CARD_IntID.
  CARD_Prop   +8 bytes (w0/w1 converted by tf3_cartas.para_tf1: the fields change bit positions; rarity 0)
  CARD_Pass   +4 bytes (TF3 passcode)
  CARD_Genre  +6 bytes (bits remapped from TF3)
  CARD_IntID  (u16 per id, base 4000): [id-4000] = index
  CARD_Name_E / CARD_Desc_E: new text at the end (UTF-16, 4-aligned); CARD_Indx_E: reuses the sentinel entry and adds a new one
  CARD_Sort_E +1 u16 at the end of the alphabetical order (CARD_Top_E does not change).
Nothing else changes: the card's effect is done separately (plugin).

Usage:  python -I tools/mods/tf3_porte_carta.py <out.ehp> <id> [<id> ...]
Input: TF1 in TF_CARDINFO (default gen/jogo/.../cardinfo_eng.ehp); TF3 extracted in ref/tf3/cardinfo (TF3_CARDINFO).
"""
import os
import struct
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(RAIZ, "tools", "game"))
import ehp  # noqa: E402
import tf3_cartas  # noqa: E402

TF1 = os.environ.get("TF_CARDINFO") or os.path.join(RAIZ, "gen", "jogo", "PSP_GAME", "USRDIR", "duelsys", "cardinfo_eng.ehp")
BASE_INTID = 4000


def _utf16(s):
    b = s.encode("utf-16le") + b"\0\0"
    return b + b"\0\0" * (len(b) % 4 // 2)


def portar(ids):
    tag, mem = ehp.ler(open(TF1, "rb").read())
    m = dict(mem)
    novas = tf3_cartas.carregar()
    prop, pas, gen = (tf3_cartas._ler(n) for n in ("CARD_Prop.bin", "CARD_Pass.bin", "CARD_Genre.bin"))
    p1 = bytearray(m["CARD_Prop.bin"])
    ps, gn = bytearray(m["CARD_Pass.bin"]), bytearray(m["CARD_Genre.bin"])
    ix, nm, ds = bytearray(m["CARD_Indx_E.bin"]), bytearray(m["CARD_Name_E.bin"]), bytearray(m["CARD_Desc_E.bin"])
    iid, srt = bytearray(m["CARD_IntID.bin"]), bytearray(m["CARD_Sort_E.bin"])
    for cid in ids:
        c = novas[cid]
        n = len(p1) // 8
        pos = (cid - BASE_INTID) * 2
        if cid < BASE_INTID or pos + 2 > len(iid) or struct.unpack_from("<H", iid, pos)[0]:
            sys.exit("id %d is not a TF1 gap (outside %d..%d or already used): only free ids inside the range work" % (cid, BASE_INTID, BASE_INTID + len(iid) // 2 - 1))
        i3 = c["idx"]
        p1 += struct.pack("<II", *tf3_cartas.para_tf1(c["w0"], c["w1"]))
        ps += pas[i3 * 4:i3 * 4 + 4]
        gn += tf3_cartas.genero_para_tf1(gen[i3 * 6:i3 * 6 + 6])
        ix[-8:] = struct.pack("<II", len(nm), len(ds))     # the sentinel becomes the new card entry
        nm += _utf16(c["nome"])
        ds += _utf16(c["desc"])
        ix += struct.pack("<II", len(nm), len(ds))         # new sentinel
        pos = (cid - BASE_INTID) * 2
        if len(iid) < pos + 2:
            iid += b"\0" * (pos + 2 - len(iid))
        struct.pack_into("<H", iid, pos, n)
        srt += struct.pack("<H", n)
    m.update({"CARD_Prop.bin": bytes(p1), "CARD_Pass.bin": bytes(ps), "CARD_Genre.bin": bytes(gn), "CARD_Indx_E.bin": bytes(ix),
              "CARD_Name_E.bin": bytes(nm), "CARD_Desc_E.bin": bytes(ds), "CARD_IntID.bin": bytes(iid), "CARD_Sort_E.bin": bytes(srt)})
    return ehp.montar([(n, m[n]) for n, _ in mem], tag)


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    saida = sys.argv[1]
    os.makedirs(os.path.dirname(os.path.abspath(saida)), exist_ok=True)
    open(saida, "wb").write(portar([int(a) for a in sys.argv[2:]]))
    print("wrote", saida)
