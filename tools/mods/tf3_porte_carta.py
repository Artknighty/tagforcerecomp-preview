"""Builds the data mod that puts Tag Force 3 cards into the Tag Force 1 game: card data, card art and the list the plugin reads.

The game has room for exactly 2480 card indices (the save keeps one collection / copy-limit record per index), every id of the game is wired to
its art, its pack pools and its effect, and the deck editor streams the art by id. So a ported card TAKES OVER A TF1 CARD: it is written into that
card's Prop index and plays under that card's id (the "slot"). Nothing outside the card data has to change; ids above 6957 and new art
blocks are not needed (they broke the trunk: thumbnails of ids the game does not know are shown as card backs). The slot is a card that no
duelist deck uses (tokens and story cards excluded), of the same kind (monster / spell / trap), with the closest name (the trunk list is sorted by
the slot's alphabetical rank, so the new card lands about where its name belongs) and a plain art block in both .cip files.
  CARD_Prop   record replaced (w0/w1 converted by tf3_cartas.para_tf1, the id field = the slot id; the slot keeps its rarity)
  CARD_Pass / CARD_Genre  entry replaced (passcode, bits remapped from TF3)
  CARD_Name_E / CARD_Desc_E: the new text at the end (UTF-16, 4-aligned); CARD_Indx_E points the index at it
  card/cardm_e.cip, card/cardh_e.cip: the new art written over the slot's block (tf3_porte_arte.py)
  ported.txt: one line per card, "<slot id> <TF3 id> <TF1 id whose effect it uses or 0> <name>" (read by the TF3Cards plugin)
Whatever other mods or hooks know about the slot card by its old id now meets the new card.

Usage:  python -I tools/mods/tf3_porte_carta.py <mod folder> <TF3 id>[:<slot id>] ...
Input: the TF1 files in gen/jogo (TF_JOGO), TF3 extracted in ref/tf3 (cardinfo/, card/; TF3_CARDINFO), gen/decks_db (tools/game/decks_extrair.py).
"""
import os
import struct
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(RAIZ, "tools", "game"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ehp  # noqa: E402
import tf3_cartas  # noqa: E402
import tf3_porte_arte  # noqa: E402

JOGO = os.environ.get("TF_JOGO") or os.path.join(RAIZ, "gen", "jogo", "PSP_GAME", "USRDIR")
TF3_CARD = os.path.join(RAIZ, "ref", "tf3", "card")
BASE_INTID = 4000
# ported card -> TF1 card whose effect it plays exactly like (the plugin lends the effect record; a card not listed here has no effect yet)
EFEITO = {6746: 5983,      # No Entry!! = Earthquake
          7234: 4352}      # Gift Card: the record of Ookazi (damage to the opponent) with the amount and sign changed by the plugin


def _utf16(s):
    b = s.encode("utf-16le") + b"\0\0"
    return b + b"\0\0" * (len(b) % 4 // 2)


def _ler(rel):
    return open(os.path.join(JOGO, rel), "rb").read()


def _cmp4(n):
    """first four letters as a base-256 number: a cheap alphabetical distance"""
    return sum(ord(ch) << (8 * (3 - i)) for i, ch in enumerate((n.lower() + "    ")[:4]))


def escolher_slots(ids, novas, fixos):
    import cartas
    import decks_lib
    usadas = set()
    for d in decks_lib.DECKS.values():
        for parte in ("principal", "extra", "side"):
            usadas |= set(d[parte])
    cip = [_ler("card/cardm_e.cip"), _ler("card/cardh_e.cip")]

    def classe(c):
        return "monster" if c.monstro else ("spell" if c.f15 == 22 else "trap")
    livres = [c for c in cartas.CARTAS.values()
              if c.senha and c.id not in usadas and c.id >= BASE_INTID + 7 and all(tf3_porte_arte.slots_ok(f, c.id) for f in cip)]
    out = dict(fixos)
    for cid in ids:
        if cid in out:
            continue
        w1 = novas[cid]["w1"] >> 17 & 31          # TF3 type field: 23 = Spell, 24 = Trap
        k = {23: "spell", 24: "trap"}.get(w1, "monster")
        cand = [c for c in livres if classe(c) == k and c.id not in out.values()]
        if not cand:
            sys.exit("no free slot for %d" % cid)
        out[cid] = min(cand, key=lambda c: (abs(_cmp4(c.nome) - _cmp4(novas[cid]["nome"])), c.id)).id
    return out


LANGS = {"e": "eng", "f": "fra", "g": "ger", "i": "ita", "s": "spa"}      # language letter -> name of cardinfo_<name>.ehp (Japanese is not ported)


def portar(ids, slots, lang="e"):
    """returns the bytes of cardinfo_<lang>.ehp with the cards written into their slots"""
    import cartas
    L = lang.upper()
    tag, mem = ehp.ler(_ler("duelsys/cardinfo_%s.ehp" % LANGS[lang]))
    m = dict(mem)
    novas = tf3_cartas.carregar(lang)
    pas, gen = (tf3_cartas._ler(n, lang) for n in ("CARD_Pass.bin", "CARD_Genre.bin"))
    p1 = bytearray(m["CARD_Prop.bin"])
    ps, gn = bytearray(m["CARD_Pass.bin"]), bytearray(m["CARD_Genre.bin"])
    ix, nm, ds = bytearray(m["CARD_Indx_%s.bin" % L]), bytearray(m["CARD_Name_%s.bin" % L]), bytearray(m["CARD_Desc_%s.bin" % L])
    iid = m["CARD_IntID.bin"]
    for cid in ids:
        c, slot = novas[cid], slots[cid]
        n = struct.unpack_from("<H", iid, (slot - BASE_INTID) * 2)[0]
        assert n and n < len(p1) // 8, "TF1 id %d has no index" % slot
        i3 = c["idx"]
        w0, w1 = tf3_cartas.para_tf1(c["w0"], c["w1"])
        w0 = (w0 & ~0x1FFF) | slot                              # plays under the slot's id
        w1 = (w1 & ~(7 << 20)) | (struct.unpack_from("<I", p1, n * 8 + 4)[0] & (7 << 20))   # the slot keeps its rarity (pack pools)
        struct.pack_into("<II", p1, n * 8, w0, w1)
        ps[n * 4:n * 4 + 4], gn[n * 6:n * 6 + 6] = pas[i3 * 4:i3 * 4 + 4], tf3_cartas.genero_para_tf1(gen[i3 * 6:i3 * 6 + 6])
        struct.pack_into("<II", ix, n * 8, len(nm), len(ds))
        nm += _utf16(c["nome"])
        ds += _utf16(c["desc"])
        ix[-8:] = struct.pack("<II", len(nm), len(ds))         # the sentinel keeps the end offsets
        if lang == "e":
            print("%d %s -> slot %d %s (index %d)" % (cid, c["nome"], slot, cartas.CARTAS[slot].nome, n))
    m.update({"CARD_Prop.bin": bytes(p1), "CARD_Pass.bin": bytes(ps), "CARD_Genre.bin": bytes(gn), "CARD_Indx_%s.bin" % L: bytes(ix),
              "CARD_Name_%s.bin" % L: bytes(nm), "CARD_Desc_%s.bin" % L: bytes(ds)})
    return ehp.montar([(n, m[n]) for n, _ in mem], tag)


def montar_mod(pasta, ids, replace=None):
    novas = tf3_cartas.carregar()
    slots = escolher_slots(ids, novas, replace or {})
    base = os.path.join(pasta, "PSP_GAME", "USRDIR")
    os.makedirs(os.path.join(base, "duelsys"), exist_ok=True)
    os.makedirs(os.path.join(base, "card"), exist_ok=True)
    for lang, nome in LANGS.items():
        open(os.path.join(base, "duelsys", "cardinfo_%s.ehp" % nome), "wb").write(portar(ids, slots, lang))
        for k in ("cardm", "cardh"):
            f = "%s_%s.cip" % (k, lang)
            out = tf3_porte_arte.porta(_ler("card/" + f), open(os.path.join(TF3_CARD, f), "rb").read(), slots)
            open(os.path.join(base, "card", f), "wb").write(out)
    with open(os.path.join(pasta, "ported.txt"), "w", encoding="utf-8", newline="\n") as f:
        for cid in ids:
            f.write("%d %d %d %s\n" % (slots[cid], cid, EFEITO.get(cid, 0), novas[cid]["nome"]))
    return slots


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    fixos = {int(a.split(":")[0]): int(a.split(":")[1]) for a in sys.argv[2:] if ":" in a}
    montar_mod(sys.argv[1], [int(a.split(":")[0]) for a in sys.argv[2:]], fixos)
    print("wrote", sys.argv[1])
