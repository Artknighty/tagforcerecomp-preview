"""Redraws the pieces of the "List of Duelists" screen for EnhancedTextures instead of upscaling them (original colours).

Pieces (game texture data hash -> output in textures/duelist_list_hd/):
  5e8560c1  frame sheet (128x128 texels, four colour rows): tab caps / stretch columns of the three tabs, the cream rounded panel and the
            right-hand panel piece with the scroll gutter. Every used piece is redrawn from the original texels with tools/mods/depixel.py.
  5c77e7d0  misc sheet (256x128): scroll bar track, thumb and arrow icons (same redraw).
Everything else in both sheets is left as the EnhancedTextures (MikeyMK) image.

The tool needs the original texels of both sheets (PSP resolution PNGs, one per data hash) in a folder, `gen/ref_texels/<hash>.png` by default:
they come from the game's texture dump (TF_TEX_DUMP_RAW=<folder>, the .png next to the .bin of that hash).

Usage: python tools/mods/mod_duelist_list.py [--texels DIR] [--register]
"""
import os
import re
import sys
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import depixel as dp

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
MODS = [os.path.join(ROOT, 'mods', 'EnhancedTextures'), os.path.join(ROOT, 'gen', 'mods', 'EnhancedTextures')]
OUT = 'duelist_list_hd'
K = dp.K

# data hash -> (EnhancedTextures image the unused parts come from, new file)
SHEETS = {
    '5e8560c1': ('mikeymk/shop/card_pack_results_ui.png', OUT + '/frame_sheet.png'),
    '5c77e7d0': ('mikeymk/ui/misc/00000000fa54e5155c77e7d0.png', OUT + '/misc_sheet.png'),
}


def frame_sheet(out, t):
    for r in range(4):                                  # four colour rows, 32 texels apart
        o = 32 * r
        dp.paste_region(out, t, 0, 64, o, o + 18)       # tab caps and stretch columns
        dp.paste_region(out, t, 64, 96, o, o + 18)      # cream rounded panel (left part of the frame)
        dp.paste_region(out, t, 96, 112, o, o + 32)     # right part of the frame with the scroll gutter
        for u in (4, 28):
            dp.flat_col(out, u, o, o + 18)              # tab middle: stretched horizontally
        dp.flat_col(out, 70, o, o + 6)                  # frame top / bottom / centre strips
        dp.flat_col(out, 70, o + 11, o + 17)
        dp.flat_col(out, 70, o + 6, o + 7)
        dp.flat_row(out, 64, 71, o + 6)                 # frame left edge / centre rows: stretched vertically
        dp.flat_row(out, 98, 112, o + 6)


# the scroll track of the original is a dark translucent tube made of mixed-alpha texels; this clean texel map has the same layout
TRACK_COLORS = {'.': (0, 0, 0, 0), 'B': (127, 127, 127, 128), 'H': (1, 1, 1, 255), 'N': (0, 0, 0, 128), 'O': (255, 255, 255, 128)}
TRACK = ['..BBBB..'] + ['.BHNNNO.'] * 18 + ['..OOOO..']


THUMB_COLORS = {'.': (0, 0, 0, 0), 'T': (170, 83, 1, 255), 'W': (199, 110, 5, 255), '2': (255, 169, 32, 255), 'Z': (226, 138, 8, 255), 'R': (0, 0, 0, 64)}
THUMB = ['.TT.', 'TWWT'] + ['T2ZT'] * 14 + ['TWWT', '.TT.', '....', '....']   # 4 x 20 texels: orange scroll thumb


def template(rows, colors):
    return np.array([[colors[c] for c in r] for r in rows], np.uint8)


def arrow(up=True, ss=8):
    """Small scroll arrow (8x8 texels): dark triangle with a grey outline and a light rim on its base."""
    from PIL import Image as I, ImageDraw as D
    n = 8 * K * ss
    def layer(poly, col, width=0):
        im = I.new('RGBA', (n, n), (0, 0, 0, 0))
        d = D.Draw(im) if False else D.Draw(im)
        d.polygon([(x * K * ss, y * K * ss) for x, y in poly], fill=col)
        return im
    tri = [(4.0, 0.5), (7.0, 4.8), (1.0, 4.8)]
    big = I.new('RGBA', (n, n), (0, 0, 0, 0))
    out = layer(tri, (127, 127, 127, 128))                                   # outline = the same triangle, grown by drawing the inner one smaller
    inner = [(4.0, 1.5), (6.1, 4.2), (1.9, 4.2)]
    big.alpha_composite(out)
    big.alpha_composite(layer(inner, (1, 1, 1, 235)))
    big.alpha_composite(layer([(1.0, 4.8), (7.0, 4.8), (7.0, 5.7), (1.0, 5.7)], (255, 255, 255, 128)))
    a = np.asarray(big, np.float32)
    al = a[..., 3:4] / 255
    pm = np.concatenate([a[..., :3] * al, a[..., 3:4]], axis=2).reshape(8 * K, ss, 8 * K, ss, 4).mean(axis=(1, 3))
    rgb = np.where(pm[..., 3:4] > 0, pm[..., :3] / np.maximum(pm[..., 3:4] / 255, 1e-6), 0)
    res = np.clip(np.concatenate([rgb, pm[..., 3:4]], axis=2) + 0.5, 0, 255).astype(np.uint8)
    return res if up else res[::-1]


def misc_sheet(out, t):
    out[0:20 * K, 55 * K:63 * K] = dp.render(template(TRACK, TRACK_COLORS))        # scroll track (dark tube)
    out[40 * K:60 * K, 55 * K:59 * K] = dp.render(template(THUMB, THUMB_COLORS))   # scroll thumb (orange bar)
    out[48 * K:56 * K, 46 * K:54 * K] = arrow(True)     # up / down arrows
    out[56 * K:64 * K, 46 * K:54 * K] = arrow(False)
    for u0, u1, v0, v1 in ((55, 63, 5, 10), (55, 59, 45, 50)):
        out[v0 * K:v1 * K, u0 * K:u1 * K] = out[(v0 + v1) // 2 * K + K // 2:(v0 + v1) // 2 * K + K // 2 + 1, u0 * K:u1 * K]   # stretched vertically


DRAW = {'5e8560c1': frame_sheet, '5c77e7d0': misc_sheet}


def build(texels_dir):
    res = {}
    for h, (src, dst) in SHEETS.items():
        t = np.asarray(Image.open(os.path.join(texels_dir, h + '.png')).convert('RGBA'))
        base = np.asarray(Image.open(os.path.join(MODS[0], 'textures', src)).convert('RGBA')).copy()
        assert base.shape[0] == t.shape[0] * K and base.shape[1] == t.shape[1] * K, (h, base.shape, t.shape)
        DRAW[h](base, t)
        res[dst] = base
    return res


def register(mod, hashes):
    ini = os.path.join(mod, 'textures', 'textures.ini')
    if not os.path.exists(ini):
        return
    s = open(ini, encoding='utf-8').read()
    for h, dst in hashes.items():
        s, n = re.subn(r'(?m)^([0-9a-f]{16}%s) = .*$' % h, lambda m: m.group(1) + ' = ' + dst, s)
        print('  ini', os.path.basename(os.path.dirname(os.path.dirname(ini))), h, n, 'rows')
    open(ini, 'w', encoding='utf-8', newline='\n').write(s)


def main():
    a = sys.argv[1:]
    texels = a[a.index('--texels') + 1] if '--texels' in a else os.path.join(ROOT, 'gen', 'ref_texels')
    res = build(texels)
    for mod in MODS:
        for dst, img in res.items():
            p = os.path.join(mod, 'textures', dst)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            Image.fromarray(img).save(p)
            print('wrote', p)
    if '--register' in a:
        for mod in MODS:
            register(mod, {h: SHEETS[h][1] for h in SHEETS})


if __name__ == '__main__':
    main()
