"""Builds the complete TF3 Cards mod folder (all the cards that exist only in Tag Force 3) with STABLE ids.

The new cards take the duel ids 6958, 6959, ... in the order of tools/mods/tf3_gids.txt (TF3 card ids, one per line, append-only). Decks and the collection
(collection_ext.bin) saved by the player refer to those ids, so a card keeps its id when the pack is built again; cards that are new to the list
(a better matching, more cards) are appended at the end. The file is rewritten with the appended ids.

Usage:  python -I tools/mods/tf3_pacote.py <output folder>      (the folder gets the files of mods/TF3Cards without plugin.dll, mod.txt and config.ini)
"""
import os
import struct
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(RAIZ, "tools", "game"))
sys.path.insert(0, os.path.join(RAIZ, "tools", "mods"))
LISTA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "tf3_gids.txt")


def ordem():
    """the TF3 ids in id order (stable); candidates that are not in the file yet are appended"""
    import mapa_cartas
    tf3c = open(os.path.join(RAIZ, "ref", "tf3", "card", "cardh_e.cip"), "rb").read()
    lo = struct.unpack_from("<I", tf3c, 0x14)[0]
    ok = lambda c: c >= lo and (lambda v: v and not v & 0x80000000)(struct.unpack_from("<I", tf3c, 0x30 + (c - lo) * 8)[0])   # the card has simple art
    candidatos = [c for c in mapa_cartas.ids_novos() if ok(c)]
    atual = [int(x) for x in open(LISTA).read().split()] if os.path.exists(LISTA) else []
    novos = [c for c in candidatos if c not in set(atual)]
    return atual + novos, novos


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    import tf3_porte_ext as X
    ids, novos = ordem()
    open(LISTA, "w").write("\n".join(map(str, ids)) + "\n")
    print(len(ids), "cards,", len(novos), "appended")
    X.montar_mod(sys.argv[1], ids)
