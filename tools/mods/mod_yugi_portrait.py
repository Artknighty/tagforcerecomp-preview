"""Puts a cut-out of Yugi's head (from reference art supplied by the user) into the face of the Yugi mod's character slot.

The list/duel/versus face of every filler duelist is one cell of a 256x128 sheet; the EnhancedTextures pack has the sheet at 8x
(mikeymk/extras/00000000979e93bf9b6b639f.png is the variant the slot uses). Picture 116 belongs to the slot only (checked against the game's
class table), so replacing its cell (the 2nd of the top row) changes nobody else. The script starts from the untouched pack file (copied once to
gen/backup_enhanced/yugi/), clears the cell, cuts the head out of the reference (white background -> transparent) and pastes it into the cell.

    python tools/mods/mod_yugi_portrait.py [reference.webp]     reference default: gen/yugi_work/yugi_ref2.png (art with transparent background; art on white also works, see cut_out)
    python tools/mods/mod_yugi_portrait.py --preview            also writes gen/yugi_work/yugi_head_preview.png

It also writes mods/Yugi/portrait.rgba, the picture the Yugi plugin draws over the Free Duel versus frame (the game covers the bottom of the head
portraits there with a jacket strip): 256x256, crimson gradient background like the opponent's plate, 8-byte header (u32 w, u32 h) + RGBA.
"""
import os, sys, shutil
import numpy as np
from PIL import Image, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
NAME = '00000000979e93bf9b6b639f.png'
TARGET = os.path.join(ROOT, 'mods', 'EnhancedTextures', 'textures', 'mikeymk', 'extras', NAME)
BACKUP = os.path.join(ROOT, 'gen', 'backup_enhanced', 'yugi', NAME)
PORTRAIT_RGBA = os.path.join(ROOT, 'mods', 'Yugi', 'portrait.rgba')
VERSUS_BOX = (128, 0, 472, 344)     # square part of the reference shown in the versus frame
DEFAULT_REF = os.path.join(ROOT, 'gen', 'yugi_work', 'yugi_ref2.png')
CELL = (412, 0, 812, 512)           # the 2nd cell of the top row of the 2048x1024 sheet (cells are 409.6 x 512); neighbours start at x = 815
HEAD_IN_REF = (150, 0, 450, 333)   # hair to the collar in the 522x757 reference (aspect 0.9)
WINDOW = (460, 65, 757, 395)         # the part of the sheet the portrait frame shows: the Free Duel versus screen shows x 460..757, y 100..395; the duelist lists x 457..760, y 42..388. The game draws a class-colour jacket over the bottom ~12% of the versus frame, so the art ends at the collar there.


def cut_out(img):
    """White background -> transparent: flood fill of near-white pixels from the borders, then a soft 1 px edge."""
    rgb = np.asarray(img.convert('RGB')).astype(int)
    h, w = rgb.shape[:2]
    near_white = (rgb.min(axis=2) > 235)
    bg = np.zeros((h, w), bool)
    stack = [(0, x) for x in range(w)] + [(h - 1, x) for x in range(w)] + [(y, 0) for y in range(h)] + [(y, w - 1) for y in range(h)]
    while stack:
        y, x = stack.pop()
        if y < 0 or x < 0 or y >= h or x >= w or bg[y, x] or not near_white[y, x]: continue
        bg[y, x] = True
        stack += [(y + 1, x), (y - 1, x), (y, x + 1), (y, x - 1)]
    alpha = Image.fromarray(((~bg) * 255).astype(np.uint8)).filter(ImageFilter.MinFilter(3)).filter(ImageFilter.GaussianBlur(0.8))
    out = img.convert('RGBA'); out.putalpha(alpha)
    return out


def write_versus_portrait(art):
    n = 256
    top, bottom = np.array([138, 24, 46], float), np.array([222, 80, 106], float)
    t = np.linspace(0, 1, n)[:, None, None]
    bg = np.concatenate([(top * (1 - t) + bottom * t).repeat(n, axis=1), np.full((n, n, 1), 255.0)], axis=2).astype(np.uint8)
    pic = Image.fromarray(bg, 'RGBA')
    box = art.crop(VERSUS_BOX).resize((n, n), Image.LANCZOS)
    pic.alpha_composite(box)
    os.makedirs(os.path.dirname(PORTRAIT_RGBA), exist_ok=True)
    with open(PORTRAIT_RGBA, 'wb') as f:
        f.write(np.array([n, n], '<u4').tobytes()); f.write(np.asarray(pic).tobytes())
    return pic


def main():
    ref = next((a for a in sys.argv[1:] if not a.startswith('--')), DEFAULT_REF)
    if not os.path.exists(BACKUP):
        os.makedirs(os.path.dirname(BACKUP), exist_ok=True); shutil.copyfile(TARGET, BACKUP)
    sheet = Image.open(BACKUP).convert('RGBA')
    art = Image.open(ref)
    pic = write_versus_portrait(art.convert('RGBA') if 'A' in art.getbands() else cut_out(art.convert('RGB')))
    head = art.convert('RGBA').crop(HEAD_IN_REF) if 'A' in art.getbands() else cut_out(art.convert('RGB').crop(HEAD_IN_REF))   # art with its own transparency is used as it is
    s = min((WINDOW[2] - WINDOW[0]) / head.width, (WINDOW[3] - WINDOW[1]) / head.height)
    head = head.resize((round(head.width * s), round(head.height * s)), Image.LANCZOS)
    sheet.paste(Image.new('RGBA', (CELL[2] - CELL[0], CELL[3] - CELL[1]), (0, 0, 0, 0)), CELL[:2])   # clear the old head
    x = WINDOW[0] + (WINDOW[2] - WINDOW[0] - head.width) // 2
    y = WINDOW[3] - head.height
    sheet.alpha_composite(head, (x, y))
    sheet.save(TARGET)
    print('wrote', TARGET, 'and', PORTRAIT_RGBA)
    if '--preview' in sys.argv:
        os.makedirs(os.path.join(ROOT, 'gen', 'yugi_work'), exist_ok=True)
        c = sheet.crop((CELL[0] - 20, 0, CELL[2] + 20, 512)); bg = Image.new('RGBA', c.size, (0, 110, 0, 255)); bg.alpha_composite(c)
        bg.convert('RGB').save(os.path.join(ROOT, 'gen', 'yugi_work', 'yugi_head_preview.png')); pic.convert('RGB').save(os.path.join(ROOT, 'gen', 'yugi_work', 'yugi_versus_preview.png'))


if __name__ == '__main__':
    main()
