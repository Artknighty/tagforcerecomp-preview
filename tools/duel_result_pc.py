#!/usr/bin/env python3
"""Reveste a tela Duel Result no EnhancedTextures com a paleta marinho/dourado da interface escura (dark_ui.h).

Cobre o que a tela tem de textura: o fundo xadrez (results_background), as capsulas do titulo/rodape (oval_*), e o texto cinza do titulo
(sheet `duel_result`, so os pixels sem cor: o resto da folha - WIN/DRAW/LOSE, icones - fica como esta) e a tabela inteira (folha b602d300,
regiao u0..35 x v0..47 em texels do PSP; medida com o comando texdump do tf.py).

Saida: textures/duelresult_pc/*.png + chaves do textures.ini apontando para eles (idempotente; a 1a execucao guarda o ini em
textures.ini.bak_resultado; se o pacote for regerado, apague o .bak antes de rodar de novo). Testes: python tools/test_duel_result_pc.py
Uso: python tools/duel_result_pc.py [--desfazer]
"""
import os
import re
import shutil
import sys

import numpy as np
from PIL import Image

RAIZ = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
TEX = os.path.join(RAIZ, "gen", "mods", "EnhancedTextures", "textures")
SAIDA = "duelresult_pc"
INI = os.path.join(TEX, "textures.ini")
BAK = INI + ".bak_resultado"

NAVY_ESCURO = np.array([15, 19, 33], np.float32)    # fundo (dark::BAR/BG)
NAVY_CLARO = np.array([27, 35, 62], np.float32)     # casas claras do xadrez
OURO = np.array([222, 182, 92], np.float32)         # dark::ACC
CELULA_ESCURA = np.array([34, 42, 72], np.float32)  # celulas de DP e cabecalho da tabela (claras do degrade)
MOLDURA_ESCURA = np.array([46, 56, 92], np.float32)   # moldura e separadores (parte escura)
MOLDURA_CLARA = np.array([120, 134, 176], np.float32)  # moldura e separadores (parte clara)
LINHA_CLARA = np.array([226, 231, 246], np.float32)   # linhas da tabela (o texto do jogo e preto)
CREME_ORIGINAL = np.array([255, 247, 223], np.float32)
PAINEL_U, PAINEL_V, ESCALA = 35, 47, 8                 # regiao da folha b602d300 usada pela tabela (texels do PSP) e fator do pacote

# nome do PNG de origem (relativo a textures/) -> chaves de dados (hash) do textures.ini
ALVOS = {
    "mikeymk/ui/duels/results_background.png": ("fundo", ["20b3dd72"]),
    "mikeymk/ui/oval/oval_tall_left.png": ("oval", ["85169591"]),
    "mikeymk/ui/oval/oval_tall_center.png": ("oval", ["1320facf"]),
    "mikeymk/ui/oval/oval_tall_right.png": ("oval", ["87f4af60"]),
    "mikeymk/ui/oval/oval_transparent_tall_left.png": ("oval_t", ["59cfec77"]),
    "mikeymk/ui/oval/oval_transparent_tall_center.png": ("oval_t", ["b441d53d"]),
    "mikeymk/ui/oval/oval_transparent_tall_right.png": ("oval_t", ["9d15ec20"]),
    "mikeymk/ui/text/duel_result.png": ("titulo", ["f6f6c44f"]),
    "mikeymk/extras/00000000658d9d5fb602d300.png": ("painel", ["b602d300"]),   # folha da tabela (painel, cabecalho, linhas)
}


def fundo(a):
    g = a[..., 1].astype(np.float32)                      # 206..250: escuro..claro do xadrez (4 tons)
    t = np.clip((g - 206.0) / 44.0, 0, 1)[..., None]
    rgb = NAVY_ESCURO * (1 - t) + NAVY_CLARO * t
    return np.dstack([rgb, a[..., 3:4]])


def endireitar_xadrez(a, casas=8):
    """O xadrez do pacote (2048x2048 = 8x8 casas lisas de 256 px) saiu da ampliacao com bordas onduladas (+-5 px, deslocadas de uma fileira para
    outra) e cantos arredondados, que no jogo aparecem como emendas tortas. As casas sao de cor lisa: cada uma vira um quadrado exato da cor do seu centro."""
    h, w = a.shape[:2]
    ch, cw = h // casas, w // casas
    out = a.copy()
    for ty in range(casas):
        for tx in range(casas):
            out[ty * ch:(ty + 1) * ch, tx * cw:(tx + 1) * cw] = a[ty * ch + ch // 2, tx * cw + cw // 2]
    return out


def oval(a, transparente):
    rgb = a[..., :3].astype(np.float32)
    if transparente:   # so a sombra translucida: o alfa da textura continua mandando
        out = np.broadcast_to(NAVY_ESCURO, rgb.shape)
    else:              # aro brilhante (cinza) -> dourado; miolo preto -> marinho
        lum = rgb.mean(axis=2, keepdims=True) / 255.0
        out = NAVY_ESCURO * (1 - np.clip(lum * 2, 0, 1)) + OURO * np.clip(lum * 1.15, 0, 1)
    return np.dstack([np.clip(out, 0, 255), a[..., 3:4]])


def titulo(a):
    rgb = a[..., :3].astype(np.float32)
    mx, mn = rgb.max(axis=2), rgb.min(axis=2)
    sem_cor = ((mx - mn) < 40)[..., None] & (a[..., 3:4] > 0)
    lum = (rgb.mean(axis=2, keepdims=True) / 255.0)
    dourado = np.clip(OURO * (0.45 + 0.75 * lum), 0, 255)
    out = np.where(sem_cor, dourado, rgb)
    return np.dstack([out, a[..., 3:4]])


# Trechos da folha b602d300 que o jogo ESTICA na tela (medido com texdump no Duel Result): uma coluna de 1 texel vira 260+ pixels de largura, ou
# uma linha de 1 texel vira 75 de altura. No pacote HD essa coluna/linha tem 8 pixels e o ruido da ampliacao entre eles (ex.: pixels quase
# brancos na linha de separacao acima do Total) aparecia como faixas. Cada coluna/linha vira uniforme (media dos 8 pixels).
COLUNAS_ESTICADAS = [(10, 0, 47), (24, 0, 16), (25, 16, 19), (27, 0, 16)]   # (u, v inicial, v final) em texels do PSP
LINHAS_ESTICADAS = [(20, 0, 6), (20, 17, 23), (18, 23, 25)]                  # (v, u inicial, u final)


def uniformizar_esticados(a):
    out = a.astype(np.float32)
    for u, v0, v1 in COLUNAS_ESTICADAS:
        bloco = out[v0 * ESCALA:v1 * ESCALA, u * ESCALA:(u + 1) * ESCALA]
        out[v0 * ESCALA:v1 * ESCALA, u * ESCALA:(u + 1) * ESCALA] = bloco.mean(axis=1, keepdims=True)
    for v, u0, u1 in LINHAS_ESTICADAS:
        bloco = out[v * ESCALA:(v + 1) * ESCALA, u0 * ESCALA:u1 * ESCALA]
        out[v * ESCALA:(v + 1) * ESCALA, u0 * ESCALA:u1 * ESCALA] = bloco.mean(axis=0, keepdims=True)
    return np.round(out).astype(np.uint8)


def painel(a):
    """Folha b602d300 (256x64 do PSP; no pacote, x8): a tabela inteira do Duel Result e montada da regiao u0..35 x v0..47 (cantos, bordas, cabecalho,
    linhas, celulas de DP; medido com o comando texdump: UVs em texels). Cinzas neutros viram marinho (escuro) e azul-acinzentado (moldura);
    o creme das linhas vira branco-azulado (o texto do jogo sobre ele e PRETO). O resto da folha (portas WIN/DRAW/LOSE, setas) nao muda."""
    a = uniformizar_esticados(a)
    rgb = a[..., :3].astype(np.float32)
    reg = np.zeros(a.shape[:2], bool); reg[0:PAINEL_V * ESCALA, 0:PAINEL_U * ESCALA] = True
    mx, mn = rgb.max(axis=2), rgb.min(axis=2)
    lum = rgb.mean(axis=2) / 255.0
    visivel = a[..., 3] > 0
    neutro = ((mx - mn) < 14) & visivel & reg
    creme_m = reg & visivel & (rgb[..., 0] > 235) & (rgb[..., 1] > 225) & (rgb[..., 2] > 190) & (rgb[..., 2] < rgb[..., 0] - 12)
    out = rgb.copy()
    escuro = neutro & (lum < 0.36)                                   # cabecalho, celulas de DP, rodape
    t = np.clip(lum / 0.36, 0, 1)[..., None]
    out = np.where(escuro[..., None], NAVY_ESCURO * (1 - t) + CELULA_ESCURA * t, out)
    moldura = neutro & ~escuro & (lum < 0.93)                        # moldura e separadores
    t2 = np.clip((lum - 0.36) / 0.57, 0, 1)[..., None]
    out = np.where(moldura[..., None], MOLDURA_ESCURA * (1 - t2) + MOLDURA_CLARA * t2, out)
    novo = LINHA_CLARA * (rgb / CREME_ORIGINAL)
    out = np.where(creme_m[..., None], np.clip(novo, 0, 255), out)
    return np.dstack([out, a[..., 3:4]])


def gerar():
    os.makedirs(os.path.join(TEX, SAIDA), exist_ok=True)
    if not os.path.exists(BAK):
        shutil.copyfile(INI, BAK)
    with open(INI, encoding="utf-8") as f:
        ini = f.read()
    for src, (modo, hashes) in ALVOS.items():
        a = np.array(Image.open(os.path.join(TEX, src)).convert("RGBA"))
        if modo == "fundo": o = fundo(endireitar_xadrez(a))
        elif modo == "oval": o = oval(a, False)
        elif modo == "oval_t": o = oval(a, True)
        elif modo == "painel": o = painel(a)
        else: o = titulo(a)
        nome = SAIDA + "/" + os.path.basename(src)
        Image.fromarray(np.clip(o, 0, 255).astype(np.uint8), "RGBA").save(os.path.join(TEX, nome), optimize=True)
        for h in hashes:   # todas as chaves (com e sem paleta) desse hash de dados
            ini, n = re.subn(r"(?m)^([0-9a-f]{16}" + h + r") = (?:" + re.escape(src) + "|" + re.escape(nome) + r")$", lambda m: m.group(1) + " = " + nome, ini)
            print(src, h, n, "chave(s)")
    open(INI, "w", encoding="utf-8", newline="\n").write(ini)


if __name__ == "__main__":
    if "--desfazer" in sys.argv:
        shutil.copyfile(BAK, INI); print("textures.ini restaurado")
    else:
        gerar()
