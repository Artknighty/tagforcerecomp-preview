"""Redraws flat pixel-art UI pieces as clean shapes (no filtering, no blur).

Each PSP texel is one flat colour ("label"). The label map is interpolated (one-hot per label, tent kernel) at the output
resolution and every output sample takes the label with the largest weight, so:
  * straight band edges stay exactly on the texel boundaries and perfectly sharp,
  * stair-steps and chamfered corners become smooth curves,
  * every area keeps exactly the colour the artist picked (no new colours).
Output is supersampled and averaged (premultiplied) for anti-aliasing.
"""
import numpy as np

K = 8   # output pixels per PSP texel (EnhancedTextures sheets are 8x)


def texel(a, u, v, k=K):
    """Original PSP colour of texel (u, v) taken from an upscaled sheet: median of its inner block."""
    m = k // 4
    return np.median(a[v * k + m:(v + 1) * k - m, u * k + m:(u + 1) * k - m].reshape(-1, 4), axis=0)


def sheet_texels(a, k=K):
    """Whole sheet back to PSP resolution as an (h, w, 4) uint8 array."""
    h, w = a.shape[0] // k, a.shape[1] // k
    m = k // 4
    blk = a[:h * k, :w * k].reshape(h, k, w, k, 4)[:, m:k - m, :, m:k - m]
    return np.median(blk.transpose(0, 2, 1, 3, 4).reshape(h, w, -1, 4), axis=2).astype(np.uint8)


def _labels(t):
    """(h, w, 4) uint8 -> (label ids, palette). Anti-aliasing texels of the original (same colour as a stronger neighbour) are dropped."""
    h, w = t.shape[:2]
    t = t.copy()
    t[t[..., 3] == 0] = 0
    keep = np.ones((h, w), bool)
    for y in range(h):
        for x in range(w):
            p = t[y, x]
            if p[3] == 0 or p[3] == 255:
                continue
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    yy, xx = y + dy, x + dx
                    if (dy or dx) and 0 <= yy < h and 0 <= xx < w:
                        q = t[yy, xx]
                        if q[3] > p[3] and (q[:3] == p[:3]).all() and p[3] < 200:
                            keep[y, x] = False
    t[~keep] = 0
    tr = (t[..., 3] > 0) & (t[..., 3] < 255)
    t[tr & (t[..., :3].max(2) > 8)] = 0                     # translucent coloured texels are anti-aliasing of the artist: dropped
    sh = tr & (t[..., :3].max(2) <= 8)                       # translucent black = drop shadow: two strengths only
    t[sh & (t[..., 3] <= 100), 3] = 64
    t[sh & (t[..., 3] > 100), 3] = 128
    # single texels whose colour no 4-neighbour shares are corner dots of the original art: take the commonest colour around them
    src = t.copy()
    for y in range(h):
        for x in range(w):
            p = src[y, x]
            if not any(0 <= y + dy < h and 0 <= x + dx < w and (src[y + dy, x + dx] == p).all() for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1))):
                around = [tuple(src[y + dy, x + dx]) for dy in (-1, 0, 1) for dx in (-1, 0, 1) if (dy or dx) and 0 <= y + dy < h and 0 <= x + dx < w]
                t[y, x] = max(set(around), key=around.count)
    pal, ids = np.unique(t.reshape(-1, 4), axis=0, return_inverse=True)
    return ids.reshape(h, w), pal


def _tent(n_in, n_out):
    """(n_out, n_in) tent weights between texel centres, edge-clamped; output sample i sits at (i + 0.5) / n_out * n_in."""
    x = (np.arange(n_out) + 0.5) / n_out * n_in - 0.5
    i0 = np.floor(x).astype(int)
    f = x - i0
    m = np.zeros((n_out, n_in), np.float32)
    for off, wgt in ((0, 1 - f), (1, f)):
        idx = np.clip(i0 + off, 0, n_in - 1)
        np.add.at(m, (np.arange(n_out), idx), wgt)
    return m


def _gauss_w(n_in, n_out, sig):
    """(n_out, n_in) gaussian weights between texel centres (sigma in texels), normalised over the valid texels (edge-clamped)."""
    x = (np.arange(n_out) + 0.5) / n_out * n_in
    c = np.arange(n_in) + 0.5
    m = np.exp(-((x[:, None] - c[None, :]) ** 2) / (2 * sig * sig)).astype(np.float32)
    return m / m.sum(1, keepdims=True)


def render(t, k=K, ss=3, sigma=None):
    """Redraw an (h, w, 4) uint8 texel block as an (h*k, w*k, 4) uint8 RGBA image."""
    ids, pal = _labels(t)
    h, w = ids.shape
    Y, X = h * k * ss, w * k * ss
    wy, wx = (_gauss_w(h, Y, sigma), _gauss_w(w, X, sigma)) if sigma else (_tent(h, Y), _tent(w, X))
    best = np.full((Y, X), -1.0, np.float32)
    lab = np.zeros((Y, X), np.int32)
    for li in range(len(pal)):
        f = wy @ (ids == li).astype(np.float32) @ wx.T
        up = f > best
        best[up] = f[up]
        lab[up] = li
    rgba = pal.astype(np.float32)[lab]
    a = rgba[..., 3:4] / 255
    pm = np.concatenate([rgba[..., :3] * a, rgba[..., 3:4]], axis=2)
    pm = pm.reshape(h * k, ss, w * k, ss, 4).mean(axis=(1, 3))
    al = pm[..., 3:4]
    rgb = np.where(al > 0, pm[..., :3] / np.maximum(al / 255, 1e-6), 0)
    return np.clip(np.concatenate([rgb, al], axis=2) + 0.5, 0, 255).astype(np.uint8)


def paste_region(out, src_tex, u0, u1, v0, v1, k=K, sigma=None):
    """Redraw texels [u0,u1) x [v0,v1) of the sheet and overwrite that area of `out` (the upscaled sheet array)."""
    out[v0 * k:v1 * k, u0 * k:u1 * k] = render(src_tex[v0:v1, u0:u1], k, sigma=sigma)


def flat_col(out, u, v0, v1, k=K):
    """Texel column the game stretches horizontally: every pixel column equals its centre column."""
    out[v0 * k:v1 * k, u * k:(u + 1) * k] = out[v0 * k:v1 * k, u * k + k // 2:u * k + k // 2 + 1]


def flat_row(out, u0, u1, v, k=K):
    """Texel row the game stretches vertically: every pixel row equals its centre row."""
    out[v * k:(v + 1) * k, u0 * k:u1 * k] = out[v * k + k // 2:v * k + k // 2 + 1, u0 * k:u1 * k]
