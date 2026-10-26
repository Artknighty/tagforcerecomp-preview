"""Redraws the 9-slice UI frames of the menu screens (Free Duel and friends) at 8x instead of upscaling them.

The game draws these frames from 3 slices per axis: two corner blocks plus a single texel that is stretched over the whole
edge. The pixel-art original is a stack of flat colour rings (outline, light bevel, gold, interior) plus a 1 px drop shadow.
Each frame is redrawn analytically: the ring colours are sampled from the original along the middle row/column, then every
ring is rendered with true rounded corners (mitred bevel) at 16x and reduced to 8x. The stretched centre is uniform by
construction, so there is no banding or seam at any size.

Inputs: the original textures dumped with TF_TEX_DUMP (gen/dump_tex/fd/<hash>.png) and the existing 8x sheet of the mod
(the pieces that are not redrawn here are kept as they are).
Output: textures/menus/pc/<hash>.png; --register repoints the textures.ini / origem.tsv rows of those hashes.

Usage: python tools/mods/mod_menu_frames.py [--register]
"""
import os
import sys
import numpy as np
from PIL import Image

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
DUMP = os.path.join(ROOT, 'gen', 'dump_tex', 'fd')
MODS = [os.path.join(ROOT, 'mods', 'EnhancedTextures'), os.path.join(ROOT, 'gen', 'mods', 'EnhancedTextures')]
K = 8      # output texels per game texel
S = 2      # supersampling on top of K
OUTDIR = 'menus/pc'

# sheet hash -> source sheet in the mod (8x) used as the base for the pieces that are not redrawn
BASE = {
    '76bc9800': 'menus/misc__00000000836977a003b3d6f2.png',
    'b8f61dd4': '_unreferenced/mikeymk__ui__other_ui.png',
}


def cells_grid():
    """3x3 button grid of 76bc9800: 12x12 cells with a 1 px shadow, pitch 13, as drawn by the menus (4 + 1 + 5 slices)."""
    out = []
    for r in range(3):
        for c in range(3):
            u, v = 138 + 13 * c, 23 + 13 * r
            out.append(dict(sheet='76bc9800', radius=2.4, cols=(u, u + 4, u + 6, u + 8, u + 13),
                            rows=(v, v + 4, v + 4, v + 8, v + 13)))
    return out


def cells_strips():
    """Header strips (cream / tan / dark / gold) of 76bc9800: 1 px outline, 2 px corner blocks, no shadow."""
    out = []
    for i in range(4):
        v = 0 + 16 * i
        out.append(dict(sheet='76bc9800', radius=1.9, cols=(186, 188, 192, 200, 202), rows=(v, v + 2, v + 8, v + 14, v + 16)))
    return out


def cells_panels():
    """Cream panels of b8f61dd4 (4 colour variants stacked every 32 texels): 8 px corner blocks."""
    out = []
    for i in range(4):
        v = 32 * i
        out.append(dict(sheet='b8f61dd4', radius=4.5, cols=(64, 72, 72, 88, 96), rows=(v, v + 8, v + 8, v + 9, v + 17)))
    return out


def spec_cells():
    return cells_grid() + cells_strips() + cells_panels()


def profiles(a, cell):
    """Ring colours sampled from the original: left/right/top/bottom, outermost first, plus the interior colour."""
    c0, c1, cm, c2, c3 = cell['cols']
    r0, r1, rm, r2, r3 = cell['rows']
    f = a.astype(np.float32) / 255
    left = f[rm, c0:c1]
    right = f[rm, c2:c3][::-1]
    top = f[r0:r1, cm]
    bottom = f[r2:r3, cm][::-1]
    return left, right, top, bottom, f[rm, cm]


def depth_in(xs, ys, x0, y0, x1, y1, R):
    """Inward distance from the rounded rectangle [x0,x1] x [y0,y1] (negative outside), and the mitre side
    (0 left, 1 right, 2 top, 3 bottom) whose ring colour applies at each point."""
    dL, dR, dT, dB = xs - x0, x1 - xs, ys - y0, y1 - ys
    R = max(R, 0.25)
    d = np.minimum(np.minimum(dL, dR), np.minimum(dT, dB))
    cx = np.clip(xs, x0 + R, x1 - R)
    cy = np.clip(ys, y0 + R, y1 - R)
    dd = np.hypot(xs - cx, ys - cy)
    corner = dd > 0
    d = np.where(corner, R - dd, d)
    ex = np.where(xs < x0 + R, x0 + R - xs, np.where(xs > x1 - R, xs - (x1 - R), -1e9))
    ey = np.where(ys < y0 + R, y0 + R - ys, np.where(ys > y1 - R, ys - (y1 - R), -1e9))
    side_x = np.where(xs < (x0 + x1) / 2, 0, 1)
    side_y = np.where(ys < (y0 + y1) / 2, 2, 3)
    straight = np.argmin(np.stack([dL, dR, dT, dB]), axis=0)
    side = np.where(corner & (ex >= 0) & (ey >= 0), np.where(ex >= ey, side_x, side_y), straight)
    return d, side


def render_cell(a, cell):
    """Virtual (l+1+r) x (t+1+b) texel picture, at K texels... K*S pixels per texel, reduced to K; premultiplied RGBA."""
    c0, c1, cm, c2, c3 = cell['cols']
    r0, r1, rm, r2, r3 = cell['rows']
    l, r_ = c1 - c0, c3 - c2
    t, b = r1 - r0, r3 - r2
    W, H = l + 1 + r_, t + 1 + b
    profs = profiles(a, cell)[:4]
    inner = profiles(a, cell)[4]
    R = cell['radius']
    n = K * S
    ys, xs = np.meshgrid((np.arange(H * n) + 0.5) / n, (np.arange(W * n) + 0.5) / n, indexing='ij')
    def shadow(p):          # a translucent black first texel is the drop shadow (right / bottom only)
        return int(p[0][3] < 0.99 and p[0][:3].sum() < 0.05)
    er, eb = shadow(profs[1]), shadow(profs[3])
    sidecol = [profs[0], profs[1][er:], profs[2], profs[3][eb:]]
    out = np.zeros((H * n, W * n, 4), np.float32)
    if er or eb:
        sd, _ = depth_in(xs, ys, er, eb, W, H, R)
        sh = np.concatenate([profs[1][:1], profs[3][:1]]).mean(0) if er and eb else (profs[1][0] if er else profs[3][0])
        out[sd > 0] = sh
    x1, y1 = W - er, H - eb
    d, side = depth_in(xs, ys, 0, 0, x1, y1, R)
    inside = d > 0
    # the ring k of a point is its (floored) inset depth; deeper than the profile -> interior colour
    k = np.floor(d).astype(int)
    for sid in range(4):
        prof = sidecol[sid]
        kk = np.clip(k, 0, len(prof) - 1)
        col = prof[kk]
        m = (side == sid) & inside
        out[m] = col[m]
    out[..., :3] *= out[..., 3:4]
    return out.reshape(H * K, S, W * K, S, 4).mean((1, 3)), (W, H)


def paste(sheet, tile, cell, size):
    """Writes the virtual tile into the 8x sheet over the whole block [c0, c3) x [r0, r3), filling the skipped texels with the
    stretched centre."""
    c0, c1, cm, c2, c3 = cell['cols']
    r0, r1, rm, r2, r3 = cell['rows']
    l, t = c1 - c0, r1 - r0
    colmap = [(c - c0) if c < c1 else (l if c < c2 else l + 1 + (c - c2)) for c in range(c0, c3)]
    rowmap = [(r - r0) if r < r1 else (t if r < r2 else t + 1 + (r - r2)) for r in range(r0, r3)]
    for ri, rv in zip(range(r0, r3), rowmap):
        for ci, cv in zip(range(c0, c3), colmap):
            blk = tile[rv * K:(rv + 1) * K, cv * K:(cv + 1) * K]
            sheet[ri * K:(ri + 1) * K, ci * K:(ci + 1) * K] = blk


def main():
    sheets = {}
    for cell in spec_cells():
        h = cell['sheet']
        orig = np.asarray(Image.open(os.path.join(DUMP, h + '.png')).convert('RGBA'))
        if h not in sheets:
            base = None
            for mod in MODS:
                p = os.path.join(mod, 'textures', BASE[h])
                if os.path.exists(p):
                    base = p
                    break
            if base is None:
                sys.exit('missing base sheet for ' + h)
            im = np.asarray(Image.open(base).convert('RGBA'), np.float32) / 255
            im[..., :3] *= im[..., 3:4]
            sheets[h] = im
        tile, size = render_cell(orig, cell)
        paste(sheets[h], tile, cell, size)
    for h, im in sheets.items():
        a = im[..., 3:4]
        rgb = np.where(a > 1e-4, im[..., :3] / np.maximum(a, 1e-4), 0)
        out = np.clip(np.dstack([rgb, a]) * 255 + 0.5, 0, 255).astype(np.uint8)
        for mod in MODS:
            if not os.path.isdir(mod):
                continue
            dst = os.path.join(mod, 'textures', OUTDIR, h + '.png')
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            Image.fromarray(out, 'RGBA').save(dst)
            print('wrote', dst)
            if '--register' in sys.argv:
                register(mod, h)


def register(mod, h):
    rel = OUTDIR + '/' + h + '.png'
    for name, sep in (('textures/textures.ini', ' = '), ('origem.tsv', '\t')):
        p = os.path.join(mod, name)
        if not os.path.exists(p):
            continue
        lines = open(p, encoding='utf-8', newline='').read().split('\n')
        for i, l in enumerate(lines):
            if h not in l:
                continue
            if name.endswith('.ini'):
                k, v = l.split(sep, 1)
                if not k.endswith(h):
                    continue
                lines[i] = k + sep + rel + ('\r' if v.endswith('\r') else '')
            else:
                t = l.rstrip('\r').split('\t')
                if t[0] != h:
                    continue
                t[2], t[3] = 'MenuFramesHD', rel
                lines[i] = '\t'.join(t) + ('\r' if l.endswith('\r') else '')
        open(p, 'w', encoding='utf-8', newline='').write('\n'.join(lines))
        print('registered', h, 'in', p)


if __name__ == '__main__':
    main()
