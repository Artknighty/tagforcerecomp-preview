"""Data-driven effects for the cards the TF3 Cards mod adds (see plugins/src/TF3Cards: effects.bin).

A new card borrows the effect record of a game card that plays the same way (`like`) and lists which cards its effect may pick. The list is resolved
here, once, into a sorted set of duel card ids (Tag Force 1 ids and the ids of the new cards), so any predicate works and the plugin only does a lookup.

Spec file (tools/mods/tf3_efeitos.json): { "<TF3 card id>": {"like": <TF1 card id>, "zone": "deck", "filter": {...}} }
  zone    "deck": searches the Deck (like Reinforcement of the Army, 5328); "grave": picks from the Graveyard (The Warrior Returning Alive, 5330);
          "grave_summon": Graveyard cards that can be Special Summoned (Call of the Haunted, 4989); "hand": Special Summon from the hand (Magnet Circle LV2, 6572)
          "hand_deck_summon": one named Chrysalis Dolphin target from either hand or Deck (donor 6764)
          "deck_field_required": named Field Spell search plus continuous self-destruction without it (donor 6044)
  filter  monster (bool), category_in (list of card categories: 2, 3 = Fusion, 4, 5 = Ritual), icon (native subtype number; 4 = Continuous), normal (bool: no effect), level_min, level_max, type (name or number), attribute (name or number),
          atk_min, atk_max, def_min, def_max, name_has (substring of the English name, case-insensitive), name_is (exact English name)
  to      "deck": the cards go back to the Deck instead of the hand (grave zone); then_draw: N cards drawn afterwards (0-7)
  count   (deck / grave zones) how many cards the effect moves to the hand: the game picks the first, the plugin moves count-1 more matching ones
Output: effects.bin = "TF3E", u32 n, then per card: u16 duel id, u8 zone, u8 extra picks (count - 1), u32 count, u16 ids[count] (sorted).
"""
import json
import os
import struct
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(RAIZ, "tools", "game"))
SPEC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "tf3_efeitos.json")             # written by hand (and by the card editor)
SPEC_AUTO = os.path.join(os.path.dirname(os.path.abspath(__file__)), "tf3_efeitos_auto.json")   # written by tf3_efeitos_texto.py from the card texts
ZONAS = {"deck": 1, "grave": 2, "grave_summon": 3, "hand": 4, "hand_deck_summon": 5, "deck_field_required": 6}
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
        out[c.id] = dict(name=c.nome, category=c.f0, icon=c.f12, level=c.f8, type=c.f15, attribute=c.f4, atk=c.atk, defe=c.defe)
    novas = tf3_cartas.carregar()
    for cid, gid in gids.items():
        c = novas[cid]
        w0, w1 = tf3_cartas.para_tf1(c["w0"], c["w1"])
        a, d = (w0 >> 13) & 0x1FF, (w0 >> 22) & 0x1FF
        out[gid] = dict(name=c["nome"], category=w1 & 15, icon=(w1 >> 12) & 7, level=(w1 >> 8) & 15, type=(w1 >> 15) & 31, attribute=(w1 >> 4) & 15,
                        atk=None if a == 0x1FF else a * 10, defe=None if d == 0x1FF else d * 10)
    return out


def combina(p, f):
    if "not" in f and combina(p, f["not"]):
        return False
    if f.get("monster") is not None and (p["category"] < 13) != f["monster"]:
        return False
    if "category_in" in f and p["category"] not in f["category_in"]:
        return False
    if "icon" in f and p.get("icon") != f["icon"]:
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
        spec = _carregar(SPEC_AUTO, SPEC)         # the hand-written file wins
    prop = propriedades(gids)
    entradas, likes = [], {}
    for k, e in sorted(spec.items(), key=lambda x: int(x[0])):
        cid = int(k)
        if cid not in gids:
            continue
        likes[cid] = e["like"]
        if "zone" not in e:      # only borrows the record (equip cards: the rule that gives the bonus is in stat.bin)
            continue
        if e['zone'] == 'hand_deck_summon' and (e['like'] != 6764 or
                set(e) - {'like', 'zone', 'filter', 'name'} or
                set(e['filter']) != {'name_is'}):
            raise ValueError('hand_deck_summon requires a single named Chrysalis Dolphin target')
        ids = sorted(i for i, p in prop.items() if combina(p, e["filter"]))
        if e['zone'] == 'hand_deck_summon' and (len(ids) != 1 or prop[ids[0]]['category'] != 1):
            raise ValueError('Chrysalis requires exactly one ordinary Effect Monster target')
        if e['zone'] == 'deck_field_required' and (e['like'] != 6044 or
                set(e) - {'like', 'zone', 'filter', 'name'} or
                set(e['filter']) != {'name_is'} or len(ids) != 1 or
                prop[ids[0]]['category'] != 13 or prop[ids[0]]['icon'] != 2):
            raise ValueError('deck_field_required requires one named Field Spell and native discard search')
        entradas.append(struct.pack("<HBBI", gids[cid], ZONAS[e["zone"]] | (16 if e.get("to") == "deck" else 0) | (min(int(e.get("then_draw", 0)), 7) << 5), max(int(e.get("count", 1)) - 1, 0), len(ids)) + struct.pack("<%dH" % len(ids), *ids))
    return b"TF3E" + struct.pack("<I", len(entradas)) + b"".join(entradas), likes


def resolver_tributos(gids, spec=None):
    """TF3B: source/native donor/allowed Normal target IDs for double-Tribute predicates."""
    if spec is None:
        spec = _carregar(SPEC_AUTO, SPEC)
    import tf3_cartas
    raw = tf3_cartas.carregar()
    prop = propriedades(gids)
    # The older metadata converter truncates Tuner categories. Exclude those
    # explicitly here rather than treating a Normal Tuner as a Normal Monster.
    unsupported = {gid for cid, gid in gids.items() if ((raw[cid]['w1'] >> 1) & 31) != 0}
    donors = {1: 5409, 2: 6040, 3: 6120, 4: 6260, 5: 6121, 6: 6259}
    rows = []
    for key, effect in sorted(spec.items(), key=lambda pair: int(pair[0])):
        if 'double_tribute_normal' not in effect:
            continue
        attribute = _num(effect['double_tribute_normal'], ATRIBUTOS)
        if attribute not in donors:
            raise ValueError('double_tribute_normal requires an Attribute with a native donor')
        if int(key) not in gids:
            continue
        ids = sorted(gid for gid, p in prop.items()
                     if p['category'] == 0 and p['attribute'] == attribute and gid not in unsupported)
        rows.append(struct.pack('<HHI', gids[int(key)], donors[attribute], len(ids)) + struct.pack('<%dH' % len(ids), *ids))
    return b'TF3B' + struct.pack('<I', len(rows)) + b''.join(rows)


def resolver_invocacoes(gids, spec=None):
    """TF3S: u32 count, then u16 source ID / flags (both=1, opponent=2, inherent prohibition=4)."""
    if spec is None:
        spec = _carregar(SPEC_AUTO, SPEC)
    rows = []
    for key, effect in sorted(spec.items(), key=lambda pair: int(pair[0])):
        lock = effect.get('special_summon_lock')
        if lock is not None and lock not in ('both', 'opponent'):
            raise ValueError('special_summon_lock must be both or opponent')
        forbidden = effect.get('cannot_special_summon', False)
        if type(forbidden) is not bool:
            raise ValueError('cannot_special_summon must be boolean')
        flags = {None: 0, 'both': 1, 'opponent': 2}[lock] | (4 if forbidden else 0)
        attribute = effect.get('special_summon_except')
        if attribute is not None:
            if flags or attribute not in ATRIBUTOS:
                raise ValueError('Attribute summon locks require a standalone valid Attribute')
            value = ATRIBUTOS[attribute]
            if not 1 <= value <= 6:
                raise ValueError('Unsupported Attribute summon lock')
            flags = value << 8
        if flags and int(key) in gids:
            rows.append(struct.pack('<HH', gids[int(key)], flags))
    return b'TF3S' + struct.pack('<I', len(rows)) + b''.join(rows)

def resolver_lords(gids, spec=None):
    """TF3L: u32 count, then u16 duel id / u8 monster Type. Spec: {"lord_tribute": "<Type>"} = Tribute Summoned only by Tributing 3 monsters of that Type
    (the plugin reuses the 3-Tribute flow of the Egyptian Gods and refuses a Tribute of another Type)."""
    if spec is None:
        spec = _carregar(SPEC_AUTO, SPEC)
    rows = []
    for key, effect in sorted(spec.items(), key=lambda pair: int(pair[0])):
        kind = effect.get('lord_tribute')
        if kind is None or int(key) not in gids:
            continue
        if kind in ('any', 'all'):
            type_val = 0
        elif isinstance(kind, str) and kind in TIPOS:
            type_val = TIPOS[kind]
        else:
            raise ValueError('lord_tribute needs a monster Type name or "any"')
        rows.append(struct.pack('<HB', gids[int(key)], type_val))
    return b'TF3L' + struct.pack('<I', len(rows)) + b''.join(rows)


RITUAL_DONORS = {4676}   # native Ritual Spell record used by every specific-target imported Ritual Spell (Black Luster Ritual)


def resolver_rituais(gids, spec=None):
    """TF3R: u32 count, then u16 Ritual Spell duel id / u16 Ritual Monster duel id / u8 total Levels (the plugin appends these rows to the
    native Ritual table 09f767ac while the native Ritual functions run). Level byte bit 7 requests an exact sum; the low six bits hold the Level.
    Spec: {"ritual": {"monster": <TF3 id>, "level": N}} with like 4676; optional exact: true and monsters: [ids] / level: "monster"."""
    if spec is None:
        spec = _carregar(SPEC_AUTO, SPEC)
    rows = []
    prop = propriedades(gids)
    for key, effect in sorted(spec.items(), key=lambda pair: int(pair[0])):
        ritual = effect.get('ritual')
        if ritual is None or int(key) not in gids:
            continue
        # {"monster": id, "level": N} or {"monsters": [ids], "level": "monster"} (each row takes the Ritual Monster's printed Level)
        if effect.get('like') not in RITUAL_DONORS or set(ritual) - {'exact'} not in ({'monster', 'level'}, {'monsters', 'level'}):
            raise ValueError('ritual requires like 4676 and monster/monsters + level')
        if not isinstance(ritual.get('exact', False), bool):
            raise ValueError('ritual exact must be a boolean')
        monsters = [ritual['monster']] if 'monster' in ritual else list(ritual['monsters'])
        for monster in monsters:
            if int(monster) not in gids:
                raise ValueError('Ritual Monster %s is not installed' % monster)
            level = prop[gids[int(monster)]]['level'] if ritual['level'] == 'monster' else ritual['level']
            if not isinstance(level, int) or not 1 <= level <= 63 or prop[gids[int(monster)]]['category'] not in (4, 5):
                raise ValueError('ritual monster %s needs a Ritual category and a Level 1..63' % monster)
            rows.append(struct.pack('<HHB', gids[int(key)], gids[int(monster)], level | (0x80 if ritual.get('exact') else 0)))
    if len(rows) > 24:
        raise ValueError('at most 24 imported Ritual rows are supported')
    return b'TF3R' + struct.pack('<I', len(rows)) + b''.join(rows)


def rule_outputs(gids):
    """-> ([(file name, bytes)...], {TF3 id: like}): every rule file of the TF3Cards mod from one spec. Add new rule files HERE only; the packagers,
    the review queue and the test runners all call this."""
    fx, likes = resolver(gids)
    return [('effects.bin', fx), ('trigger.bin', resolver_gatilhos(gids)), ('stat.bin', resolver_stats(gids)),
            ('amount.bin', resolver_valores(gids)), ('destroy.bin', resolver_destruir(gids)),
            ('summon.bin', resolver_invocacoes(gids)), ('tribute.bin', resolver_tributos(gids)),
            ('ritual.bin', resolver_rituais(gids)), ('lord.bin', resolver_lords(gids))], likes


RULE_FILES = ('effects.bin', 'trigger.bin', 'stat.bin', 'amount.bin', 'destroy.bin', 'summon.bin', 'tribute.bin', 'ritual.bin', 'lord.bin')



def resolver_gatilhos(gids, spec=None):
    """-> trigger.bin bytes: {card: donor} for the spec entries with "trigger_like" (the donor's id is used while the game asks whether the effect starts);
    Donor flags: bit 15 = summon/post-action gate; bit 14 = battle gate;
    bit 13 = native attack marker, end-of-Battle-Phase Defense and timed position lock."""
    if spec is None:
        spec = _carregar(SPEC_AUTO, SPEC)
    pares = [(gids[int(k)], e["trigger_like"] | {"summon": 0x8000, "battle": 0x4000}.get(e.get("trigger_event"), 0) |
              (0x2000 if e.get("after_attack_defense") else 0)) for k, e in sorted(spec.items(), key=lambda x: int(x[0]))
             if int(k) in gids and "trigger_like" in e]
    data = b"TF3T" + struct.pack("<I", len(pares)) + b"".join(struct.pack("<HH", a, b) for a, b in pares)
    conditions = []
    for key, rule in sorted(spec.items(), key=lambda x: int(x[0])):
        if int(key) not in gids or not rule.get('trigger_condition'):
            continue
        unknown = set(rule['trigger_condition']) - {'destroyed', 'by_effect', 'opponent_effect', 'face_up', 'equipped_tributed', 'equipped_to_deck', 'from_deck'}
        if unknown or 'trigger_like' not in rule:
            raise ValueError('Invalid trigger condition for %s: %s' % (key, rule['trigger_condition']))
        flags = sum(bit for name, bit in [('destroyed', 1), ('by_effect', 2), ('opponent_effect', 4), ('face_up', 8), ('equipped_tributed', 16), ('equipped_to_deck', 32), ('from_deck', 64)]
                    if rule['trigger_condition'].get(name))
        conditions.append(struct.pack('<HH', gids[int(key)], flags))
    if conditions:
        data += b'TGC1' + struct.pack('<I', len(conditions)) + b''.join(conditions)
    records = [struct.pack('<HH', gids[int(key)], rule['trigger_record_like'])
               for key, rule in sorted(spec.items(), key=lambda x: int(x[0]))
               if int(key) in gids and rule.get('trigger_record_like')]
    if records:
        data += b'TGR1' + struct.pack('<I', len(records)) + b''.join(records)
    kinds = []
    for key, rule in sorted(spec.items(), key=lambda x: int(x[0])):
        if int(key) not in gids or 'trigger_record_kind' not in rule:
            continue
        kind = rule['trigger_record_kind']
        if type(kind) is not int or kind not in range(4) or not rule.get('trigger_record_like') or not rule.get('trigger_like'):
            raise ValueError('Record-kind overrides require explicit trigger and record donors and a kind in 0..3')
        kinds.append(struct.pack('<HH', gids[int(key)], kind))
    if kinds:
        data += b'TRK1' + struct.pack('<I', len(kinds)) + b''.join(kinds)
    return data


KIND_VALUES = {"draw": 1, "lp": 2}
LADOS_POR = {"own": 0, "opp": 1, "both": 2}


BATTLE_NUMBERS = {"destroyed_def": -2147483647, "destroyed_atk": -2147483646}   # sentinels the plugin turns into the DEF / ATK of the monster destroyed in battle


def _numero(v):
    if isinstance(v, str):
        if v not in BATTLE_NUMBERS:
            raise ValueError("Unknown life point amount: %r" % v)
        return BATTLE_NUMBERS[v]
    return v


def resolver_valores(gids, spec=None):
    """-> amount.bin bytes: the numbers of the draw / life point effects of the installed cards (see the plugin: hk_draw, hk_lp_amounts).
    Spec keys: "draw": {"self": N, "opp": N}  cards drawn by the card's controller / by the other player;
               "lp": {"self": N, "opp": N}    life point change of each side (positive = gain, negative = damage); "opp": "destroyed_def" /
                                              "destroyed_atk" = damage equal to the DEF / ATK of the monster just destroyed in battle (battle triggers only)
    Either may carry "per": {"side": own|opp|both, "filter": {...}, "distinct": bool}: the numbers are multiplied by how many face-up monsters of that side
    match the filter (distinct = counting different card ids only)
    Either may carry "discard": {"count": N, "filter": {...}}: a cost - N matching cards of the controller's hand are sent to the Graveyard first; if the hand has
    fewer, nothing is drawn / gained; "to": "deck" puts them on the Deck instead; "both": true makes the other player pay it too (count up to 63)
    Either may carry "mill": {"count": N, "filter": {...}}: after the draw, the first N matching cards of the controller's Deck go to the Graveyard"""
    if spec is None:
        spec = _carregar(SPEC_AUTO, SPEC)
    out, per, cost, mill = [], [], [], []
    prop = None
    for k, e in sorted(spec.items(), key=lambda x: int(x[0])):
        if int(k) not in gids:
            continue
        for kind, code in KIND_VALUES.items():
            if kind in e:
                out.append(struct.pack("<HBBii", gids[int(k)], code, 0, _numero(e[kind].get("self", 0)), _numero(e[kind].get("opp", 0))))
                d = e[kind].get("discard")
                if d:
                    prop = prop or propriedades(gids)
                    ids = sorted(i for i, q in prop.items() if combina(q, d.get("filter", {})))
                    cost.append(struct.pack("<HBBH", gids[int(k)], code, d.get("count", 1) | (0x80 if d.get("to") == "deck" else 0) | (0x40 if d.get("both") else 0), len(ids)) + struct.pack("<%dH" % len(ids), *ids))
                d = e[kind].get("mill")
                if d:
                    prop = prop or propriedades(gids)
                    ids = sorted(i for i, q in prop.items() if combina(q, d.get("filter", {})))
                    mill.append(struct.pack("<HBBH", gids[int(k)], code, d.get("count", 1), len(ids)) + struct.pack("<%dH" % len(ids), *ids))
                p = e[kind].get("per")
                if p:
                    prop = prop or propriedades(gids)
                    ids = sorted(i for i, q in prop.items() if combina(q, p.get("filter", {})))
                    flags = (1 if p.get("distinct") else 0) | (LADOS_POR[p.get("side", "own")] << 1)
                    per.append(struct.pack("<HBBH", gids[int(k)], code, flags, len(ids)) + struct.pack("<%dH" % len(ids), *ids))
    return b"TF3V" + struct.pack("<I", len(out)) + b"".join(out) + b"TF3M" + struct.pack("<I", len(per)) + b"".join(per) \
        + b"TF3C" + struct.pack("<I", len(cost)) + b"".join(cost) + b"TF3X" + struct.pack("<I", len(mill)) + b"".join(mill)


LADOS_DESTRUIR = {"own": 1, "opp": 2, "both": 3}
ZONAS_DESTRUIR = {"monster": 1, "st": 2, "all": 3}
CONDS_DESTRUIR = {None: 0, "opp_monsters_full": 1, "opp_st_full": 2, "controls": 4, "opp_attack_monsters": 5, "target": 6, "target_all": 7, "target_all_not": 9}


def resolver_destruir(gids, spec=None):
    """-> destroy.bin bytes: "Destroy all" effects of the installed cards (see the plugin: hk_target_all). The card borrows the record of Dark Hole (4342, both
    sides), Raigeki (4343, opponent) or Heavy Storm (4891, Spell / Trap zones too) with "like".
    Spec: "destroy": {"side": own|opp|both, "zones": monster|st|all, "cond": {"kind": opp_monsters_full | opp_st_full | controls, "n": N, "filter": {...}}}
      cond kind "target_all": same, every matching card is destroyed (for the donors that destroy all, Ladybug 5139); zones "st" / "all" also make Spell / Trap Cards targets
      cond kind "target" (zones monster): "Destroy 1 face-up monster matching the filter" - only matching face-up monsters are targets, the first one found is destroyed
      cond kind "target_all_not" (zones monster): every face-down monster and every face-up one that does NOT match the filter is destroyed ("destroy all face-down and non-EARTH monsters")
      cond = "Activate only when ...": the opponent's 5 monster (or Spell / Trap) zones are all occupied; or you control N face-up monsters that match the filter"""
    if spec is None:
        spec = _carregar(SPEC_AUTO, SPEC)
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


def _carregar(auto, mao):
    """Generated specs, then the hand-written ones (they win), then the temporary ones of TF3_SPEC_EXTRA (a JSON file: used by tools/test/tf3_lote.py --spec to try a spec without touching the project files)."""
    spec = json.load(open(auto, encoding="utf-8")) if os.path.exists(auto) else {}
    spec.update(json.load(open(mao, encoding="utf-8")))
    extra = os.environ.get("TF3_SPEC_EXTRA")
    if extra and os.path.exists(extra):
        e = json.load(open(extra, encoding="utf-8"))
        spec.update(e.get("stats", {}) if mao == SPEC_STATS else e.get("effects", {}))
    return spec
ESCOPOS = {"self": 0, "own": 1, "opp": 2, "all": 3, "equip": 4}
LADOS = {None: 0, "own": 1, "opp": 2, "any": 3}


def resolver_stats(gids, spec=None):
    """-> stat.bin bytes: passive ATK/DEF rules of the monsters among the installed cards (see the plugin: hk_stats).
    Spec: { "<TF3 id>": {"scope": self|own|opp|all, "atk": N, "def": N, "target": {filter}, "cond": {"side": own|opp|any, "filter": {...}, "per": bool, "excl": bool}} }
      scope self: the bonus goes to the card itself; equip: to the monster the equip card is attached to; own / opp / all: to the monsters of that side that match `target` (all when absent)
      source "st": the card is a Spell / Trap (continuous Spell) and the rule works while it is face-up in a Spell / Trap zone of either side
      cond: the rule only works while a face-up monster matching the filter is on that side (relative to the card); per = once per such monster; excl = not counting the card; zone "grave" counts the cards of that side's Graveyard instead of face-up monsters; zone "hand" counts that side's hand (filter ignored);
      "set": true makes atk/def replace the printed value (Slifer: hand x 1000)"""
    if spec is None:
        spec = _carregar(SPEC_STATS_AUTO, SPEC_STATS)
    prop = propriedades(gids)
    out = []
    levels = {}
    protections = {}
    control_locks = set()
    battle_guards = {}
    battle_bonuses = {}
    role_bonuses = {}
    regras = [(k, r) for k, v in sorted(spec.items(), key=lambda x: int(x[0])) for r in (v if isinstance(v, list) else [v])]   # a card may list several rules
    for k, r in regras:
        cid = int(k)
        if cid not in gids:
            continue
        if 'battle_role_bonus' in r:
            bonus = r['battle_role_bonus']
            if set(r) != {'battle_role_bonus'} or type(bonus) is not int or not 0 < bonus <= 9990 or bonus % 10:
                raise ValueError('Battle role bonuses require a standalone positive multiple of ten')
            from tf3_cartas import carregar
            if ((carregar()[cid]['w1'] >> 1) & 31) != 1:
                raise ValueError('Battle role bonuses require a supported ordinary Effect Monster')
            role_bonuses[gids[cid]] = bonus
            continue
        if 'battle_light_bonus' in r:
            bonus = r['battle_light_bonus']
            if set(r) != {'battle_light_bonus'} or type(bonus) is not int or not 0 < bonus <= 9990 or bonus % 10:
                raise ValueError('LIGHT battle bonuses require a standalone positive multiple of ten')
            from tf3_cartas import carregar
            if ((carregar()[cid]['w1'] >> 1) & 31) != 1:
                raise ValueError('LIGHT battle bonuses require a supported ordinary Effect Monster')
            battle_bonuses[gids[cid]] = bonus
            continue
        if 'battle_guard' in r:
            guard = r['battle_guard']
            if set(r) != {'battle_guard'} or set(guard) != {'stat', 'min'} or guard['stat'] != 'atk' or type(guard['min']) is not int or not 0 < guard['min'] <= 32767:
                raise ValueError('Battle guards require a standalone ATK minimum rule')
            from tf3_cartas import carregar
            if ((carregar()[cid]['w1'] >> 1) & 31) != 1:
                raise ValueError('Battle guards currently require a supported ordinary Effect Monster')
            battle_guards[gids[cid]] = (1, guard['min'])
            continue
        if r.get('control_lock'):
            control_locks.add(gids[cid])
        if r.get('level'):
            if r.get('scope') != 'equip' or not 0 < r['level'] <= 32767:
                raise ValueError('level bonuses currently require an equip rule and a positive integer')
            if gids[cid] in levels and levels[gids[cid]] != r['level']:
                raise ValueError('conflicting level bonuses for one equip card')
            levels[gids[cid]] = r['level']
        if r.get('attack_protect'):
            protection = r['attack_protect']
            f = protection.get('filter') or {}
            if set(f) - {'monster', 'type', 'attribute'} or f.get('monster') is not True:
                raise ValueError('attack protection currently supports monster type and attribute conditions')
            value = ({'self': 1, 'same_name': 2}[protection.get('exclude', 'self')],
                     _num(f['type'], TIPOS) if 'type' in f else 0, _num(f['attribute'], ATRIBUTOS) if 'attribute' in f else 0)
            if gids[cid] in protections and protections[gids[cid]] != value:
                raise ValueError('conflicting attack protections for one card')
            protections[gids[cid]] = value
        tgt = sorted(i for i, p in prop.items() if combina(p, r["target"])) if r.get("target") else []
        c = r.get("cond") or {}
        if r.get('after_flip') and (r.get('scope', 'self') != 'self' or r.get('source') or c):
            raise ValueError('Resolved-FLIP bonuses require an unconditional self monster rule')
        cond = sorted(i for i, p in prop.items() if combina(p, c["filter"])) if c.get("filter") else []
        if c.get('zone') not in (None, 'grave', 'hand', 'st', 'controls'):
            raise ValueError('Unsupported stat condition zone: %s' % c['zone'])
        if c.get('sum_levels') and (c.get('zone') is not None or not c.get('per') or not c.get('filter', {}).get('monster')):
            raise ValueError('Level sums require a per-monster field condition')
        out.append(struct.pack("<HBBBBiiII", gids[cid], ESCOPOS[r.get("scope", "self")] | (16 if r.get("source") == "st" else 0) | (32 if r.get("target") else 0), LADOS[c.get("side")] | (4 if c.get("zone") == "grave" else 8 if c.get("zone") == "hand" else 16 if c.get("zone") == "st" else 32 if c.get("zone") == "controls" else 0), (1 if c.get("per") else 0) | (2 if r.get('per_level') else 0) | (4 if r.get('set') else 0) | (8 if c.get('sum_levels') else 0) | (16 if r.get('after_flip') else 0),
                               1 if c.get("excl") else 0, r.get("atk", 0), r.get("def", 0), len(tgt), len(cond))
                   + struct.pack("<%dH" % len(tgt), *tgt) + struct.pack("<%dH" % len(cond), *cond))
    trailer = b'LVL1' + struct.pack('<I', len(levels)) + b''.join(struct.pack('<Hh', cid, level) for cid, level in sorted(levels.items())) if levels else b''
    if protections:
        trailer += b'ATP1' + struct.pack('<I', len(protections)) + b''.join(struct.pack('<HBBBB', cid, *value, 0) for cid, value in sorted(protections.items()))
    if control_locks:
        trailer += b'CTL1' + struct.pack('<I', len(control_locks)) + b''.join(struct.pack('<H', cid) for cid in sorted(control_locks))
    if battle_guards:
        trailer += b'BGP1' + struct.pack('<I', len(battle_guards)) + b''.join(struct.pack('<HHI', cid, *value) for cid, value in sorted(battle_guards.items()))
    if battle_bonuses:
        trailer += b'BAP1' + struct.pack('<I', len(battle_bonuses)) + b''.join(struct.pack('<HH', cid, bonus) for cid, bonus in sorted(battle_bonuses.items()))
    if role_bonuses:
        trailer += b'BAR1' + struct.pack('<I', len(role_bonuses)) + b''.join(struct.pack('<HH', cid, bonus) for cid, bonus in sorted(role_bonuses.items()))
    return b"TF3S" + struct.pack("<I", len(out)) + b"".join(out) + trailer


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    import tf3_porte_ext as X
    ids = [int(a) for a in sys.argv[1:]] or sorted(int(k) for k in json.load(open(SPEC)))
    g = {cid: X.FIRST_ID + n for n, cid in enumerate(ids)}
    data, likes = resolver(g)
    print(len(data), "bytes;", likes)
