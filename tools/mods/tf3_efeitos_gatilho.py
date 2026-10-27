"""Writes specs (tf3_efeitos_auto.json) for Tag Force 3 monsters whose text is "<trigger>, <effect>" with a trigger and an effect the engine already knows.

trigger (the game answers the trigger predicate as the donor)         effect (the machine of the donor's record + the data filter)
  "sent from the field to the Graveyard"           -> Sangan 4054        add 1 <filter> from your Deck to your hand        -> record of Sangan / Scarr
  "destroyed by battle and sent to the Graveyard"  -> Scarr 6506  (add)  Special Summon 1 <filter> from your Deck          -> record of Howling Insect / Mystic Tomato
  "sent to the Graveyard as a result of battle"    -> Mystic Tomato 4930 (summon)
  "destroyed by an effect and sent to the Graveyard" -> Babycerasaurus 6799 (summon)
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
    # ("summoned", "add"): (6151, 6151),    # DISABLED: Green Gadget's record scans with a generic id (0xfffe) and, when it finds nothing, cancels the Summon itself; Green Gadget: "When you Normal Summon or Special Summon this card, add 1 ... from your Deck"
    ("flip", "add"): (5769, 5769),           # Iron Blacksmith Kotetsu: "FLIP: select 1 ... from your Deck and add it to your hand"
}
GATILHOS = [
    ("field_gy", r"sent from the field to the Graveyard"),
    ("battle_gy", r"destroyed (?:by battle|as a result of battle) and sent to the Graveyard|sent to the Graveyard as a result of battle|destroyed by battle"),
    ("effect_gy", r"destroyed by (?:a card )?effect and sent to the Graveyard"),
    ("summoned", r"(?:Normal|Special|Flip|Tribute)?(?: or (?:Normal|Special|Flip|Tribute))* ?Summoned|Normal Summoned, Flip Summoned, or Special Summoned"),
]
EFEITOS = [
    ("add", r"(?:select 1 .+? from your Deck and )?add (?:1|any) (?P<f>.+?) (?:from your Deck to your hand|to your hand from your Deck)", "deck"),
    ("summon", r"Special Summon 1 (?P<f>.+?) from your Deck(?: in face-up Attack Position| to your side of the field)?", "deck"),
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
        m = re.match(r"^(?:When|If) this (?:face-up )?card (?:is|was) (?P<g>[^,]+?),\s*(?:you can )?(?P<e>.+?)\.$", t)
        if not m:
            m = re.match(r"^(?P<g>FLIP):\s*(?:You can )?(?P<e>.+?)\.$", t)
        if not m or re.search(r"[Pp]ay|[Dd]iscard|[Tt]ribute|then|Then|and if|\. [A-Z]", t[m.start("e"):-1]):
            continue
        gat = "flip" if m.group("g") == "FLIP" else next((k for k, rx in GATILHOS if re.fullmatch(rx, m.group("g"))), None)
        if gat is None:
            continue
        for kind, rx, zona in EFEITOS:
            mm = re.fullmatch(rx, m.group("e"), re.I)
            if mm and (gat, kind) in TABELA:
                f = T.filtro(mm.group("f"))
                if f is None:
                    break
                trig, rec = TABELA[(gat, kind)]
                out[str(e["tf3_id"])] = {"like": rec, "trigger_like": trig, "zone": zona, "filter": f, "name": e["name"]}
                break
    return out


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    r = gerar()
    for k, v in r.items():
        print(k, v["name"], "trig", v["trigger_like"], v["zone"], v["filter"])
    print(len(r), "cards")
    if "--list" not in sys.argv:
        auto = os.path.join(AQUI, "tf3_efeitos_auto.json")
        atual = json.load(open(auto, encoding="utf-8")) if os.path.exists(auto) else {}
        atual.update(r)
        json.dump(atual, open(auto, "w", encoding="utf-8"), indent=1, ensure_ascii=False)
