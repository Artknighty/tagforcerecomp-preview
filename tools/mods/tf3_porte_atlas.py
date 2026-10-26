"""Puts the mini card of a Tag Force 3 card into the deck editor thumbnail atlas of Tag Force 1 (deck/all_<l>.ehp -> cis_all.gim).

The trunk grid of the deck editor does not draw the card art files (card/cardm_*.cip, cardh_*.cip): it draws small card faces cut from one
atlas image, cis_all.gim, so a card whose art was swapped there still showed the old face in the grid. This tool swaps the face too.

Formats (checked against the game's own decoded textures):
  card/cardm_<l>.cip block of an id: a 0x80-byte GIM header, then 64x64 DXT1 data (2048 bytes) = the whole card face squeezed into a square.
    DXT1 blocks are stored as {4-byte indices, then the two RGB565 colors} (the usual order swapped); the data runs into the next unit.
  cis_all.gim (GIM "MIG.00.1PSP"): one image block (format 5 = 8-bit indexed, PSP swizzled in 16x8-byte tiles, 512x512, N pages one after the
    other) and one palette block with one 256 x RGBA8888 palette per page. A page holds 23 x 17 cells of 22 x 30 pixels, 391 per page;
    the cell order is NOT the card index (the drift grows through the file), so the cell of a card is found by comparing images.
Usage:  python tools/mods/tf3_porte_atlas.py <pairs "<tf3 id>:<slot id>" ...>   (checks the cell lookup and writes gen/ctl/atlas_check.png)
"""
import os
import struct
import sys

import numpy as np
from PIL import Image

CW, CH, COLS, ROWS, PAGE = 22, 30, 23, 17, 512
PER_PAGE = COLS * ROWS


def dxt1(data, w, h):
    """RGB image from DXT1 blocks stored as {indices, colors}"""
    img = np.zeros((h, w, 3), np.uint8)
    bw = w // 4
    for i in range(len(data) // 8):
        bits, c0, c1 = struct.unpack_from("<IHH", data, i * 8)
        bx, by = (i % bw) * 4, (i // bw) * 4
        if by >= h:
            break

        def rgb(c):
            return ((c >> 11 & 31) * 255 // 31, (c >> 5 & 63) * 255 // 63, (c & 31) * 255 // 31)
        a, b = rgb(c0), rgb(c1)
        if c0 > c1:
            cols = [a, b, tuple((2 * x + y) // 3 for x, y in zip(a, b)), tuple((x + 2 * y) // 3 for x, y in zip(a, b))]
        else:
            cols = [a, b, tuple((x + y) // 2 for x, y in zip(a, b)), (0, 0, 0)]
        for k in range(16):
            img[by + k // 4, bx + k % 4] = cols[bits >> (2 * k) & 3]
    return Image.fromarray(img)


def face(cardm, cid):
    """the 64x64 card face of an id from the bytes of a cardm_<l>.cip"""
    lo = struct.unpack_from("<I", cardm, 0x14)[0]
    unit = struct.unpack_from("<I", cardm, 0x30 + (cid - lo) * 8)[0]
    return dxt1(cardm[unit * 0x800 + 0x80:unit * 0x800 + 0x80 + 2048], 64, 64)


def _swizzle(lin, rowb, h, inverse=False):
    """PSP swizzle in tiles of 16 bytes x 8 rows; inverse=True turns the stored order into rows"""
    out = bytearray(len(lin))
    bpr = rowb // 16
    pos = 0
    for by in range(0, h, 8):
        for bx in range(bpr):
            for r in range(8):
                o = (by + r) * rowb + bx * 16
                if inverse:
                    out[o:o + 16] = lin[pos:pos + 16]
                else:
                    out[pos:pos + 16] = lin[o:o + 16]
                pos += 16
    return bytes(out)


class Atlas:
    def __init__(self, gim):
        self.gim = bytearray(gim)
        blocks, p = {}, 0x30
        while p < len(gim):
            bid, _, size = struct.unpack_from("<HHI", gim, p)
            blocks[bid] = (p, size)
            p += size
        (ip, isz), (pp, psz) = blocks[4], blocks[5]
        n = isz // (PAGE * PAGE)
        self.n = n
        self.pix0, self.pal0 = ip + isz - n * PAGE * PAGE, pp + psz - n * 1024
        self.lin = [bytearray(_swizzle(gim[self.pix0 + i * PAGE * PAGE:self.pix0 + (i + 1) * PAGE * PAGE], PAGE, PAGE, True)) for i in range(n)]
        self.pal = [np.frombuffer(gim[self.pal0 + i * 1024:self.pal0 + (i + 1) * 1024], np.uint8).reshape(256, 4)[:, :3].astype(np.int32) for i in range(n)]
        self._cells = None

    def cell_xy(self, i):
        p, c = divmod(i, PER_PAGE)
        return p, (c % COLS) * CW, (c // COLS) * CH

    def cell_rgb(self, i):
        """RGB of the 22x28 part of a cell that holds the card face (the first two rows are margin)"""
        p, x, y = self.cell_xy(i)
        page = np.frombuffer(bytes(self.lin[p]), np.uint8).reshape(PAGE, PAGE)
        return self.pal[p][page[y + 2:y + CH, x:x + CW]].astype(np.float32)

    @staticmethod
    def squeeze(img):
        """a card face (64x64) as it looks in a cell (22x28)"""
        return np.array(img.crop((0, 1, 63, 62)).resize((CW, CH - 2), Image.BOX)).astype(np.float32)

    def find(self, img):
        """(cell, error, error of the second best) of the cell that shows a card face"""
        if self._cells is None:
            self._cells = np.stack([self.cell_rgb(i) for i in range(self.n * PER_PAGE)])
        d = np.abs(self._cells - self.squeeze(img)).mean(axis=(1, 2, 3))
        o = np.argsort(d)
        return int(o[0]), float(d[o[0]]), float(d[o[1]])

    def put(self, i, img):
        """draws a card face into cell i with the colors of the page palette"""
        p, x, y = self.cell_xy(i)
        rgb = self.squeeze(img).astype(np.int32)
        pal = self.pal[p]
        idx = np.abs(rgb[:, :, None, :] - pal[None, None, :, :]).sum(axis=3).argmin(axis=2).astype(np.uint8)
        page = np.frombuffer(bytes(self.lin[p]), np.uint8).reshape(PAGE, PAGE).copy()
        page[y + 2:y + CH, x:x + CW] = idx
        self.lin[p] = bytearray(page.tobytes())
        if self._cells is not None:
            self._cells[i] = self.cell_rgb(i)

    def bytes(self):
        out = bytearray(self.gim)
        for i in range(self.n):
            out[self.pix0 + i * PAGE * PAGE:self.pix0 + (i + 1) * PAGE * PAGE] = _swizzle(bytes(self.lin[i]), PAGE, PAGE)
        return bytes(out)


def porta(gim, cardm_tf1, cardm_tf3, slots, report=None):
    """slots {TF3 id: TF1 id}: the cell that shows the TF1 card (found by image) gets the face of the TF3 card"""
    a = Atlas(gim)
    for cid, slot in slots.items():
        cell, e1, e2 = a.find(face(cardm_tf1, slot))
        assert e1 < e2 - 2, "cell of card %d is not unique (%.1f vs %.1f)" % (slot, e1, e2)
        a.put(cell, face(cardm_tf3, cid))
        if report is not None:
            report.append((cid, slot, cell, e1, e2))
    return a.bytes()


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    raiz = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    sys.path.insert(0, os.path.join(raiz, "tools", "game"))
    import ehp
    jogo = os.path.join(raiz, "gen", "jogo", "PSP_GAME", "USRDIR")
    pares = {int(a.split(":")[0]): int(a.split(":")[1]) for a in sys.argv[1:]}
    _, mem = ehp.ler(open(os.path.join(jogo, "deck", "all_e.ehp"), "rb").read())
    gim = dict(mem)["cis_all.gim"]
    rel = []
    novo = porta(gim, open(os.path.join(jogo, "card", "cardm_e.cip"), "rb").read(), open(os.path.join(raiz, "ref", "tf3", "card", "cardm_e.cip"), "rb").read(), pares, rel)
    print(rel)
    a, b = Atlas(gim), Atlas(novo)
    tiles = []
    for cid, slot, cell, _, _ in rel:
        for at in (a, b):
            p, x, y = at.cell_xy(cell)
            page = np.frombuffer(bytes(at.lin[p]), np.uint8).reshape(PAGE, PAGE)
            tiles.append(Image.fromarray(at.pal[p][page[y:y + CH, x:x + CW]].astype(np.uint8)).resize((CW * 6, CH * 6), Image.NEAREST))
    m = Image.new("RGB", (len(tiles) * (CW * 6 + 8), CH * 6), "gray")
    for k, t in enumerate(tiles):
        m.paste(t, (k * (CW * 6 + 8), 0))
    os.makedirs(os.path.join(raiz, "gen", "ctl"), exist_ok=True)
    m.save(os.path.join(raiz, "gen", "ctl", "atlas_check.png"))
    print("round trip of the untouched atlas:", Atlas(gim).bytes() == gim)


_MARK = bytes([1, 0, 3, 0])      # [.., 1, 3, pages] precedes the table of frame offsets in the header of both blocks


def _gim_blocks(gim, n, pix, pal):
    """a GIM file like `gim` (the game's atlas) with n pages: the headers of its image and palette blocks are patched (page count, frame
    offsets, end offsets); the header grows to 0x60 bytes when n > 7"""
    blocks, p = {}, 0x30
    while p < len(gim):
        bid, _, size = struct.unpack_from("<HHI", gim, p)
        blocks[bid] = (p, size)
        p += size
    out = []
    for bid, frame, body in ((5, 0x400, pal), (4, PAGE * PAGE, pix)):
        p0, size0 = blocks[bid]
        old = gim[p0:p0 + size0]
        tab = old.index(_MARK) - 2
        hdr = (tab - 0x10 + 8 + 4 * n + 15) & ~15
        head = bytearray(old[:tab + 8])
        struct.pack_into("<H", head, tab + 6, n)
        struct.pack_into("<I", head, 0x2c, hdr)                        # [0x30, header length, end, 0] at +0x28
        struct.pack_into("<I", head, 0x30, hdr + n * frame)
        head += b"".join(struct.pack("<I", hdr + k * frame) for k in range(n))
        head += bytes(0x10 + hdr - len(head))
        size = len(head) + len(body)
        struct.pack_into("<I", head, 4, size)
        struct.pack_into("<I", head, 8, size)
        out.append(bytes(head) + body)
    total = 0x30 + len(out[0]) + len(out[1])
    top = bytearray(gim[:0x30])
    struct.pack_into("<I", top, 0x14, total - 0x10)
    struct.pack_into("<I", top, 0x24, total - 0x20)
    return bytes(top) + out[0] + out[1]


def extend(gim, filetbl, cardm_tf3, gids):
    """appends the faces of TF3 cards to the atlas: cells go after the last one, in the order of gids (TF3 id -> new game id, ascending);
    pages that receive faces get a new palette (median cut of old and new colors). Returns (cis_all.gim, fileTbl.dat)"""
    a = Atlas(gim)
    ids = list(struct.unpack_from("<%dH" % (len(filetbl) // 2 - 2), filetbl, 4))
    count0, n_new = len(ids), len(gids)
    total = count0 + n_new
    pages = -(-total // PER_PAGE)
    rgb = []
    for p in range(a.n):
        page = np.frombuffer(bytes(a.lin[p]), np.uint8).reshape(PAGE, PAGE)
        rgb.append(a.pal[p][page].astype(np.uint8))
    while len(rgb) < pages:
        rgb.append(np.zeros((PAGE, PAGE, 3), np.uint8))
    touched = set()
    for k, (tf3, gid) in enumerate(sorted(gids.items(), key=lambda x: x[1])):
        cell = count0 + k
        p, x, y = divmod(cell, PER_PAGE)[0], (cell % PER_PAGE % COLS) * CW, (cell % PER_PAGE // COLS) * CH
        rgb[p][y + 2:y + CH, x:x + CW] = a.squeeze(face(cardm_tf3, tf3)).astype(np.uint8)
        touched.add(p)
        ids.append(gid)
    lin, pal = [], []
    for p in range(pages):
        if p in touched:
            q = Image.fromarray(rgb[p]).quantize(256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
            pl = np.array(q.getpalette()[:768], np.uint8).reshape(256, 3)
            lin.append(bytes(np.array(q, np.uint8).tobytes()))
        else:
            if p < a.n:
                lin.append(bytes(a.lin[p]))
                pl = a.pal[p].astype(np.uint8)
            else:
                lin.append(bytes(PAGE * PAGE))
                pl = np.zeros((256, 3), np.uint8)
        pal.append(np.concatenate([pl, np.full((256, 1), 255, np.uint8)], axis=1).tobytes())
    pix = b"".join(_swizzle(l, PAGE, PAGE) for l in lin)
    ft = struct.pack("<HH", len(ids), 0) + struct.pack("<%dH" % len(ids), *ids)
    return _gim_blocks(gim, pages, pix, b"".join(pal)), ft
