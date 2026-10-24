"""Redraws the Duel Result screen for EnhancedTextures instead of upscaling it (original colours, no recolour).

Pieces (game texture hash -> output in textures/duel_result_hd/):
  20b3dd72  checkered background: 8x8 flat squares, each one exactly the colour of the centre of the pack cell (no wavy edges).
  f6f6c44f  title sheet (DUEL RESULT, DUEL BONUS, NEW HIGH SCORE BONUS, CHALLENGE COMPLETE BONUS, INTERMEDIATE RESULT, FINAL RESULT):
            every line is drawn with Arial Black fitted into the ink box of the original line; the white-to-grey vertical shading of the
            face is sampled row by row from the original, plus a soft dark drop shadow.
  b602d300  table sheet: the columns/rows the game stretches over the screen (separator above Total, row dividers) are made uniform so the
            upscale noise cannot show as stripes; colours are the original ones.
  tall pill pieces (the palette-specific rows of this screen): the same analytic pill as the menus (oval_hd/, see mod_menu_oval.py).

Usage: python tools/mods/mod_duel_result.py [--register]
"""
import os
import re
import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
MODS = [os.path.join(ROOT, 'mods', 'EnhancedTextures'), os.path.join(ROOT, 'gen', 'mods', 'EnhancedTextures')]
OUT = 'duel_result_hd'
FONT = 'C:/Windows/Fonts/ariblk.ttf'
K = 8
LINES = ['DUEL RESULT', 'DUEL BONUS', 'NEW HIGH SCORE BONUS', 'CHALLENGE COMPLETE BONUS', 'INTERMEDIATE RESULT', 'FINAL RESULT']

SRC_BG = 'mikeymk/ui/duels/results_background.png'
SRC_TITLE = 'mikeymk/ui/text/duel_result.png'
SRC_TABLE = 'mikeymk/extras/00000000658d9d5fb602d300.png'
# parts of the sheet the game stretches (measured with the tf.py texdump command): (u, v0, v1) columns and (v, u0, u1) rows, in PSP texels
COLUMNS = [(10, 0, 47), (24, 0, 16), (25, 16, 19), (27, 0, 16)]
ROWS = [(20, 0, 6), (20, 17, 23), (18, 23, 25)]
# every hash -> (source key used by the pack, new file)
TARGETS = {
    'b602d300': OUT + '/table_sheet.png',
    '20b3dd72': OUT + '/results_background.png',
    'f6f6c44f': OUT + '/duel_result.png',
    '85169591': 'oval_hd/oval_tall_left.png',
    '1320facf': 'oval_hd/oval_tall_center.png',
    '87f4af60': 'oval_hd/oval_tall_right.png',
    '59cfec77': 'oval_hd/oval_transparent_tall_left.png',
    'b441d53d': 'oval_hd/oval_transparent_tall_center.png',
    '9d15ec20': 'oval_hd/oval_transparent_tall_right.png',
}


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


def table(a):
    out = a.astype(np.float32)
    for u, v0, v1 in COLUMNS:
        b = out[v0 * K:v1 * K, u * K:(u + 1) * K]
        out[v0 * K:v1 * K, u * K:(u + 1) * K] = b.mean(axis=1, keepdims=True)
    for v, u0, u1 in ROWS:
        b = out[v * K:(v + 1) * K, u0 * K:u1 * K]
        out[v * K:(v + 1) * K, u0 * K:u1 * K] = b.mean(axis=0, keepdims=True)
    return np.round(out).astype(np.uint8)


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
