"""Redraws the "first/second to go" pills and their labels (duel start, after rock-paper-scissors) at 8x for EnhancedTextures.

The game draws each pill from four 256x32 textures (the selected one is tinted gold, the other grey, by the vertex colour):
  8d64251c  silver tube (tinted)            b3a147df  gloss layer (added on top: top highlight + thin lower reflection)
  ea15151f  "FIRST TO GO" label             3d0789ee  "SECOND TO GO" label
Instead of upscaling them this script draws the same shapes analytically at 2048x256: a stadium (SDF) shaded like a glossy tube,
the gloss layer inside it, and the labels in Arial Black fitted into the face box of the originals (black face with a light
bevel, white outline). Geometry is in the originals' 256x32 grid: pill x 17..239, y 1..30 (outline included).

Output: textures/menus/pc/<hash>.png (+ --register repoints every textures.ini / origem.tsv row of the four hashes).
Usage: python tools/mods/mod_menu_pills.py [--register]
"""
import os
import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
FONT = 'C:/Windows/Fonts/ariblk.ttf'
K = 8
W, H = 256 * K, 32 * K
OUTDIR = 'menus/pc'
# stadium in the 256x32 grid
X0, X1, Y0, Y1 = 17.0, 239.0, 1.0, 30.0
CY, R = (Y0 + Y1) / 2, (Y1 - Y0) / 2
# label face boxes (x0, y0, x1, y1) in the 256x32 grid, measured on the originals
LABELS = {'ea15151f': ('FIRST TO GO', (64, 9, 193, 24)), '3d0789ee': ('SECOND TO GO', (45, 9, 211, 24))}
OUTLINE = 1.2   # white outline width, in grid pixels
HASHES = ['8d64251c', 'b3a147df', 'ea15151f', '3d0789ee']


def sstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def grid():
    ys, xs = np.mgrid[0:H, 0:W].astype(np.float32)
    return (xs + 0.5) / K, (ys + 0.5) / K


def stadium_sdf(x, y, x0=X0, x1=X1, cy=CY, r=R):
    """Signed distance (outside > 0) of a stadium with caps of radius r centred at x0+r and x1-r."""
    dx = np.maximum(np.maximum(x0 + r - x, x - (x1 - r)), 0)
    return np.hypot(dx, y - cy) - r, dx


def lerp_profile(yy, rows, vals):
    """Piecewise-linear (smoothed) profile sampled at the rows of the original (row centres at r + 0.5)."""
    return np.interp(yy, np.asarray(rows, np.float32) + 0.5, vals)


def base():
    """Silver tube. Vertical profile taken from the original centre column (bright band near the top, dip in the middle,
    second lighter band, dark lower edge); the caps darken towards the rim so the ends read as round."""
    x, y = grid()
    d, dx = stadium_sdf(x, y)
    rows = list(range(1, 30))
    vals = [0, 114, 166, 198, 217, 228, 233, 233, 228, 218, 203, 183, 163, 145, 130, 133, 140, 149, 160, 176, 176, 174, 173, 164,
            153, 132, 97, 71, 0]
    g = lerp_profile(y, rows, np.array(vals, np.float32)) / 255
    u = -d
    cap = sstep(0.0, R, dx)                                        # 0 on the straight part, 1 at the end of the cap (smooth, no notch where the cap starts)
    rim = 1 - cap * (1 - sstep(0.4, 4.2, u)) * 0.62                # darker rim round the caps
    g = g * rim
    ol = 1 - sstep(1.0 - 1.0 / K, 1.0 + 1.0 / K, u)                # 1 px black outline
    g = g * (1 - ol * 0.97)
    a = sstep(1.0 / K, -1.0 / K, d)
    return np.dstack([g, g, g, a])


def gloss():
    """Gloss layer (added over the tube): bell-shaped highlight on the top half, thin reflection near the bottom; black elsewhere."""
    x, y = grid()
    d, _ = stadium_sdf(x, y)
    body = sstep(1.0 / K, -1.0 / K, d)
    out = np.full(x.shape, 2 / 255, np.float32)
    # top highlight: capsule x 24..231, rows 3..14, brightness 202 at the middle, soft sides
    t = np.clip((y - 3.0) / 11.0, 0, 1)
    bell = np.clip(np.sin(np.pi * t), 0, 1) ** 0.8
    dh, _ = stadium_sdf(x, y, 24.0, 231.0, 8.5, 5.5)
    top = sstep(0.8, -0.2, dh) * bell * (202 / 255)
    # lower reflection: lens x 23..232, rows 19.5..25, brightness ~65
    t2 = np.clip((y - 19.0) / 6.0, 0, 1)
    lens = np.clip(np.sin(np.pi * t2), 0, 1) ** 0.6
    dl, _ = stadium_sdf(x, y, 23.0, 232.0, 22.0, 3.0)
    low = sstep(0.8, -0.2, dl) * lens * (66 / 255)
    g = np.clip(out + top + low, 0, 1)
    return np.dstack([g, g, g, body])


def edt(mask):
    """Exact Euclidean distance (pixels) from every True pixel to the nearest False pixel (Felzenszwalb, separable)."""
    inf = 1e12

    def pass1d(f):
        n = len(f)
        v = np.zeros(n, int)
        z = np.zeros(n + 1)
        k = 0
        z[0], z[1] = -inf, inf
        for q in range(1, n):
            while True:
                s = ((f[q] + q * q) - (f[v[k]] + v[k] ** 2)) / (2 * q - 2 * v[k])
                if s <= z[k] and k > 0:
                    k -= 1
                    continue
                break
            if s <= z[k]:
                v[k] = q
                z[k], z[k + 1] = -inf, inf
            else:
                k += 1
                v[k] = q
                z[k], z[k + 1] = s, inf
        out = np.empty(n)
        k = 0
        for q in range(n):
            while z[k + 1] < q:
                k += 1
            out[q] = (q - v[k]) ** 2 + f[v[k]]
        return out

    f = np.where(mask, inf, 0.0)
    f = np.apply_along_axis(pass1d, 0, f)
    f = np.apply_along_axis(pass1d, 1, f)
    return np.sqrt(np.minimum(f, inf))


def label(text, box):
    """Black face with a light bevel and a white outline, the text fitted into `box` (grid pixels, face only)."""
    big = Image.new('L', (4000, 500), 0)
    ImageDraw.Draw(big).text((100, 50), text, font=ImageFont.truetype(FONT, 200), fill=255)
    m = big.crop(big.getbbox())
    x0, y0, x1, y1 = box
    m = m.resize(((x1 - x0) * K, (y1 - y0) * K), Image.LANCZOS)
    face = Image.new('L', (W, H), 0)
    face.paste(m, (x0 * K, y0 * K))
    f = np.asarray(face, np.float32) / 255
    fm = f > 0.5
    din = edt(fm) / K                                  # depth inside the face, grid pixels
    dout = edt(~fm) / K                                # distance outside the face
    sd = np.where(fm, -din, dout)                      # signed distance to the face edge
    aa = 1.0 / K
    face_a = sstep(aa, -aa, sd)
    ring_a = sstep(OUTLINE + aa, OUTLINE - aa, sd)     # face + outline
    # bevel: light edge on the top-left inside the face, from the gradient of the signed distance
    gy, gx = np.gradient(np.asarray(Image.fromarray((sd * 40 + 128).clip(0, 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(2)), np.float32))
    nrm = np.maximum(np.hypot(gx, gy), 1e-3)
    light = np.clip(-(gx * -0.6 + gy * -0.8) / nrm, 0, 1)   # outward normal facing up-left
    bevel = (1 - sstep(0.0, 1.3, din)) * light * 0.34
    grey = bevel * face_a                              # face is black plus the bevel
    rgb = grey * face_a + 1.0 * (1 - face_a)           # white outline outside the face
    return np.dstack([rgb, rgb, rgb, ring_a])


def save(arr, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    Image.fromarray((np.clip(arr, 0, 1) * 255 + 0.5).astype(np.uint8), 'RGBA').save(dst)
    print('wrote', dst)


def register(mod):
    for name in ('textures/textures.ini', 'origem.tsv'):
        p = os.path.join(mod, name)
        if not os.path.exists(p):
            continue
        lines = open(p, encoding='utf-8', newline='').read().split('\n')
        for i, l in enumerate(lines):
            cr = '\r' if l.endswith('\r') else ''
            for h in HASHES:
                rel = '%s/%s.png' % (OUTDIR, h)
                if name.endswith('.ini'):
                    k, sep, v = l.partition(' = ')
                    if sep and k.endswith(h):
                        lines[i] = k + sep + rel + cr
                else:
                    t = l.rstrip('\r').split('\t')
                    if len(t) >= 4 and t[0] == h:
                        t[2], t[3] = 'MenuPillsHD', rel
                        lines[i] = '\t'.join(t) + cr
        open(p, 'w', encoding='utf-8', newline='').write('\n'.join(lines))
        print('registered in', p)


def main():
    imgs = {'8d64251c': base(), 'b3a147df': gloss()}
    for h, (text, box) in LABELS.items():
        imgs[h] = label(text, box)
    if '--preview' in sys.argv:
        out = sys.argv[sys.argv.index('--preview') + 1]
        # approximate in-game composite: gold tube + gloss (added) + label
        b, g_, t1, t2 = imgs['8d64251c'], imgs['b3a147df'], imgs['ea15151f'], imgs['3d0789ee']
        bg = np.zeros((H * 2 + 40, W, 3), np.float32) + np.array([0.05, 0.2, 0.2], np.float32)
        for i, (tint, tx) in enumerate(((np.array([1, 0.8, 0.0]), t1), (np.array([0.49, 0.6, 0.61]), t2))):
            y0 = i * (H + 20)
            reg = bg[y0:y0 + H]
            reg[:] = reg * (1 - b[..., 3:4]) + b[..., :3] * tint * b[..., 3:4]
            reg[:] = np.clip(reg + g_[..., :3] * g_[..., 3:4], 0, 1)
            lt = np.array([1, 1, 1]) if i == 0 else np.array([0.49] * 3)
            reg[:] = reg * (1 - tx[..., 3:4]) + tx[..., :3] * lt * tx[..., 3:4]
        Image.fromarray((bg * 255).astype(np.uint8)).save(out)
        print('preview', out)
        return
    for mod in (os.path.join(ROOT, 'mods', 'EnhancedTextures'), os.path.join(ROOT, 'gen', 'mods', 'EnhancedTextures')):
        if not os.path.isdir(mod):
            continue
        for h, arr in imgs.items():
            save(arr, os.path.join(mod, 'textures', OUTDIR, h + '.png'))
        if '--register' in sys.argv:
            register(mod)


if __name__ == '__main__':
    main()
