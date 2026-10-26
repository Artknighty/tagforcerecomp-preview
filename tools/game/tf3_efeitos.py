"""Effect records of Tag Force 3 cards compared with Tag Force 1: which TF1 card plays like a TF3 card.

Both duel modules keep one 6-word effect record per card, {id, h1..h5}, in a table sorted by id (see docs/HOOKS.md, hook 09eb4574).
Cards that work the same way share the handler h1 (e.g. in TF3 Gift Card and Ookazi both use h1 0x96248; No Entry!! and Earthquake share 0x97eac),
so the record of an equivalent TF1 card can be lent to the ported card (EFEITO in tools/mods/tf3_porte_carta.py).

Usage:  python -I tools/game/tf3_efeitos.py <TF3 id> [...]     prints the TF3 record, the TF3 cards sharing h1 and which of them exist in TF1
Input:  ref/tf3/rel_duel_eng.prx (TF3_DUEL: duel module extracted from the TF3 ISO), gen/jogo/.../gmodule/libduel_eng.prx (TF1)
"""
import os
import struct
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
TF1 = os.environ.get("TF_DUEL1") or os.path.join(RAIZ, "gen", "jogo", "PSP_GAME", "USRDIR", "gmodule", "libduel_eng.prx")
TF3 = os.environ.get("TF3_DUEL") or os.path.join(RAIZ, "ref", "tf3", "rel_duel_eng.prx")


def registros(caminho):
    """{card id: (h1..h5)} of the sorted 6-word record table"""
    d = open(caminho, "rb").read()
    n = len(d) // 4
    w = struct.unpack_from("<%dI" % n, d, 0)
    ok = lambda i: 4000 <= w[i] < 7400
    out = {}
    i = 0
    while i < n - 6 * 4:
        if not ok(i):
            i += 1
            continue
        j = i
        while j + 6 <= n and ok(j) and (j == i or w[j] > w[j - 6]):
            j += 6
        if (j - i) // 6 >= 50:                  # the record table is one long ascending run; the id/pointer pair tables are not
            for k in range(i, j, 6):
                out.setdefault(w[k], tuple(w[k + 1:k + 6]))
            i = j
        else:
            i += 1
    return out


def comparar(ids):
    import cartas
    import tf3_cartas
    r1, r3 = registros(TF1), registros(TF3)
    nomes = tf3_cartas.carregar()
    print("records: TF1 %d, TF3 %d" % (len(r1), len(r3)))
    for cid in ids:
        r = r3.get(cid)
        print("%d %s: TF3 record %s" % (cid, nomes[cid]["nome"] if cid in nomes else "?", [hex(x) for x in r] if r else "none"))
        if not r:
            continue
        for o, ro in sorted(r3.items()):
            if o != cid and ro[0] == r[0]:
                c1 = cartas.CARTAS.get(o)
                print("   same h1 in TF3: %d %s%s" % (o, nomes[o]["nome"] if o in nomes else "?",
                                                      "  -> in TF1, record %s" % [hex(x) for x in r1[o]] if o in r1 else ("  (in TF1, no record)" if c1 else "")))


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    sys.stdout.reconfigure(encoding="utf-8")
    comparar([int(a) for a in sys.argv[1:]])
