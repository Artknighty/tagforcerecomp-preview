"""Adds Tag Force 3 cards to the Tag Force 1 game BEYOND its 2480-card table (no card of the game is replaced).

The game keeps one collection record per card index in the save (2480 of them) and every card id of the game is below 6958; a new card
gets the next free index (2480, 2481, ...) and the next free id (6958, 6959, ... up to 8191, the largest id the 13-bit id field of
CARD_Prop holds). The pieces:
  ext_info_<l>.bin          CARD_Prop / Pass / Genre rows, CARD_IntID, CARD_Indx / Name / Desc texts and the CARD_Sort array of the new cards; the plugin
                            appends them to copies of the game's tables (the cardinfo file itself is not in the mod)
                            (CARD_IntID grows to cover the new ids; CARD_Sort is the alphabetical rank per index, rebuilt per language)
  card/cardm_<l>.cip, cardh_<l>.cip   rebuilt (tf3_porte_arte.extend, lazy mode): index for the new ids, no art, a pool of placeholder blocks;
  ext_art_<m|h>_<l>.bin     the art blocks of the new cards: the plugin copies one into a pool block when the game asks for that card
  deck/all_<l>.ehp          fileTbl.dat (ids in atlas order) and cis_all.gim (thumbnail atlas) grow (tf3_porte_atlas.extend)
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


def ext_info(ids, lang="e"):
    """the data of the new cards for cardinfo_<lang>.ehp, as one blob (ext_info_<lang>.bin): the game's file is NOT changed (it is read whole into a
    fixed buffer), the plugin appends these rows to copies of the tables when the game asks for them (BOOT 08833540, member lookup).
    ids = TF3 ids in the order they get the new indices/ids. Layout: b"TF3I", u32 n, u32 sort entries, u32 name bytes, u32 desc bytes, then
    CARD_Prop rows (8 each), CARD_Pass (4), CARD_Genre (6), CARD_IntID (2: the index of the id 6958 + k), CARD_Sort (the whole new array, 2 per index),
    u32 name offsets (n + 1), the names (UTF-16, 0-terminated), u32 description offsets (n + 1), the descriptions."""
    L = lang.upper()
    tag, mem = ehp.ler(_ler("duelsys/cardinfo_%s.ehp" % LANGS[lang]))
    m = dict(mem)
    novas = tf3_cartas.carregar(lang)
    pas, gen = (tf3_cartas._ler(n, lang) for n in ("CARD_Pass.bin", "CARD_Genre.bin"))
    assert len(m["CARD_Prop.bin"]) // 8 == TF1_CARDS and len(m["CARD_IntID.bin"]) == (FIRST_ID - BASE_INTID) * 2
    prop, pass_, genre, intid, novos, names, descs = bytearray(), bytearray(), bytearray(), bytearray(), [], [], []
    for k, cid in enumerate(ids):
        c, n, gid = novas[cid], TF1_CARDS + k, FIRST_ID + k
        assert gid <= LAST_ID, "too many cards for the id field"
        i3 = c["idx"]
        w0, w1 = tf3_cartas.para_tf1(c["w0"], c["w1"])
        prop += struct.pack("<II", (w0 & ~0x1FFF) | gid, w1 & ~(7 << 20))     # rarity 0: the card is in no booster pool
        pass_ += pas[i3 * 4:i3 * 4 + 4]
        genre += tf3_cartas.genero_para_tf1(gen[i3 * 6:i3 * 6 + 6])
        intid += struct.pack("<H", n)
        novos.append((n, c["nome"]))
        names.append(_utf16(c["nome"]))
        descs.append(_utf16(c["desc"]))
    sort = _sort(m, L, novos)

    def table(items):
        offs = [0]
        for t in items:
            offs.append(offs[-1] + len(t))
        return struct.pack("<%dI" % len(offs), *offs) + b"".join(items), offs[-1]
    nt, nb = table(names)
    dt, db = table(descs)
    return (b"TF3I" + struct.pack("<IIII", len(ids), len(sort) // 2, nb, db) + bytes(prop) + bytes(pass_) + bytes(genre) + bytes(intid) + sort + nt + dt)


def montar_mod(pasta, ids):
    import tf3_porte_arte
    import tf3_porte_atlas
    novas = tf3_cartas.carregar()
    base = os.path.join(pasta, "PSP_GAME", "USRDIR")
    for sub in ("card", "deck"):
        os.makedirs(os.path.join(base, sub), exist_ok=True)
    assert len(ids) <= MAX_CARDS, "at most %d cards fit the id field; got %d" % (MAX_CARDS, len(ids))
    gids = {cid: FIRST_ID + k for k, cid in enumerate(ids)}
    tf3_card = os.path.join(RAIZ, "ref", "tf3", "card")
    atlas = None
    for lang, nome in LANGS.items():
        open(os.path.join(pasta, "ext_info_%s.bin" % lang), "wb").write(ext_info(ids, lang))
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
