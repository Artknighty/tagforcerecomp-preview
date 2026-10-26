#!/usr/bin/env python3
"""Desenha a folha `ui/menu_text_boxes.png` do EnhancedTextures (128x128 do PSP, 8x = 1024x1024): as celulas de botao do jogo, o botao
"estilo PC" do menu e os icones do menu principal/titulo.

A folha e COMPARTILHADA: as listas (drawList), a tela Options e outras telas cortam as mesmas celulas de 20x24 no padrao do jogo:
capa esquerda = colunas 0..5, coluna 6 esticada na largura, capa direita = colunas 17..19, e os paineis altos (Options) esticam a linha 10
na altura. Na textura 8x cada coluna/linha esticada tem 8 subcolunas/sublinhas: se nao forem identicas, o painel vira faixas. Por isso as
celulas do jogo aqui tem a forma original (chanfro no canto superior esquerdo em parte delas), com o chanfro reduzido para nao chegar perto da
coluna 6 nem da linha 10, preenchimento liso entre as linhas 8 e 18 e nada fora do corpo (sombra/brilho cortados no limite da celula viram
linhas retas).

O botao estilo PC (marca `menu_estilo_pc.txt` no mod; ver drawModernButton em decomp/libtitle/lists.cpp) fica numa area livre da folha,
em duas pecas neutras de 20x12 (o botao de 24 linhas comprimido 2x na altura; o jogo as estica de volta): preenchimento (cinza com degrade) e
borda+brilho (branco). O codigo tinge cada uma pela cor da celula da lista. Paralelogramo: capa esquerda = colunas 0..8, coluna 9 esticada,
capa direita = colunas 10..18, diagonais a >= 1,8 px da coluna 9 e as duas pontas iguais (a mesma forma girada 180 graus).
Icones (16x16 brancos, tingidos no jogo) tambem ficam em areas livres: posicoes em kModernMainIcons/kModernTitleIcons (swaps.cpp).

Uso: python tools/mods/menu_estilo_pc.py [--saida arquivo.png]   (padrao: grava no Enhanced e cria a marca)
"""
import argparse
import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

RAIZ = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
MOD = os.path.join(RAIZ, "mods", "EnhancedTextures")
ATLAS = os.path.join(MOD, "textures", "menus", "menu_text_boxes.png")
ORIGINAL = os.path.join(RAIZ, "gen", "backup_enhanced", "menu_text_boxes_esrgan.png")
ESC = 8          # pixels do atlas de saida por pixel do PSP
SS = 4           # supersampling
BRANCO = np.array([255, 255, 255], np.float32)

# celulas do jogo: (cx, cy), cor base (RGB) e corpo (x0, y0, x1, y1), medidos no pacote base; chanfro = canto superior esquerdo cortado
CELULAS = [
    ((0, 0), (251, 147, 13), (0.5, 0.0, 18.75, 21.75), True), ((1, 0), (183, 79, 128), (0.25, 0.0, 18.75, 21.75), True),
    ((2, 0), (105, 78, 180), (0.25, 0.0, 18.75, 21.75), True), ((3, 0), (183, 144, 83), (0.25, 0.0, 18.75, 21.75), True),
    ((4, 0), (84, 175, 183), (0.25, 0.0, 18.75, 21.75), True), ((5, 0), (170, 184, 82), (0.25, 0.0, 18.75, 21.75), True),
    ((0, 1), (86, 96, 134), (0.5, 0.25, 18.75, 21.75), True), ((1, 1), (163, 147, 104), (0.25, 0.25, 18.75, 21.75), True),
    ((2, 1), (83, 145, 192), (0.25, 0.25, 18.75, 21.75), False), ((3, 1), (129, 77, 193), (0.25, 0.25, 18.75, 21.75), False),
    ((4, 1), (174, 77, 192), (0.25, 0.25, 18.75, 21.75), False), ((5, 1), (193, 78, 133), (0.25, 0.25, 18.75, 21.75), False),
    ((0, 2), (205, 7, 11), (0.5, 0.25, 18.75, 18.75), True), ((1, 2), (14, 30, 201), (0.25, 0.25, 18.75, 18.75), True),
    ((2, 2), (205, 7, 11), (0.25, 0.25, 18.75, 18.75), False), ((3, 2), (14, 30, 201), (0.25, 0.25, 18.75, 18.75), False),
    ((0, 3), (110, 78, 108), (0.5, 0.25, 18.75, 19.75), False), ((1, 3), (81, 103, 108), (0.25, 0.25, 18.75, 19.75), False),
]
FILETE = 1.2   # largura do filete claro/escuro, em px do PSP
SOMBRA_TOPO, SOMBRA_DESLOC, SOMBRA_S_CIMA, SOMBRA_S_BAIXO, SOMBRA_FORCA = 0.3, 0.5, 0.3, 1.3, 0.45   # sombra: fina em cima, mais larga embaixo
CHANFRO_X, CHANFRO_Y = 4.0, 7.0   # o original ia ate x 5,5: a borda do chanfro encostava na coluna 6 (esticada)
ICONES = [(80, 48), (100, 48), (40, 72), (56, 72), (72, 72), (88, 72), (104, 72), (40, 96), (56, 96), (72, 96), (88, 96)]   # = kModernMainIcons + kModernTitleIcons
MODERNO_U, MODERNO_V_FILL, MODERNO_V_BORDA = 104, 96, 108   # = kModernSheetU, kModernFillV, kModernBorderV (swaps.cpp)

# painel PC (vidro): duas pecas neutras 12x12 em 9 fatias (cantos 5x5, miolo 2x2), area livre da folha em v 116..128
PAINEL_BORDA = 1.8   # espessura da borda, em px do PSP (era 1,1; o usuario pediu um pouco mais grossa e cantos retos, 2026-10-12)
PAINEL_U_FILL, PAINEL_U_BORDA, PAINEL_V, PAINEL_N, PAINEL_CANTO, PAINEL_RAIO = 0, 12, 116, 12, 5, 0.0   # = kPanel* (swaps.cpp)

# paralelogramo do botao PC (unidades do PSP, botao de 20x24)
MARGEM, INCL = 1.4, 5.6
BL, TL = (MARGEM, 22.0), (MARGEM + INCL, 2.0)
TR, BR = (19.0 - MARGEM, 2.0), (19.0 - MARGEM - INCL, 22.0)


def sdf_poligono(pts, px, py):
    """Distancia com sinal ao poligono convexo (pontos em sentido horario na tela): negativa dentro (a menor distancia as retas), positiva fora
    (distancia EUCLIDIANA ao contorno: o maximo das retas deixaria o brilho abrir um 'espinho' nos cantos agudos)."""
    dentro = np.full(px.shape, -1e9)
    fora = np.full(px.shape, 1e9)
    for i in range(len(pts)):
        ax, ay = pts[i]
        bx, by = pts[(i + 1) % len(pts)]
        ex, ey = bx - ax, by - ay
        n = math.hypot(ex, ey)
        nx, ny = ey / n, -ex / n
        dentro = np.maximum(dentro, (px - ax) * nx + (py - ay) * ny)
        t = np.clip(((px - ax) * ex + (py - ay) * ey) / (n * n), 0, 1)
        fora = np.minimum(fora, np.hypot(px - (ax + t * ex), py - (ay + t * ey)))
    return np.where(dentro > 0, fora, dentro)


def mix(a, b, t):
    return a + (b - a) * t


def grade(w, h):
    k = ESC * SS
    ys, xs = np.mgrid[0:h * k, 0:w * k].astype(np.float32)
    return (xs + 0.5) / k, (ys + 0.5) / k


def reduzir(rgb, alfa, w, h):
    out = np.dstack([np.clip(rgb, 0, 255), np.clip(alfa, 0, 1) * 255]).astype(np.uint8)
    return Image.fromarray(out, "RGBA").resize((w * ESC, h * ESC), Image.BOX)   # caixa: cada texel so ve o proprio bloco


def celula_jogo(base, corpo, chanfro, cursor):
    """Uma celula do jogo (20x24) no desenho do original: preenchimento CHAPADO (sem degrade) e um filete de ~1,2 px do PSP, claro nas bordas
    de cima/esquerda/chanfro e escuro nas de baixo/direita. Cantos suaves (supersampling), sem escada. A parte que o jogo estica
    (coluna 6, linhas 8..18) continua lisa: o filete e constante ao longo das bordas retas."""
    px, py = grade(20, 24)
    x0, y0, x1, y1 = corpo
    y0 = max(y0, SOMBRA_TOPO)   # abre espaco para a sombra fina de cima
    if chanfro:   # canto superior esquerdo cortado, com a curva suave do original
        pts = [(x0, y0 + CHANFRO_Y), (x0 + CHANFRO_X, y0), (x1, y0), (x1, y1), (x0, y1)]   # diagonal reta, como no original
    else:
        pts = [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
    dentro = -sdf_poligono(pts, px, py)
    mais_perto = np.full(px.shape, 1e9, np.float32)
    tom = np.zeros(px.shape, np.float32)   # +1 = aresta virada para cima/esquerda (clara), -1 = para baixo/direita (escura)
    for k in range(len(pts)):
        ax, ay = pts[k]
        bx, by = pts[(k + 1) % len(pts)]
        ex, ey = bx - ax, by - ay
        n = math.hypot(ex, ey)
        nx, ny = ey / n, -ex / n   # normal para fora (horario na tela)
        d = -((px - ax) * nx + (py - ay) * ny)
        melhor = d < mais_perto
        mais_perto = np.where(melhor, d, mais_perto)
        tom = np.where(melhor, max(-1.0, min(1.0, -(nx + ny))), tom)
    base = np.array(base, np.float32)
    claro, escuro = mix(base, BRANCO, 0.5), base * 0.5
    filete = np.where(tom[..., None] >= 0, mix(base, claro, tom[..., None]), mix(base, escuro, -tom[..., None]))
    faixa = np.clip((FILETE - dentro) / 0.3, 0, 1)
    cor = mix(np.broadcast_to(base, px.shape + (3,)), filete, faixa[..., None])
    alfa = np.clip(dentro / 0.06 + 0.5, 0, 1)
    corpo_img = reduzir(cor, alfa, 20, 24)
    # sombra suave: fina em cima, mais larga embaixo (poligono deslocado para baixo, desfoque assimetrico)
    sd = sdf_poligono([(x, y + SOMBRA_DESLOC) for x, y in pts], px, py)
    s = np.where(py < (y0 + y1) / 2, SOMBRA_S_CIMA, SOMBRA_S_BAIXO)
    sombra = SOMBRA_FORCA * np.exp(-(np.clip(sd, 0, None) / s) ** 2) * (sd > -0.5)
    img = reduzir(np.zeros(px.shape + (3,), np.float32), sombra, 20, 24)
    img.alpha_composite(corpo_img)
    return img


def pecas_modernas():
    """O botao PC em duas camadas neutras de 20x24, comprimidas para 20x12: preenchimento (cinza, degrade) e borda + brilho (branco)."""
    px, py = grade(20, 24)
    sd = sdf_poligono([BL, TL, TR, BR], px, py)
    dentro = -sd
    cobertura = np.clip(dentro / 0.06 + 0.5, 0, 1)
    t = np.clip((py - 2.0) / 20.0, 0, 1)
    cinza = (mix(0.88, 0.62, t) + 0.10 * np.exp(-((py - 4.0) / 2.2) ** 2)) * 255
    fill = reduzir(np.dstack([cinza] * 3), cobertura * 0.95, 20, 24)
    faixa = np.clip((1.55 - dentro) / 0.75, 0, 1) * (dentro > -0.03) * cobertura
    janela = np.clip(np.minimum.reduce([px, 19.0 - px, py, 24.0 - py]) / 0.9, 0, 1)
    janela = janela * janela * (3 - 2 * janela)
    glow = np.where(sd > 0, 0.62 * np.exp(-np.clip(sd, 0, None) / 0.6), 0.0) * (1 - cobertura) * janela
    borda = reduzir(np.full(px.shape + (3,), 255.0), faixa + glow * (1 - faixa), 20, 24)
    return fill.resize((20 * ESC, 12 * ESC), Image.BOX), borda.resize((20 * ESC, 12 * ESC), Image.BOX)


def pecas_painel():
    """O painel de vidro, em duas camadas de 12x12 (o jogo as estica em 9 fatias): preenchimento (cinza claro, brilho no topo e sombra embaixo
    so nas fatias de cima/baixo: o miolo e as laterais ficam lisos, senao o esticado vira faixas) e borda (branca, PAINEL_BORDA px; cantos retos)."""
    n = PAINEL_N
    px, py = grade(n, n)
    # retangulo arredondado de 0,3 a n-0,3; dentro > 0 no interior (distancia ao contorno)
    cx, cy = n / 2.0, n / 2.0
    hx, hy = n / 2.0 - 0.3, n / 2.0 - 0.3
    qx, qy = np.abs(px - cx) - (hx - PAINEL_RAIO), np.abs(py - cy) - (hy - PAINEL_RAIO)
    sd = np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - PAINEL_RAIO
    dentro = -sd
    cobertura = np.clip(dentro / 0.06 + 0.5, 0, 1)
    topo = np.clip((PAINEL_CANTO - py) / PAINEL_CANTO, 0, 1)
    base = np.clip((py - (n - PAINEL_CANTO)) / PAINEL_CANTO, 0, 1)
    cinza = (0.96 + 0.04 * topo - 0.08 * base) * 255
    fill = reduzir(np.dstack([cinza] * 3), cobertura * 0.98, n, n)
    faixa = np.clip((PAINEL_BORDA - dentro) / 0.6, 0, 1) * cobertura
    realce = np.clip((2.4 - dentro) / 1.2, 0, 1) * 0.0
    borda = reduzir(np.full(px.shape + (3,), 255.0), np.clip(faixa + realce, 0, 1), n, n)
    return fill, borda


# ---------------------------------------------------------------- icones (16x16 PSP, brancos)
def icone(nome):
    n = 16 * ESC * SS
    u = n / 16.0
    im = Image.new("RGBA", (n, n), (255, 255, 255, 0))
    d = ImageDraw.Draw(im)
    W = (255, 255, 255, 255)
    lw = int(1.25 * u)
    if nome == "espada":   # lamina diagonal grossa, guarda e cabo
        d.polygon([(3.6 * u, 11.6 * u), (12.0 * u, 3.2 * u), (14.2 * u, 1.8 * u), (14.2 * u, 1.8 * u), (12.8 * u, 4.0 * u), (4.8 * u, 12.8 * u)], fill=W)
        d.polygon([(11.0 * u, 2.4 * u), (14.4 * u, 1.6 * u), (13.6 * u, 5.0 * u)], fill=W)
        d.line([(4.4 * u, 8.6 * u), (8.0 * u, 12.2 * u)], fill=W, width=int(1.9 * u))
        d.line([(3.8 * u, 12.4 * u), (2.0 * u, 14.2 * u)], fill=W, width=int(2.1 * u))
        d.ellipse([0.8 * u, 13.0 * u, 2.8 * u, 15.0 * u], fill=W)
    elif nome == "disco":   # duel disk visto de cima
        d.rounded_rectangle([1.4 * u, 4.2 * u, 14.6 * u, 12.6 * u], radius=2.2 * u, outline=W, width=lw)
        d.rectangle([6.0 * u, 6.0 * u, 12.0 * u, 11.0 * u], outline=W, width=int(1.0 * u))
        d.line([(3.4 * u, 6.4 * u), (3.4 * u, 10.6 * u)], fill=W, width=int(1.0 * u))
    elif nome == "cubo":
        c = (8 * u, 8 * u)
        pts = [(8 * u, 1.4 * u), (14 * u, 4.7 * u), (14 * u, 11.3 * u), (8 * u, 14.6 * u), (2 * u, 11.3 * u), (2 * u, 4.7 * u)]
        d.polygon(pts, outline=W, width=lw)
        d.line([(2 * u, 4.7 * u), (8 * u, 8 * u), (14 * u, 4.7 * u)], fill=W, width=int(1.0 * u))
        d.line([(8 * u, 8 * u), (8 * u, 14.6 * u)], fill=W, width=int(1.0 * u))
    elif nome == "banco":   # database: tres barras
        for i in range(3):
            y0 = (2.0 + i * 4.6) * u
            d.rounded_rectangle([1.8 * u, y0, 14.2 * u, y0 + 3.2 * u], radius=0.8 * u, outline=W, width=int(1.0 * u))
            d.ellipse([3.0 * u, y0 + 0.9 * u, 4.4 * u, y0 + 2.3 * u], fill=W)
            d.line([(6.0 * u, y0 + 1.6 * u), (11.0 * u, y0 + 1.6 * u)], fill=W, width=int(0.9 * u))
    elif nome == "rede":
        d.rectangle([5.6 * u, 1.4 * u, 10.4 * u, 5.0 * u], outline=W, width=int(1.1 * u))
        for x0 in (0.8, 6.0, 11.2):
            d.rectangle([x0 * u, 11.0 * u, (x0 + 4.0) * u, 14.6 * u], outline=W, width=int(1.1 * u))
        d.line([(8 * u, 5.0 * u), (8 * u, 8.2 * u)], fill=W, width=int(1.0 * u))
        d.line([(2.8 * u, 11.0 * u), (2.8 * u, 8.2 * u), (13.2 * u, 8.2 * u), (13.2 * u, 11.0 * u)], fill=W, width=int(1.0 * u))
        d.line([(8 * u, 8.2 * u), (8 * u, 11.0 * u)], fill=W, width=int(1.0 * u))
    elif nome == "engrenagem":
        cx = cy = 8 * u
        pts = []
        dentes = 8
        for i in range(dentes * 2):
            ang = math.pi * i / dentes
            r = 7.0 * u if i % 2 == 0 else 5.0 * u
            for da in (-0.13, 0.13):
                pts.append((cx + r * math.cos(ang + da), cy + r * math.sin(ang + da)))
        d.polygon(pts, fill=W)
        d.ellipse([cx - 2.2 * u, cy - 2.2 * u, cx + 2.2 * u, cy + 2.2 * u], fill=(255, 255, 255, 0))
    elif nome == "monitor":
        d.rounded_rectangle([1.2 * u, 2.0 * u, 14.8 * u, 11.6 * u], radius=1.0 * u, outline=W, width=lw)
        d.line([(8 * u, 11.8 * u), (8 * u, 14.0 * u)], fill=W, width=int(1.3 * u))
        d.line([(4.6 * u, 14.2 * u), (11.4 * u, 14.2 * u)], fill=W, width=int(1.3 * u))
    elif nome == "continuar":   # triangulo de "play" com aro
        d.ellipse([1.2 * u, 1.2 * u, 14.8 * u, 14.8 * u], outline=W, width=lw)
        d.polygon([(6.0 * u, 4.6 * u), (11.8 * u, 8 * u), (6.0 * u, 11.4 * u)], fill=W)
    elif nome == "novo":   # estrela de 4 pontas (faisca)
        cx = cy = 8 * u
        pts = []
        for i in range(8):
            ang = math.pi / 4 * i - math.pi / 2
            r = 7.2 * u if i % 2 == 0 else 2.3 * u
            pts.append((cx + r * math.cos(ang), cy + r * math.sin(ang)))
        d.polygon(pts, fill=W)
    elif nome == "sair":   # porta com seta
        d.line([(9.0 * u, 2.0 * u), (2.4 * u, 2.0 * u), (2.4 * u, 14.0 * u), (9.0 * u, 14.0 * u)], fill=W, width=lw, joint="curve")
        d.line([(6.0 * u, 8 * u), (14.0 * u, 8 * u)], fill=W, width=int(1.5 * u))
        d.polygon([(14.8 * u, 8 * u), (11.2 * u, 4.8 * u), (11.2 * u, 11.2 * u)], fill=W)
    elif nome == "globo":
        d.ellipse([1.4 * u, 1.4 * u, 14.6 * u, 14.6 * u], outline=W, width=lw)
        d.ellipse([5.0 * u, 1.4 * u, 11.0 * u, 14.6 * u], outline=W, width=int(1.0 * u))
        d.line([(1.6 * u, 8 * u), (14.4 * u, 8 * u)], fill=W, width=int(1.0 * u))
        d.line([(2.6 * u, 4.8 * u), (13.4 * u, 4.8 * u)], fill=W, width=int(0.9 * u))
        d.line([(2.6 * u, 11.2 * u), (13.4 * u, 11.2 * u)], fill=W, width=int(0.9 * u))
    return im.resize((16 * ESC, 16 * ESC), Image.LANCZOS)


def montar(saida):
    atlas = Image.open(ORIGINAL).convert("RGBA")
    assert atlas.size == (1024, 1024), atlas.size
    vazio = Image.new("RGBA", (20 * ESC, 24 * ESC), (0, 0, 0, 0))
    for i, ((cx, cy), cor, corpo, chanfro) in enumerate(CELULAS):
        atlas.paste(vazio, (cx * 20 * ESC, cy * 24 * ESC))
        atlas.alpha_composite(celula_jogo(cor, corpo, chanfro, cursor=(i == 0)), (cx * 20 * ESC, cy * 24 * ESC))
    nomes = ["espada", "disco", "cubo", "banco", "rede", "engrenagem", "monitor", "globo", "continuar", "novo", "sair"]
    for (u, v), nome in zip(ICONES, nomes):
        atlas.paste(Image.new("RGBA", (16 * ESC, 16 * ESC), (0, 0, 0, 0)), (u * ESC, v * ESC))
        atlas.alpha_composite(icone(nome), (u * ESC, v * ESC))
    fill, borda = pecas_modernas()
    for v, peca in ((MODERNO_V_FILL, fill), (MODERNO_V_BORDA, borda)):
        atlas.paste(Image.new("RGBA", peca.size, (0, 0, 0, 0)), (MODERNO_U * ESC, v * ESC))
        atlas.alpha_composite(peca, (MODERNO_U * ESC, v * ESC))
    fill_p, borda_p = pecas_painel()
    for u, peca in ((PAINEL_U_FILL, fill_p), (PAINEL_U_BORDA, borda_p)):
        atlas.paste(Image.new("RGBA", peca.size, (0, 0, 0, 0)), (u * ESC, PAINEL_V * ESC))
        atlas.alpha_composite(peca, (u * ESC, PAINEL_V * ESC))
    atlas.save(saida)
    print("gravado:", saida)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--saida", default=ATLAS)
    args = ap.parse_args()
    montar(args.saida)
    if args.saida == ATLAS:
        print("folha gravada (sem criar a marca menu_estilo_pc.txt: o usuario voltou ao menu no formato original, 2026-10-16)")


if __name__ == "__main__":
    main()
