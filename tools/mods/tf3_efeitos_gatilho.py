"""Writes specs (tf3_efeitos_auto.json) for Tag Force 3 monsters whose text is "<trigger>, <effect>" with a trigger and an effect the engine already knows.

trigger (the game answers the trigger predicate as the donor)         effect (the machine of the donor's record + the data filter)
  "sent from the field to the Graveyard"           -> Sangan 4054        add 1 <filter> from your Deck to your hand        -> record of Sangan / Scarr
  "destroyed by battle and sent to the Graveyard"  -> Scarr 6506  (add)  Special Summon 1 <filter> from your Deck          -> record of Howling Insect / Mystic Tomato
  "sent to the Graveyard as a result of battle"    -> Mystic Tomato 4930 (summon)
  "destroyed by an effect and sent to the Graveyard" -> Babycerasaurus 6799 (summon)
Draw / life point effects ("draw N cards", "inflict N damage to your opponent", "both players take / gain N") keep the trigger donor but play the record of a draw or life
point card (6502 Broww, 6327 Mine Golem); the numbers go to amount.bin.
Texts with a cost ("pay ... Life Points", "discard") or a second clause are skipped.
Usage:  python -I tools/mods/tf3_efeitos_gatilho.py [--list]
"""
import json
import os
import re
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(RAIZ, "tools", "game"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tf3_efeitos_texto as T

AQUI = os.path.dirname(os.path.abspath(__file__))
# (regex of the trigger, kind of effect) -> (trigger donor, record donor)
TABELA = {
    ("field_gy", "add"): (4054, 4054),
    ("battle_gy", "add"): (6506, 6506),
    ("battle_gy", "summon"): (4930, 4930),
    ("effect_gy", "summon"): (6799, 6799),
    ("summoned", "add"): (6151, 6151),    # Green Gadget: "When you Normal Summon or Special Summon this card, add 1 ... from your Deck" (the alias holds while the post-action scan runs: hk_post_action)
    ("flip", "add"): (5769, 5769),           # Iron Blacksmith Kotetsu: "FLIP: select 1 ... from your Deck and add it to your hand"
}
# draw / life point effects keep the trigger of the donor above but play another record (the plugin looks it up while the alias runs): Broww draws, Mine Golem
# changes life points; the numbers come from amount.bin
# Not wired yet; "battle_kill" has an experimental alias (trigger_event "battle", plugin hk_battle): the event is queued under the donor id but the effect never starts (the aliased id does not reach their gates: "destroys a monster by battle" 5962 KA-2 Des Scissors, "inflicts Battle Damage" 4226 Masked Sorcerer,
# "Special Summoned from the Graveyard" 5879 Molten Zombie; each is tested in other functions than 09f30ddc / 09e89110: 09e75258, 09f28de8, 09e8ab24)
for _g, _t in (("field_gy", 4054), ("battle_gy", 6506), ("effect_gy", 6799), ("summoned", 6151)):
    TABELA[(_g, "draw")] = (_t, 6502)
    TABELA[(_g, "lp")] = (_t, 6327)
# "inflicts Battle Damage to your opponent": 4226 Masked Sorcerer is the donor of both the gate and the record (draw 1 for its controller); the plugin swaps the fighter id
# while 09f28de8 runs (hk_battle_dmg, trigger_event "battle"). Checked in a duel: a new 2000 ATK card with this alias drew 1 card (own deck 36 -> 35) after dealing battle damage
TABELA[("battle_damage", "draw")] = (4226, 4226)
POS_ACAO = {"summoned", "flip", "sp_summon_gy"}   # triggers the game detects while it scans the events of a finished action (09e89110)
GATILHOS = [
    ("field_gy", r"sent from the field to the Graveyard"),
    ("battle_gy", r"destroyed (?:and sent to the Graveyard )?(?:by battle|in battle|as a result of battle)(?: and sent to the Graveyard)?|sent to the Graveyard as a result of battle"),
    ("effect_gy", r"destroyed by (?:a card )?effect and sent to the Graveyard"),
    ("battle_kill", r"destroys an? (?:opponent's )?monsters? (?:and sends it to the Graveyard )?(?:as a result of|by|in) battle(?: and sends it to the Graveyard)?"),
    ("battle_damage", r"inflicts Battle Damage to your opponent(?:'s Life Points)?"),
    ("sp_summon_gy", r"Special Summoned from the Graveyard"),
    ("summoned", r"(?:Normal|Special|Flip|Tribute)?(?: or (?:Normal|Special|Flip|Tribute))* ?Summoned|Normal Summoned, Flip Summoned, or Special Summoned"),
]
EFEITOS = [
    ("add", r"(?:select 1 .+? from your Deck and )?add (?:1|any) (?P<f>.+?) (?:from your Deck to your hand|to your hand from your Deck)", "deck"),
    ("summon", r"Special Summon 1 (?P<f>.+?) from your Deck(?: in face-up Attack Position| to your side of the field)?", "deck"),
]
# (kind, regex, spec key, spec value built from the match groups)
VALORES = [
    ("draw", r"draw (?P<n>\d+) cards?", "draw", lambda n: {"self": n}),
    ("lp", r"inflict (?P<n>\d+) (?:points of )?damage to your opponent", "lp", lambda n: {"opp": -n}),
    ("lp", r"(?:both players take (?P<n>\d+) damage|inflict (?P<m>\d+) damage to both players)", "lp", None),
    ("lp", r"both players gain (?P<n>\d+) Life Points", "lp", lambda n: {"self": n, "opp": n}),
]


def gerar():
    import mapa_cartas
    mapa = mapa_cartas.montar()
    ids = set(mapa_cartas.ids_novos())
    out = {}
    for e in mapa["cards"]:
        if e["tf3_id"] not in ids or e["kind"] != "monster":
            continue
        t = (e["text_tf3"] or "").replace("\n", " ").strip()
        m = re.match(r"^(?:This card cannot be Special Summoned\. )?(?:When|If) this (?:face-up )?card (?:is|was) (?P<g>[^,]+?),\s*(?:you can )?(?P<e>.+?)\.$", t)
        if not m:
            m = re.match(r"^(?:When|If|Each time) this (?:face-up )?card (?P<g>(?:destroys|inflicts) [^,]+?),\s*(?:you can )?(?P<e>.+?)\.$", t)
        if not m:
            m = re.match(r"^(?P<g>FLIP):\s*(?:You can )?(?P<e>.+?)\.$", t)
        if not m or re.search(r"[Pp]ay|[Dd]iscard|[Tt]ribute|then|Then|and if|\. [A-Z]", t[m.start("e"):-1]):
            continue
        gat = "flip" if m.group("g") == "FLIP" else next((k for k, rx in GATILHOS if re.fullmatch(rx, m.group("g"), re.I)), None)
        if gat is None:
            continue
        for kind, rx, chave, mk in VALORES:
            mm = re.fullmatch(rx, m.group("e"), re.I)
            if mm and (gat, kind) in TABELA:
                n = int(mm.groupdict().get("n") or mm.groupdict().get("m"))
                v = mk(n) if mk else {"self": -n, "opp": -n}
                trig, rec = TABELA[(gat, kind)]
                if gat == "battle_damage" and (kind != "draw" or n != 1):
                    break
                out[str(e["tf3_id"])] = {"like": rec, "trigger_like": trig, chave: v, "name": e["name"]}
                if gat == "battle_damage":
                    del out[str(e["tf3_id"])][chave]   # the record of the donor already draws 1
                if gat in POS_ACAO or gat in ("battle_kill", "battle_damage"):
                    out[str(e["tf3_id"])]["trigger_event"] = "battle" if gat in ("battle_kill", "battle_damage") else "summon"
                break
        else:
            mm = None
        if mm:
            continue
        for kind, rx, zona in EFEITOS:
            mm = re.fullmatch(rx, m.group("e"), re.I)
            if mm and (gat, kind) in TABELA:
                f = T.filtro(mm.group("f"))
                if f is None:
                    break
                trig, rec = TABELA[(gat, kind)]
                out[str(e["tf3_id"])] = {"like": rec, "trigger_like": trig, "zone": zona, "filter": f, "name": e["name"]}
                if gat in POS_ACAO or gat == "battle_kill":
                    out[str(e["tf3_id"])]["trigger_event"] = "battle" if gat == "battle_kill" else "summon"
                break
    return out


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    r = gerar()
    for k, v in r.items():
        print(k, v["name"], "trig", v["trigger_like"], v.get("zone") or {x: v[x] for x in ("draw", "lp") if x in v}, v.get("filter", ""))
    print(len(r), "cards")
    if "--list" not in sys.argv:
        auto = os.path.join(AQUI, "tf3_efeitos_auto.json")
        atual = json.load(open(auto, encoding="utf-8")) if os.path.exists(auto) else {}
        atual.update(r)
        json.dump(atual, open(auto, "w", encoding="utf-8"), indent=1, ensure_ascii=False)
