"""Adds Tag Force 3 cards to the Tag Force 1 game BEYOND its 2480-card table (no card of the game is replaced).

The game keeps one collection record per card index in the save (2480 of them) and every card id of the game is below 6958; a new card
gets the next free index (2480, 2481, ...) and the next free id (6958, 6959, ... up to 8191, the largest id the 13-bit id field of
CARD_Prop holds). The pieces:
  CARD_Prop / Pass / Genre  one record more per card (id field = the new id)
  CARD_IntID                grows to cover the new ids ([id - 4000] = index; the game looks it up without a bound check)
  CARD_Indx / Name / Desc   text at the end, one Indx entry per new index, the sentinel moves
  CARD_Sort                 alphabetical rank per index: rebuilt so the new cards sit where their names belong (per language)
  card/cardm_<l>.cip, cardh_<l>.cip   rebuilt (tf3_porte_arte.extend): index and blocks for the new ids
  deck/all_<l>.ehp          fileTbl.dat (ids in atlas order) and cis_all.gim (thumbnail atlas) grow (tf3_porte_atlas.extend)
  ext_desc_<l>.bin          the descriptions of the new cards (the plugin points their CARD_Indx entries at them: the file may not grow much)
  ported.txt                "<game id> <TF3 id> <TF1 id whose effect it uses or 0> <name>", read by the TF3Cards plugin, which also gives the
                            collection records of the new indices (the save has no room for them)
Usage:  python tools/mods/tf3_porte_ext.py <mod folder> <TF3 id> [<TF3 id> ...]
"""
import bisect
import os
import struct
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(RAIZ, "tools", "game"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ehp  # noqa: E402
import tf3_cartas  # noqa: E402
from tf3_porte_carta import EFEITO, JOGO, LANGS, _ler, _utf16  # noqa: E402

BASE_INTID = 4000
FIRST_ID, LAST_ID = 6958, 8191       # ids of the new cards (the TF1 game uses 4007..6957)
TF1_CARDS = 2480                      # indices 0..2479 exist in the game
POOL = {"cardm": 64, "cardh": 24}        # placeholder art blocks per file: cards whose art is on screen at the same time (duel: hands and fields)
MAX_CARDS = LAST_ID - FIRST_ID + 1   # the 13-bit id field; the art comes on demand (lazy pool), so the game's art buffer no longer limits the count


def _nomes(m, L):
    ix, nm = m["CARD_Indx_%s.bin" % L], m["CARD_Name_%s.bin" % L]
    n = len(ix) // 8 - 1
    out = []
    for i in range(n):
        a, _ = struct.unpack_from("<II", ix, i * 8)
        b, _ = struct.unpack_from("<II", ix, i * 8 + 8)
        out.append(nm[a:b].decode("utf-16le").rstrip("\0"))
    return out


def _sort(m, L, novos):
    """rank per index with the new cards (list of (index, name)) inserted alphabetically; the order of the old ones does not change"""
    nomes = _nomes(m, L)
    rank = struct.unpack("<%dH" % (len(m["CARD_Sort_%s.bin" % L]) // 2), m["CARD_Sort_%s.bin" % L])
    ordem = sorted(range(len(rank)), key=lambda i: rank[i])
    chaves = [nomes[i].lower() for i in ordem]
    for idx, nome in sorted(novos, key=lambda x: x[1].lower()):
        k = bisect.bisect_right(chaves, nome.lower())
        chaves.insert(k, nome.lower())
        ordem.insert(k, idx)
    out = [0] * len(ordem)
    for r, i in enumerate(ordem):
        out[i] = r
    return struct.pack("<%dH" % len(out), *out)


def cardinfo(ids, lang="e"):
    """returns the bytes of cardinfo_<lang>.ehp with the cards appended; ids = TF3 ids, in the order they get new indices/ids"""
    L = lang.upper()
    tag, mem = ehp.ler(_ler("duelsys/cardinfo_%s.ehp" % LANGS[lang]))
    m = dict(mem)
    novas = tf3_cartas.carregar(lang)
    pas, gen = (tf3_cartas._ler(n, lang) for n in ("CARD_Pass.bin", "CARD_Genre.bin"))
    p1, ps, gn = (bytearray(m[n]) for n in ("CARD_Prop.bin", "CARD_Pass.bin", "CARD_Genre.bin"))
    ix, nm, ds = (bytearray(m["CARD_%s_%s.bin" % (k, L)]) for k in ("Indx", "Name", "Desc"))
    iid = bytearray(m["CARD_IntID.bin"])
    assert len(p1) // 8 == TF1_CARDS
    novos, texts = [], []
    for k, cid in enumerate(ids):
        c, n, gid = novas[cid], TF1_CARDS + k, FIRST_ID + k
        assert gid <= LAST_ID, "too many cards for the id field"
        i3 = c["idx"]
        w0, w1 = tf3_cartas.para_tf1(c["w0"], c["w1"])
        p1 += struct.pack("<II", (w0 & ~0x1FFF) | gid, w1 & ~(7 << 20))     # rarity 0: the card is in no booster pool
        ps += pas[i3 * 4:i3 * 4 + 4]
        gn += tf3_cartas.genero_para_tf1(gen[i3 * 6:i3 * 6 + 6])
        ix[-8:] = struct.pack("<II", len(nm), len(ds))                         # the sentinel becomes the new entry
        nm += _utf16(c["nome"])
        texts.append(_utf16(c["desc"]))                                        # the descriptions do not go in the file: the game reads the whole cardinfo into a fixed buffer
        ix += struct.pack("<II", len(nm), len(ds))                             # new sentinel
        pos = (gid - BASE_INTID) * 2
        if len(iid) < pos + 2:
            iid += bytes(pos + 2 - len(iid))
        struct.pack_into("<H", iid, pos, n)
        novos.append((n, c["nome"]))
    m.update({"CARD_Prop.bin": bytes(p1), "CARD_Pass.bin": bytes(ps), "CARD_Genre.bin": bytes(gn), "CARD_IntID.bin": bytes(iid),
              "CARD_Indx_%s.bin" % L: bytes(ix), "CARD_Name_%s.bin" % L: bytes(nm), "CARD_Desc_%s.bin" % L: bytes(ds)})
    m["CARD_Sort_%s.bin" % L] = _sort(m, L, novos)
    offs = [0]
    for t in texts:
        offs.append(offs[-1] + len(t))
    side = struct.pack("<I", len(texts)) + struct.pack("<%dI" % len(offs), *offs) + b"".join(texts)
    return ehp.montar([(n, m[n]) for n, _ in mem], tag), side


def montar_mod(pasta, ids):
    import tf3_porte_arte
    import tf3_porte_atlas
    novas = tf3_cartas.carregar()
    base = os.path.join(pasta, "PSP_GAME", "USRDIR")
    for sub in ("duelsys", "card", "deck"):
        os.makedirs(os.path.join(base, sub), exist_ok=True)
    assert len(ids) <= MAX_CARDS, "at most %d cards fit the id field; got %d" % (MAX_CARDS, len(ids))
    gids = {cid: FIRST_ID + k for k, cid in enumerate(ids)}
    tf3_card = os.path.join(RAIZ, "ref", "tf3", "card")
    atlas = None
    for lang, nome in LANGS.items():
        info, side = cardinfo(ids, lang)
        open(os.path.join(base, "duelsys", "cardinfo_%s.ehp" % nome), "wb").write(info)
        open(os.path.join(pasta, "ext_desc_%s.bin" % lang), "wb").write(side)
        for k in ("cardm", "cardh"):
            f = "%s_%s.cip" % (k, lang)
            out, art = tf3_porte_arte.extend(_ler("card/" + f), open(os.path.join(tf3_card, f), "rb").read(), gids, lazy=(k[-1], lang, POOL[k]))
            open(os.path.join(base, "card", f), "wb").write(out)
            open(os.path.join(pasta, "ext_art_%s_%s.bin" % (k[-1], lang)), "wb").write(art)
        tag, mem = ehp.ler(_ler("deck/all_%s.ehp" % lang))
        mem = dict(mem) if False else mem
        d = dict(mem)
        if atlas is None:
            atlas = tf3_porte_atlas.extend(d["cis_all.gim"], d["fileTbl.dat"], open(os.path.join(tf3_card, "cardm_e.cip"), "rb").read(), gids)
        mem = [(n, atlas[0] if n == "cis_all.gim" else atlas[1] if n == "fileTbl.dat" else b) for n, b in mem]
        open(os.path.join(base, "deck", "all_%s.ehp" % lang), "wb").write(ehp.montar(mem, tag))
    with open(os.path.join(pasta, "ported.txt"), "w", encoding="utf-8", newline="\n") as f:
        for cid in ids:
            f.write("%d %d %d %s\n" % (gids[cid], cid, EFEITO.get(cid, 0), novas[cid]["nome"]))
    return gids


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    print(montar_mod(sys.argv[1], [int(a) for a in sys.argv[2:]]))
