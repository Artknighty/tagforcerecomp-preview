"""Flicker finder for the F8 black-box sequences (seq_<n> folders of 90 presented frames, R = real, M = in-between).

A flicker is a block of the screen where the two real frames around an in-between frame AGREE (nothing moved there) but the
in-between frame shows something else (a card squashed, a panel faded, a sprite swapped). Blocks that move are ignored, so fast
camera/hand motion does not drown the result.

  python tools/analysis/flick_seq.py <captures folder | seq folder> [--png out_dir] [--top 6]

Prints, per sequence, the in-between frames ranked by flagged blocks (16x16 px of the 960x544 image), the bounding box of the
flagged area and whether the pattern repeats; --png writes prev | mid | next | mask strips for the worst frames.
"""
import glob, os, sys
import numpy as np
from PIL import Image

B = 16


def ld(f):
    return np.asarray(Image.open(f).convert('RGB')).astype(np.int16)


def blocks(mask):
    h, w = mask.shape
    h2, w2 = h // B * B, w // B * B
    return mask[:h2, :w2].reshape(h2 // B, B, w2 // B, B).mean(axis=(1, 3))


def analyse(seq, png=None, top=6):
    fs = sorted(glob.glob(os.path.join(seq, '*.png')))
    kinds = [os.path.basename(f).split('_')[1] for f in fs]
    R = [i for i, k in enumerate(kinds) if k == 'R']
    rows = []
    for a, b in zip(R, R[1:]):
        A, Bf = ld(fs[a]), ld(fs[b])
        agree = np.abs(A - Bf).max(axis=2) < 16
        for m in range(a + 1, b):
            M = ld(fs[m])
            diff = np.minimum(np.abs(M - A).max(axis=2), np.abs(M - Bf).max(axis=2))
            mk = agree & (diff > 48)
            bl = blocks(mk) > 0.25
            n = int(bl.sum())
            if n:
                ys, xs = np.where(bl)
                rows.append((n, m, (int(xs.min()) * B, int(ys.min()) * B, int(xs.max() + 1) * B, int(ys.max() + 1) * B), a, b, mk))
    rows.sort(key=lambda r: -r[0])
    print('%s: %d in-between frames flagged of %d' % (seq, len(rows), len(fs) - len(R)))
    for n, m, bb, a, b, mk in rows[:top]:
        print('  frame %2d (%s)  blocks=%3d  box=%s' % (m, os.path.basename(fs[m]), n, bb))
    if png and rows:
        os.makedirs(png, exist_ok=True)
        for n, m, bb, a, b, mk in rows[:top]:
            x0, y0, x1, y1 = bb
            x0, y0 = max(0, x0 - 32), max(0, y0 - 32)
            x1, y1 = x1 + 32, y1 + 32
            tiles = [np.asarray(Image.open(fs[i]).convert('RGB'))[y0:y1, x0:x1] for i in (a, m, b)]
            msk = np.repeat((mk[y0:y1, x0:x1] * 255).astype(np.uint8)[:, :, None], 3, axis=2)
            Image.fromarray(np.concatenate(tiles + [msk], axis=1)).save(os.path.join(png, '%s_f%02d.png' % (os.path.basename(seq), m)))
    return rows


if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    png = sys.argv[sys.argv.index('--png') + 1] if '--png' in sys.argv else None
    top = int(sys.argv[sys.argv.index('--top') + 1]) if '--top' in sys.argv else 6
    if png and png in args: args.remove(png)
    if str(top) in args and '--top' in sys.argv: args.remove(str(top))
    base = args[0]
    seqs = sorted(glob.glob(os.path.join(base, 'seq_*'))) or [base]
    for s in seqs:
        analyse(s, png, top)
