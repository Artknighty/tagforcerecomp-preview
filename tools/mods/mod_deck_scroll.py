"""Deck Edit list scroll bar (track, thumbs, end markers) redrawn from geometry.

The pieces live in the 128x128 attribute/race icon sheet (data hash 4f371dfc), in its right-hand strip (x >= 118). The icons are kept
exactly as the EnhancedTextures sheet has them; only the strip pieces the Deck Edit list draws are replaced.
Output: textures/deck/pc/4f371dfc_0.png (16 px per original texel) + its textures.ini row.
Usage: python tools/mods/mod_deck_scroll.py [--register]
"""
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(__file__))
import mod_deck_edit as de  # noqa: E402

K = 16
HASH = '4f371dfc'
SRC = os.path.join(de.ROOT, 'mods', 'EnhancedTextures', 'textures', '_unreferenced', 'mikeymk__cards__frame__icons_attribute_race.png')


def grid(x0, y0, x1, y1):
    xs = x0 + (np.arange(int(round((x1 - x0) * K))) + 0.5) / K
    ys = y0 + (np.arange(int(round((y1 - y0) * K))) + 0.5) / K
    return np.meshgrid(xs, ys)


def aa(depth):
    return np.clip(depth * K + 0.5, 0.0, 1.0)


def over(dst, rgb, alpha):
    a0 = dst[..., 3:4] / 255.0
    a1 = np.asarray(alpha, np.float32)[..., None]
    ao = a1 + a0 * (1 - a1)
    col = (np.asarray(rgb, np.float32) * a1 + dst[..., :3] * a0 * (1 - a1)) / np.maximum(ao, 1e-6)
    dst[..., :3] = col
    dst[..., 3:4] = ao * 255.0


def poly_depth(X, Y, pts):
    """Signed depth inside a convex polygon given counter-clockwise in image coordinates (y down: clockwise on screen)."""
    d = np.full(X.shape, 1e9, np.float32)
    n = len(pts)
    for i in range(n):
        (ax, ay), (bx, by) = pts[i], pts[(i + 1) % n]
        ex, ey = bx - ax, by - ay
        ln = float(np.hypot(ex, ey))
        d = np.minimum(d, ((X - ax) * (-ey) + (Y - ay) * ex) / ln)
    return d


def thumb(reg, x0, y0, x1, y1, rim, stripe, mid, right, spec):
    """A 4-texel-wide capsule: dark rim, lit left stripe fading to the right edge, small specular blob at the top, soft shadow to the left / below."""
    X, Y = grid_of(reg)
    r = (x1 - x0) / 2.0
    over(reg, (0, 0, 0), 0.5 * aa(de.rr_depth(X, Y, (x0 - 0.9, y0 + 0.6, x1 - 0.9, y1 + 1.8), r)))
    d = de.rr_depth(X, Y, (x0, y0, x1, y1), r)
    u = (X - x0) / (x1 - x0)
    stops = [0.25, 0.36, 0.56, 0.74]
    cols = np.array([stripe * 0.92, stripe, mid, right], np.float32)
    body = np.stack([np.interp(u, stops, cols[:, c]) for c in range(3)], axis=-1)
    body = np.where((Y > y1 - 2.3)[..., None], body * 0.86, body)
    over(reg, rim, aa(d))
    over_arr(reg, body, aa(d - 0.85))
    sp = np.hypot((X - (x0 + 1.5)) / 0.55, (Y - (y0 + 2.4)) / 0.8)
    over(reg, spec, 0.85 * np.clip(1.4 - sp, 0, 1) * aa(d - 0.85))


def over_arr(dst, rgb_arr, alpha):
    a0 = dst[..., 3:4] / 255.0
    a1 = np.asarray(alpha, np.float32)[..., None]
    ao = a1 + a0 * (1 - a1)
    dst[..., :3] = (rgb_arr * a1 + dst[..., :3] * a0 * (1 - a1)) / np.maximum(ao, 1e-6)
    dst[..., 3:4] = ao * 255.0


_REG_XY = {}


def grid_of(reg):
    return _REG_XY[id(reg)]


def canvas(x0, y0, x1, y1):
    X, Y = grid(x0, y0, x1, y1)
    reg = np.zeros(X.shape + (4,), np.float32)
    _REG_XY[id(reg)] = (X, Y)
    return reg


def put(sheet, reg, x0, y0):
    """Replace the sheet area of reg (whole, with its transparency) by reg."""
    h, w = reg.shape[:2]
    sheet[int(y0 * K):int(y0 * K) + h, int(x0 * K):int(x0 * K) + w] = np.clip(reg + 0.5, 0, 255).astype(np.uint8)


def track(sheet):
    reg = canvas(118, 0, 128, 20)
    X, Y = _REG_XY[id(reg)]
    d = de.rr_depth(X, Y, (120, 0, 126, 20), 2.7)
    left = X < 123
    over(reg, np.where(left[..., None], np.array([127, 127, 127], np.float32), np.array([255, 255, 255], np.float32)), 0.5 * aa(d) * (1 - aa(d - 0.9)))
    over(reg, (0, 0, 0), 0.5 * aa(d - 0.9))
    over(reg, (0, 0, 0), 0.9 * aa(d - 0.9) * (1 - aa(d - 1.9)))
    put(sheet, reg, 118, 0)


def end_markers(sheet):
    up = canvas(118, 108, 128, 116)
    X, Y = _REG_XY[id(up)]
    pts = [(122.5, 108.1), (123.5, 108.1), (126.2, 112.9), (119.8, 112.9)]
    d = poly_depth(X, Y, pts)
    over(up, (127, 127, 127), 0.5 * aa(d) * (1 - aa(d - 0.9)))
    over(up, (0, 0, 0), 0.85 * aa(d - 0.9))
    over(up, (255, 255, 255), 0.5 * aa(d) * (Y > 112.0))
    put(sheet, up, 118, 108)
    dn = canvas(118, 116, 128, 124)
    X, Y = _REG_XY[id(dn)]
    pts = [(119.8, 116.1), (126.2, 116.1), (123.5, 120.9), (122.5, 120.9)]
    d = poly_depth(X, Y, pts)
    over(dn, (127, 127, 127), 0.5 * aa(d) * (1 - aa(d - 0.9)))
    over(dn, (0, 0, 0), 0.8 * aa(d - 0.9))
    over(dn, (255, 255, 255), 0.5 * aa(d) * (Y > 120.0))
    put(sheet, dn, 118, 116)
    # the list draws a second, tinted copy of the down marker from rows 68..76 at the same screen spot: keep it identical so the two coincide
    dn2 = canvas(118, 68, 128, 76)
    X, Y = _REG_XY[id(dn2)]
    pts = [(119.8, 68.1), (126.2, 68.1), (123.5, 72.9), (122.5, 72.9)]
    d = poly_depth(X, Y, pts)
    over(dn2, (127, 127, 127), 0.5 * aa(d) * (1 - aa(d - 0.9)))
    over(dn2, (0, 0, 0), 0.85 * aa(d - 0.9))
    over(dn2, (255, 255, 255), 0.5 * aa(d) * (Y > 72.0))
    put(sheet, dn2, 118, 68)


def thumbs(sheet):
    pink = canvas(118, 20, 128, 40)
    thumb(pink, 119, 20, 123, 38, np.array([169, 0, 77]), np.array([255, 31, 106]), np.array([227, 8, 84]), np.array([199, 5, 84]), np.array([255, 143, 141]))
    thumb(pink, 123, 20, 127, 38, np.array([116, 53, 82]), np.array([176, 88, 118]), np.array([161, 76, 105]), np.array([130, 67, 84]), np.array([211, 191, 170]))
    put(sheet, pink, 118, 20)
    org = canvas(118, 40, 128, 60)
    thumb(org, 119, 40, 123, 58, np.array([170, 83, 1]), np.array([253, 155, 9]), np.array([226, 138, 8]), np.array([199, 121, 7]), np.array([239, 213, 146]))
    thumb(org, 123, 40, 127, 58, np.array([117, 85, 55]), np.array([172, 140, 90]), np.array([157, 125, 77]), np.array([134, 105, 52]), np.array([212, 212, 211]))
    put(sheet, org, 118, 40)


def main():
    img = Image.open(SRC).convert('RGBA')
    if img.size != (128 * K, 128 * K):                                         # the icons only; the strip is redrawn below
        img = img.resize((128 * K, 128 * K), Image.LANCZOS)
    sheet = np.asarray(img).copy()
    for y0, y1 in ((0, 20), (20, 60), (68, 76), (108, 124)):                    # clear the strip pieces (keep the icons to its left)
        sheet[y0 * K:y1 * K, 118 * K:128 * K] = 0
    track(sheet)
    thumbs(sheet)
    end_markers(sheet)
    name = HASH + '_0'
    for mod in de.MODS:
        p = os.path.join(mod, 'textures', de.OUT, name + '.png')
        os.makedirs(os.path.dirname(p), exist_ok=True)
        Image.fromarray(sheet).save(p)
    print('wrote', name)
    if '--register' in sys.argv:
        for mod in de.MODS:
            de.register(mod, [name])


if __name__ == '__main__':
    main()
