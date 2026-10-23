"""Redraws the menu title sheet (texture 1d377d3b, 256x128: MAIN MENU, ENTER NAME, DOWNLOADED CARD LIST, DUEL PUZZLE, OPTIONS,
LANGUAGE SETTINGS) at 8x for the EnhancedTextures mod instead of upscaling it.

Input: the original texture dumped with TF_TEX_DUMP (gen/dump_tex/mainmenu/faltam/1d377d3b.png).
Output: textures/menu_titles_hd/1d377d3b.png in the mod (the textures.ini / origem.tsv rows are repointed by --register).
Each line is drawn with Arial Black (upright), fitted exactly into the ink box of the original line
(same position, cap height and width), yellow face with a light vertical shading and a dark offset shadow.

Usage: python tools/mods/mod_menu_titles.py [--register]
"""
import os
import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
SRC = os.path.join(ROOT, 'gen', 'dump_tex', 'mainmenu', 'faltam', '1d377d3b.png')
FONT = 'C:/Windows/Fonts/ariblk.ttf'
K = 8
LINES = ['MAIN MENU', 'ENTER NAME', 'DOWNLOADED CARD LIST', 'DUEL PUZZLE', 'OPTIONS', 'LANGUAGE SETTINGS']
SHADOW = np.array([120, 118, 30], np.float32) / 255
SHEAR = 0.0   # the original letters are upright
TOP = np.array([255, 255, 135], np.float32) / 255      # light vertical shading of the face, per line
BOTTOM = np.array([238, 236, 52], np.float32) / 255
REL = 'menu_titles_hd/1d377d3b.png'


def line_boxes(a):
    face = (a[..., 0] >= 225) & (a[..., 3] > 200)
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
    return out


def sheared_mask(text):
    big = Image.new('L', (4000, 500), 0)
    ImageDraw.Draw(big).text((100, 50), text, font=ImageFont.truetype(FONT, 200), fill=255)
    bb = big.getbbox()
    c = big.crop(bb)
    pad = int(c.height * SHEAR) + 2
    c2 = Image.new('L', (c.width + pad, c.height), 0)
    c2.paste(c, (0, 0))
    # x' = x + SHEAR * (h - y): the top leans right
    if SHEAR:
        c2 = c2.transform(c2.size, Image.AFFINE, (1, SHEAR, -SHEAR * c.height, 0, 1, 0), Image.BICUBIC)
    return c2.crop(c2.getbbox())


def main():
    a = np.asarray(Image.open(SRC).convert('RGBA'))
    boxes = line_boxes(a)
    assert len(boxes) == len(LINES), boxes
    H, W = a.shape[0] * K, a.shape[1] * K
    face = Image.new('L', (W, H), 0)
    color = np.zeros((H, W, 3), np.float32)
    for text, (x0, y0, x1, y1) in zip(LINES, boxes):
        m = sheared_mask(text)
        m = m.resize(((x1 - x0) * K, (y1 - y0) * K), Image.LANCZOS)
        face.paste(m, (x0 * K, y0 * K))
        t = np.linspace(0, 1, (y1 - y0) * K, dtype=np.float32)[:, None, None]
        color[y0 * K:y1 * K] = np.broadcast_to((1 - t) * TOP + t * BOTTOM, ((y1 - y0) * K, W, 3))
    f = np.asarray(face, np.float32) / 255
    sh = np.asarray(face.filter(ImageFilter.GaussianBlur(3)), np.float32) / 255
    sh = np.clip(np.roll(np.roll(sh * 1.6, K, axis=0), K, axis=1), 0, 1)
    alpha = f + sh * (1 - f)
    rgb = (color * f[..., None] + SHADOW * (sh * (1 - f))[..., None]) / np.maximum(alpha, 1e-4)[..., None]
    out = np.dstack([np.clip(rgb, 0, 1), np.clip(alpha, 0, 1)])
    for mod in (os.path.join(ROOT, 'mods', 'EnhancedTextures'), os.path.join(ROOT, 'gen', 'mods', 'EnhancedTextures')):
        if not os.path.isdir(mod):
            continue
        dst = os.path.join(mod, 'textures', REL)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        Image.fromarray((out * 255 + 0.5).astype(np.uint8), 'RGBA').save(dst)
        print('wrote', dst)
        if '--register' in sys.argv:
            register(mod)


def register(mod):
    for name, sep, cols in (('textures/textures.ini', ' = ', None), ('origem.tsv', '\t', None)):
        p = os.path.join(mod, name)
        if not os.path.exists(p):
            continue
        lines = open(p, encoding='utf-8', newline='').read().split('\n')
        for i, l in enumerate(lines):
            if '1d377d3b' not in l:
                continue
            if name.endswith('.ini'):
                k, v = l.split(sep, 1)
                lines[i] = k + sep + REL + ('\r' if v.endswith('\r') else '')
            else:
                t = l.rstrip('\r').split('\t')
                t[2], t[3] = 'MenuTitlesHD', REL
                lines[i] = '\t'.join(t) + ('\r' if l.endswith('\r') else '')
        open(p, 'w', encoding='utf-8', newline='').write('\n'.join(lines))
        print('registered in', p)


if __name__ == '__main__':
    main()
