"""Redraws the diagonal title bands of the duel intro screen (textures 80dcc5b8, 256x64, and its stretched strip 681677a8,
16x32) at 8x for the EnhancedTextures mod instead of upscaling them.

The band is a stack of flat grey rings (edge highlight, bevel steps, flat interior) plus a few 1 px outlines. The rings
are exactly representable on the 8x grid except along the diagonal, where the original has a staircase: there the label
field (one grey level per ring) is smoothed in the zone around the diagonal (indicator of every ring, blurred, argmax), which
turns the staircase into straight, crisp ring edges with the same ring positions. The outer edge gets a soft anti-aliased
alpha with a thin bright rim like the original. Everything else (flat steps, axis-aligned outlines) is drawn crisp, as in
the original. The strip is the same step profile as the band without the diagonal.

Inputs: gen/dump_tex/rps/80dcc5b8.png and 681677a8.png (TF_TEX_DUMP).
Output: textures/menu_bands_hd/<hash>.png; --register repoints the textures.ini / origem.tsv rows of those hashes.

Usage: python tools/mods/mod_menu_bands.py [--register]
"""
import os
import sys
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mod_menu_frames import depth_in   # noqa: E402

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
DUMP = os.path.join(ROOT, 'gen', 'dump_tex', 'rps')
MODS = [os.path.join(ROOT, 'mods', 'EnhancedTextures'), os.path.join(ROOT, 'gen', 'mods', 'EnhancedTextures')]
OUTDIR = 'menu_bands_hd'
K = 8
SIGMA = 1.1                       # label smoothing, in source texels
ZONE = (60, 130, 0, 56)           # x0, x1, y0, y1 of the diagonal zone (source texels)
THIN = (90, 106, 128, 157, 167, 170, 172, 175)   # one-texel highlight rings: boosted so that the blur does not erase them
THIN_GAIN = 2.0
BANDS = '80dcc5b8'
STRIP = '681677a8'


def load(h):
    return np.asarray(Image.open(os.path.join(DUMP, h + '.png')).convert('RGBA'))


def blur(a, sigma):
    """Separable gaussian blur of a 2D array, edge-replicated."""
    r = int(np.ceil(sigma * 3))
    k = np.exp(-0.5 * (np.arange(-r, r + 1) / sigma) ** 2)
    k /= k.sum()
    p = np.pad(a, ((0, 0), (r, r)), mode='edge')
    a = sum(k[i] * p[:, i:i + a.shape[1]] for i in range(len(k)))
    p = np.pad(a, ((r, r), (0, 0)), mode='edge')
    return sum(k[i] * p[i:i + a.shape[0]] for i in range(len(k)))


def upsample(a):
    """Bilinear upsampling by K of a 2D float array (texel centres)."""
    h, w = a.shape
    ys = (np.arange(h * K) + 0.5) / K - 0.5
    xs = (np.arange(w * K) + 0.5) / K - 0.5
    y0 = np.clip(np.floor(ys).astype(int), 0, h - 2)
    x0 = np.clip(np.floor(xs).astype(int), 0, w - 2)
    fy = np.clip(ys - y0, 0, 1)[:, None]
    fx = np.clip(xs - x0, 0, 1)[None, :]
    return (a[y0][:, x0] * (1 - fy) * (1 - fx) + a[y0][:, x0 + 1] * (1 - fy) * fx +
            a[y0 + 1][:, x0] * fy * (1 - fx) + a[y0 + 1][:, x0 + 1] * fy * fx)


def nearest(a):
    return np.repeat(np.repeat(a, K, 0), K, 1)


def clean_source(a):
    """The source without its 1 px outlines (left / right / bottom of the two panels): they are redrawn as smooth rounded
    strokes afterwards, so the base only carries the rings and flat interior."""
    a = a.copy()
    a[2:62, 0:3] = a[2:62, 3:4]
    a[2:62, 73:77] = a[2:62, 77:78]
    a[62:64, 3:77] = a[61:62, 3:77]
    a[62:64, 77:210] = a[61:62, 77:210]
    a[41:62, 205:210] = a[41:62, 210:211]
    a[36:41, 205:210] = a[36:41, 210:211]
    flat = a[50, 66]                      # the interior grey, used to wipe the corner pixels of the outlines
    for (r0, r1, c0, c1) in ((54, 64, 0, 8), (54, 64, 64, 80), (54, 64, 200, 212)):
        a[r0:r1, c0:c1] = flat
    return a


def outline_layer(shape_alpha):
    """Smooth rounded strokes of the two panel outlines on a (H*K, W*K) float RGBA (premultiplied-free grey) canvas."""
    H, W = 64, 256
    n = K * 2
    ys, xs = np.meshgrid((np.arange(H * n) + 0.5) / n, (np.arange(W * n) + 0.5) / n, indexing='ij')
    grey = np.zeros(ys.shape, np.float32)
    cov = np.zeros(ys.shape, np.float32)
    dark = np.interp(ys, [10, 13, 30, 48], [82, 62, 62, 41]).astype(np.float32)
    # (x0, y0, x1, y1, radius, per side [outer, inner]); None = nothing (the base already has that colour)
    boxes = [
        (1, -20, 76, 64, 3.0, {0: (97.0, dark), 1: (116.0, 140.0), 3: (140.0, None), 2: (None, None)}),
        (76, 20, 209, 64, 3.0, {0: (97.0, None), 1: (116.0, 140.0), 3: (90.0, 116.0), 2: (None, None)}),
    ]
    for x0, y0, x1, y1, R, sides in boxes:
        d, side = depth_in(xs, ys, x0, y0, x1, y1, R)
        k = np.floor(d).astype(int)
        for sid, (c_out, c_in) in sides.items():
            for ring, c in ((0, c_out), (1, c_in)):
                if c is None:
                    continue
                m = (side == sid) & (d > 0) & (k == ring)
                grey = np.where(m, c, grey)
                cov = np.where(m, 1.0, cov)
    cov = np.where((xs > 150) & (ys < 41), 0, cov)
    cov = np.where(ys < 2.5, 0, cov)      # the strokes start below the top highlight rows
    return grey, cov, n


RING_GREY = np.array([116, 170, 140, 116, 97, 82], np.float32)     # grey of ring 0 (outer edge) .. 5 (flat interior)
# ring thresholds (distance from the edge, texels) measured on the original: top edge, diagonal, lower edge
T_TOP = np.array([1, 2, 4, 8, 12], np.float32)
T_DIAG = np.array([1.2, 2.1, 4.75, 9.15, 13.5], np.float32)
T_LOW = np.array([1, 2, 5, 9, 12], np.float32)
XE0, SLOPE, RHO = 78.6, 0.535, 5.0     # diagonal edge x = XE0 + SLOPE * y, from the top corner to the lower edge at y = 36
LOWER_Y = 36.0
SS = 2                                  # supersampling on top of K
RIM_GREY, RIM_ALPHA, RIM_W = 182.0, 0.55, 0.45


def ring_coord(d, T):
    """Continuous ring coordinate: crosses the integer j + 1 exactly at the threshold T[j]; 0 at the edge, 5 inside."""
    xp = np.concatenate([[0], T, [T[-1] + 1e3]])
    fp = np.array([0, 1, 2, 3, 4, 5, 5], np.float32)
    return np.interp(d, xp, fp)


def analytic_zone():
    """The diagonal zone drawn analytically: rings are straight offsets of the top edge, the diagonal and the lower edge; the
    concave bend between the diagonal and the lower edge is a fillet of radius RHO; the join with the top edge is rounded."""
    x0, x1, y0, y1 = ZONE
    n = K * SS
    ys, xs = np.meshgrid(y0 + (np.arange((y1 - y0) * n) + 0.5) / n, x0 + (np.arange((x1 - x0) * n) + 0.5) / n, indexing='ij')
    nn = np.hypot(SLOPE, 1.0)
    yD = LOWER_Y - RHO
    xD = XE0 + SLOPE * yD + RHO * nn
    u1 = (-SLOPE / nn, -1.0 / nn)
    t1 = np.clip((xs - xD) * u1[0] + (ys - yD) * u1[1], 0, None)
    d1 = np.hypot(xs - (xD + t1 * u1[0]), ys - (yD + t1 * u1[1]))
    d2 = np.hypot(xs - (xD + np.clip(xs - xD, 0, None)), ys - yD)
    dist_er = np.minimum(d1, d2)
    dd = dist_er - RHO                            # distance inside the shape from the diagonal / lower edge
    diag_x = XE0 + SLOPE * ys
    in_w = (ys < LOWER_Y) & (xs > diag_x)         # the transparent wedge above the lower edge and right of the diagonal
    in_wer = (ys < yD) & (xs > diag_x + RHO * nn)
    transparent = in_wer | (in_w & (dist_er < RHO))   # the wedge with its tip rounded (fillet of radius RHO)
    inside = ~transparent
    psi = np.arctan2(ys - yD, xs - xD)
    psi = np.where(psi < -np.pi / 2, psi + 2 * np.pi, psi)
    psi1, psi2 = np.arctan2(SLOPE, -1.0), np.pi / 2
    phi = np.clip((psi1 - psi) / (psi1 - psi2), 0, 1)
    T = (1 - phi)[..., None] * T_DIAG + phi[..., None] * T_LOW
    u_dd = np.zeros(dd.shape, np.float32)
    for j in range(5):                            # piecewise-linear ring coordinate with per-pixel thresholds
        lo = T[..., j - 1] if j else np.zeros_like(dd)
        hi = T[..., j]
        u_dd += np.clip((dd - lo) / np.maximum(hi - lo, 1e-3), 0, 1)
    u_top = ring_coord(ys, T_TOP)
    k = 0.6
    h = np.clip(k - np.abs(u_dd - u_top), 0, None) / k
    smin = np.minimum(u_dd, u_top) - h * h * k / 4          # polynomial smooth minimum: rounds the join of the two edges
    u = np.where(np.minimum(u_dd, u_top) >= 5, 5.0, smin)
    u = np.where(ys < LOWER_Y, u, u_dd)
    ring = np.clip(np.floor(u), 0, 5).astype(int)
    grey = RING_GREY[ring]
    d_out = np.where(in_wer, dist_er + RHO, RHO - dist_er)       # distance of a transparent point to the shape
    rim = (~inside) & (d_out < RIM_W) & (ys > 1.0) & (xs > XE0 - 1)
    col = np.where(inside, grey, RIM_GREY)
    al = np.where(inside, 1.0, np.where(rim, RIM_ALPHA, 0.0)).astype(np.float32)
    pm = col * al
    pm = pm.reshape((y1 - y0) * K, SS, (x1 - x0) * K, SS).mean((1, 3))
    al = al.reshape((y1 - y0) * K, SS, (x1 - x0) * K, SS).mean((1, 3))
    g = np.where(al > 1e-4, pm / np.maximum(al, 1e-4), 0)
    patch = np.zeros(g.shape + (4,), np.float32)
    patch[..., 0] = patch[..., 1] = patch[..., 2] = g
    patch[..., 3] = al * 255
    return patch


def make_bands():
    a = clean_source(load(BANDS).astype(np.int32))
    op = a[..., 3] >= 250
    base = nearest(np.dstack([a[..., 0], a[..., 1], a[..., 2], np.where(op, 255, 0)]).astype(np.uint8))
    out = base.copy()
    x0, x1, y0, y1 = ZONE
    patch = analytic_zone()
    crisp = base[y0 * K:y1 * K, x0 * K:x1 * K].astype(np.float32)
    # axis-aligned highlight rows stay crisp where they are straight (top edge left of the corner, lower edge right of it)
    for (ya, yb, xa, xb) in ((0, 2, 0, 76), (36, 38, 104, 256)):
        ya_, yb_ = max(ya, y0), min(yb, y1)
        xa_, xb_ = max(xa, x0), min(xb, x1)
        patch[(ya_ - y0) * K:(yb_ - y0) * K, (xa_ - x0) * K:(xb_ - x0) * K] = crisp[(ya_ - y0) * K:(yb_ - y0) * K, (xa_ - x0) * K:(xb_ - x0) * K]
    out[y0 * K:y1 * K, x0 * K:x1 * K] = np.clip(patch + 0.5, 0, 255).astype(np.uint8)
    # panel outlines: smooth rounded strokes over the opaque body
    grey, cov, n = outline_layer(None)
    grey = grey.reshape(64 * K, 2, 256 * K, 2).mean((1, 3))
    covr = cov.reshape(64 * K, 2, 256 * K, 2).mean((1, 3))
    body = out[..., 3:4].astype(np.float32) / 255
    a_ = covr[..., None] * (body > 0.5)
    rgb = out[..., :3].astype(np.float32) * (1 - a_) + grey[..., None] / np.maximum(covr[..., None], 1e-4) * a_ * 1.0
    out[..., :3] = np.clip(rgb + 0.5, 0, 255).astype(np.uint8)
    return out


def make_strip():
    a = load(STRIP)
    return nearest(a)


def save(rel, arr):
    for mod in MODS:
        if not os.path.isdir(mod):
            continue
        dst = os.path.join(mod, 'textures', OUTDIR, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        Image.fromarray(arr, 'RGBA').save(dst)
        print('wrote', dst)


def register(mod, hashes):
    for name, sep in (('textures/textures.ini', ' = '), ('origem.tsv', '\t')):
        p = os.path.join(mod, name)
        if not os.path.exists(p):
            continue
        lines = open(p, encoding='utf-8', newline='').read().split('\n')
        for i, l in enumerate(lines):
            for h in hashes:
                if h not in l:
                    continue
                rel = OUTDIR + '/' + h + '.png'
                if name.endswith('.ini'):
                    if sep not in l:
                        continue
                    k, v = l.split(sep, 1)
                    if not k.endswith(h):
                        continue
                    lines[i] = k + sep + rel + ('\r' if v.endswith('\r') else '')
                else:
                    t = l.rstrip('\r').split('\t')
                    if len(t) < 4 or t[0] != h:
                        continue
                    t[2], t[3] = 'MenuBandsHD', rel
                    lines[i] = '\t'.join(t) + ('\r' if l.endswith('\r') else '')
        open(p, 'w', encoding='utf-8', newline='').write('\n'.join(lines))
        print('registered in', p)


def main():
    save(BANDS + '.png', make_bands())
    save(STRIP + '.png', make_strip())
    if '--register' in sys.argv:
        for mod in MODS:
            if os.path.isdir(mod):
                register(mod, (BANDS, STRIP))


if __name__ == '__main__':
    main()
