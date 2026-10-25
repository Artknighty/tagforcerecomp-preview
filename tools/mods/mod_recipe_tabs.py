"""Redraws the "User" / "Character" tabs of the Recipe Viewer (Deck Menu > Recipe Viewer) from geometry, not from the original texels.

Two 128x32 PSP textures (4efbece8 = User, blue; 72966f8d = Character, red). Each is a tab sitting on a panel border:
  * outline = tab rectangle (rounded top corners) united with the panel rectangle; three 1-texel bevel bands (outer / light / mid) run
    along the outline, so the tab and the panel border are one continuous shape with a clean mitre where they meet;
  * flat tab fill, three flat stripes under the tab (where it overlaps the panel), cream panel below, and on the Character tab the
    red gutter at the right edge;
  * a simple sheet-of-paper icon (page, folded corner, text lines) instead of the 11x13 texel original, and the label typeset
    again with the same face as the other Deck Edit tabs.
Everything is drawn at 8x (1024x256) with signed distances; only the flat colours are sampled from the originals.
Needs gen/ref_texels/deckedit/<hash>_0.png (TF_TEX_DUMP_RAW dump, raw_*_<hash>.png).

Usage: python tools/mods/mod_recipe_tabs.py [--register]
"""
import os
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mod_deck_edit as de  # noqa: E402
from mod_deck_edit import K, aa, grid, over, px, rr_depth, typeset  # noqa: E402

ROOT = de.ROOT
W, H = 128, 32
TAB = dict(y0=3, y1=15, col=(255, 255, 255), cap=9.0, base=13.2, outline=True)

# per tab: label, tab rectangle x range, panel rectangle x range, icon box x range, text x range, icon accent
TABS = {
    '4efbece8': dict(text='User', tab=(0, 72), panel=(0, 140), icon=(4.2, 13.4), tx=(15.5, 68), accent=(40, 104, 170)),
    '72966f8d': dict(text='Character', tab=(55, 128), panel=(-12, 128), icon=(59.2, 68.4), tx=(70.5, 124), accent=(190, 20, 160)),
}


def sample(t, h):
    """Flat colours of the original: outline / light / mid bevel bands, tab fill, stripes under the tab, cream, and (Character) gutter."""
    if h == '4efbece8':
        return dict(bands=[px(t, 0, 5), px(t, 1, 5), px(t, 2, 5)], fill=px(t, 3, 40), stripes=[px(t, 15, 30), px(t, 16, 30), px(t, 17, 30)],
                    cream=px(t, 25, 60), light_r=px(t, 1, 5))
    return dict(bands=[px(t, 0, 70), px(t, 1, 70), px(t, 2, 70)], fill=px(t, 3, 100), stripes=[px(t, 15, 90), px(t, 16, 90), px(t, 17, 90)],
                cream=px(t, 25, 60), light_r=px(t, 10, 126), gutter_line=px(t, 25, 117), gutter=px(t, 25, 120))


def paper_icon(reg, box, accent, fill):
    """A sheet of paper: soft shadow, accent-coloured rim, white page, folded top-right corner and four text lines (the last one short)."""
    x0, x1 = box
    y0, y1 = 3.0, 15.4
    X, Y = grid(0, 0, W, H)
    page = (x0, y0, x1, y1)
    over(reg, (0, 0, 0), 0.38 * aa(rr_depth(X, Y, (x0 + 0.5, y0 + 0.7, x1 + 0.5, y1 + 0.7), 1.3)))
    d = rr_depth(X, Y, page, 1.3)
    over(reg, accent, aa(d))                                   # rim
    over(reg, (255, 255, 255), aa(d - 0.9))                    # page
    fold = 3.2                                                 # folded top-right corner: cut the triangle off, draw the flap on the page
    cx, cy = x1 - fold, y0 + fold
    sq = (X > cx - 0.6) & (X < x1 + 0.6) & (Y > y0 - 0.6) & (Y < cy + 0.6)
    beyond = ((X - cx) - (Y - y0)) * K * 0.7071 + 0.5          # > 0 on the outer side of the anti-diagonal
    reg[..., 3] *= 1 - np.clip(beyond, 0, 1) * sq
    flap = sq & (X < x1 - 0.2) & (Y > y0 + 0.2)
    flap_cov = np.clip(-beyond + 1.0, 0, 1) * flap * (X > cx) * (Y < cy)
    over(reg, tuple(int(c * 0.30 + 255 * 0.70) for c in accent), flap_cov)
    edge = np.clip(1 - np.abs(((X - cx) - (Y - y0))) * K * 0.7071 / 3.0, 0, 1) * sq * (X > cx - 0.3) * (Y < cy + 0.3)
    over(reg, accent, edge * (d > 0))
    for i, ln in enumerate((7.2, 9.3, 11.4, 13.5)):            # text lines
        xe = x1 - 1.6 - (2.6 if i == 3 else 0.0)
        la = aa(rr_depth(X, Y, (x0 + 1.9, ln - 0.45, xe, ln + 0.45), 0.4))
        over(reg, accent, la)


def build(t, h):
    spec, c = TABS[h], sample(t, h)
    X, Y = grid(0, 0, W, H)
    tab = rr_depth(X, Y, (spec['tab'][0], 0, spec['tab'][1], 80), 3.0)
    panel = rr_depth(X, Y, (spec['panel'][0], 15, spec['panel'][1], 80), 0.2)
    if h == '72966f8d':
        panel = np.maximum(panel, -9)                          # (the panel only needs its top edge and right border here)
    d = np.maximum(tab, panel)                                 # depth inside the union of tab and panel
    reg = np.zeros((H * K, W * K, 4), np.float32)
    ob, lb, mb = c['bands']
    light = np.where(X > 124, 1, 0)
    over(reg, ob[:3], aa(d))
    for k, col in ((1, lb), (2, mb)):
        a = aa(d - k)
        if k == 1 and 'light_r' in c:
            pass
        over(reg, col[:3], a)
    if c['light_r'] != lb:                                     # the right-hand light band is a touch brighter in the original
        over(reg, c['light_r'][:3], aa(d - 1) * (1 - aa(d - 2)) * light)
    # interior (depth > 3): tab fill above row 15, stripes under the tab, cream panel below
    inner = aa(d - 3)
    x0, x1 = spec['tab']
    in_tab = (X > x0 + 2.5) & (X < x1 - 2.5)
    layer = np.zeros(reg.shape, np.float32)
    layer[..., 3] = 255
    layer[..., :3] = c['cream'][:3]
    layer[(Y < 15)] = c['fill']
    for i, s in enumerate(c['stripes']):
        m = (Y >= 15 + i) & (Y < 16 + i) & in_tab
        layer[m] = s
    m = (Y >= 15) & (Y < 18) & ~in_tab
    layer[m] = c['cream']
    if 'gutter' in c:                                          # red gutter down the right edge of the Character panel
        g = (X >= 117) & (Y >= 18)
        layer[g] = c['gutter']
        layer[(X >= 117) & (X < 118) & (Y >= 18)] = c['gutter_line']
    over(reg, np.zeros(3), np.zeros(d.shape))
    a0 = reg[..., 3:4] / 255.0
    a1 = inner[..., None]
    ao = a1 + a0 * (1 - a1)
    reg[..., :3] = (layer[..., :3] * a1 + reg[..., :3] * a0 * (1 - a1)) / np.maximum(ao, 1e-6)
    reg[..., 3:4] = ao * 255.0
    ic = np.zeros_like(reg)                                    # the icon has its own layer: the folded-corner cut must not punch the tab
    paper_icon(ic, spec['icon'], spec['accent'], c['fill'])
    a0, a1 = reg[..., 3:4] / 255.0, ic[..., 3:4] / 255.0
    ao = a1 + a0 * (1 - a1)
    reg[..., :3] = (ic[..., :3] * a1 + reg[..., :3] * a0 * (1 - a1)) / np.maximum(ao, 1e-6)
    reg[..., 3:4] = ao * 255.0
    out = np.clip(reg + 0.5, 0, 255).astype(np.uint8)
    item = dict(TAB, text=spec['text'], x0=spec['tx'][0] - 0.5)
    return typeset(out, item, spec['tx'][1], c['fill'][:3])


def main():
    a = sys.argv[1:]
    res = {}
    for h in TABS:
        t = np.asarray(Image.open(os.path.join(ROOT, 'gen', 'ref_texels', 'deckedit', h + '_0.png')).convert('RGBA'))
        res[h + '_0'] = build(t, h)
    for mod in de.MODS:
        for n, img in res.items():
            p = os.path.join(mod, 'textures', de.OUT, n + '.png')
            os.makedirs(os.path.dirname(p), exist_ok=True)
            Image.fromarray(img).save(p)
    print('wrote', len(res), 'textures')
    if '--register' in a:
        for mod in de.MODS:
            de.register(mod, res)


if __name__ == '__main__':
    main()
