"""Remaster of the Deck Edit screen UI (tabs, panel frame, header plates, stat boxes), redrawn from the original texels.

Every piece is a separate small PSP texture. Each one is redrawn with tools/mods/depixel.py (flat shapes, smooth outlines)
and written to EnhancedTextures/textures/deck_edit_hd/<hash>.png; the ini rows of that hash are repointed there.
Pieces the game stretches (all columns or all rows equal in the original) are kept flat along that direction.
Needs the original texels in gen/ref_texels/deckedit/<hash>_<variant>.png (TF_TEX_DUMP_RAW dump, raw_*_<hash>.png).

Usage: python tools/mods/mod_deck_edit.py [--only HASH,...] [--register]
"""
import os
import re
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import depixel  # noqa: E402
from mod_duelist_list import beveled  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MODS = [os.path.join(ROOT, 'mods', 'EnhancedTextures'), os.path.join(ROOT, 'gen', 'mods', 'EnhancedTextures')]
K = depixel.K
SIGMA = 0.65
OUT = 'deck_edit_hd'

# tab / panel pieces and header plates drawn by the Deck Edit screen
PIECES = '''02cf9d44 b77db24b 1d34b07b 8ab9c0f6 270d1417 b125544b 1f9c71e3 e0a7c50a b4c89e29 936aa71c 533b0214 56dfbf73 4afa4ec8
dba54dc6 92c07948 53a27e1e a6610a0b 46dec002 05809703 34a9652d 9dead951 fd0f9c3b faddc253 657a5145 1659d75e 4400d119
403380d6 8e3f02b7 c630f1be 3ce8d1c8'''.split()


FONT = 'C:/Windows/Fonts/bahnschrift.ttf'
# baked lettering that is erased from the original texels and typeset again at 8x. Each item:
# text, first column, last column + 1 (None = up to the white number field), first row, last row + 1, fill texel (row, col),
# colour, cap height in texels, baseline row, outline
TAB = dict(x0=4, x1=None, y0=3, y1=15, fill=(3, 5), col=(255, 255, 255), cap=9.0, base=13.2, outline=True)
TEXTS = {
    'b77db24b': [dict(TAB, text='Deck')],
    '34a9652d': [dict(TAB, text='Fusion')],
    'fd0f9c3b': [dict(TAB, text='Side')],
    '92c07948': [dict(TAB, text='Trunk')],
    '4400d119': [
        dict(TAB, text='Card types in possession', x0=5, y1=19, fill=(3, 6), base=14.0),
        dict(text='START', cx=172.5, x0=164, x1=184, y0=3, y1=8, fill=(2, 172), col=(255, 255, 255), cap=4.6, base=7.6, outline=False),
        dict(text='?', cx=157, x0=153, x1=161, y0=13, y1=20, fill=(12, 153), col=(0, 0, 0), cap=6.4, base=19.5, outline=False),
    ],
}


def white_box_x(t):
    """First column of the biggest pure-white blob (the number field of the tab / plate)."""
    return white_box(t)[0]


def white_box(t):
    """(first, last) column of the biggest pure-white blob (the number field of the tab / plate)."""
    m = (t[..., :3] >= 250).all(2) & (t[..., 3] == 255)
    seen = np.zeros(m.shape, bool)
    best = (0, 0, 0)
    for y, x in zip(*np.nonzero(m)):
        if seen[y, x]:
            continue
        st, cells = [(y, x)], []
        seen[y, x] = True
        while st:
            a, b = st.pop()
            cells.append((a, b))
            for da, db in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                c, d = a + da, b + db
                if 0 <= c < m.shape[0] and 0 <= d < m.shape[1] and m[c, d] and not seen[c, d]:
                    seen[c, d] = True
                    st.append((c, d))
        if len(cells) > best[0]:
            best = (len(cells), min(b for _, b in cells), max(b for _, b in cells))
    return best[1], best[2]


def typeset(out, item, x1, fill, face='Bold Condensed'):
    """Draw one piece of lettering onto the redrawn image `out` (8x): condensed face, fill colour, outline in the dark tone of the piece."""
    text, x0 = item['text'], item['x0']
    dark = tuple(int(c) for c in np.asarray(fill[:3], float) * 0.45)
    size = 120
    f = ImageFont.truetype(FONT, size)
    f.set_variation_by_name(face)
    hb = f.getbbox('H')
    f = ImageFont.truetype(FONT, int(size * item['cap'] * K / (hb[3] - hb[1])))
    f.set_variation_by_name(face)
    stroke = 3 if item['outline'] else 0
    bb = f.getbbox(text, stroke_width=stroke)
    hb = f.getbbox('H')
    layer = Image.new('RGBA', (bb[2] - bb[0] + 4, bb[3] - bb[1] + 4), (0, 0, 0, 0))
    ImageDraw.Draw(layer).text((2 - bb[0], 2 - bb[1]), text, font=f, fill=item['col'] + (255,), stroke_width=stroke, stroke_fill=dark + (255,))
    avail = (x1 - x0 - 0.5) * K
    if layer.width > avail:                          # squeeze to the free width (the original lettering is condensed as well)
        layer = layer.resize((int(avail), layer.height), Image.LANCZOS)
    px = int((x0 + 0.5) * K)
    if 'cx' in item:                                 # centred on a column (buttons)
        px = int(item['cx'] * K - layer.width / 2)
    py = int(item['base'] * K - (hb[3] - bb[1] + 2))
    img = Image.fromarray(out)
    if item['outline']:
        sh = Image.new('RGBA', layer.size, dark + (150,))
        sh.putalpha(layer.split()[3].point(lambda v: v * 150 // 255))
        img.alpha_composite(sh, (px + 4, py + 5))
    img.alpha_composite(layer, (px, py))
    return np.asarray(img).copy()


def lettered(h, t):
    """Redraw of a piece that carries baked lettering: erase the old letters, draw the piece, typeset the new ones. None if it has none."""
    if h not in TEXTS:
        return None
    e = t.copy()
    box = white_box_x(t) - 2
    items = []
    for it in TEXTS[h]:
        x1 = it['x1'] or box
        fill = t[it['fill']].copy()
        e[it['y0']:it['y1'], it['x0']:x1] = fill
        items.append((it, x1, fill))
    out = tab_head(t) if h in TAB_HEADS else redraw(e, h)
    if h == '4400d119':
        out[:] = types_plate(t)
        btn = types_buttons(t)
        out[:, 152 * K:] = btn[:, 152 * K:]
        out[:, 151 * K:152 * K] = np.where(out[:, 151 * K:152 * K, 3:4] > 0, out[:, 151 * K:152 * K], btn[:, 151 * K:152 * K])
    for it, x1, fill in items:
        out = typeset(out, it, x1, fill if it['outline'] else (0, 0, 0))
    return out


HARD = {'403380d6'}                      # ruled paper: the fine lines must stay exact labels, no colour smoothing
SOFT = {'1659d75e': 0.9, '4400d119': 0.9}  # header plates: rounder inner corners


FINE = {'c630f1be': dict(csigma=0.55, tau=28)}   # stat boxes: keep the shading rows of the number windows


def tiled(t, tile=48, pad=4, **kw):
    """render_smooth on a big atlas, tile by tile with an overlap (memory)."""
    H, W = t.shape[:2]
    out = np.zeros((H * K, W * K, 4), np.uint8)
    for y in range(0, H, tile):
        for x in range(0, W, tile):
            ya, yb, xa, xb = max(0, y - pad), min(H, y + tile + pad), max(0, x - pad), min(W, x + tile + pad)
            r = depixel.render_smooth(t[ya:yb, xa:xb], K, **kw)
            y1, x1 = min(H, y + tile), min(W, x + tile)
            out[y * K:y1 * K, x * K:x1 * K] = r[(y - ya) * K:(y1 - ya) * K, (x - xa) * K:(x1 - xa) * K]
    return out


def components(t, min_area):
    """Bounding boxes (x0, y0, x1, y1 inclusive) of the 4-connected opaque blobs of the atlas with at least min_area texels."""
    m = t[..., 3] > 0
    seen = np.zeros(m.shape, bool)
    res = []
    for y, x in zip(*np.nonzero(m)):
        if seen[y, x]:
            continue
        st, cells = [(y, x)], []
        seen[y, x] = True
        while st:
            a, b = st.pop()
            cells.append((a, b))
            for da, db in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                c, d = a + da, b + db
                if 0 <= c < m.shape[0] and 0 <= d < m.shape[1] and m[c, d] and not seen[c, d]:
                    seen[c, d] = True
                    st.append((c, d))
        if len(cells) >= min_area:
            ys, xs = zip(*cells)
            res.append((min(xs), min(ys), max(xs), max(ys)))
    return res


def marble_strips(out, t):
    """List row backgrounds (marbled parchment in a bevelled frame): the frame is redrawn crisp, the marble inside is smoothed, not labelled."""
    for x0, y0, x1, y1 in components(t[:, :210], 400):
        if y1 - y0 < 25 or x1 - x0 < 150:                         # the six big strips only
            continue
        ix0, ix1, iy0, iy1 = x0 + 5, x1 - 5, y0 + 3, y1 - 3          # frame: 5 texels at the sides, 3 above / below, 1 shadow row and column
        inner = t[iy0:iy1, ix0:ix1]
        e = t.copy()
        med = lambda a: np.median(a.reshape(-1, 4), axis=0).astype(np.uint8)   # noqa: E731
        left = [med(t[iy0:iy1, x0 + j]) for j in range(5)]                      # the bevel bands are noisy in the original (marble bleeds in): one colour per band
        right = [med(t[iy0:iy1, x1 - 1 - j]) for j in range(5)]
        top = [med(t[y0 + j, ix0:ix1]) for j in range(3)]
        bottom = [med(t[y1 - 1 - j, ix0:ix1]) for j in range(3)]
        for y in range(y0, y1):
            for x in range(x0, x1):
                if t[y, x, 3] != 255:
                    continue
                side, tb = x < ix0 or x >= ix1, y < iy0 or y >= iy1
                ds, dt = min(x - x0, x1 - 1 - x), min(y - y0, y1 - 1 - y)
                if side and tb:                                   # corner: mitre, the nearer edge wins
                    side = dt >= ds
                    tb = not side
                if side:
                    e[y, x] = left[x - x0] if x < ix0 else right[x1 - 1 - x]
                elif tb:
                    e[y, x] = top[y - y0] if y < iy0 else bottom[y1 - 1 - y]
        e[iy0:iy1, ix0:ix1] = med(inner)
        pa = 2
        ya, yb, xa, xb = max(0, y0 - pa), y1 + 1 + pa, max(0, x0 - pa), min(t.shape[1], x1 + 1 + pa)
        r = depixel.render(e[ya:yb, xa:xb], K, sigma=0.3)
        out[y0 * K:(y1 + 1) * K, x0 * K:(x1 + 1) * K] = r[(y0 - ya) * K:(y1 + 1 - ya) * K, (x0 - xa) * K:(x1 + 1 - xa) * K]
        img = Image.fromarray(np.ascontiguousarray(inner)).resize(((ix1 - ix0) * K, (iy1 - iy0) * K), Image.BICUBIC)
        out[iy0 * K:iy1 * K, ix0 * K:ix1 * K] = np.asarray(img)


def aa(depth):
    """Coverage 0..1 of a signed depth in texels, antialiased over one output pixel."""
    return np.clip(depth * K + 0.5, 0.0, 1.0)


def over(dst, rgb, alpha):
    """Composite a flat colour with per-pixel alpha (0..1) over the RGBA float image dst (in place)."""
    a0 = dst[..., 3:4] / 255.0
    a1 = alpha[..., None]
    ao = a1 + a0 * (1 - a1)
    col = (np.asarray(rgb, np.float32)[None, None, :3] * a1 + dst[..., :3] * a0 * (1 - a1)) / np.maximum(ao, 1e-6)
    dst[..., :3] = col
    dst[..., 3:4] = ao * 255.0


def grid(x0, y0, x1, y1):
    """Texel coordinates of the output pixel centres of the region [x0, x1) x [y0, y1) (texels)."""
    xs = x0 + (np.arange((x1 - x0) * K) + 0.5) / K
    ys = y0 + (np.arange((y1 - y0) * K) + 0.5) / K
    return np.meshgrid(xs, ys)


def small_parts(out, t):
    """Small marble strips and the white number windows of the list rows, from geometry: rounded body, soft offset shadow, no stair-stepped corners."""
    for x0, y0, x1, y1 in components(t[:, :210], 40):
        w, h = x1 - x0 + 1, y1 - y0 + 1
        if 70 <= w <= 75 and 12 <= h <= 14:                            # small marble strip: body one texel smaller than the blob (shadow column / row)
            bx1, by1 = x1, y1
            X, Y = grid(x0, y0, x1 + 1, y1 + 1)
            reg = np.zeros(X.shape + (4,), np.float32)
            over(reg, (0, 0, 0), 0.47 * aa(rr_depth(X, Y, (x0 + 1, y0 + 1, bx1 + 1, by1 + 1), 1.2)))
            body = np.asarray(Image.fromarray(np.ascontiguousarray(t[y0:by1, x0:bx1])).resize(((bx1 - x0) * K, (by1 - y0) * K), Image.BICUBIC), np.float32)
            full = np.zeros(X.shape + (4,), np.float32)
            full[:(by1 - y0) * K, :(bx1 - x0) * K] = body
            cov = aa(rr_depth(X, Y, (x0, y0, bx1, by1), 1.2))
            full[..., 3] = cov * 255.0
            a0 = reg[..., 3:4] / 255.0
            a1 = cov[..., None]
            ao = a1 + a0 * (1 - a1)
            res = np.zeros_like(reg)
            res[..., :3] = (full[..., :3] * a1 + reg[..., :3] * a0 * (1 - a1)) / np.maximum(ao, 1e-6)
            res[..., 3:4] = ao * 255.0
            out[y0 * K:(y1 + 1) * K, x0 * K:(x1 + 1) * K] = np.clip(res + 0.5, 0, 255).astype(np.uint8)
        elif w == 24 and h == 24:                                      # plain bevelled square: dark top-left edge, light bottom-right edge
            d = 1 if t[y0, x0 + 12, 0] > 130 else 2
            dark, light = tuple(t[y0 + 12, x0]), tuple(t[y1 - 12, x1])
            lightc = tuple(t[y1 - 12, x1 - (d - 1)])
            fill = tuple(t[y0 + 12, x0 + 12])
            X, Y = grid(x0, y0, x1 + 1, y1 + 1)
            reg = np.zeros(X.shape + (4,), np.float32)
            base = aa(rr_depth(X, Y, (x0, y0, x1 + 1, y1 + 1), 1.4))
            over(reg, dark[:3], base)
            over(reg, light[:3], base * aa(rr_depth(X, Y, (x0 + d, y0 + d, x1 + 1 + d, y1 + 1 + d), 1.4)))
            over(reg, fill[:3], aa(rr_depth(X, Y, (x0 + d, y0 + d, x1 + 1 - d, y1 + 1 - d), 0.9)))
            out[y0 * K:(y1 + 1) * K, x0 * K:(x1 + 1) * K] = np.clip(reg + 0.5, 0, 255).astype(np.uint8)
        elif 13 <= w <= 21 and h == 13 and t[y0 + 5, x0 + 6, 3] == 255 and (t[y0 + 5, x0 + 6, :3] > 240).all():   # white number window
            X, Y = grid(x0, y0, x1 + 1, y1 + 1)
            reg = np.zeros(X.shape + (4,), np.float32)
            over(reg, (0, 0, 0), 0.3 * aa(rr_depth(X, Y, (x0, y0, x1 + 1, y1 + 1), 0.6)))
            over(reg, (255, 255, 255), aa(rr_depth(X, Y, (x0 + 2, y0 + 1, x1, y1), 0.5)))
            out[y0 * K:(y1 + 1) * K, x0 * K:(x1 + 1) * K] = np.clip(reg + 0.5, 0, 255).astype(np.uint8)


def paint(reg, x0, y0, x1, y1, rgb, a=1.0):
    """Blend a flat rectangle (texel coordinates) over the float RGBA canvas reg."""
    sl = (slice(int(round(y0 * K)), int(round(y1 * K))), slice(int(round(x0 * K)), int(round(x1 * K))))
    A = reg[sl][..., 3:4] / 255.0
    ao = a + A * (1 - a)
    reg[sl][..., :3] = (np.asarray(rgb[:3], np.float32) * a + reg[sl][..., :3] * A * (1 - a)) / np.maximum(ao, 1e-6)
    reg[sl][..., 3:4] = ao * 255.0


def stat_boxes(out, t):
    """The six card-type counters (mini cards: coloured frame, blue lip, shaded window, brown side strip, light bar) from geometry.
    The sheet is minified 4x horizontally in game, so the rectangles follow the original 4-texel columns; the window shading is interpolated, not stepped."""
    reg = np.zeros((114 * K, 78 * K, 4), np.float32)
    rim, rim_top, lip, brown = (126, 116, 111), (149, 138, 140), (106, 112, 143), (100, 84, 56)
    grad = np.array([t[0 + k, 40, :3] for k in range(4, 13)], np.float32)
    img = Image.fromarray(np.clip(grad[:, None, :], 0, 255).astype(np.uint8)).resize((1, 9 * K), Image.BICUBIC)
    gradpx = np.asarray(img, np.float32)[:, 0, :]
    for i in range(6):
        r = 19 * i
        F, dF, bar = px(t, r + 1, 4), px(t, r + 2, 20), px(t, r + 15, 20)
        paint(reg, 2, r + 18, 78, r + 19, (0, 0, 0), 0.5)               # shadow: bottom row and right columns
        paint(reg, 74, r + 1, 78, r + 19, (0, 0, 0), 0.5)
        paint(reg, 0, r, 74, r + 1, rim_top)
        paint(reg, 0, r + 1, 74, r + 18, rim)
        paint(reg, 2, r + 1, 70, r + 17, F)
        paint(reg, 6, r + 2, 66, r + 3, dF)
        paint(reg, 6, r + 3, 66, r + 4, lip)
        paint(reg, 6, r + 4, 10, r + 13, lip)
        paint(reg, 62, r + 4, 66, r + 14, brown)
        paint(reg, 6, r + 13, 66, r + 14, brown)
        sl = (slice((r + 4) * K, (r + 13) * K), slice(10 * K, 62 * K))
        reg[sl][..., :3] = gradpx[:, None, :]
        reg[sl][..., 3] = 255.0
        paint(reg, 6, r + 15, 66, r + 16, bar)
    out[:114 * K, :78 * K] = np.clip(reg + 0.5, 0, 255).astype(np.uint8)


def rr_depth(X, Y, rect, r):
    """Depth inside the rounded rectangle rect = (x0, y0, x1, y1) (texels, negative outside) at the continuous points X, Y."""
    x0, y0, x1, y1 = rect
    cx, cy, hx, hy = (x0 + x1) / 2, (y0 + y1) / 2, (x1 - x0) / 2, (y1 - y0) / 2
    qx, qy = np.abs(X - cx) - (hx - r), np.abs(Y - cy) - (hy - r)
    return -(np.minimum(np.maximum(qx, qy), 0) + np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) - r)


def px(t, y, x):
    return tuple(float(c) for c in t[y, x])


def personal_plate(t):
    """'Personal Deck' plate drawn from geometry: gold bevelled frame, cream rim, one dark rounded field; the deck-case icon is redrawn from its texels."""
    gold = [px(t, 11, 0), px(t, 11, 1), px(t, 11, 2)]
    cream, rim, dark = px(t, 3, 60), px(t, 4, 60), px(t, 8, 60)

    def inside(X, Y):
        col = np.empty(X.shape + (4,), np.float32)
        col[:] = cream
        d = rr_depth(X, Y, (15, 4, 130, 18), 3.0)
        col[d > 0] = rim
        col[d > 2] = dark
        return col
    out = beveled(256, 32, (0, 0, 135, 22), (3.2, 3.2, 3.2, 3.2), gold, inside, (1, 1))
    icon = depixel.render_smooth(t[4:19, 4:15], K, sigma=SIGMA)
    out[4 * K:19 * K, 4 * K:15 * K] = icon
    return out


def types_plate(t):
    """'Card types in possession' plate from geometry (x < 152): gold bevelled frame, dark field, white rounded number field."""
    gold = [px(t, 11, 1), px(t, 11, 2), px(t, 11, 3)]
    dark, white = px(t, 8, 60), (255, 255, 255, 255)

    def inside(X, Y):
        col = np.empty(X.shape + (4,), np.float32)
        col[:] = dark
        col[rr_depth(X, Y, (107, 5, 143.5, 17), 2.2) > 0] = white
        return col
    return beveled(256, 32, (1, 0, 150, 22), (3.2, 3.2, 3.2, 3.2), gold, inside, (1, 1))


def types_buttons(t):
    """START pill and '?' box next to the plate, from geometry (lettering is typeset afterwards)."""
    def flat(col):
        return lambda X, Y: np.broadcast_to(np.array(col, np.float32), X.shape + (4,)).copy()
    pill = beveled(256, 32, (161, 1, 184, 11), (3.6,) * 4, [px(t, 2, 172)], flat(px(t, 8, 170)), (1, 1))
    box = beveled(256, 32, (152, 12, 162, 21), (2.4,) * 4, [], flat(px(t, 16, 152)), (1, 1))
    img = Image.fromarray(pill)
    img.alpha_composite(Image.fromarray(box))
    return np.asarray(img).copy()


TAB_HEADS = {'b77db24b', '34a9652d', 'fd0f9c3b', '92c07948'}


def tab_head(t):
    """Tab head (Deck / Fusion / Side / Trunk) from geometry: rounded top corners, three flat bevel bands, one fill, a rounded white number field;
    below the tab the stripes and the panel border / cream that the original texture carries."""
    H, W = t.shape[:2]
    outer, light, dark = px(t, 5, 0), px(t, 5, 1), px(t, 5, 2)
    fill = px(t, 3, 10)
    cream = px(t, 20, 30)
    stripes = [px(t, 15, 10), px(t, 16, 10), px(t, 17, 10)]
    edge = [px(t, 15, W - 2), px(t, 16, W - 2), px(t, 17, W - 2)]
    bx1 = int(np.nonzero(t[0, :, 3])[0].max()) + 1
    f0, f1 = white_box(t)

    def inside(X, Y):
        col = np.empty(X.shape + (4,), np.float32)
        col[:] = fill
        col[rr_depth(X, Y, (f0, 3.0, f1 + 1, 15.0), 1.2) > 0] = (255, 255, 255, 255)
        return col
    body = beveled(W, H, (0, 0, bx1, 18), (3.6, 3.6, 0, 0), [outer, light, dark], inside, (0, 0))
    c = np.zeros((H, W, 4), np.uint8)
    c[15:, 0] = outer
    c[15:, 1] = light
    c[15:, 2] = dark
    c[18:, 3:] = cream
    for i in range(3):
        c[15 + i, 3:bx1 - 3] = stripes[i]
        c[15 + i, bx1 - 3:] = edge[i]
    low = depixel.render(c, K)                       # straight rectangles: no smoothing
    out = low.copy()
    out[:15 * K] = body[:15 * K]
    return out


def cursor_frame(out):
    """The yellow list cursor from geometry: gold ring with a thin yellow line and a soft inner shadow, rounded corners; the game 9-slices it, so the straight edges stay constant."""
    x0, y0, x1, y1 = 213, 122, 255, 164
    X, Y = grid(x0 - 1, y0 - 1, x1 + 1, y1 + 2)
    reg = np.zeros(X.shape + (4,), np.float32)
    d = rr_depth(X, Y, (x0, y0, x1, y1), 0.8)
    over(reg, (0, 0, 0), 0.5 * aa(d - 3.0) * (1 - aa(d - 4.0)))
    over(reg, (214, 181, 0), aa(d) * (1 - aa(d - 3.0)))
    over(reg, (252, 255, 1), aa(d - 1.0) * (1 - aa(d - 2.0)))
    out[(y0 - 1) * K:(y1 + 2) * K, (x0 - 1) * K:(x1 + 1) * K] = np.clip(reg + 0.5, 0, 255).astype(np.uint8)


def pill_bar(t):
    """The dark ATK/DEF pill under the big card from geometry: rounded olive bar with its bevel rows, soft drop shadow."""
    H, W = t.shape[:2]
    xe = int(np.nonzero(t[..., 3].any(axis=0))[0].max()) - 1            # last opaque column + 1 (the shadow runs two texels further)
    xe = xe - 1
    rows = [tuple(float(c) for c in t[y, 60, :3]) for y in range(14)]
    X, Y = grid(0, 0, W, H)
    reg = np.zeros(X.shape + (4,), np.float32)
    body = aa(rr_depth(X, Y, (0, 0, xe, 14), 3.2))
    over(reg, (0, 0, 0), 0.5 * aa(rr_depth(X, Y, (0.5, 1.5, xe + 1.5, 15.5), 3.4)))
    prof = np.zeros(X.shape + (3,), np.float32)
    for y in range(14):
        prof[y * K:(y + 1) * K] = rows[y]
    d = rr_depth(X, Y, (0, 0, xe, 14), 3.2)
    side = (np.abs(Y - 7) < 6) & ((d > 1.0) & (d < 2.0)) & (np.minimum(X, xe - X) < 3)
    prof[side & (X < xe / 2)] = (50, 55, 44)
    prof[side & (X >= xe / 2)] = (31, 34, 29)
    a = body[..., None]
    reg[..., :3] = np.where(a > 0, (prof * a + reg[..., :3] * (reg[..., 3:4] / 255.0) * (1 - a)) / np.maximum(a + (reg[..., 3:4] / 255.0) * (1 - a), 1e-6), reg[..., :3])
    reg[..., 3] = (a[..., 0] + reg[..., 3] / 255.0 * (1 - a[..., 0])) * 255.0
    return np.clip(reg + 0.5, 0, 255).astype(np.uint8)


def redraw(t, h=None):
    if h == '3ce8d1c8':
        return pill_bar(t)
    if h == '1659d75e':
        return personal_plate(t)
    if h == '8e3f02b7':
        out = tiled(t, sigma=SIGMA)
        marble_strips(out, t)
        small_parts(out, t)
        cursor_frame(out)
        return out
    if h in HARD:
        out = depixel.render(t, K, sigma=SIGMA)
    else:
        out = depixel.render_smooth(t, K, sigma=SOFT.get(h, SIGMA), **FINE.get(h, {}))
    if h == 'c630f1be':
        stat_boxes(out, t)
        out[:, 124 * K:] = 0                         # two stray opaque columns at the sheet's right edge: the game draws them as a lone vertical line beside the Deck tab
    h, w = t.shape[:2]
    if (t == t[:, :1]).all():                       # every column equal: the game stretches it horizontally
        out[:] = out[:, w * K // 2:w * K // 2 + 1]
    if (t == t[:1]).all():                          # every row equal: stretched vertically
        out[:] = out[h * K // 2:h * K // 2 + 1]
    return out


def variants(texels_dir):
    """{hash: [texel array per palette variant]} from gen/ref_texels/deckedit/<hash>_<k>.png."""
    res = {}
    for f in sorted(os.listdir(texels_dir)):
        m = re.fullmatch(r'([0-9a-f]{8})_(\d+)\.png', f)
        if m:
            res.setdefault(m.group(1), []).append(np.asarray(Image.open(os.path.join(texels_dir, f)).convert('RGBA')))
    return res


def build(texels_dir, only):
    res = {}
    for h, vs in variants(texels_dir).items():
        if h in PIECES and (not only or h in only):
            for k, t in enumerate(vs):
                out = lettered(h, t)
                if out is None:
                    out = redraw(t, h)
                res['%s_%d' % (h, k)] = out
    return res


def register(mod, names):
    """Rows of each hash are replaced: key = 8 zeros + variant number + data hash (variants are told apart by average colour)."""
    ini = os.path.join(mod, 'textures', 'textures.ini')
    if not os.path.exists(ini):
        return
    nl = chr(10)
    s = open(ini, encoding='utf-8', newline='').read()
    for h in sorted({n[:8] for n in names}):
        s, removed = re.subn('(?m)^[0-9a-f]{16}' + h + ' = .*' + nl, '', s)
        rows = ''.join('%016x%s = %s/%s.png%s' % (int(n[9:]), h, OUT, n, nl) for n in sorted(names) if n[:8] == h)
        s = s.replace('[hashes]' + nl, '[hashes]' + nl + rows, 1)
        print('  ini', os.path.basename(os.path.dirname(os.path.dirname(ini))), h, 'removed', removed, 'rows')
    open(ini, 'w', encoding='utf-8', newline='').write(s)


def main():
    a = sys.argv[1:]
    only = set(a[a.index('--only') + 1].split(',')) if '--only' in a else set()
    res = build(os.path.join(ROOT, 'gen', 'ref_texels', 'deckedit'), only)
    for mod in MODS:
        for h, img in res.items():
            p = os.path.join(mod, 'textures', OUT, h + '.png')
            os.makedirs(os.path.dirname(p), exist_ok=True)
            Image.fromarray(img).save(p)
    print('wrote', len(res), 'textures')
    if '--register' in a:
        for mod in MODS:
            register(mod, res)


if __name__ == '__main__':
    main()
