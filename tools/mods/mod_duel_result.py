"""Redraws the Duel Result screen for EnhancedTextures instead of upscaling it (original colours, no recolour).

Pieces (game texture hash -> output in textures/duel/pc/):
  20b3dd72  checkered background: 8x8 flat squares, each one exactly the colour of the centre of the pack cell (no wavy edges).
  f6f6c44f  title sheet (DUEL RESULT, DUEL BONUS, NEW HIGH SCORE BONUS, CHALLENGE COMPLETE BONUS, INTERMEDIATE RESULT, FINAL RESULT):
            every line is drawn with Arial Black fitted into the ink box of the original line; the white-to-grey vertical shading of the
            face is sampled row by row from the original, plus a soft dark drop shadow.
  b602d300  table sheet: the flat bands of the frame (stretched columns/rows, row strips) are rebuilt as exact flat texels with hard straight
            edges, and the dotted ends of the line above Total are replaced by the plain line; colours are the original ones.
  tall pill pieces (the palette-specific rows of this screen): the same analytic pill as the menus (menus/pc/, see mod_menu_oval.py).

Usage: python tools/mods/mod_duel_result.py [--register]
"""
import os
import re
import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
MODS = [os.path.join(ROOT, 'mods', 'EnhancedTextures'), os.path.join(ROOT, 'gen', 'mods', 'EnhancedTextures')]
OUT = 'duel/pc'
FONT = 'C:/Windows/Fonts/ariblk.ttf'
K = 8
LINES = ['DUEL RESULT', 'DUEL BONUS', 'NEW HIGH SCORE BONUS', 'CHALLENGE COMPLETE BONUS', 'INTERMEDIATE RESULT', 'FINAL RESULT']

SRC_BG = '_unreferenced/mikeymk__ui__duels__results_background.png'
SRC_TITLE = '_unreferenced/mikeymk__ui__text__duel_result.png'
SRC_TABLE = '_unreferenced/mikeymk__extras__00000000658d9d5fb602d300.png'
# parts of the sheet that are flat bands (stretched or tiled by the game; measured with the tf.py texdump command), in PSP texels:
# (u0, u1, v0, v1) half-open. Each texel becomes an exact flat 8x8 block (median of its inner 4x4), so every band edge is a hard straight line.
FLAT = [(10, 11, 0, 47),    # header / bottom stretch column
        (0, 6, 20, 21), (17, 23, 20, 21),    # left / right wall rows
        (23, 29, 0, 19)]    # row strip: cell borders, cream interior, DP cell, joints between rows
# every game texture hash -> new file (relative to textures/)
TARGETS = {
    'b602d300': OUT + '/table_sheet.png',
    '20b3dd72': OUT + '/results_background.png',
    'f6f6c44f': OUT + '/duel_result.png',
    '85169591': 'menus/pc/oval_tall_left.png',
    '1320facf': 'menus/pc/oval_tall_center.png',
    '87f4af60': 'menus/pc/oval_tall_right.png',
    '59cfec77': 'menus/pc/oval_transparent_tall_left.png',
    'b441d53d': 'menus/pc/oval_transparent_tall_center.png',
    '9d15ec20': 'menus/pc/oval_transparent_tall_right.png',
}

JOINTS = [(23, 16), (33, 16), (23, 26), (33, 26)]   # (u, v) of the four corner joints of the row area


def checkerboard(a, cells=8):
    h, w = a.shape[:2]
    ch, cw = h // cells, w // cells
    out = a.copy()
    for ty in range(cells):
        for tx in range(cells):
            out[ty * ch:(ty + 1) * ch, tx * cw:(tx + 1) * cw] = a[ty * ch + ch // 2, tx * cw + cw // 2]
    out[..., 3] = 255
    return out


def line_boxes(a):
    face = (a[..., :3].min(2) > 110) & (a[..., 3] > 200)
    rows = np.where(face.any(1))[0]
    groups, s, p = [], rows[0], rows[0]
    for r in rows[1:]:
        if r != p + 1:
            groups.append((s, p)); s = r
        p = r
    groups.append((s, p))
    out = []
    for s, e in groups:
        cols = np.where(face[s:e + 1].any(0))[0]
        out.append((cols.min(), s, cols.max() + 1, e + 1))
    return out, face


def text_mask(text):
    big = Image.new('L', (6000, 600), 0)
    ImageDraw.Draw(big).text((100, 50), text, font=ImageFont.truetype(FONT, 300), fill=255)
    return big.crop(big.getbbox())


def gradient(a, face, box):
    """Median colour of the face pixels of each row of the original line (vertical shading), as float 0..1."""
    x0, y0, x1, y1 = box
    rows = []
    for y in range(y0, y1):
        m = face[y, x0:x1]
        rows.append(np.median(a[y, x0:x1][m][:, :3], axis=0) if m.sum() > 20 else None)
    last = np.array([255, 255, 255.0])
    for i, r in enumerate(rows):
        if r is None:
            rows[i] = last
        last = rows[i]
    return np.array(rows, np.float32) / 255


def title(a):
    boxes, face = line_boxes(a)
    assert len(boxes) == len(LINES), boxes
    H, W = a.shape[:2]
    mask = Image.new('L', (W, H), 0)
    color = np.zeros((H, W, 3), np.float32)
    for text, box in zip(LINES, boxes):
        x0, y0, x1, y1 = box
        m = text_mask(text).resize((x1 - x0, y1 - y0), Image.LANCZOS)
        mask.paste(m, (x0, y0))
        g = gradient(a, face, box)
        color[y0:y1] = g[:, None, :]
    f = np.asarray(mask, np.float32) / 255
    sh = np.asarray(mask.filter(ImageFilter.GaussianBlur(K * 0.5)), np.float32) / 255
    sh = np.clip(np.roll(np.roll(sh * 1.5, K, axis=0), K, axis=1), 0, 1) * 0.85
    alpha = f + sh * (1 - f)
    rgb = color * (f / np.maximum(alpha, 1e-4))[..., None]
    return (np.dstack([np.clip(rgb, 0, 1), np.clip(alpha, 0, 1)]) * 255 + 0.5).astype(np.uint8)


def texel(a, u, v):
    return np.median(a[v * K + 2:v * K + 6, u * K + 2:u * K + 6].reshape(-1, 4), axis=0)


def _gauss(a, sig):
    r = int(sig * 3) + 1
    x = np.arange(-r, r + 1)
    k = np.exp(-x * x / (2.0 * sig * sig)); k /= k.sum()
    for ax in (0, 1):
        pad = [(0, 0), (0, 0)]; pad[ax] = (r, r)
        b = np.pad(a, pad, mode='edge')
        n = a.shape[ax]
        a = sum(k[i] * (b[i:i + n] if ax == 0 else b[:, i:i + n]) for i in range(2 * r + 1))
    return a


def _lerp_col(vals, y):
    """Piecewise-linear interpolation through the texel centres (y in texels)."""
    return np.interp(y, np.arange(len(vals)) + 0.5, vals)


def door(a):
    """The table frame (23 x 47 texels of the sheet, drawn at K per texel) rebuilt from geometry: left wall with the slanted corner, dark
    header panel, line above Total, right wall and soft shadow. Bands are exact (hard straight edges), the slanted edges and the rounded outer
    corners are antialiased; colours come from the original texels."""
    S = 4
    R = K * S
    W, H = 23, 47
    x = ((np.arange(W * R) + 0.5) / R)[None, :]
    y = ((np.arange(H * R) + 0.5) / R)[:, None]
    g = lambda u, v: float(texel(a, u, v)[:3].mean())
    # silhouette: slanted left edge, notched top-right and bottom-left corners, then rounded
    ol = 5.0 - 0.5556 * y
    ol = 0.5 * (ol + np.sqrt(ol * ol + 0.5))            # smooth max(ol, 0): the slanted edge bends into the vertical wall
    right = np.where(y < 1, 21.0, 22.0)
    bottom = np.where(x < 1, 45.0, 46.0)
    solid = ((x >= ol) & (x < right) & (y < bottom)).astype(np.float32)
    solid = np.clip((_gauss(solid, 0.4 * R) - 0.5) * (R * 0.5) + 0.5, 0, 1)   # rounded convex corners with a ~1 px soft edge
    # vertical profile of the body (column u=10 of the sheet); header rows get the plain grey beside the dark panel
    vb = np.array([g(10, v) for v in range(H)], np.float32)
    body = vb.copy(); body[3:17] = g(18, 8)
    rows = np.minimum(y.astype(int), H - 1)
    col = body[rows] * np.ones_like(x)                    # (H*R, W*R)
    # line above Total: only between x=4 and x=18, the rest of those rows is plain grey
    plain = g(10, 22)
    slot = (rows >= 23) & (rows <= 25) & ((x < 4) | (x >= 18))
    col = np.where(slot, plain, col)
    # dark header panel: left edge follows the slanted corner, 4 texels in from the wall below it
    pl = np.maximum(4.0, 8.0 - 0.5 * (y - 3.0))
    panel = (y >= 3) & (y < 17) & (x >= pl) & (x < 18)
    col = np.where(panel, 0.5 * (g(10, 3) + g(10, 10)), col)
    # right wall: two grey texels, then the dark outline columns (shaded along y)
    cy = [g(20, v) for v in range(H)]; cz = [g(21, v) for v in range(H)]
    c20 = _lerp_col(cy, y) * np.ones_like(x); c21 = _lerp_col(cz, y) * np.ones_like(x)
    top = y < 3
    col = np.where((x >= 18) & (x < 19) & ~top, g(18, 8), col)
    col = np.where((x >= 19) & (x < 20) & ~top, g(19, 8), col)
    col = np.where((x >= 20) & (x < 21), c20, col)
    col = np.where((x >= 21) & (x < 22), c21, col)
    # left wall in coordinates sheared along the slanted edge: black outline, bright edge (narrower on the slant), then the body
    s = x - ol
    hw = np.clip(0.55 + 0.45 * (y - 7.0) / 2.5, 0.55, 1.0)
    hl = _lerp_col([g(1, min(max(v, 9), 43)) for v in range(H)], y) * np.ones_like(x)
    hl = np.where(y < 9, 205.0, hl)
    dark = (s < 1.5 - hw / 2) & ~(y < 1)
    edge = (np.abs(s - 1.5) <= hw / 2) & ~(y < 1)
    wall = s < 3.0
    col = np.where(wall & ~(y < 1), np.where(dark, 4.0 + 12.0 * np.clip((5.0 - y) / 5.0, 0, 1), np.where(edge, hl, np.where(top, col, g(3, 20)))), col)
    # bottom rows: dark outline then shadow row
    col = np.where((y >= 44) & (y < 45), g(10, 44), col)
    col = np.where((y >= 45), g(10, 45), col)
    # soft shadow right of the wall
    sh = _lerp_col([texel(a, 22, v)[3] for v in range(H)], y) * np.ones_like(x)
    alpha = np.where(x >= 22, np.where(x < 23, sh / 255.0, 0.0), solid)
    rgb = np.where(x >= 22, 0.0, col)
    pm = np.stack([rgb * alpha, rgb * alpha, rgb * alpha, alpha], -1)
    pm = pm.reshape(H * K, S, W * K, S, 4).mean(axis=(1, 3))
    al = pm[..., 3:4]
    grey = np.where(al > 1e-4, pm[..., :3] / np.maximum(al, 1e-4), 0)
    return np.dstack([np.clip(grey, 0, 255), np.clip(al * 255, 0, 255)]).round().astype(np.uint8)


# WIN / DRAW / LOSE below the doors of the table sheet: (word, face colour, ink box x0, y0, x1, y1 in sheet pixels at 8x)
WORDS = [('WIN', (255, 254, 0), 99, 390, 324, 492), ('DRAW', (202, 0, 97), 461, 387, 832, 494), ('LOSE', (38, 81, 255), 908, 388, 1250, 493)]


def words(out):
    out[384:500, 80:1262] = 0
    for text, color, x0, y0, x1, y1 in WORDS:
        w, h = x1 - x0, y1 - y0
        m = text_mask(text).resize((w, h), Image.LANCZOS)
        face = Image.new('L', (w + 40, h + 40), 0); face.paste(m, (20, 20))
        f = np.asarray(face, np.float32) / 255
        under = np.asarray(face.filter(ImageFilter.MaxFilter(5)).filter(ImageFilter.GaussianBlur(1.5)), np.float32) / 255
        under = np.roll(np.roll(under, 4, axis=0), 3, axis=1) * 0.85           # dark outline + offset shadow, as in the original
        alpha = f + under * (1 - f)
        rgb = np.array(color, np.float32) * (f / np.maximum(alpha, 1e-4))[..., None]
        patch = np.dstack([rgb, alpha * 255]).clip(0, 255).round().astype(np.uint8)
        hh = min(patch.shape[0], out.shape[0] - (y0 - 20))
        out[y0 - 20:y0 - 20 + hh, x0 - 20:x0 - 20 + patch.shape[1]] = patch[:hh]


def table(a):
    out = a.copy()
    words(out)
    out[0:47 * K, 0:23 * K] = door(a)
    for u0, u1, v0, v1 in FLAT:
        for v in range(v0, v1):
            for u in range(u0, u1):
                if u0 == 10 or u0 == 0 or u0 == 17:
                    continue                       # the stretched column and wall rows come from the redrawn frame
                c = texel(a, u, v).copy()
                if u0 == 23:
                    c = texel(a, u, 1 if v == 0 else (14 if v == 15 and u < 26 else v)).copy()
                    c[3] = 255                      # no notched corners on the row cells
                out[v * K:(v + 1) * K, u * K:(u + 1) * K] = c
    plain = texel(a, 24, 17).copy(); plain[3] = 255
    for u, v in JOINTS:   # 2x2 corner pieces of the row area (notched, bevelled in the original): plain grey squares
        out[v * K:(v + 2) * K, u * K:(u + 2) * K] = plain
    return out


def register(mod, files):
    for name in ('textures/textures.ini', 'origem.tsv'):
        p = os.path.join(mod, name)
        if not os.path.exists(p):
            continue
        s = open(p, encoding='utf-8', newline='').read()
        n = 0
        for h, rel in files.items():
            if name.endswith('.ini'):
                s, k = re.subn(r'(?m)^([0-9a-f]{16}' + h + r') = [^\r\n]*', lambda m: m.group(1) + ' = ' + rel, s)
            else:
                k = 0
            n += k
        open(p, 'w', encoding='utf-8', newline='').write(s)
        print('registered', n, 'rows in', p)


def main():
    for mod in MODS:
        tex = os.path.join(mod, 'textures')
        if not os.path.isdir(tex):
            continue
        os.makedirs(os.path.join(tex, OUT), exist_ok=True)
        bg = np.asarray(Image.open(os.path.join(tex, SRC_BG)).convert('RGBA'))
        Image.fromarray(checkerboard(bg), 'RGBA').save(os.path.join(tex, TARGETS['20b3dd72']), optimize=True)
        tt = np.asarray(Image.open(os.path.join(tex, SRC_TITLE)).convert('RGBA'))
        Image.fromarray(title(tt), 'RGBA').save(os.path.join(tex, TARGETS['f6f6c44f']), optimize=True)
        tb = np.asarray(Image.open(os.path.join(tex, SRC_TABLE)).convert('RGBA'))
        Image.fromarray(table(tb), 'RGBA').save(os.path.join(tex, TARGETS['b602d300']), optimize=True)
        print('wrote', tex)
        if '--register' in sys.argv:
            register(mod, TARGETS)


if __name__ == '__main__':
    main()
