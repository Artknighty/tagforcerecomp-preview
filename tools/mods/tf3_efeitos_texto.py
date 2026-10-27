"""Writes data-driven effect specs (tools/mods/tf3_efeitos_auto.json) for the Tag Force 3 cards whose card text is one of the patterns the engine
can play with the effect of a Tag Force 1 card plus an id filter (see tf3_efeitos_param.py):

  spell  "Add 1 <filter> from your Deck to your hand."                       like 5328 Reinforcement of the Army   zone deck
  spell  "Special Summon 1 <filter> from your hand."                         like 6572 Magnet Circle LV2           zone hand
  spell  "Select 1 <filter> in your Graveyard and add it to your hand." /
         "Add 1 <filter> from your Graveyard to your hand."                  like 5330 The Warrior Returning Alive zone grave
  trap   "Select 1 <filter> from your Graveyard and Special Summon it in (face-up) Attack Position. When this card is removed from the field,
         destroy that monster. When that monster is removed from the field / destroyed, destroy this card."   like 4989 Call of the Haunted  zone grave_summon

<filter> is read piece by piece (Level N [or lower|higher], attribute, Normal, X-Type, "Name" [monster], ATK/DEF limits); a text with anything left over is skipped.
Usage:  python -I tools/mods/tf3_efeitos_texto.py [--list]       (--list only prints what would be written)
"""
import json
import os
import re
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(RAIZ, "tools", "game"))
SAIDA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "tf3_efeitos_auto.json")
TIPOS = ["Beast-Warrior", "Winged Beast", "Sea Serpent", "Spellcaster", "Dragon", "Zombie", "Fiend", "Pyro", "Rock", "Machine", "Fish", "Dinosaur",
         "Insect", "Beast", "Plant", "Aqua", "Warrior", "Fairy", "Thunder", "Reptile", "Psychic"]
ATRIBUTOS = ["LIGHT", "DARK", "WATER", "FIRE", "EARTH", "WIND"]


def filtro(s):
    """text of a card kind ('Level 4 or lower "Gladiator Beast" monster') -> filter dict, or None when a piece is not understood"""
    f = {"monster": True}
    s = s.strip()
    while s:
        m = re.match(r"Level (\d+) or lower\s*", s)
        if m:
            f["level_max"] = int(m.group(1)); s = s[m.end():]; continue
        m = re.match(r"Level (\d+) or higher\s*", s)
        if m:
            f["level_min"] = int(m.group(1)); s = s[m.end():]; continue
        m = re.match(r"Level (\d+)\s*", s)
        if m:
            f["level_min"] = f["level_max"] = int(m.group(1)); s = s[m.end():]; continue
        m = re.match(r"(%s)-Type\s*" % "|".join(TIPOS), s)
        if m:
            f["type"] = m.group(1).lower(); s = s[m.end():]; continue
        m = re.match(r"(%s)\s*" % "|".join(ATRIBUTOS), s)
        if m:
            f["attribute"] = m.group(1).lower(); s = s[m.end():]; continue
        m = re.match(r"Normal\s*", s)
        if m:
            f["normal"] = True; s = s[m.end():]; continue
        m = re.match(r"\"([^\"]+)\"\s*", s)
        if m:
            nome = m.group(1); s = s[m.end():]
            if re.match(r"[Mm]onster", s):
                f["name_has"] = nome
            else:
                f["name_is"] = nome
                f.pop("monster")
            continue
        m = re.match(r"(?:monster|Monster)s?\s*", s)
        if m:
            s = s[m.end():]; continue
        m = re.match(r"with (\d+) or (less|more) (ATK|DEF)\s*", s)
        if m:
            campo = "atk" if m.group(3) == "ATK" else "def"
            f["%s_%s" % (campo, "max" if m.group(2) == "less" else "min")] = int(m.group(1)); s = s[m.end():]; continue
        m = re.match(r"with an? (ATK|DEF) of (\d+) or (less|more)\s*", s)
        if m:
            campo = "atk" if m.group(1) == "ATK" else "def"
            f["%s_%s" % (campo, "max" if m.group(3) == "less" else "min")] = int(m.group(2)); s = s[m.end():]; continue
        m = re.match(r"with (\d+) (ATK|DEF)\s*", s)
        if m:
            campo = "atk" if m.group(2) == "ATK" else "def"
            f["%s_min" % campo] = f["%s_max" % campo] = int(m.group(1)); s = s[m.end():]; continue
        return None
    return f


REVIVE_TAIL = (r" and Special Summon it in (?:face-up )?Attack Position\. When this card is removed from the field, destroy that monster\. "
               r"When that monster is (?:removed from the field|destroyed), destroy this card\.$")
PADROES = [
    ("spell", r"^Add 1 (?P<f>.+?) from your Deck to your hand\.$", 5328, "deck"),
    ("spell", r"^Special Summon 1 (?P<f>.+?) from your hand\.$", 6572, "hand"),
    ("spell", r"^Select 1 (?P<f>.+?) in your Graveyard and add it to your hand\.$", 5330, "grave"),
    ("spell", r"^Add 1 (?P<f>.+?) from your Graveyard to your hand\.$", 5330, "grave"),
    ("trap", r"^Select 1 (?P<f>.+?) from your Graveyard" + REVIVE_TAIL, 4989, "grave_summon"),
]


def gerar():
    import mapa_cartas
    mapa = mapa_cartas.montar()
    ids = set(mapa_cartas.ids_novos())
    out = {}
    for e in mapa["cards"]:
        if e["tf3_id"] not in ids:
            continue
        for tipo, rx, like, zona in PADROES:
            m = re.match(rx, (e["text_tf3"] or "").replace("\n", " ").strip())
            if m and e["kind"] == tipo:
                f = filtro(m.group("f"))
                if f is not None:
                    out[str(e["tf3_id"])] = {"like": like, "zone": zona, "filter": f, "name": e["name"]}
                break
    return out


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    r = gerar()
    for k, v in r.items():
        print(k, v["name"], v["zone"], v["filter"])
    print(len(r), "cards")
    if "--list" not in sys.argv:
        json.dump(r, open(SAIDA, "w", encoding="utf-8"), indent=1, ensure_ascii=False)
