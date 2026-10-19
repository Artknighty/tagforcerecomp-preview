"""Texto da decisao: IDENTICO ao montar_prompt() de plugins/src/IAAprimorada/decisor.cpp (o modelo e treinado e usado com ele)."""
def montar_prompt(tipo, situacao, opcoes):
    p = "Yu-Gi-Oh! duel. Choose the best option.\nDECISION: " + tipo + "\n" + situacao + "OPTIONS:\n"
    for i, o in enumerate(opcoes):
        p += "%d) %s\n" % (i, o)
    return p + "ANSWER:\n"
