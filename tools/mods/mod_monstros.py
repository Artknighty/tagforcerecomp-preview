"""Generates the sprites of the "Monsters on the field" mod (gen/mods/MonstersOnField/sprites/<id>.rgba) from the large card art.
Usage: python tools/mods/mod_monstros.py [texture_pack] [mod_folder] [id ...]
  defaults: gen/mods/PPSSPP_MikeyMK/textures (read only: third-party packs are never modified) and gen/mods/MonstersOnField; with no ids = the monsters of the starter deck (list below).
Each large art (cards/large/<id>.png, 512x256 atlas: the art sits in the 312x240 block at the top-left corner) becomes an RGBA sprite with the edges
faded in an oval (the monster is not cut out: the art background stays; a real cut-out needs segmentation or hand-drawn sprites). .rgba format:
w (uint32 LE), h (uint32 LE), w*h pixels of 4 bytes R,G,B,A (straight alpha). No game data goes into the repository (gen/ is outside git)."""
import os, struct, sys
import numpy as np
from PIL import Image

RAIZ = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
# monsters of the starting Water deck
DECK_LEO = [5066, 5759, 5874, 4453, 4076, 4456, 4077, 4367, 4219, 5060, 4926, 5803, 5026, 4530, 5896, 5521, 4653, 4359, 4605]
ART = (0, 0, 312, 240)


def oval(w, h, interno=0.70, externo=1.0):
    y, x = np.mgrid[0:h, 0:w].astype(np.float32)
    r = np.sqrt(((x + 0.5 - w / 2) / (w / 2)) ** 2 + ((y + 0.5 - h / 2) / (h / 2)) ** 2)
    t = np.clip((externo - r) / (externo - interno), 0, 1)
    return t * t * (3 - 2 * t)   # smoothstep


def gerar(pacote, saida, cid):
    f = os.path.join(pacote, 'cards', 'large', '%d.png' % cid)
    if not os.path.exists(f):
        print('sem arte', cid); return False
    im = Image.open(f).convert('RGBA').crop(ART)
    a = np.asarray(im).astype(np.float32)
    a[..., 3] = a[..., 3] * oval(im.width, im.height)
    out = np.clip(a + 0.5, 0, 255).astype(np.uint8)
    with open(os.path.join(saida, 'sprites', '%d.rgba' % cid), 'wb') as g:
        g.write(struct.pack('<II', im.width, im.height)); g.write(out.tobytes())
    return True


if __name__ == '__main__':
    pacote = sys.argv[1] if len(sys.argv) > 1 else os.path.join(RAIZ, 'gen', 'mods', 'PPSSPP_MikeyMK', 'textures')
    saida = sys.argv[2] if len(sys.argv) > 2 else os.path.join(RAIZ, 'gen', 'mods', 'MonstersOnField')
    ids = [int(x) for x in sys.argv[3:]] or DECK_LEO
    os.makedirs(os.path.join(saida, 'sprites'), exist_ok=True)
    n = sum(gerar(pacote, saida, i) for i in ids)
    print('%d sprite(s) em %s' % (n, os.path.join(saida, 'sprites')))
