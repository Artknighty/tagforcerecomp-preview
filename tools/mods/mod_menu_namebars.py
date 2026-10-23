"""Redraws the pieces of the rock-paper-scissors / duel intro screen (the duelist name plates, the top and bottom bars, the
portrait frames and the portrait backdrops) at 8x for the EnhancedTextures mod instead of upscaling them.

The plates and bars are drawn by the game from three 16x32 pieces (left cap, one stretched texel column, right cap) that are
tinted at draw time (cream for the duelist plates, red / blue for the bars). Each piece is cut from one virtual plate that is
rendered at 16x and reduced to 8x, so the rings (dark outline, light bevel, gold), the rounded corners and the drop shadow are
continuous across the pieces and there is no seam. The ring colours are sampled from the original pieces; the person icon is
redrawn as a small vector shape. The portrait frame (128x128, 70x70 ring box with a shadow) is redrawn the same way, and the
portrait backdrops (32x32 vertical gradients, red and blue palettes) become smooth gradients.

Inputs: the original textures dumped with TF_TEX_DUMP (gen/dump_tex/rps/<hash>.png) and, for the red backdrop palette, the
existing MikeyMK 8x file in the mod.
Output: textures/menu_namebars_hd/<name>.png; --register repoints the textures.ini / origem.tsv rows of those hashes.

Usage: python tools/mods/mod_menu_namebars.py [--register]
"""
import os
import sys
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mod_menu_frames import depth_in                                          # noqa: E402
from mod_menu_plates import over, person_icon                                 # noqa: E402

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
DUMP = os.path.join(ROOT, 'gen', 'dump_tex', 'rps')
MODS = [os.path.join(ROOT, 'mods', 'EnhancedTextures'), os.path.join(ROOT, 'gen', 'mods', 'EnhancedTextures')]
OUTDIR = 'menu_namebars_hd'
K, S = 8, 2
N = K * S
RADIUS = 2.4

# pieces: hash -> (name, kind); kind 'L' left cap, 'LI' left cap with the person icon, 'M' stretched column, 'R' right cap
PIECES = {'0fe98bdb': 'L', '484c8a76': 'LI', '17821325': 'M', 'c4b6b4df': 'R'}
PLATE_Y0, PLATE_Y1 = 5, 27          # rows of the plate (rings included), the shadow is the row below
CAP = 16                            # piece width in texels
FRAME_HASH = 'e14be8d3'
BACK_HASH = 'd86a2519'
BACK_RED = 'mikeymk/extras/000000000fa77384d86a2519.png'
BACK_BLUE = 'mikeymk/extras/000000006f7932d5d86a2519.png'


def load(h):
    return np.asarray(Image.open(os.path.join(DUMP, h + '.png')).convert('RGBA'))


def rgbf(a, x, y):
    return a[y, x, :3].astype(np.float32) / 255


def finish(out, w, h):
    """Premultiplied supersampled canvas -> straight RGBA uint8 at K texels per source texel."""
    out = out.reshape(h * K, S, w * K, S, 4).mean((1, 3))
    al = out[..., 3:4]
    col = np.where(al > 1e-4, out[..., :3] / np.maximum(al, 1e-4), 0)
    return np.clip(np.dstack([col, al]) * 255 + 0.5, 0, 255).astype(np.uint8)


def render_plate(a_left, a_mid, a_right, with_icon):
    """The whole plate as a virtual picture: left cap | one stretched column | right cap (shadow included)."""
    W, H = CAP + 1 + CAP, 32
    ys, xs = np.meshgrid((np.arange(H * N) + 0.5) / N, (np.arange(W * N) + 0.5) / N, indexing='ij')
    out = np.zeros((H * N, W * N, 4), np.float32)
    x0, x1, y0, y1 = 0, W - 1, PLATE_Y0, PLATE_Y1
    sd, _ = depth_in(xs, ys, x0 + 1, y0 + 1, x1 + 1, y1 + 1, RADIUS)
    over(out, (0, 0, 0), (sd > 0) * (129 / 255))
    d, _ = depth_in(xs, ys, x0, y0, x1, y1, RADIUS)
    mid = (y0 + y1) // 2
    rings = [rgbf(a_left, i, mid) for i in range(3)]
    interior = rgbf(a_mid, 8, mid)
    k = np.floor(d).astype(int)
    col = np.broadcast_to(interior, xs.shape + (3,)).copy()
    for i in range(3):
        col = np.where((k == i)[..., None], rings[i], col)
    over(out, col, (d > 0).astype(np.float32))
    if with_icon:
        inter = d >= 3
        sil, _, layers = person_icon(xs, ys, 0, y0 + 0.25)
        sil2, _, _ = person_icon(xs - 1, ys - 1, 0, y0 + 0.25)
        over(out, (0, 0, 0), (sil2 & inter & ~sil) * 0.5)
        over(out, (0, 0, 0), (sil & inter).astype(np.float32))
        for m, c in layers:
            over(out, c, (m & inter).astype(np.float32))
    return finish(out, W, H)


def make_pieces():
    left, mid, right = load('0fe98bdb'), load('17821325'), load('c4b6b4df')
    outs = {}
    for icon, h in ((False, '0fe98bdb'), (True, '484c8a76')):
        img = render_plate(load(h), mid, right, icon)
        outs[h] = img[:, :CAP * K]
        if not icon:
            colw = img[:, CAP * K:(CAP + 1) * K]
            outs['17821325'] = np.tile(colw[:, :1], (1, CAP * K, 1))
            outs['c4b6b4df'] = img[:, (CAP + 1) * K:]
    return outs


def make_frame():
    a = load(FRAME_HASH)
    W = H = 128
    ys, xs = np.meshgrid((np.arange(H * N) + 0.5) / N, (np.arange(W * N) + 0.5) / N, indexing='ij')
    out = np.zeros((H * N, W * N, 4), np.float32)
    x0, y0, x1, y1 = 29, 29, 99, 99
    rings = [rgbf(a, x0 + i, 64) for i in range(3)]
    sd, _ = depth_in(xs, ys, x0 + 1, y0 + 1, x1 + 1, y1 + 1, RADIUS + 0.4)
    d, _ = depth_in(xs, ys, x0, y0, x1, y1, RADIUS + 0.4)
    ring = (d > 0) & (d < 3)
    k = np.floor(np.clip(d, 0, 2.999)).astype(int)
    col = np.zeros(xs.shape + (3,), np.float32)
    for i in range(3):
        col = np.where((k == i)[..., None], rings[i], col)
    over(out, (0, 0, 0), ((sd > 0) & (d <= 0)) * (129 / 255))
    over(out, col, ring.astype(np.float32))
    return finish(out, W, H)


def gradient(rows, h_out):
    """Smooth vertical gradient through the row colours (32 rows -> h_out px), a tiny blur to hide the steps."""
    rows = np.asarray(rows, np.float32)
    n = len(rows)
    yy = (np.arange(h_out) + 0.5) / h_out * n - 0.5
    g = np.stack([np.interp(yy, np.arange(n), rows[:, c]) for c in range(3)], -1)
    ker = np.exp(-0.5 * (np.arange(-24, 25) / 10.0) ** 2)
    ker /= ker.sum()
    pad = np.pad(g, ((24, 24), (0, 0)), mode='edge')
    g = np.stack([np.convolve(pad[:, c], ker, mode='valid') for c in range(3)], -1)
    return g


def make_backdrops():
    outs = {}
    for name, rel in (('0fa77384', BACK_RED), ('6f7932d5', BACK_BLUE)):
        p = None
        for mod in MODS:
            q = os.path.join(mod, 'textures', rel)
            if os.path.exists(q):
                p = q
                break
        if p is None:
            sys.exit('missing ' + rel)
        a = np.asarray(Image.open(p).convert('RGB')).astype(np.float32) / 255
        step = a.shape[0] // 32
        rows = [a[i * step + step // 2, a.shape[1] // 2] for i in range(32)]
        g = gradient(rows, 32 * K)
        img = np.dstack([np.repeat(g[:, None, :], 32 * K, 1), np.ones((32 * K, 32 * K, 1), np.float32)])
        outs[name] = np.clip(img * 255 + 0.5, 0, 255).astype(np.uint8)
    return outs


def save(rel, arr):
    for mod in MODS:
        if not os.path.isdir(mod):
            continue
        dst = os.path.join(mod, 'textures', OUTDIR, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        Image.fromarray(arr, 'RGBA').save(dst)
        print('wrote', dst)


def register(mod, rules):
    """rules: list of (hash, clut or None for every row of the hash, relative path, mod name)."""
    for name, sep in (('textures/textures.ini', ' = '), ('origem.tsv', '\t')):
        p = os.path.join(mod, name)
        if not os.path.exists(p):
            continue
        lines = open(p, encoding='utf-8', newline='').read().split('\n')
        for i, l in enumerate(lines):
            for h, clut, rel, nm in rules:
                if h not in l:
                    continue
                if name.endswith('.ini'):
                    if sep not in l:
                        continue
                    k, v = l.split(sep, 1)
                    if not k.endswith(h) or (clut and clut not in k):
                        continue
                    lines[i] = k + sep + OUTDIR + '/' + rel + ('\r' if v.endswith('\r') else '')
                else:
                    t = l.rstrip('\r').split('\t')
                    if len(t) < 4 or t[0] != h or (clut and t[1] != clut):
                        continue
                    t[2], t[3] = nm, OUTDIR + '/' + rel
                    lines[i] = '\t'.join(t) + ('\r' if l.endswith('\r') else '')
        open(p, 'w', encoding='utf-8', newline='').write('\n'.join(lines))
        print('registered in', p)


def main():
    rules = []
    for h, img in make_pieces().items():
        save(h + '.png', img)
        rules.append((h, None, h + '.png', 'MenuNameBarsHD'))
    save(FRAME_HASH + '.png', make_frame())
    rules.append((FRAME_HASH, None, FRAME_HASH + '.png', 'MenuNameBarsHD'))
    for clut, img in make_backdrops().items():
        save('%s_%s.png' % (BACK_HASH, clut), img)
        rules.append((BACK_HASH, clut, '%s_%s.png' % (BACK_HASH, clut), 'MenuNameBarsHD'))
    if '--register' in sys.argv:
        for mod in MODS:
            if os.path.isdir(mod):
                register(mod, rules)


if __name__ == '__main__':
    main()
