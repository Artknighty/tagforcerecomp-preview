"""Card database of Tag Force 3 EU (duelsys/cardinfo_eng.ehp): names, stats and texts (Huffman coded).

Text format (CARD_Desc_E.bin), confirmed 2026-10-25:
  CARD_Indx_E.bin: 8 bytes per card = (name offset in bytes, text offset in BITS).
  CARD_Huff_E.bin: binary tree of u16. Inner node: (left, right) = byte offsets (4 bytes per node);
                   leaf: (0, 0, symbol) (6 bytes). Symbol 0 = end of text.
  Bits are read most significant first. Symbols are UTF-16 characters; the text carries literal markers:
  "$dNNN" = word NNN of DICT_E.bin, "$mID" = name of the card with id ID.
  DICT_E.bin: u32 n, u32 12 (table), u32 base; table of n offsets relative to base; NUL-terminated UTF-16 strings.

Reads the members already extracted (ehp.py extract "iso:PSP_GAME/USRDIR/duelsys/cardinfo_eng.ehp" ref/tf3/cardinfo; or TF3_CARDINFO).
Usage:  python -I tools/game/tf3_cartas.py [id]     (no argument: lists the cards that exist only in TF3)
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


def _u16s(b, o):
    e = o
    while e + 1 < len(b) and b[e:e + 2] != b"\0\0":
        e += 2
    return b[o:e].decode("utf-16le", "replace")


class Texto:
    def __init__(self, huff, dic, desc):
        self.h, self.ds = huff, desc
        n, tab, base = struct.unpack_from("<III", dic, 0)
        self.dic = [_u16s(dic, base + struct.unpack_from("<I", dic, tab + 4 * i)[0]) for i in range(n)]

    def _no(self, p):
        l, r = struct.unpack_from("<HH", self.h, p)
        if l == 0 and r == 0:
            return None, struct.unpack_from("<H", self.h, p + 4)[0]
        return (l, r), 0

    def simbolos(self, bit):
        out, p, ds = [], 0, self.ds
        while bit < len(ds) * 8:
            b = (ds[bit >> 3] >> (7 - (bit & 7))) & 1
            bit += 1
            filhos, _ = self._no(p)
            p = filhos[b]
            filhos, s = self._no(p)
            if filhos is None:
                if s == 0:
                    break
                out.append(s)
                p = 0
        return out


def para_tf1(w0, w1):
    """8-byte CARD_Prop record of TF3 -> TF1 layout (checked against the 2451 cards both games share).
    TF3 w0: id 0-12, ATK/10 at 14-22, DEF/10 at 23-31 (TF1 uses 13 and 22; TF1 bit 31 = 1). TF3 w1: category at 1-4, attribute at 6-9,
    level at 10-13, icon at 14-16, type at 17-21 (23/24 = Spell/Trap become 22/23); rarity stays 0 (TF3 encodes it differently)."""
    tipo = (w1 >> 17) & 31
    tipo = {23: 22, 24: 23}.get(tipo, tipo)
    n0 = (w0 & 0x1FFF) | (((w0 >> 14) & 0x1FF) << 13) | (((w0 >> 23) & 0x1FF) << 22) | 0x80000000
    n1 = ((w1 >> 1) & 15) | (((w1 >> 6) & 15) << 4) | (((w1 >> 10) & 15) << 8) | (((w1 >> 14) & 7) << 12) | (tipo << 15)
    return n0, n1


def genero_para_tf1(g3):
    """CARD_Genre (6 bytes per card, 48-bit mask) of TF3 -> TF1. Bits 1-9 are the same; TF1 bits 10-34 sit 3 higher in TF3,
    except bit 30 (TF3 bit 10); each bit agrees on 2431 to 2451 of the 2451 shared cards (TF3 reclassified a few cards)."""
    v = int.from_bytes(g3, "little")
    mapa = {p: (p if p < 10 else p + 3) for p in range(1, 35)}
    mapa[30] = 10
    return sum(((v >> q) & 1) << p for p, q in mapa.items()).to_bytes(6, "little")


RAIZ = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PASTA = os.environ.get("TF3_CARDINFO") or os.path.join(RAIZ, "ref", "tf3", "cardinfo")


def _ler(n):
    return open(os.path.join(PASTA, n), "rb").read()


def carregar():
    prop, ix, nm = _ler("CARD_Prop.bin"), _ler("CARD_Indx_E.bin"), _ler("CARD_Name_E.bin")
    tx = Texto(_ler("CARD_Huff_E.bin"), _ler("DICT_E.bin"), _ler("CARD_Desc_E.bin"))
    cartas = {}
    for i in range(len(prop) // 8):
        w0, w1 = struct.unpack_from("<II", prop, i * 8)
        if not w0 & 0x1FFF:
            continue
        no, do = struct.unpack_from("<II", ix, i * 8)
        cartas[w0 & 0x1FFF] = dict(id=w0 & 0x1FFF, idx=i, nome=_u16s(nm, no), w0=w0, w1=w1, bit=do)
    nomes = {c["id"]: c["nome"] for c in cartas.values()}
    for c in cartas.values():
        t = "".join(chr(s) for s in tx.simbolos(c["bit"]))
        t = re.sub(r"\$d(\d{3})", lambda m: tx.dic[int(m.group(1))], t)
        c["desc"] = re.sub(r"\$m(\d{4})", lambda m: nomes.get(int(m.group(1)), m.group(0)), t)
    return cartas


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    C = carregar()
    if len(sys.argv) > 1:
        c = C[int(sys.argv[1])]
        print(c["id"], c["nome"], hex(c["w0"]), hex(c["w1"]))
        print(c["desc"])
    else:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import cartas as tf1
        for c in sorted(C.values(), key=lambda x: x["id"]):
            if c["nome"] not in {x.nome for x in tf1.CARTAS.values()}:
                print(c["id"], c["nome"], "|", c["desc"][:90].replace(chr(10), " "))
