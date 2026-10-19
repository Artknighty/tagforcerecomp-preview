#!/usr/bin/env python3
"""Redesenha a folha `ui/text_boxes.png` do EnhancedTextures (64x128 do PSP, 8x = 512x1024): as caixas de dialogo do jogo (a caixa escura
"Checking save data...", os baloes de fala, os paineis laterais e os quadradinhos), FIEIS ao original: mesmas formas, camadas e cores,
mas DESENHADAS em vetor (distancia com sinal + antialias analitico), nunca ampliadas do original.

O despejo do original (`tf.py run "w28 texdump w4"` -> gen/tex_088951f0_5_64x128.bmp) so serve de referencia: as cores de cada camada
sao amostradas dele; a geometria (medidas dos filetes, raios, diagonal do painel, rabinho) foi medida a mao e esta nas constantes.

Layout da folha (px do PSP): baloes 32x24 em (0, 24k) e paineis 32x24 em (32, 24k), k=0..3 (creme, bege, escuro, branco); quadradinhos
16x16 em y=96 (x=0,16,32,48) e um bloco branco 12x12 em (0,112). O jogo corta tudo em 9 fatias e ESTICA colunas/linhas do miolo (colunas 3..4
e linhas 3..11 dos baloes; o painel nao tem borda de baixo): por isso os cantos ficam dentro de 3 px e o miolo e liso (formas por
construcao uniformes ali).

Uso: python tools/mods/caixas_hd.py [--origem gen/tex_088951f0_5_64x128.bmp] [--saida arquivo.png]   (padrao: grava no Enhanced e guarda o
antigo em gen/backup_enhanced/text_boxes_esrgan.png)
"""
import argparse
import os
import shutil

import numpy as np
from PIL import Image

RAIZ = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
SAIDA = os.path.join(RAIZ, "gen", "mods", "EnhancedTextures", "textures", "mikeymk", "ui", "text_boxes.png")
BACKUP = os.path.join(RAIZ, "gen", "backup_enhanced", "text_boxes_esrgan.png")
ORIGEM = os.path.join(RAIZ, "gen", "tex_088951f0_5_64x128.bmp")
ESC = 8
W, H = 64, 128

# balao: corpo (x0, y0, x1, y1) em px do PSP dentro da celula 32x24, raio do canto externo; rabinho (poligono externo, convexo)
BALAO = (0.0, 0.0, 31.0, 16.0)
BALAO_R = 3.0
RABINHO = [(7.9, 13.0), (15.1, 13.0), (14.3, 16.2), (13.1, 18.8), (12.5, 21.3), (11.7, 22.4), (11.0, 21.2), (9.6, 18.6), (8.6, 16.0)]
FILETE = 1.0      # largura de cada camada (A escuro, claro, dourado), em px do PSP
# painel: retangulo (0..31) aberto embaixo, diagonal do canto superior direito de (DIAG_X, 0) a (31, 16)
PAINEL = (0.0, 0.0, 31.0)
PAINEL_R = 2.2
DIAG_X = 23.0


def malha(x0, y0, x1, y1):
    """Coordenadas (px do PSP) dos centros dos pixels de saida da regiao [x0,x1)x[y0,y1)."""
    xs = (np.arange(x0 * ESC, x1 * ESC) + 0.5) / ESC
    ys = (np.arange(y0 * ESC, y1 * ESC) + 0.5) / ESC
    return np.meshgrid(xs, ys)


def sdf_retangulo(px, py, x0, y0, x1, y1, r):
    """Distancia com sinal (negativa dentro) a um retangulo de cantos arredondados."""
    cx, cy, hx, hy = (x0 + x1) / 2, (y0 + y1) / 2, (x1 - x0) / 2, (y1 - y0) / 2
    r = min(r, hx, hy)
    qx, qy = np.abs(px - cx) - (hx - r), np.abs(py - cy) - (hy - r)
    return np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - r


def sdf_convexo(px, py, pts, r=0.0):
    """Distancia com sinal a um poligono convexo (qualquer sentido), com cantos arredondados de raio r (poligono recuado r e engordado r)."""
    n = len(pts)
    area = sum(pts[i][0] * pts[(i + 1) % n][1] - pts[(i + 1) % n][0] * pts[i][1] for i in range(n))
    s = 1.0 if area > 0 else -1.0
    h = []
    for i in range(n):
        ax, ay = pts[i]
        bx, by = pts[(i + 1) % n]
        ex, ey = bx - ax, by - ay
        l = np.hypot(ex, ey)
        nx, ny = s * ey / l, -s * ex / l                      # normal para fora
        h.append((px - ax) * nx + (py - ay) * ny + r)         # recuado r
    h = np.stack(h)
    dentro = h.max(axis=0)
    fora = np.sqrt((np.maximum(h, 0) ** 2).sum(axis=0))
    return np.where(dentro > 0, fora, dentro) - r


def poligono_recuado(pts, d):
    """Recua um poligono convexo d px (desloca cada aresta para dentro e reintersecta)."""
    n = len(pts)
    area = sum(pts[i][0] * pts[(i + 1) % n][1] - pts[(i + 1) % n][0] * pts[i][1] for i in range(n))
    s = 1.0 if area > 0 else -1.0
    ret = []
    for i in range(n):
        ax, ay = pts[i]
        bx, by = pts[(i + 1) % n]
        ex, ey = bx - ax, by - ay
        l = np.hypot(ex, ey)
        nx, ny = s * ey / l, -s * ex / l
        ret.append((ax - nx * d, ay - ny * d, ex, ey))
    out = []
    for i in range(n):
        x1, y1, dx1, dy1 = ret[i - 1]
        x2, y2, dx2, dy2 = ret[i]
        den = dx1 * dy2 - dy1 * dx2
        t = ((x2 - x1) * dy2 - (y2 - y1) * dx2) / den
        out.append((x1 + dx1 * t, y1 + dy1 * t))
    return out


def sobre(img, cob, cor):
    """Pinta `cor` (RGB) com cobertura `cob` (0..1) por cima de img (RGBA float, alfa reto)."""
    a0 = img[..., 3:4] / 255.0
    c = cob[..., None]
    a = c + a0 * (1 - c)
    rgb = (np.array(cor, np.float32) * c + img[..., :3] * a0 * (1 - c)) / np.maximum(a, 1e-6)
    img[..., :3] = rgb
    img[..., 3:4] = a * 255.0


def cobertura(sdf):
    return np.clip(0.5 - sdf * ESC, 0.0, 1.0)


def amostra(ref, x, y):
    return tuple(int(v) for v in ref.getpixel((x, y)))


def balao(ref, k):
    oy = 24 * k
    cores = [amostra(ref, 5, oy + i) for i in (0, 1, 2, 5)]   # escuro, claro, dourado, miolo
    px, py = malha(0, oy, 32, oy + 24)
    px, py = px, py - oy
    img = np.zeros(px.shape + (4,), np.float32)
    x0, y0, x1, y1 = BALAO
    for i, cor in enumerate(cores):
        d = i * FILETE
        corpo = sdf_retangulo(px, py, x0 + d, y0 + d, x1 - d, y1 - d, max(BALAO_R - d, 0.0))   # miolo com canto vivo: um canto arredondado entraria na faixa que o jogo estica
        d_tail = (0.0, 0.5, None, 1.15)[i]                    # o rabinho e estreito: filetes mais finos e sem a faixa dourada
        sdf = corpo
        if d_tail is not None:
            ponta = RABINHO if i == 0 else [(RABINHO[0][0], 9.0), (RABINHO[1][0], 9.0)] + RABINHO[1:]   # abre a borda na boca do rabinho
            sdf = np.minimum(corpo, sdf_convexo(px, py, ponta, 0.15) + d_tail)
        sobre(img, cobertura(sdf), cor)
    return img


def painel(ref, k):
    oy = 24 * k
    cores = [amostra(ref, 40, oy + i) for i in (0, 1, 2, 5)]
    px, py = malha(32, oy, 64, oy + 24)
    px, py = px - 32, py - oy
    img = np.zeros(px.shape + (4,), np.float32)
    x0, y0, x1 = PAINEL
    base = [(x0, y0), (DIAG_X, y0), (x1, y0 + 2 * (x1 - DIAG_X)), (x1, 60.0), (x0, 60.0)]   # aberto embaixo (alem da celula)
    for i, cor in enumerate(cores):
        d = i * FILETE
        sdf = sdf_convexo(px, py, poligono_recuado(base, d), max(PAINEL_R - d, 0.35))
        sobre(img, cobertura(sdf), cor)
    return img


def quadradinho(img, ref, x, y, camadas, raio, lado=15.0):
    """Quadrado arredondado em (x, y) com camadas (cor, largura) de fora para dentro."""
    px, py = malha(x, y, x + 16, y + 16)
    px, py = px - x, py - y
    sub = np.zeros(px.shape + (4,), np.float32)
    d = 0.0
    for cor, larg in camadas:
        sdf = sdf_retangulo(px, py, d, d, lado - d, lado - d, max(raio - d, 0.4))
        sobre(sub, cobertura(sdf), cor)
        d += larg
    img[y * ESC:(y + 16) * ESC, x * ESC:(x + 16) * ESC] = sub


def refazer(ref):
    ref = ref.convert("RGB")
    img = np.zeros((H * ESC, W * ESC, 4), np.float32)
    for k in range(4):
        img[24 * k * ESC:(24 * k + 24) * ESC, 0:32 * ESC] = balao(ref, k)
        img[24 * k * ESC:(24 * k + 24) * ESC, 32 * ESC:64 * ESC] = painel(ref, k)
    c = lambda x, y: amostra(ref, x, y)
    quadradinho(img, ref, 0, 96, [(c(7, 100), 0)], 1.0)
    quadradinho(img, ref, 16, 96, [(c(16, 100), 2.0), (c(26, 100), 0)], 3.0)
    quadradinho(img, ref, 32, 96, [(c(32, 100), 1.0), (c(40, 100), 0)], 3.0)
    quadradinho(img, ref, 48, 96, [(c(48, 100), 2.0), (c(56, 100), 0)], 3.0)
    px, py = malha(0, 112, 12, 124)
    img[112 * ESC:124 * ESC, 0:12 * ESC] = [255, 255, 255, 255]
    return Image.fromarray(np.clip(img.round(), 0, 255).astype(np.uint8), "RGBA")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--origem", default=ORIGEM)
    ap.add_argument("--saida")
    o = ap.parse_args()
    img = refazer(Image.open(o.origem))
    if o.saida:
        img.save(o.saida)
        print("gravado", o.saida)
        return
    if os.path.exists(SAIDA) and not os.path.exists(BACKUP):
        os.makedirs(os.path.dirname(BACKUP), exist_ok=True)
        shutil.copy2(SAIDA, BACKUP)
    img.save(SAIDA)
    print("gravado", SAIDA, img.size)


if __name__ == "__main__":
    main()
