"""Redraws the orange pill under the LP bar of the duel HUD (sprite at texels 54..77 x 18..25 of texture ee0c44c3, 128x128)
at 16x for EnhancedTextures instead of upscaling it.

The game draws the pill 1:1 (UV 54,18 - 81,26, 27x8 texels at the top of each player's panel). The original is a 23x7 pixel-art
stadium: a darker top row, a lighter row below it, a dithered mid band, a lighter lower band, a dark bottom row, a small gloss
spot on the left cap and a 1 texel half-transparent drop shadow to the lower right. It is redrawn analytically: anti-aliased
stadium (signed distance), the vertical shading is the median of the original central columns interpolated between row
centres (no dither), a gloss spot on the left cap, darker rims on the caps and the shadow as the shape shifted by one texel.
Only the box x 53..79, y 18..26 of the existing 16x sheet is replaced; everything else on the sheet is kept.

Input: gen/dump_tex/<dump>/ee0c44c3.png (TF_TEX_DUMP) and the existing 16x sheet of the mod.
Output: textures/menu_hud_pill_hd/ee0c44c3.png; --register repoints the textures.ini / origem.tsv rows of the hash.

Usage: python tools/mods/mod_hud_lp_pill.py [--register]
"""
import os
import sys
import numpy as np
from PIL import Image

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
H = 'ee0c44c3'
DUMP = next((p for p in (os.path.join(ROOT, 'gen', 'dump_tex', d, H + '.png') for d in ('duel', 'hand', 'fd', 'dk', 'pre', 'rps'))
             if os.path.exists(p)), None)
MODS = [os.path.join(ROOT, 'mods', 'EnhancedTextures'), os.path.join(ROOT, 'gen', 'mods', 'EnhancedTextures')]
BASE = 'mikeymk/extras/0000000042538387ee0c44c3.png'
OUTDIR = 'menu_hud_pill_hd'
K, S = 16, 4
N = K * S
X0, X1, Y0, Y1 = 54.0, 77.0, 18.0, 25.0          # pill body in texels
BOX = (53, 18, 79, 26)                             # replaced region (x0, y0, x1, y1), shadow included
SHADOW = 0.5
GLOSS = (56.5, 19.6, 1.5, 0.85)                    # centre x, y and radii of the left cap gloss spot
GLOSS_COL = (1.0, 0.80, 0.42)


def sstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def stadium(xs, ys, dx=0.0, dy=0.0):
    """Signed distance (outside > 0) of the pill shifted by (dx, dy), plus the horizontal overhang past the straight part."""
    r = (Y1 - Y0) / 2
    cy = (Y0 + Y1) / 2 + dy
    ov = np.maximum(np.maximum(X0 + dx + r - xs, xs - (X1 + dx - r)), 0)
    return np.hypot(ov, ys - cy) - r, ov


def over(dst, col, alpha):
    col = np.broadcast_to(col, dst.shape[:2] + (3,))
    al = alpha[..., None]
    dst[..., :3] = col * al + dst[..., :3] * (1 - al)
    dst[..., 3:4] = al + dst[..., 3:4] * (1 - al)


def draw(a):
    rows = np.arange(int(Y0), int(Y1))
    cols = np.stack([np.median(a[r, 62:73, :3].astype(np.float32), axis=0) / 255 for r in rows])
    bx0, by0, bx1, by1 = BOX
    ys, xs = np.meshgrid(by0 + (np.arange((by1 - by0) * N) + 0.5) / N, bx0 + (np.arange((bx1 - bx0) * N) + 0.5) / N, indexing='ij')
    out = np.zeros(xs.shape + (4,), np.float32)
    d, ov = stadium(xs, ys)
    ds, _ = stadium(xs, ys, 1.0, 1.0)
    over(out, (0, 0, 0), (SHADOW * sstep(0.5 / K, -0.5 / K, ds) * (d > 0)).astype(np.float32))
    body = np.stack([np.interp(ys, rows + 0.5, cols[:, c]) for c in range(3)], -1)
    cap = sstep(0.0, 1.0, ov)
    rim = (1 - sstep(0.0, 0.9, -d)) * cap * 0.22
    body = body * (1 - rim[..., None])
    gx, gy, rx, ry = GLOSS
    g = np.clip(1 - np.hypot((xs - gx) / rx, (ys - gy) / ry), 0, 1)
    body = body * (1 - 0.6 * g[..., None]) + np.asarray(GLOSS_COL, np.float32) * 0.6 * g[..., None]
    over(out, body, sstep(0.5 / K, -0.5 / K, d).astype(np.float32))
    return out.reshape((by1 - by0) * K, S, (bx1 - bx0) * K, S, 4).mean((1, 3))


def main():
    if DUMP is None:
        sys.exit('missing dump of ' + H + ' (TF_TEX_DUMP)')
    a = np.asarray(Image.open(DUMP).convert('RGBA'))
    base = next((os.path.join(m, 'textures', BASE) for m in MODS if os.path.exists(os.path.join(m, 'textures', BASE))), None)
    if base is None:
        sys.exit('missing base sheet')
    im = np.asarray(Image.open(base).convert('RGBA'), np.float32) / 255
    if im.shape[0] != 128 * K:
        sys.exit('unexpected base size %s' % (im.shape,))
    im[..., :3] *= im[..., 3:4]
    x0, y0, x1, y1 = BOX
    im[y0 * K:y1 * K, x0 * K:x1 * K] = draw(a)
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
                t[2], t[3] = 'HudLpPillHD', rel
                lines[i] = '\t'.join(t) + ('\r' if l.endswith('\r') else '')
        open(p, 'w', encoding='utf-8', newline='').write('\n'.join(lines))
        print('registered', H, 'in', p)


if __name__ == '__main__':
    main()
