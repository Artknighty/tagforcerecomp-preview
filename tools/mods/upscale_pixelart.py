#!/usr/bin/env python3
"""Upscales one PSP pixel-art texture (a sprite sheet) from the ORIGINAL texels, instead of re-upscaling an already-upscaled pack.

Method (tested on the player sprites): the texture is first enlarged 2x with nearest neighbour (hard pixels, no ringing), colour is
spread under the transparent area, Real-ESRGAN (`realesrgan-x4plus-anime`) runs on that, and the result is brought to the target size
with Lanczos. The silhouette comes from the original alpha (bicubic + soft threshold), not from the network, so there are no halos.

  python tools/mods/upscale_pixelart.py <original.png> <out.png> [--factor 4]

The original comes from a texture dump of the game: `TF_TEX_DUMP=<folder>` (see docs/internal/ENV.md); with no mods loaded the
unmodified texture lands in `<folder>/faltam/<hash>.png`.
"""
import argparse
import os
import subprocess
import tempfile

import numpy as np
from PIL import Image, ImageFilter

RAIZ = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
FERR = os.path.join(RAIZ, "gen", "ferramentas_ai", "realesrgan")


def spread_colour(im):
    """RGB of an RGBA image with the colour of solid texels pushed into the transparent area (no dark/green halo after the network)."""
    a = np.array(im.convert("RGBA"))
    rgb, solid = a[..., :3].copy(), a[..., 3] > 0
    mask = Image.fromarray((solid * 255).astype(np.uint8))
    pre = Image.fromarray(rgb)
    for _ in range(8):
        rgb = np.where(solid[..., None], rgb, np.array(pre.filter(ImageFilter.MaxFilter(3))))
        mask = mask.filter(ImageFilter.MaxFilter(3))
        solid = np.array(mask) > 0
        pre = Image.fromarray(rgb)
    return Image.fromarray(rgb)


def smooth_alpha(alpha, size):
    return alpha.resize(size, Image.BICUBIC).point(lambda v: 0 if v < 100 else (255 if v > 155 else int((v - 100) * 255 / 55)))


def upscale(src, factor=4, model="realesrgan-x4plus-anime"):
    im = Image.open(src).convert("RGBA")
    out_size = (im.width * factor, im.height * factor)
    with tempfile.TemporaryDirectory() as d:
        fin, fout = os.path.join(d, "in.png"), os.path.join(d, "out.png")
        spread_colour(im).resize((im.width * 2, im.height * 2), Image.NEAREST).save(fin)
        subprocess.run([os.path.join(FERR, "realesrgan-ncnn-vulkan.exe"), "-i", fin, "-o", fout, "-n", model, "-s", "4"],
                       cwd=FERR, check=True, capture_output=True)
        rgb = Image.open(fout).convert("RGB").resize(out_size, Image.LANCZOS)
    res = rgb.convert("RGBA")
    res.putalpha(smooth_alpha(im.getchannel("A"), out_size))
    return res


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--factor", type=int, default=4)
    a = ap.parse_args()
    upscale(a.src, a.factor).save(a.dst)
    print("ok", a.dst)
