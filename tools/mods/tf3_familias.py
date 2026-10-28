"""Groups the Tag Force 3 cards that still have no data effect by the shape of their text, so the families that free the most cards are built first.

A text is normalised (numbers -> N, "Quoted Names" -> "X", types / attributes -> T / A) and cards with the same shape are counted.
Usage:  python -I tools/mods/tf3_familias.py [min_count]      (default 3)
"""
import json
import os
import re
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(RAIZ, "tools", "game"))
AQUI = os.path.dirname(os.path.abspath(__file__))
TIPOS = r"Beast-Warrior|Winged Beast|Sea Serpent|Spellcaster|Dragon|Zombie|Fiend|Pyro|Rock|Machine|Fish|Dinosaur|Insect|Beast|Plant|Aqua|Warrior|Fairy|Thunder|Reptile|Psychic"


def forma(t):
    t = re.sub(r'"[^"]+"', '"X"', t)
    t = re.sub(r"\b(%s)\b" % TIPOS, "T", t)
    t = re.sub(r"\b(LIGHT|DARK|WATER|FIRE|EARTH|WIND)\b", "A", t)
    t = re.sub(r"\d+", "N", t)
    return re.sub(r"\s+", " ", t).strip()


def main():
    import mapa_cartas
    minimo = int(sys.argv[1]) if len(sys.argv) > 1 else 3
    mapa = mapa_cartas.montar()
    ids = set(mapa_cartas.ids_novos())
    feitas = set()
    for nome in ("tf3_efeitos.json", "tf3_efeitos_auto.json", "tf3_stats.json", "tf3_stats_auto.json"):
        p = os.path.join(AQUI, nome)
        if os.path.exists(p):
            feitas |= set(json.load(open(p, encoding="utf-8")))
    grupos = {}
    for e in mapa["cards"]:
        if e["tf3_id"] not in ids or str(e["tf3_id"]) in feitas:
            continue
        t = (e["text_tf3"] or "").replace("\n", " ").strip()
        if not t or e["kind"] == "monster" and not re.search(r"[.]", t) and len(t) < 40:
            continue
        # a monster whose text is only flavour or a vanilla line needs nothing: keep effect-looking texts
        if e["kind"] == "monster" and not re.search(r"\b(?:can|cannot|When|If|FLIP|once per turn|gains?|loses?|Special Summon|destroy|inflict|draw|negate)\b", t, re.I):
            continue
        f = forma(t)
        # first sentence only: families are told apart by their first clause
        f = re.split(r"(?<=[.])\s", f)[0]
        grupos.setdefault((e["kind"], f), []).append((e["tf3_id"], e["name"]))
    ordem = sorted(grupos.items(), key=lambda kv: -len(kv[1]))
    total = sum(len(v) for v in grupos.values())
    print("cards with an effect-looking text and no spec yet:", total)
    for (kind, f), cs in ordem:
        if len(cs) < minimo:
            break
        print("%3d  %-7s %s\n       e.g. %s" % (len(cs), kind, f[:150], ", ".join(n for _, n in cs[:3])))


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    main()
