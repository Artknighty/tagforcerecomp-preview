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
SPEC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "tf3_efeitos.json")             # written by hand (and by the card editor)
SPEC_AUTO = os.path.join(os.path.dirname(os.path.abspath(__file__)), "tf3_efeitos_auto.json")   # written by tf3_efeitos_texto.py from the card texts
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
    if "not" in f and combina(p, f["not"]):
        return False
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
    if spec is None:
        spec = json.load(open(SPEC_AUTO, encoding="utf-8")) if os.path.exists(SPEC_AUTO) else {}
        spec.update(json.load(open(SPEC, encoding="utf-8")))         # the hand-written file wins
    prop = propriedades(gids)
    entradas, likes = [], {}
    for k, e in sorted(spec.items(), key=lambda x: int(x[0])):
        cid = int(k)
        if cid not in gids:
            continue
        likes[cid] = e["like"]
        if "zone" not in e:      # only borrows the record (equip cards: the rule that gives the bonus is in stat.bin)
            continue
        ids = sorted(i for i, p in prop.items() if combina(p, e["filter"]))
        entradas.append(struct.pack("<HBBI", gids[cid], ZONAS[e["zone"]], 0, len(ids)) + struct.pack("<%dH" % len(ids), *ids))
    return b"TF3E" + struct.pack("<I", len(entradas)) + b"".join(entradas), likes


def resolver_gatilhos(gids, spec=None):
    """-> trigger.bin bytes: {card: donor} for the spec entries with "trigger_like" (the donor's id is used while the game asks whether the effect starts);
    "trigger_event": "summon" also sets bit 15 of the donor: the alias holds while the game scans the events of a finished action (Summon triggers)"""
    if spec is None:
        spec = json.load(open(SPEC_AUTO, encoding="utf-8")) if os.path.exists(SPEC_AUTO) else {}
        spec.update(json.load(open(SPEC, encoding="utf-8")))
    pares = [(gids[int(k)], e["trigger_like"] | {"summon": 0x8000, "battle": 0x4000}.get(e.get("trigger_event"), 0)) for k, e in sorted(spec.items(), key=lambda x: int(x[0]))
             if int(k) in gids and "trigger_like" in e]
    return b"TF3T" + struct.pack("<I", len(pares)) + b"".join(struct.pack("<HH", a, b) for a, b in pares)


KIND_VALUES = {"draw": 1, "lp": 2}


def resolver_valores(gids, spec=None):
    """-> amount.bin bytes: the numbers of the draw / life point effects of the installed cards (see the plugin: hk_draw, hk_lp_amounts).
    Spec keys: "draw": {"self": N, "opp": N}  cards drawn by the card's controller / by the other player;
               "lp": {"self": N, "opp": N}    life point change of each side (positive = gain, negative = damage)"""
    if spec is None:
        spec = json.load(open(SPEC_AUTO, encoding="utf-8")) if os.path.exists(SPEC_AUTO) else {}
        spec.update(json.load(open(SPEC, encoding="utf-8")))
    out = []
    for k, e in sorted(spec.items(), key=lambda x: int(x[0])):
        if int(k) not in gids:
            continue
        for kind, code in KIND_VALUES.items():
            if kind in e:
                out.append(struct.pack("<HBBii", gids[int(k)], code, 0, e[kind].get("self", 0), e[kind].get("opp", 0)))
    return b"TF3V" + struct.pack("<I", len(out)) + b"".join(out)


LADOS_DESTRUIR = {"own": 1, "opp": 2, "both": 3}
ZONAS_DESTRUIR = {"monster": 1, "st": 2, "all": 3}
CONDS_DESTRUIR = {None: 0, "opp_monsters_full": 1, "opp_st_full": 2, "controls": 4}


def resolver_destruir(gids, spec=None):
    """-> destroy.bin bytes: "Destroy all" effects of the installed cards (see the plugin: hk_target_all). The card borrows the record of Dark Hole (4342, both
    sides), Raigeki (4343, opponent) or Heavy Storm (4891, Spell / Trap zones too) with "like".
    Spec: "destroy": {"side": own|opp|both, "zones": monster|st|all, "cond": {"kind": opp_monsters_full | opp_st_full | controls, "n": N, "filter": {...}}}
      cond = "Activate only when ...": the opponent's 5 monster (or Spell / Trap) zones are all occupied; or you control N face-up monsters that match the filter"""
    if spec is None:
        spec = json.load(open(SPEC_AUTO, encoding="utf-8")) if os.path.exists(SPEC_AUTO) else {}
        spec.update(json.load(open(SPEC, encoding="utf-8")))
    prop = None
    out = []
    for k, e in sorted(spec.items(), key=lambda x: int(x[0])):
        if int(k) not in gids or "destroy" not in e:
            continue
        d = e["destroy"]
        c = d.get("cond") or {}
        ids = []
        if c.get("filter"):
            prop = prop or propriedades(gids)
            ids = sorted(i for i, p in prop.items() if combina(p, c["filter"]))
        out.append(struct.pack("<HBBBBH", gids[int(k)], LADOS_DESTRUIR[d.get("side", "opp")], ZONAS_DESTRUIR[d.get("zones", "monster")],
                               CONDS_DESTRUIR[c.get("kind")], c.get("n", 0), len(ids)) + struct.pack("<%dH" % len(ids), *ids))
    return b"TF3D" + struct.pack("<I", len(out)) + b"".join(out)


SPEC_STATS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "tf3_stats.json")
SPEC_STATS_AUTO = os.path.join(os.path.dirname(os.path.abspath(__file__)), "tf3_stats_auto.json")
ESCOPOS = {"self": 0, "own": 1, "opp": 2, "all": 3, "equip": 4}
LADOS = {None: 0, "own": 1, "opp": 2, "any": 3}


def resolver_stats(gids, spec=None):
    """-> stat.bin bytes: passive ATK/DEF rules of the monsters among the installed cards (see the plugin: hk_stats).
    Spec: { "<TF3 id>": {"scope": self|own|opp|all, "atk": N, "def": N, "target": {filter}, "cond": {"side": own|opp|any, "filter": {...}, "per": bool, "excl": bool}} }
      scope self: the bonus goes to the card itself; equip: to the monster the equip card is attached to; own / opp / all: to the monsters of that side that match `target` (all when absent)
      source "st": the card is a Spell / Trap (continuous Spell) and the rule works while it is face-up in a Spell / Trap zone of either side
      cond: the rule only works while a face-up monster matching the filter is on that side (relative to the card); per = once per such monster; excl = not counting the card"""
    if spec is None:
        spec = json.load(open(SPEC_STATS_AUTO, encoding="utf-8")) if os.path.exists(SPEC_STATS_AUTO) else {}
        spec.update(json.load(open(SPEC_STATS, encoding="utf-8")))
    prop = propriedades(gids)
    out = []
    regras = [(k, r) for k, v in sorted(spec.items(), key=lambda x: int(x[0])) for r in (v if isinstance(v, list) else [v])]   # a card may list several rules
    for k, r in regras:
        cid = int(k)
        if cid not in gids:
            continue
        tgt = sorted(i for i, p in prop.items() if combina(p, r["target"])) if r.get("target") else []
        c = r.get("cond") or {}
        cond = sorted(i for i, p in prop.items() if combina(p, c["filter"])) if c else []
        out.append(struct.pack("<HBBBBiiII", gids[cid], ESCOPOS[r.get("scope", "self")] | (16 if r.get("source") == "st" else 0), LADOS[c.get("side")], 1 if c.get("per") else 0,
                               1 if c.get("excl") else 0, r.get("atk", 0), r.get("def", 0), len(tgt), len(cond))
                   + struct.pack("<%dH" % len(tgt), *tgt) + struct.pack("<%dH" % len(cond), *cond))
    return b"TF3S" + struct.pack("<I", len(out)) + b"".join(out)


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    import tf3_porte_ext as X
    ids = [int(a) for a in sys.argv[1:]] or sorted(int(k) for k in json.load(open(SPEC)))
    g = {cid: X.FIRST_ID + n for n, cid in enumerate(ids)}
    data, likes = resolver(g)
    print(len(data), "bytes;", likes)
