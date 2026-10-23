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
S = 2                       # output = 256 grid * 2
CY, R = 112.0, 88.0         # body centre line and radius (grid units)
ARC_L, ARC_R = 128.0, 135.0
ORIG = 'mikeymk/ui/oval/oval_short_%s.png'
NEW = 'oval_hd/oval_short_%s.png'


def sstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def piece(kind):
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
    inside = sstep(px, -px, d)
    shadow = (1 - sstep(0, 3.2, d)) * 0.62 * (1 - inside)
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
    for name in ('textures/textures.ini', 'origem.tsv'):
        p = os.path.join(mod, name)
        if not os.path.exists(p):
            continue
        s = open(p, encoding='utf-8', newline='').read()
        for k in ('left', 'center', 'right'):
            s = s.replace(ORIG % k, NEW % k)
        open(p, 'w', encoding='utf-8', newline='').write(s)
        print('registered in', p)


def main():
    for mod in (os.path.join(ROOT, 'mods', 'EnhancedTextures'), os.path.join(ROOT, 'gen', 'mods', 'EnhancedTextures')):
        if not os.path.isdir(mod):
            continue
        for k in ('left', 'center', 'right'):
            dst = os.path.join(mod, 'textures', NEW % k)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            Image.fromarray((piece(k) * 255 + 0.5).astype(np.uint8), 'RGBA').save(dst)
            print('wrote', dst)
        if '--register' in sys.argv:
            register(mod)


if __name__ == '__main__':
    main()
