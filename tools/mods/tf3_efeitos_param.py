"""Data-driven effects for the cards the TF3 Cards mod adds (see plugins/src/TF3Cards: effects.bin).

A new card borrows the effect record of a game card that plays the same way (`like`) and lists which cards its effect may pick. The list is resolved
here, once, into a sorted set of duel card ids (Tag Force 1 ids and the ids of the new cards), so any predicate works and the plugin only does a lookup.

Spec file (tools/mods/tf3_efeitos.json): { "<TF3 card id>": {"like": <TF1 card id>, "zone": "deck", "filter": {...}} }
  zone    "deck": searches the Deck (like Reinforcement of the Army, 5328); "grave": picks from the Graveyard (The Warrior Returning Alive, 5330);
          "grave_summon": Graveyard cards that can be Special Summoned (Call of the Haunted, 4989); "hand": Special Summon from the hand (Magnet Circle LV2, 6572)
  filter  monster (bool), normal (bool: no effect), level_min, level_max, type (name or number), attribute (name or number),
          atk_min, atk_max, def_min, def_max, name_has (substring of the English name, case-insensitive), name_is (exact English name)
Output: effects.bin = "TF3E", u32 n, then per card: u16 duel id, u8 zone, u8 0, u32 count, u16 ids[count] (sorted).
"""
import json
import os
import struct
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(RAIZ, "tools", "game"))
SPEC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "tf3_efeitos.json")
ZONAS = {"deck": 1, "grave": 2, "grave_summon": 3, "hand": 4}
TIPOS = {"dragon": 1, "zombie": 2, "fiend": 3, "pyro": 4, "sea serpent": 5, "rock": 6, "machine": 7, "fish": 8, "dinosaur": 9, "insect": 10,
         "beast": 11, "beast-warrior": 12, "plant": 13, "aqua": 14, "warrior": 15, "winged beast": 16, "fairy": 17, "spellcaster": 18,
         "thunder": 19, "reptile": 20, "psychic": 21}
ATRIBUTOS = {"light": 1, "dark": 2, "water": 3, "fire": 4, "earth": 5, "wind": 6, "divine": 7}


def _num(v, tabela):
    return tabela[v.lower()] if isinstance(v, str) else v


def propriedades(gids):
    """{duel id: dict(name, category, level, type, attribute, atk, defe)} of every card of the game and of the new ones (gids = {TF3 id: duel id})"""
    import cartas
    import tf3_cartas
    out = {}
    for c in cartas.CARTAS.values():
        out[c.id] = dict(name=c.nome, category=c.f0, level=c.f8, type=c.f15, attribute=c.f4, atk=c.atk, defe=c.defe)
    novas = tf3_cartas.carregar()
    for cid, gid in gids.items():
        c = novas[cid]
        w0, w1 = tf3_cartas.para_tf1(c["w0"], c["w1"])
        a, d = (w0 >> 13) & 0x1FF, (w0 >> 22) & 0x1FF
        out[gid] = dict(name=c["nome"], category=w1 & 15, level=(w1 >> 8) & 15, type=(w1 >> 15) & 31, attribute=(w1 >> 4) & 15,
                        atk=None if a == 0x1FF else a * 10, defe=None if d == 0x1FF else d * 10)
    return out


def combina(p, f):
    if f.get("monster") is not None and (p["category"] < 13) != f["monster"]:
        return False
    if f.get("normal") is not None and (p["category"] == 0) != f["normal"]:
        return False
    if "level_min" in f and not (p["category"] < 13 and p["level"] >= f["level_min"]):
        return False
    if "level_max" in f and not (p["category"] < 13 and p["level"] <= f["level_max"]):
        return False
    if "type" in f and p["type"] != _num(f["type"], TIPOS):
        return False
    if "attribute" in f and p["attribute"] != _num(f["attribute"], ATRIBUTOS):
        return False
    for k, campo, cmp in (("atk_min", "atk", min), ("atk_max", "atk", max), ("def_min", "defe", min), ("def_max", "defe", max)):
        if k in f:
            v = p[campo]
            if v is None or (v < f[k] if cmp is min else v > f[k]):
                return False
    if "name_has" in f and f["name_has"].lower() not in p["name"].lower():
        return False
    if "name_is" in f and f["name_is"].lower() != p["name"].lower():
        return False
    return True


def resolver(gids, spec=None):
    """-> (effects.bin bytes, {TF3 id: like}) for the cards of the spec that are among the installed ones (gids)"""
    spec = spec if spec is not None else json.load(open(SPEC, encoding="utf-8"))
    prop = propriedades(gids)
    entradas, likes = [], {}
    for k, e in sorted(spec.items(), key=lambda x: int(x[0])):
        cid = int(k)
        if cid not in gids:
            continue
        ids = sorted(i for i, p in prop.items() if combina(p, e["filter"]))
        entradas.append(struct.pack("<HBBI", gids[cid], ZONAS[e["zone"]], 0, len(ids)) + struct.pack("<%dH" % len(ids), *ids))
        likes[cid] = e["like"]
    return b"TF3E" + struct.pack("<I", len(entradas)) + b"".join(entradas), likes


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    import tf3_porte_ext as X
    ids = [int(a) for a in sys.argv[1:]] or sorted(int(k) for k in json.load(open(SPEC)))
    g = {cid: X.FIRST_ID + n for n, cid in enumerate(ids)}
    data, likes = resolver(g)
    print(len(data), "bytes;", likes)
