"""Card id map Tag Force 1 -> Tag Force 3 (writes ref/tf3/mapa_ids.tsv).

Main key: the passcode (CARD_Pass), the same in both games; tokens (no passcode) match by name, and repeated names (Sheep, Ojama, Lamb)
match in id order. Result (2026-10-26): every TF1 card exists in TF3 with the SAME id, except tokens (27 change id, 1 has no match);
42 cards were renamed in TF3.
Columns: id_tf1, nome_tf1, id_tf3, nome_tf3, como (senha=passcode|nome=name|ordem=order|nenhum=none), mesmo_id (same id)
Usage: python -I tools/game/tf3_mapa_ids.py     (TF_CARDINFO = TF1 cardinfo, TF3_CARDINFO = folder extracted from TF3)
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cartas  # noqa: E402
import tf3_cartas  # noqa: E402


def mapa():
    c3 = tf3_cartas.carregar()
    d3 = tf3_cartas._ler("CARD_Pass.bin")
    por_senha = {}
    for c in c3.values():
        por_senha.setdefault(struct.unpack_from("<I", d3, c["idx"] * 4)[0], []).append(c["id"])
    por_nome = {}
    for c in sorted(c3.values(), key=lambda x: x["id"]):
        por_nome.setdefault(c["nome"], []).append(c["id"])
    usados, out = set(), {}
    for c in sorted(cartas.CARTAS.values(), key=lambda x: x.id):
        cand = por_senha.get(c.senha, []) if c.senha else []
        if len(cand) == 1:
            out[c.id] = (cand[0], "senha")
            usados.add(cand[0])
    for c in sorted(cartas.CARTAS.values(), key=lambda x: x.id):
        if c.id in out:
            continue
        livres = [i for i in por_nome.get(c.nome, []) if i not in usados]
        if livres:
            out[c.id] = (livres[0], "nome" if len(por_nome[c.nome]) == 1 else "ordem")
            usados.add(livres[0])
        else:
            out[c.id] = (None, "nenhum")
    return c3, out


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    c3, m = mapa()
    pasta = os.path.join(tf3_cartas.RAIZ, "ref", "tf3")
    with open(os.path.join(pasta, "mapa_ids.tsv"), "w", encoding="utf-8", newline="\n") as f:
        f.write("id_tf1\tnome_tf1\tid_tf3\tnome_tf3\tcomo\tmesmo_id\n")
        for i1, (i3, como) in m.items():
            f.write("%d\t%s\t%s\t%s\t%s\t%d\n" % (i1, cartas.CARTAS[i1].nome, i3 or "", c3[i3]["nome"] if i3 else "", como, int(i1 == i3)))
    n_igual = sum(1 for i1, (i3, _) in m.items() if i1 == i3)
    print("%d TF1 cards: %d with the same id, %d with a different id, %d without a match" % (
        len(m), n_igual, sum(1 for i1, (i3, _) in m.items() if i3 and i1 != i3), sum(1 for _, (i3, _) in m.items() if not i3)))
    todos = {i for i, _ in m.values() if i}
    print("TF3-only cards:", len(c3) - len(todos))
