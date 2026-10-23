"""Redraws the duelist / deck name plates of the duel setup screen (texture 905aa050, 256x256) at 8x instead of upscaling them.

The plates are 1:1 sprites of the sheet: a red and a blue deck bar (deck icon at the left) and a cream duelist plate (person
icon). Each plate is a 3 px ring (dark gold outline, light bevel, gold) with rounded corners, a 1 px translucent drop shadow
and an interior: a vertical gradient (bars, sampled from the original) or flat cream. The ring and gradient colours are
sampled from the original; the icons are redrawn as small vector shapes. Everything else on the sheet (the team tags and the
outline frames) is kept from the existing 8x sheet of the mod.

Inputs: gen/dump_tex/<dump>/905aa050.png (TF_TEX_DUMP) and the existing 8x sheet of the mod.
Output: textures/menu_plates_hd/905aa050.png; --register repoints the textures.ini / origem.tsv rows of the hash.

Usage: python tools/mods/mod_menu_plates.py [--register]
"""
import os
import sys
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mod_menu_frames import depth_in   # noqa: E402

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
DUMP = os.path.join(ROOT, 'gen', 'dump_tex', 'dk', '905aa050.png')
if not os.path.exists(DUMP):
    DUMP = os.path.join(ROOT, 'gen', 'dump_tex', 'pre', '905aa050.png')
MODS = [os.path.join(ROOT, 'mods', 'EnhancedTextures'), os.path.join(ROOT, 'gen', 'mods', 'EnhancedTextures')]
BASE = 'mikeymk/extras/0000000041bc211d905aa050.png'
OUTDIR = 'menu_plates_hd'
H = '905aa050'
K, S = 8, 2
N = K * S
RADIUS = 2.4
RINGS = 3

# plate boxes (texels, ring included, shadow excluded): x, y, w, h
RED = (0, 72, 147, 22)
BLUE = (0, 96, 147, 22)
CREAM = (121, 169, 131, 22)
PORTRAIT_RED = (0, 0, 70, 70)       # portrait frames with their backdrop gradient (same look as the duel intro frame)
PORTRAIT_BLUE = (72, 0, 70, 70)
PORTRAIT_RADIUS = 2.8


def rgb(a, x, y):
    return a[y, x, :3].astype(np.float32) / 255


def over(dst, col, alpha):
    """dst: premultiplied RGBA (h, w, 4); col: (3,) or (h, w, 3) straight colour; alpha: (h, w)."""
    col = np.broadcast_to(col, dst.shape[:2] + (3,))
    al = alpha[..., None]
    dst[..., :3] = col * al + dst[..., :3] * (1 - al)
    dst[..., 3:4] = al + dst[..., 3:4] * (1 - al)


def ell(xs, ys, cx, cy, rx, ry, p=2.0):
    return (np.abs((xs - cx) / rx) ** p + np.abs((ys - cy) / ry) ** p) <= 1


def rect(xs, ys, x0, y0, x1, y1):
    return (xs >= x0) & (xs < x1) & (ys >= y0) & (ys < y1)


def lerp(t, c0, c1):
    t = np.clip(t, 0, 1)[..., None]
    return np.asarray(c0, np.float32) * (1 - t) + np.asarray(c1, np.float32) * t


def rr(xs, ys, x0, y0, x1, y1, r):
    return depth_in(xs, ys, x0, y0, x1, y1, r)[0] > 0


def deck_icon(xs, ys, ox, oy, top, bot):
    """Deck case icon, plate-relative box (6,6)-(14,17), rounded."""
    X, Y = xs - ox, ys - oy
    outer = rr(X, Y, 6, 6, 14, 17, 1.4)
    inner = rr(X, Y, 7, 7, 13, 16, 0.6)
    black = outer & ~inner
    black |= rect(X, Y, 8, 11, 12, 12) | rect(X, Y, 7, 10, 8, 11) | rect(X, Y, 12, 10, 13, 11)
    black |= rr(X, Y, 9, 9, 11, 10, 0.4)
    fill = inner & ~black
    col = lerp((Y - 7) / 9, top, bot)
    return outer, black, fill, col


def person_icon(xs, ys, ox, oy):
    """Duelist icon: round head with hair, neck, rounded purple shoulders; drawn in its own crop coordinates
    (head 4..14, body 14..19), plate-relative origin (+1, -1)."""
    X, Y = xs - ox - 1, ys - oy + 1
    head_o = ell(X, Y, 9.05, 8.5, 5.05, 4.7)
    head_i = ell(X, Y, 9.05, 8.5, 4.05, 3.7)
    neck_o = rr(X, Y, 7, 12.4, 11.2, 14.4, 0.5)
    sh_o = ell(X, Y, 9.0, 19.2, 5.0, 5.4, 2.6) & (Y < 19.0)
    sh_i = ell(X, Y, 9.0, 19.2, 4.0, 4.4, 2.6) & (Y < 18.0)
    silhouette = head_o | neck_o | sh_o
    hair = head_i & (Y < 7.0 + 2.6 * ((X - 9.05) / 4.0) ** 2)
    face = head_i & ~hair
    hair_col = lerp((Y - 4.5) / 3.5, (0.40, 0.31, 0.20), (0.66, 0.55, 0.40))
    hair_col = lerp((np.abs(X - 9.05) - 2.4) / 1.8, hair_col, (0.45, 0.37, 0.25))
    face_col = lerp((Y - 8) / 4.5, (0.875, 0.80, 0.62), (0.77, 0.64, 0.41))
    neck_i = rr(X, Y, 8, 12.4, 10.2, 14.0, 0.3)
    body_col = lerp((Y - 15) / 3, (0.67, 0.27, 0.93), (0.48, 0.19, 0.78))
    body_col = lerp((np.abs(X - 9) - 2.6) / 1.4, body_col, (0.36, 0.06, 0.62))
    return silhouette, silhouette, [(face, face_col), (hair, hair_col), (neck_i, (0.66, 0.54, 0.31)),
                                    (sh_i, body_col)]


def draw_plate(a, box, kind):
    x0, y0, w, h = box
    X0, Y0 = x0, y0
    Wt, Ht = w + 1, h + 1                       # shadow included
    ys, xs = np.meshgrid(Y0 + (np.arange(Ht * N) + 0.5) / N, X0 + (np.arange(Wt * N) + 0.5) / N, indexing='ij')
    out = np.zeros((Ht * N, Wt * N, 4), np.float32)
    # drop shadow: the plate shape moved by one texel
    sd, _ = depth_in(xs, ys, x0 + 1, y0 + 1, x0 + 1 + w, y0 + 1 + h, RADIUS)
    over(out, (0, 0, 0), (sd > 0) * 0.5)
    d, _ = depth_in(xs, ys, x0, y0, x0 + w, y0 + h, RADIUS)
    inside = d > 0
    mid = y0 + h // 2
    rings = [rgb(a, x0 + i, mid) for i in range(RINGS)]
    k = np.floor(d).astype(int)
    if kind == 'cream':
        interior = np.broadcast_to(np.array([0xff, 0xf8, 0xe6], np.float32) / 255, xs.shape + (3,))
    else:
        rows = np.arange(y0 + RINGS, y0 + h - RINGS)
        cols = np.stack([rgb(a, 70, r) for r in rows])
        interior = np.stack([np.interp(ys - 0.5, rows, cols[:, c]) for c in range(3)], -1)
    col = interior.copy()
    for i in range(RINGS):
        col = np.where((k == i)[..., None], rings[i], col)
    over(out, col, inside.astype(np.float32))
    inter = d >= RINGS
    # icons
    if kind == 'cream':
        sil, sh, layers = person_icon(xs, ys, x0, y0)
        sil2, _, _ = person_icon(xs - 1, ys - 1, x0, y0)
        over(out, (0, 0, 0), (sil2 & inter & ~sil) * 0.5)
        over(out, (0, 0, 0), (sil & inter).astype(np.float32))
        for m, c in layers:
            over(out, c, (m & inter).astype(np.float32))
    else:
        top = rgb(a, 8, y0 + 8)
        bot = rgb(a, 10, y0 + 14)
        outer, black, fill, icol = deck_icon(xs, ys, x0, y0, top, bot)
        outer2, _, _, _ = deck_icon(xs - 1, ys - 1, x0, y0, top, bot)
        over(out, (0, 0, 0), (outer2 & ~outer & inter) * 0.45)
        over(out, (0, 0, 0), (black & inter).astype(np.float32))
        over(out, icol, (fill & inter).astype(np.float32))
    out = out.reshape(Ht * K, S, Wt * K, S, 4).mean((1, 3))
    return out, (X0, Y0, Wt, Ht)


def draw_portrait(a, box):
    """Portrait frame: 3 rings (dark outline, light bevel, gold) around a smooth vertical backdrop gradient, 1 px shadow."""
    x0, y0, w, h = box
    Wt, Ht = w + 1, h + 1
    ys, xs = np.meshgrid(y0 + (np.arange(Ht * N) + 0.5) / N, x0 + (np.arange(Wt * N) + 0.5) / N, indexing='ij')
    out = np.zeros((Ht * N, Wt * N, 4), np.float32)
    sd, _ = depth_in(xs, ys, x0 + 1, y0 + 1, x0 + 1 + w, y0 + 1 + h, PORTRAIT_RADIUS)
    d, _ = depth_in(xs, ys, x0, y0, x0 + w, y0 + h, PORTRAIT_RADIUS)
    over(out, (0, 0, 0), ((sd > 0) & (d <= 0)) * 0.5)
    rows = np.arange(y0 + RINGS, y0 + h - RINGS)
    xm = x0 + w // 2
    cols = np.stack([rgb(a, xm, r) for r in rows])
    cols = np.stack([np.convolve(np.pad(cols[:, c], 2, mode='edge'), np.ones(5) / 5, mode='valid') for c in range(3)], 1)
    interior = np.stack([np.interp(ys - 0.5, rows, cols[:, c]) for c in range(3)], -1)
    rings = [rgb(a, x0 + i, y0 + h // 2) for i in range(RINGS)]
    k = np.floor(d).astype(int)
    col = interior.copy()
    for i in range(RINGS):
        col = np.where((k == i)[..., None], rings[i], col)
    over(out, col, (d > 0).astype(np.float32))
    return out.reshape(Ht * K, S, Wt * K, S, 4).mean((1, 3)), (x0, y0, Wt, Ht)


def main():
    a = np.asarray(Image.open(DUMP).convert('RGBA'))
    base = None
    for mod in MODS:
        p = os.path.join(mod, 'textures', BASE)
        if os.path.exists(p):
            base = p
            break
    if base is None:
        sys.exit('missing base sheet')
    im = np.asarray(Image.open(base).convert('RGBA'), np.float32) / 255
    im[..., :3] *= im[..., 3:4]
    for box, kind in ((RED, 'bar'), (BLUE, 'bar'), (CREAM, 'cream')):
        tile, (X0, Y0, Wt, Ht) = draw_plate(a, box, kind)
        im[Y0 * K:(Y0 + Ht) * K, X0 * K:(X0 + Wt) * K] = tile
    for box in (PORTRAIT_RED, PORTRAIT_BLUE):
        tile, (X0, Y0, Wt, Ht) = draw_portrait(a, box)
        im[Y0 * K:(Y0 + Ht) * K, X0 * K:(X0 + Wt) * K] = tile
    al = im[..., 3:4]
    col = np.where(al > 1e-4, im[..., :3] / np.maximum(al, 1e-4), 0)
    res = np.clip(np.dstack([col, al]) * 255 + 0.5, 0, 255).astype(np.uint8)
    for mod in MODS:
        if not os.path.isdir(mod):
            continue
        dst = os.path.join(mod, 'textures', OUTDIR, H + '.png')
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        Image.fromarray(res, 'RGBA').save(dst)
        print('wrote', dst)
        if '--register' in sys.argv:
            register(mod)


def register(mod):
    rel = OUTDIR + '/' + H + '.png'
    for name, sep in (('textures/textures.ini', ' = '), ('origem.tsv', '\t')):
        p = os.path.join(mod, name)
        if not os.path.exists(p):
            continue
        lines = open(p, encoding='utf-8', newline='').read().split('\n')
        for i, l in enumerate(lines):
            if H not in l:
                continue
            if name.endswith('.ini'):
                k, v = l.split(sep, 1)
                if not k.endswith(H):
                    continue
                lines[i] = k + sep + rel + ('\r' if v.endswith('\r') else '')
            else:
                t = l.rstrip('\r').split('\t')
                if t[0] != H:
                    continue
                t[2], t[3] = 'MenuPlatesHD', rel
                lines[i] = '\t'.join(t) + ('\r' if l.endswith('\r') else '')
        open(p, 'w', encoding='utf-8', newline='').write('\n'.join(lines))
        print('registered', H, 'in', p)


if __name__ == '__main__':
    main()
