"""Redraws the short oval frame (the translucent pill around the menu titles, e.g. behind "MAIN MENU") for EnhancedTextures.

The game draws the pill from three pieces (left cap, stretched center, right cap), 256x256 in the MikeyMK pack. Instead of upscaling them
this script draws the same shape analytically (signed distance of a stadium) at 512x512: soft black drop shadow outside, opaque black body,
a glossy rim (thick highlight along the top that thins out around the caps, a thin bright line near the bottom, a faint dark side rim).
Geometry is in the pack's 256 grid: body y 24..200 (radius 88), left cap arc centre x=128, right cap arc centre x=135.

Output: textures/oval_hd/oval_short_{left,center,right}.png (+ --register repoints textures.ini / origem.tsv rows of the 3 originals).
Usage: python tools/mods/mod_menu_oval.py [--register]
"""
import os
import sys
import numpy as np
from PIL import Image

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
# geometry in the pack's 256 grid: (output scale, body centre y, radius, left cap arc centre x, right cap arc centre x, rim scale)
GEO = {
    'short': (2, 112.0, 88.0, 128.0, 135.0, 1.0),
    'tall': (1, 127.5, 112.0, 192.0, 63.0, 1.25),   # 32x32 in the game (8x); the pill behind the menu titles, e.g. MAIN MENU
}
ORIG = 'mikeymk/ui/oval/oval_short_%s.png'
NEW = 'oval_hd/oval_short_%s.png'
NEW_TALL = 'oval_hd/oval_%s.png'
# game texture hashes of the tall pieces (the same ones the duel result screen uses; that screen has its own palette-specific rows)
TALL_HASH = {'tall_left': '85169591', 'tall_center': '1320facf', 'tall_right': '87f4af60',
             'transparent_tall_left': '59cfec77', 'transparent_tall_center': 'b441d53d', 'transparent_tall_right': '9d15ec20'}


def sstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def tall_rim(d, ny, u, px, inside, shadow):
    """Rim of the 32x32 pill pieces (8x grid): thick lavender-white top, mid-grey sides, dim grey bottom band, black body."""
    top, bot = np.maximum(-ny, 0), np.maximum(ny, 0)
    w = 12.0 + 7.0 * top + 3.0 * bot                       # rim thickness by direction
    t = np.clip(u / w, 0, 1)                               # 0 at the outer edge, 1 at the inner edge of the rim
    c_top = 0.74 - 0.20 * t                                # bright at the edge, softer inside
    c_side = 0.40 - 0.18 * t
    c_bot = 0.20 - 0.06 * t
    c = c_top * top + c_bot * bot + c_side * (1 - top - bot)
    rim = 1 - sstep(w - px, w + px, u)
    grey = c * rim
    rgb = np.dstack([grey * (1 - 0.05 * top), grey * (1 - 0.04 * top), grey * (1 + 0.04 * top)])
    alpha = np.clip(inside * 0.996 + shadow, 0, 1)
    rgb = np.clip(rgb, 0, 1) * (inside > 0)[..., None]
    return np.dstack([rgb, alpha])


def piece(kind, shape='short', transparent=False):
    S, CY, R, ARC_L, ARC_R, RS = GEO[shape]
    n = 256 * S
    ys, xs = np.mgrid[0:n, 0:n].astype(np.float32)
    x, y = (xs + 0.5) / S, (ys + 0.5) / S
    if kind == 'left':
        dx = np.maximum(ARC_L - x, 0)
    elif kind == 'right':
        dx = np.maximum(x - ARC_R, 0)
    else:
        dx = np.zeros_like(x)
    dy = y - CY
    dist = np.hypot(dx, dy)
    d = dist - R                                    # signed distance to the outer edge (outside > 0)
    ny = np.where(dist > 1e-3, dy / np.maximum(dist, 1e-3), 0)   # outward normal y: top = -1
    u = -d                                          # depth inside the body
    px = 1.0 / S                                    # antialiasing width
    if transparent:                                 # the translucent shade layer: plain stadium, alpha 65/255, no rim
        a = inside_ = sstep(px, -px, d) * 0.255
        return np.dstack([np.zeros_like(a), np.zeros_like(a), np.zeros_like(a), a])
    inside = sstep(px, -px, d)
    shadow = (1 - sstep(0, 3.2, d)) * 0.62 * (1 - inside)
    if shape == 'tall':
        return tall_rim(d, ny, u, px, inside, shadow)
    u = u / RS
    # rim layers (color in 0..1 grey, weight)
    top, bot = np.maximum(-ny, 0), np.maximum(ny, 0)
    wt = 9.5 * top ** 2.2 + 1.2                     # highlight thickness: thick on top, thin around the caps
    hi = sstep(0, px * 1.5, u) * (1 - sstep(wt - px, wt + px, u))
    hi_col = 0.80 - 0.20 * np.clip(u / np.maximum(wt, 1e-3), 0, 1) - 0.18 * (1 - top)
    hi_w = hi * np.clip(0.25 + top * 0.75 + 0.35, 0, 1) * (1 - bot * 0.85)
    line = sstep(3.0 - px, 3.0 + px, u) * (1 - sstep(5.8 - px, 5.8 + px, u))     # thin bright line near the bottom
    line_w = line * bot ** 2 * 0.95
    line_col = 0.80
    side = sstep(1.0 - px, 1.0 + px, u) * (1 - sstep(4.5 - px, 4.5 + px, u))      # faint dark-grey side rim
    side_w = side * (1 - top) * (1 - bot) * 0.28
    side_col = 0.15
    # composite on black body
    col = np.zeros_like(d)
    w = np.zeros_like(d)
    for c, ww in ((side_col, side_w), (hi_col, hi_w), (line_col, line_w)):
        col = col * (1 - ww) + c * ww
        w = w + ww * (1 - w)
    grey = col
    alpha = np.clip(inside * 0.996 + shadow, 0, 1)
    out = np.dstack([grey, grey, grey, alpha])
    out[..., :3] *= (inside > 0)[..., None]         # shadow stays black
    return out


def register(mod):
    """Repoints the short pieces (every row) and the tall pieces (only the palette-less rows: the duel result screen keeps its own)."""
    for name in ('textures/textures.ini', 'origem.tsv'):
        p = os.path.join(mod, name)
        if not os.path.exists(p):
            continue
        s = open(p, encoding='utf-8', newline='').read()
        for k in ('left', 'center', 'right'):
            s = s.replace(ORIG % k, NEW % k)
        lines = s.split('\n')
        for i, l in enumerate(lines):
            cr = '\r' if l.endswith('\r') else ''
            for k, h in TALL_HASH.items():
                if name.endswith('.ini'):
                    if l.startswith('0000000000000000' + h + ' = '):
                        lines[i] = l.split(' = ')[0] + ' = ' + NEW_TALL % k + cr
                else:
                    t = l.rstrip('\r').split('\t')
                    if len(t) >= 4 and t[0] == h and t[1] == '00000000':
                        t[2], t[3] = 'MenuOvalHD', NEW_TALL % k
                        lines[i] = '\t'.join(t) + cr
        open(p, 'w', encoding='utf-8', newline='').write('\n'.join(lines))
        print('registered in', p)


def save(arr, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    Image.fromarray((arr * 255 + 0.5).astype(np.uint8), 'RGBA').save(dst)
    print('wrote', dst)


def main():
    for mod in (os.path.join(ROOT, 'mods', 'EnhancedTextures'), os.path.join(ROOT, 'gen', 'mods', 'EnhancedTextures')):
        if not os.path.isdir(mod):
            continue
        tex = os.path.join(mod, 'textures')
        for k in ('left', 'center', 'right'):
            save(piece(k), os.path.join(tex, NEW % k))
            save(piece(k, 'tall'), os.path.join(tex, NEW_TALL % ('tall_' + k)))
            save(piece(k, 'tall', transparent=True), os.path.join(tex, NEW_TALL % ('transparent_tall_' + k)))
        if '--register' in sys.argv:
            register(mod)


if __name__ == '__main__':
    main()
