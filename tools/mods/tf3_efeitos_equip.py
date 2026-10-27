"""Writes specs (tf3_efeitos_auto.json: the borrowed record, tf3_stats_auto.json: the bonus) for the Tag Force 3 Equip Spell Cards whose text is
"Equip only to a <type|attribute> monster. It gains N ATK [and DEF]." (or the unrestricted "The equipped monster gains N ATK [and DEF]").

The record comes from a Tag Force 1 equip card with the same restriction (it decides which monsters can be equipped), the bonus from the text.
Usage:  python -I tools/mods/tf3_efeitos_equip.py [--list]
"""
import json
import os
import re
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(RAIZ, "tools", "game"))
AQUI = os.path.dirname(os.path.abspath(__file__))
TIPOS = ["Beast-Warrior", "Winged Beast", "Sea Serpent", "Spellcaster", "Dragon", "Zombie", "Fiend", "Pyro", "Rock", "Machine", "Fish", "Dinosaur", "Insect",
         "Beast", "Plant", "Aqua", "Warrior", "Fairy", "Thunder", "Reptile", "Psychic"]
ATRIBUTOS = ["LIGHT", "DARK", "WATER", "FIRE", "EARTH", "WIND"]


def restricao(t):
    m = re.match(r"Equip only to an? (%s)(?:-Type)? monster\." % "|".join(TIPOS + ATRIBUTOS), t)
    if m:
        v = m.group(1)
        return ("attribute", v.lower()) if v in ATRIBUTOS else ("type", v.lower()), t[m.end():]
    if t.startswith("The equipped monster"):
        return (None, None), t
    return False, t


def bonus(t):
    atk = def_ = 0
    m = re.search(r"(?:gains?|increases?)(?: its)? (\d+) ATK and DEF", t)
    if m:
        atk = def_ = int(m.group(1))
    else:
        m = re.search(r"gains? (\d+) ATK", t)
        if m:
            atk = int(m.group(1))
        m = re.search(r"loses? (\d+) DEF", t)
        if m:
            def_ = -int(m.group(1))
    return atk, def_


def gerar():
    import mapa_cartas
    mapa = mapa_cartas.montar()
    ids = set(mapa_cartas.ids_novos())
    doadores = {}
    for e in mapa["cards"]:
        t = e["text_tf1"] or ""
        if e["tf1_id"] and e["kind"] == "spell" and e["rec_tf1"] and e["rec_tf1"][0] == 0x783b0:
            m = re.match(r"An? (%s)(?:-Type)? monster equipped" % "|".join(TIPOS + ATRIBUTOS), t)
            if m:
                v = m.group(1)
                doadores.setdefault(("attribute", v.lower()) if v in ATRIBUTOS else ("type", v.lower()), e["tf1_id"])
            elif t.startswith("Increase the ATK of a monster equipped"):
                doadores.setdefault((None, None), e["tf1_id"])
    efeitos, stats = {}, {}
    for e in mapa["cards"]:
        if e["tf3_id"] not in ids or e["kind"] != "spell":
            continue
        t = (e["text_tf3"] or "").replace("\n", " ").strip()
        r, resto = restricao(t)
        if r is False:
            continue
        atk, dfs = bonus(resto)
        if (atk, dfs) == (0, 0) or r not in doadores or "for each" in resto:
            continue
        # only the plain cases: nothing else in the text besides the bonus and a "when destroyed" clause the game does not play
        efeitos[str(e["tf3_id"])] = {"like": doadores[r], "name": e["name"]}
        s = {"scope": "equip", "atk": atk, "def": dfs}
        if r[0]:
            s["target"] = {"monster": True, r[0]: r[1]}
        stats[str(e["tf3_id"])] = s
    return efeitos, stats, mapa


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    efeitos, stats, mapa = gerar()
    nomes = {str(e["tf3_id"]): (e["text_tf3"] or "").replace("\n", " ")[:110] for e in mapa["cards"] if e["tf3_id"]}
    for k, v in efeitos.items():
        print(k, v["name"], "like", v["like"], stats[k], "|", nomes[k].encode("ascii", "replace").decode())
    print(len(efeitos), "equip cards")
    if "--list" not in sys.argv:
        auto = os.path.join(AQUI, "tf3_efeitos_auto.json")
        atual = json.load(open(auto, encoding="utf-8")) if os.path.exists(auto) else {}
        atual.update(efeitos)
        json.dump(atual, open(auto, "w", encoding="utf-8"), indent=1, ensure_ascii=False)
        json.dump(stats, open(os.path.join(AQUI, "tf3_stats_auto.json"), "w", encoding="utf-8"), indent=1, ensure_ascii=False)
