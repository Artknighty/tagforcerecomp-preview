"""Builds the duel cursor hand sheet (texture 0a13d88f, 1024x2048 at 8x) from a redrawn hand sheet supplied as an image.

The supplied sheet has the same 28 hands in the same order (rows top to bottom, left to right) on a flat dark background.
Each hand is cut out (alpha from the distance to the background colour, edge colours un-mixed from the background), then
scaled uniformly to fit the box of the matching hand of the current sheet and centred on it, so the game's UV rectangles
keep working. A soft drop shadow is added under every hand.

Usage: python tools/mods/mod_duel_hands_sheet.py SHEET.png|jpg [--out PREVIEW.png] [--register]
Without --out the result is written to textures/duel_hands_hd/0a13d88f.png of the mod.
"""
import os
import sys
import numpy as np
from PIL import Image

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
MODS = [os.path.join(ROOT, 'mods', 'EnhancedTextures'), os.path.join(ROOT, 'gen', 'mods', 'EnhancedTextures')]
OLD = 'textures/mikeymk/ui/duels/hands.png'
OUTDIR = 'duel_hands_hd'
HASH = '0a13d88f'
SIZE = (1024, 2048)


def runs(profile, gap):
    """Index ranges where the profile is True, merging runs separated by fewer than `gap` False entries."""
    idx = np.where(profile)[0]
    if len(idx) == 0:
        return []
    out, s, p = [], idx[0], idx[0]
    for i in idx[1:]:
        if i - p > gap:
            out.append((s, p + 1))
            s = i
        p = i
    out.append((s, p + 1))
    return out


def boxes(mask, grow, step=4):
    """Bounding boxes (x0, y0, x1, y1) of the connected blobs of a mask (blobs closer than `grow` px are merged), ordered by
    row (centre y in bands of 1/14 of the sheet height) then by x."""
    h, w = mask.shape
    sm = mask[:h // step * step, :w // step * step].reshape(h // step, step, w // step, step).any((1, 3))
    r = max(1, grow // step)
    k = np.zeros_like(sm)
    ys, xs = np.nonzero(sm)
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            if dy * dy + dx * dx <= r * r:
                k[np.clip(ys + dy, 0, k.shape[0] - 1), np.clip(xs + dx, 0, k.shape[1] - 1)] = True
    lab = np.zeros(k.shape, int)
    n = 0
    for y, x in zip(*np.nonzero(k)):
        if lab[y, x]:
            continue
        n += 1
        st = [(y, x)]
        lab[y, x] = n
        while st:
            cy, cx = st.pop()
            for ny, nx in ((cy + 1, cx), (cy - 1, cx), (cy, cx + 1), (cy, cx - 1)):
                if 0 <= ny < k.shape[0] and 0 <= nx < k.shape[1] and k[ny, nx] and not lab[ny, nx]:
                    lab[ny, nx] = n
                    st.append((ny, nx))
    res = []
    for i in range(1, n + 1):
        m = (lab == i) & sm
        yy, xx = np.nonzero(m)
        if len(yy) < 30:
            continue
        res.append((xx.min() * step, yy.min() * step, (xx.max() + 1) * step, (yy.max() + 1) * step))
    return res


def order(bx, band=None):
    """Column of the sheet (quarters of its width, by the box centre), then top to bottom: both sheets hold 7 hands per column."""
    return sorted(bx, key=lambda b: (min(3, int((b[0] + b[2]) / 2 // 256)), b[1]))


def split_first_row(old, bx):
    """The two big hands of the first row touch each other in the current sheet: cut that blob at its emptiest column."""
    out = []
    for x0, y0, x1, y1 in bx:
        if x1 - x0 > 400:
            prof = (old[y0:y1, x0:x1, 3] >= 200).sum(0).astype(float)
            lo, hi = int((x1 - x0) * 0.35), int((x1 - x0) * 0.65)
            cut = x0 + lo + int(np.argmin(prof[lo:hi]))
            out += [(x0, y0, cut, y1), (cut, y0, x1, y1)]
        else:
            out.append((x0, y0, x1, y1))
    return out


def blur(a, s):
    r = int(s * 3 + 1)
    k = np.exp(-0.5 * (np.arange(-r, r + 1) / s) ** 2)
    k /= k.sum()
    p = np.pad(a, ((r, r), (0, 0)), mode='edge')
    a = sum(k[i] * p[i:i + a.shape[0]] for i in range(len(k)))
    p = np.pad(a, ((0, 0), (r, r)), mode='edge')
    return sum(k[i] * p[:, i:i + a.shape[1]] for i in range(len(k)))


def old_sheet():
    for mod in MODS:
        p = os.path.join(mod, OLD)
        if os.path.exists(p):
            return np.asarray(Image.open(p).convert('RGBA'))
    sys.exit('missing ' + OLD)


def cut_new(path):
    """RGBA cut-outs (premultiplied-free) of the 28 hands of the supplied sheet, plus their boxes."""
    im = np.asarray(Image.open(path).convert('RGB')).astype(np.float32)
    bg = np.median(np.concatenate([im[:6].reshape(-1, 3), im[-6:].reshape(-1, 3)]), axis=0)
    dist = np.sqrt(((im - bg) ** 2).sum(-1))
    mask = dist > 40
    bx = order(boxes(mask, 6), 100)
    a = np.clip((dist - 14) / 50, 0, 1)
    a = np.where(dist > 64, 1.0, a)
    c = (im - (1 - a)[..., None] * bg) / np.maximum(a, 1e-3)[..., None]
    c = np.where(a[..., None] < 0.999, np.minimum(c, im), c)        # edge mix is with the black outline
    c = np.clip(c, 0, 255)
    return np.dstack([c, a * 255]).astype(np.float32), bx


def main():
    new, nbx = cut_new(sys.argv[1])
    old = old_sheet()
    obx = order(split_first_row(old, boxes(old[..., 3] >= 200, 8)), 100)
    if len(nbx) != len(obx):
        sys.exit('hand count differs: new %d, old %d' % (len(nbx), len(obx)))
    out = np.zeros((SIZE[1], SIZE[0], 4), np.float32)
    shadow = np.zeros(SIZE[::-1], np.float32)
    for (nx0, ny0, nx1, ny1), (ox0, oy0, ox1, oy1) in zip(nbx, obx):
        src = Image.fromarray(np.clip(new[ny0:ny1, nx0:nx1], 0, 255).astype(np.uint8), 'RGBA')
        k = min((ox1 - ox0) / src.width, (oy1 - oy0) / src.height)
        w, h = max(1, round(src.width * k)), max(1, round(src.height * k))
        # resample with premultiplied colour so the edges stay black
        arr = np.asarray(src).astype(np.float32)
        pm = Image.fromarray(np.dstack([arr[..., :3] * arr[..., 3:4] / 255, arr[..., 3:4]]).astype(np.uint8), 'RGBA')
        pm = np.asarray(pm.resize((w, h), Image.LANCZOS)).astype(np.float32)
        al = np.clip(pm[..., 3], 0, 255)
        col = np.where(al[..., None] > 0.5, pm[..., :3] / np.maximum(al[..., None], 1) * 255, 0)
        px, py = (ox0 + ox1 - w) // 2, (oy0 + oy1 - h) // 2
        out[py:py + h, px:px + w, :3] = np.clip(col, 0, 255)
        out[py:py + h, px:px + w, 3] = al
        shadow[py:py + h, px:px + w] = np.maximum(shadow[py:py + h, px:px + w], al / 255)
    sh = blur(np.roll(np.roll(shadow, 7, 0), 5, 1), 7.0) * 0.5
    a = out[..., 3] / 255
    A = a + sh * (1 - a)
    rgb = np.where(A[..., None] > 1e-4, out[..., :3] * a[..., None] / np.maximum(A[..., None], 1e-4), 0)
    res = np.clip(np.dstack([rgb, A * 255]) + 0.5, 0, 255).astype(np.uint8)
    img = Image.fromarray(res, 'RGBA')
    if '--out' in sys.argv:
        img.save(sys.argv[sys.argv.index('--out') + 1])
        print('wrote preview')
        return
    for mod in MODS:
        if not os.path.isdir(mod):
            continue
        dst = os.path.join(mod, 'textures', OUTDIR, HASH + '.png')
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        img.save(dst)
        print('wrote', dst)
        if '--register' in sys.argv:
            register(mod)


def register(mod):
    rel = OUTDIR + '/' + HASH + '.png'
    for name, sep in (('textures/textures.ini', ' = '), ('origem.tsv', '\t')):
        p = os.path.join(mod, name)
        if not os.path.exists(p):
            continue
        lines = open(p, encoding='utf-8', newline='').read().split('\n')
        for i, l in enumerate(lines):
            if HASH not in l:
                continue
            if name.endswith('.ini'):
                k, v = l.split(sep, 1)
                if not k.endswith(HASH):
                    continue
                lines[i] = k + sep + rel + ('\r' if v.endswith('\r') else '')
            else:
                t = l.rstrip('\r').split('\t')
                if t[0] != HASH:
                    continue
                t[2], t[3] = 'DuelHandsHD', rel
                lines[i] = '\t'.join(t) + ('\r' if l.endswith('\r') else '')
        open(p, 'w', encoding='utf-8', newline='').write('\n'.join(lines))
        print('registered in', p)


if __name__ == '__main__':
    main()
