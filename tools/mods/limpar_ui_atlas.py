#!/usr/bin/env python3
"""Limpa atlas de interface do EnhancedTextures: bordas retas e suaves, sem os degraus/ondas deixados pela IA (Real-ESRGAN).

Metodo ("vetorizacao" por regioes de cor):
  1. agrupa as cores em K regioes (k-means em RGB, so pixels opacos);
  2. suaviza o mapa de regioes (mascara de cada regiao borrada e re-decidida por maioria): cantos e diagonais ficam limpos;
  3. cada regiao recebe de volta a cor ORIGINAL borrada so dentro dela (preserva os degrades dos paineis, mas sem vazar
     cor entre regioes vizinhas);
  4. o alfa e borrado e re-afiado (smoothstep): borda externa nitida e sem serrilhado.
Os desenhos nao ficam identicos aos do jogo (so parecidos): pequenos detalhes (<~1,5 px do PSP) se perdem de proposito.

Uso: python tools/mods/limpar_ui_atlas.py entrada.png saida.png [--k 24] [--sigma 3.0] [--cor 6.0]
"""
import argparse

import numpy as np
import torch
import torch.nn.functional as F
from PIL import Image

DEV = "cuda" if torch.cuda.is_available() else "cpu"


def gauss_kernel(sigma):
    r = max(1, int(round(sigma * 3)))
    x = torch.arange(-r, r + 1, dtype=torch.float32, device=DEV)
    k = torch.exp(-(x ** 2) / (2 * sigma ** 2))
    return k / k.sum()


def blur(t, sigma):
    """t: (C,H,W) float. Gaussiana separavel com borda espelhada."""
    k = gauss_kernel(sigma)
    r = (len(k) - 1) // 2
    c = t.shape[0]
    x = t[None]
    x = F.pad(x, (r, r, 0, 0), mode="replicate")
    x = F.conv2d(x, k.view(1, 1, 1, -1).repeat(c, 1, 1, 1), groups=c)
    x = F.pad(x, (0, 0, r, r), mode="replicate")
    x = F.conv2d(x, k.view(1, 1, -1, 1).repeat(c, 1, 1, 1), groups=c)
    return x[0]


def kmeans(px, k, iters=12, seed=1):
    g = torch.Generator(device="cpu").manual_seed(seed)
    idx = torch.randperm(px.shape[0], generator=g)[:k].to(DEV)
    cen = px[idx].clone()
    for _ in range(iters):
        d = torch.cdist(px, cen)
        lab = d.argmin(1)
        for j in range(k):
            m = lab == j
            if m.any():
                cen[j] = px[m].mean(0)
    return cen


def limpar(img, k=24, sigma=3.0, sigma_cor=6.0, halo=0.0):
    a = np.array(img.convert("RGBA")).astype(np.float32) / 255
    h, w = a.shape[:2]
    rgb = torch.from_numpy(a[..., :3]).permute(2, 0, 1).to(DEV)
    alfa = torch.from_numpy(a[..., 3]).to(DEV)[None]
    opaco = alfa[0] > 0.5
    # cores amostradas (maximo 400k px) para o k-means; classificacao de todos os pixels em blocos
    flat = rgb.permute(1, 2, 0).reshape(-1, 3)
    sel = opaco.reshape(-1).nonzero().squeeze(1)
    amostra = flat[sel[torch.randperm(len(sel))[:400000].to(DEV)]]
    cen = kmeans(amostra, k)
    lab = torch.empty(h * w, dtype=torch.long, device=DEV)
    for i in range(0, h * w, 1 << 20):
        lab[i:i + (1 << 20)] = torch.cdist(flat[i:i + (1 << 20)], cen).argmin(1)
    lab = lab.view(h, w)
    onehot = F.one_hot(lab, k).permute(2, 0, 1).float() * opaco[None].float()   # (K,H,W), so onde ha pixel opaco
    # suaviza o mapa de regioes: cada regiao vira a mascara borrada; ganha quem tem mais "voto" no pixel
    voto = blur(onehot, sigma)
    novo = voto.argmax(0)
    # cor de cada regiao: original borrada so dentro dela (normalizada)
    saida = torch.zeros_like(rgb)
    peso_total = torch.zeros(1, h, w, device=DEV)
    for j in range(k):
        m = ((novo == j) & opaco).float()[None]
        if m.sum() < 1:
            continue
        num = blur(rgb * m, sigma_cor)
        den = blur(m, sigma_cor).clamp_min(1e-4)
        col = num / den
        saida += col * m
        peso_total += m
    # onde nao ha regiao (transparente) mantem a cor original (para o filtro bilinear nao puxar preto)
    saida = torch.where(peso_total > 0, saida, rgb)
    # alfa: borra e reafia (smoothstep em torno de 0,5): borda reta e com antialias
    ab = blur(alfa, sigma * 0.8)
    t = ((ab - 0.5) / 0.18 + 0.5).clamp(0, 1)
    af = t * t * (3 - 2 * t)
    if halo > 0:
        # sombra/halo semitransparente: o degrau do halo original some borrando o alfa (e a cor, premultiplicada) com sigma `halo`
        pre = rgb * alfa
        hb = blur(alfa, halo)
        cb = blur(pre, halo) / hb.clamp_min(1e-4)
        meio = (af < 0.98).float()
        novo_a = torch.maximum(af, hb * meio)
        saida = torch.where((meio > 0) & (hb > af), cb, saida)
        af = novo_a
    out = torch.cat([saida, af], 0).permute(1, 2, 0).clamp(0, 1).cpu().numpy()
    return Image.fromarray((out * 255 + 0.5).astype(np.uint8), "RGBA")


def componentes_grandes(alfa_np, area_min, f=4):
    """Mascara (H,W) bool das pecas conexas grandes (area em px do atlas). Rotula num mapa reduzido por f."""
    import collections
    h, w = alfa_np.shape
    m = alfa_np[: h // f * f, : w // f * f].reshape(h // f, f, w // f, f).max(axis=(1, 3)) > 0.5
    visto = np.zeros_like(m)
    grande = np.zeros_like(m)
    for y in range(m.shape[0]):
        for x in range(m.shape[1]):
            if m[y, x] and not visto[y, x]:
                q = collections.deque([(y, x)]); visto[y, x] = 1; pts = []
                while q:
                    cy, cx = q.popleft(); pts.append((cy, cx))
                    for dy in (-1, 0, 1):
                        for dx in (-1, 0, 1):
                            ny, nx = cy + dy, cx + dx
                            if 0 <= ny < m.shape[0] and 0 <= nx < m.shape[1] and m[ny, nx] and not visto[ny, nx]:
                                visto[ny, nx] = 1; q.append((ny, nx))
                if len(pts) * f * f >= area_min:
                    for cy, cx in pts: grande[cy, cx] = 1
    g = np.kron(grande, np.ones((f, f), bool))
    full = np.zeros((h, w), bool); full[: g.shape[0], : g.shape[1]] = g
    return full


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("entrada")
    ap.add_argument("saida")
    ap.add_argument("--k", type=int, default=24)
    ap.add_argument("--sigma", type=float, default=3.0)
    ap.add_argument("--cor", type=float, default=6.0)
    ap.add_argument("--halo", type=float, default=0.0, help="suaviza o halo/sombra semitransparente (sigma em texels; 0 = nao mexe)")
    ap.add_argument("--area-min", type=int, default=0, help="so limpa pecas conexas com pelo menos esta area (px do atlas); o resto fica como esta")
    args = ap.parse_args()
    orig = Image.open(args.entrada).convert("RGBA")
    novo = limpar(orig, args.k, args.sigma, args.cor, args.halo)
    if args.area_min:
        a = np.array(orig).astype(np.float32) / 255
        grande = componentes_grandes(a[..., 3], args.area_min)
        # a regiao limpa inclui a franja de sombra/borda: dilata a mascara em ~6 px (box blur) antes de misturar
        mt = torch.from_numpy(grande.astype(np.float32))[None].to(DEV)
        mt = (blur(mt, 5.0) > 0.05).float()[0].cpu().numpy().astype(bool)
        o, n = np.array(orig), np.array(novo)
        o[mt] = n[mt]
        novo = Image.fromarray(o, "RGBA")
    novo.save(args.saida)
    print("gravado:", args.saida, "(", DEV, ")")


if __name__ == "__main__":
    main()
