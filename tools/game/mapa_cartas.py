"""Card map of Tag Force 1 and Tag Force 3: one JSON file with every card of both games and how their effects line up.

For each card: ids, 8-digit passcode, name, kind, stats, texts and the effect record (5 handler pointers) of each game.
Cards are matched across games by passcode (name when the passcode is 0). The two duel modules sit at different addresses, so handler
pointers cannot be compared directly: a TF3 handler is mapped to a TF1 handler through the cards both games share. If every shared card
that uses the TF3 handler uses the same TF1 handler, the handler is "mapped" and a new TF3 card with that handler can borrow the TF1 one.

Usage:  python -I tools/game/mapa_cartas.py [saida.json]      (default gen/mapa_cartas.json; the file holds game text, keep it out of git)
Input:  the same files as tf3_efeitos.py and tf3_cartas.py.
Output sections:  cards (list), handlers_tf3 (per TF3 handler: cards, mapped TF1 handler or conflict), summary.
"""
import difflib
import json
import os
import re
import struct
import sys
from collections import Counter, defaultdict

RAIZ = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cartas as tf1
import tf3_cartas
import tf3_efeitos

CATEGORIA3 = {0: "Normal", 1: "Effect", 2: "Fusion", 3: "Fusion/Effect", 4: "Ritual", 5: "Ritual/Effect", 6: "Toon", 7: "Spirit",
              8: "Union", 9: "Effect (Tuner?)", 10: "Token", 11: "God", 13: "Spell", 14: "Trap", 15: "Synchro?"}


def tipo_carta(cat):
    return "spell" if cat == 13 else "trap" if cat == 14 else "monster"


def montar():
    r1, r3 = tf3_efeitos.registros(tf3_efeitos.TF1), tf3_efeitos.registros(tf3_efeitos.TF3)
    c3 = tf3_cartas.carregar()
    pass3 = tf3_cartas._ler("CARD_Pass.bin", "e")
    cards, by_code, by_name = [], {}, {}
    for c in tf1.CARTAS.values():
        e = dict(tf1_id=c.id, tf3_id=None, name=c.nome, passcode=c.senha or 0, category=c.f0, kind=tipo_carta(c.f0),
                 atk=c.atk, defe=c.defe, level=c.f8, text_tf1=c.desc, text_tf3=None, rec_tf1=r1.get(c.id), rec_tf3=r3.get(c.id) and None)
        cards.append(e)
        if e["passcode"]:
            by_code[e["passcode"]] = e
        by_name.setdefault(c.nome.lower(), e)
    for cid, c in c3.items():
        code = struct.unpack_from("<I", pass3, 4 * c["idx"])[0]
        w0, w1 = tf3_cartas.para_tf1(c["w0"], c["w1"])
        e = by_code.get(code) if code else None
        if e is None and code == 0:
            e = by_name.get(c["nome"].lower())
        if e is None:
            cat = w1 & 15
            a, d = (w0 >> 13) & 0x1FF, (w0 >> 22) & 0x1FF
            e = dict(tf1_id=None, name=c["nome"], passcode=code, category=cat, kind=tipo_carta(cat), atk=None if a == 0x1FF else a * 10,
                     defe=None if d == 0x1FF else d * 10, level=(w1 >> 8) & 15, text_tf1=None, rec_tf1=None)
            cards.append(e)
        e["tf3_id"], e["text_tf3"], e["rec_tf3"] = cid, c["desc"], r3.get(cid)
        e["shared"] = e["tf1_id"] is not None
    for e in cards:
        e.setdefault("shared", False)
    # TF3 handler position k -> TF1 handler, learnt from the shared cards
    mapa = [defaultdict(Counter) for _ in range(5)]
    for e in cards:
        if e["shared"] and e["rec_tf1"] and e["rec_tf3"]:
            for k in range(5):
                if e["rec_tf3"][k] and e["rec_tf1"][k]:
                    mapa[k][e["rec_tf3"][k]][e["rec_tf1"][k]] += 1
    handlers = {}
    for e in cards:
        r = e["rec_tf3"]
        if r:
            h = handlers.setdefault(hex(r[0]), dict(cards=[], tf1_handler=None, status="unmapped", tf1_cards=0))
            h["cards"].append(e["name"])
    for h3, h in handlers.items():
        cnt = mapa[0].get(int(h3, 16))
        if cnt:
            h["tf1_cards"] = sum(cnt.values())
            if len(cnt) == 1:
                h["tf1_handler"], h["status"] = hex(next(iter(cnt))), "mapped"
            else:
                h["tf1_handler"], h["status"] = [hex(a) for a in cnt], "conflict"
    for e in cards:
        e["effect_status"] = None
        if e["tf3_id"] and not e["shared"]:
            if e["category"] == 0:
                e["effect_status"] = "normal"
            else:
                r = e["rec_tf3"]
                e["effect_status"] = (handlers[hex(r[0])]["status"] if r else "no_record")
    # donor: a shared card with the identical TF3 record whose TF3 text reads the most like the new card (a record only names generic handlers,
    # the card-specific part is in the code, so the text similarity tells whether lending the TF1 record is plausible)
    doadores = defaultdict(list)
    for e in cards:
        if e["shared"] and e["rec_tf1"] and e["rec_tf3"]:
            doadores[tuple(e["rec_tf3"])].append(e)
    norm = lambda t: re.sub(r"\s+", " ", re.sub(r"[\"'`]", "", t or "")).lower()
    for e in cards:
        e["donor"] = None
        if e["effect_status"] and e["rec_tf3"]:
            melhor = max(((difflib.SequenceMatcher(None, norm(e["text_tf3"]), norm(d["text_tf3"])).ratio(), d) for d in doadores.get(tuple(e["rec_tf3"]), [])),
                         key=lambda x: x[0], default=None)
            if melhor:
                e["donor"] = dict(tf1_id=melhor[1]["tf1_id"], name=melhor[1]["name"], similarity=round(melhor[0], 2))
    resumo = Counter(e["effect_status"] for e in cards if e["effect_status"])
    resumo["donor_text_0.8"] = sum(1 for e in cards if e.get("donor") and e["donor"]["similarity"] >= 0.8)
    resumo["cards_total"] = len(cards)
    resumo["shared"] = sum(1 for e in cards if e["shared"])
    resumo["tf3_only"] = sum(1 for e in cards if e["tf3_id"] and not e["shared"])
    resumo["tf1_only"] = sum(1 for e in cards if e["tf1_id"] and not e["tf3_id"])
    return dict(summary=dict(resumo), cards=cards, handlers_tf3=handlers)


def ids_novos(mapa=None, tokens=False):
    """TF3 ids of the cards Tag Force 1 does not have (matched by passcode, name when the passcode is 0); tokens left out unless asked"""
    mapa = mapa or montar()
    return sorted(e["tf3_id"] for e in mapa["cards"]
                  if e["tf3_id"] and not e["shared"] and (tokens or e["category"] != 10))


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    saida = sys.argv[1] if len(sys.argv) > 1 else os.path.join(RAIZ, "gen", "mapa_cartas.json")
    m = montar()
    json.dump(m, open(saida, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    print(json.dumps(m["summary"], indent=1))
