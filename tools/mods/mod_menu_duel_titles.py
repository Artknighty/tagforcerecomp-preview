"""Redraws the white heading sheet (texture b97cf0ce, 256x256: GROUP LIST, MAKE NEW GROUP, ENTRY LIST, DUEL, CARD TRADE,
SEND RECIPE, SEND CARD, SEND DUEL PUZZLE, FREE DUEL, NORMAL DUEL, TAG DUEL) at 8x for the EnhancedTextures mod instead of
upscaling it.

Input: the original texture dumped with TF_TEX_DUMP (gen/dump_tex/fd/b97cf0ce.png).
Output: textures/menus/pc/b97cf0ce.png in the mod (the textures.ini / origem.tsv rows are repointed by --register).
Each line is drawn with a heavy font, fitted into the ink box of the original line (same position, cap height and width).
The face keeps the original vertical shading (white on top, grey at the bottom, sampled from the original) and the soft dark
drop shadow to the lower right.

Usage: python tools/mods/mod_menu_duel_titles.py [--font PATH] [--register]
"""
import os
import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
SRC = os.path.join(ROOT, 'gen', 'dump_tex', 'fd', 'b97cf0ce.png')
FONT = 'C:/Windows/Fonts/ariblk.ttf'
K = 8
LINES = ['GROUP LIST', 'MAKE NEW GROUP', 'ENTRY LIST', 'DUEL', 'CARD TRADE', 'SEND RECIPE', 'SEND CARD',
         'SEND DUEL PUZZLE', 'FREE DUEL', 'NORMAL DUEL', 'TAG DUEL']
SHADOW_DX, SHADOW_DY = 1.5, 1.5     # PSP pixels, to the lower right
SHADOW_ALPHA = 0.5
REL = 'menus/pc/b97cf0ce.png'


def line_boxes(a):
    """Row groups of the sheet; for each, the bounding box of the opaque face pixels (the shadow is translucent)."""
    face = (a[..., 3] >= 250) & (a[..., 0] > 100)
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


def face_profile(a, boxes):
    """Average grey of the opaque face per row, relative to the line height (0..1), over all lines."""
    acc, n = np.zeros(64), 0
    for x0, y0, x1, y1 in boxes:
        reg = a[y0:y1, x0:x1].astype(np.float32)
        m = (reg[..., 3] >= 250) & (reg[..., 0] > 100)
        prof = np.array([reg[r, m[r], 0].mean() if m[r].any() else np.nan for r in range(y1 - y0)])
        ok = ~np.isnan(prof)
        prof = np.interp(np.arange(y1 - y0), np.where(ok)[0], prof[ok])
        acc += np.interp(np.linspace(0, 1, 64), np.linspace(0, 1, len(prof)), prof)
        n += 1
    return acc / n / 255


def text_mask(text, font):
    big = Image.new('L', (6000, 500), 0)
    ImageDraw.Draw(big).text((100, 50), text, font=ImageFont.truetype(font, 200), fill=255)
    return big.crop(big.getbbox())


def main():
    font = FONT
    if '--font' in sys.argv:
        font = sys.argv[sys.argv.index('--font') + 1]
    a = np.asarray(Image.open(SRC).convert('RGBA'))
    boxes = line_boxes(a)
    assert len(boxes) == len(LINES), boxes
    prof = face_profile(a, boxes)
    prof = np.convolve(np.pad(prof, 3, mode='edge'), np.ones(7) / 7, mode='valid')
    H, W = a.shape[0] * K, a.shape[1] * K
    face = Image.new('L', (W, H), 0)
    grey = np.zeros((H, W), np.float32)
    for text, (x0, y0, x1, y1) in zip(LINES, boxes):
        m = text_mask(text, font).resize(((x1 - x0) * K, (y1 - y0) * K), Image.LANCZOS)
        face.paste(m, (x0 * K, y0 * K))
        h = (y1 - y0) * K
        grey[y0 * K:y1 * K] = np.interp(np.linspace(0, 1, h), np.linspace(0, 1, 64), prof)[:, None]
    f = np.asarray(face, np.float32) / 255
    sh = np.asarray(face.filter(ImageFilter.GaussianBlur(K * 0.6)), np.float32) / 255
    sh = np.clip(np.roll(np.roll(sh * 1.5, int(SHADOW_DY * K), axis=0), int(SHADOW_DX * K), axis=1), 0, 1) * SHADOW_ALPHA
    alpha = f + sh * (1 - f)
    rgb = (grey[..., None] * f[..., None]) / np.maximum(alpha, 1e-4)[..., None]
    out = np.dstack([np.repeat(np.clip(rgb, 0, 1), 3, 2), np.clip(alpha, 0, 1)])
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
    for name, sep in (('textures/textures.ini', ' = '), ('origem.tsv', '\t')):
        p = os.path.join(mod, name)
        if not os.path.exists(p):
            continue
        lines = open(p, encoding='utf-8', newline='').read().split('\n')
        for i, l in enumerate(lines):
            if 'b97cf0ce' not in l:
                continue
            if name.endswith('.ini'):
                k, v = l.split(sep, 1)
                lines[i] = k + sep + REL + ('\r' if v.endswith('\r') else '')
            else:
                t = l.rstrip('\r').split('\t')
                t[2], t[3] = 'MenuDuelTitlesHD', REL
                lines[i] = '\t'.join(t) + ('\r' if l.endswith('\r') else '')
        open(p, 'w', encoding='utf-8', newline='').write('\n'.join(lines))
        print('registered in', p)


if __name__ == '__main__':
    main()
