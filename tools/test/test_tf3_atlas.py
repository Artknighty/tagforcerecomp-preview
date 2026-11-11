import os
import struct
import unittest
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import sys
sys.path.insert(0, os.path.join(ROOT, 'tools', 'game'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'mods'))
import ehp
from tf3_porte_atlas import Atlas, face

def bsearch(table_bytes, target):
    count = struct.unpack_from('<H', table_bytes, 0)[0]
    lo, hi = 0, count - 1
    while lo <= hi:
        mid = (lo + hi) // 2
        val = struct.unpack_from('<H', table_bytes, 2 + mid * 2)[0]
        if val == target:
            return mid
        elif val < target:
            lo = mid + 1
        else:
            hi = mid - 1
    return 0

class TestTF3Atlas(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        mod_ehp = os.path.join(ROOT, 'mods', 'TF3Cards', 'PSP_GAME', 'USRDIR', 'deck', 'all_e.ehp')
        with open(mod_ehp, 'rb') as f:
            _, mem = ehp.ler(f.read())
        d = dict(mem)
        cls.filetbl = d['fileTbl.dat']
        cls.atlas = Atlas(d['cis_all.gim'])
        with open(os.path.join(ROOT, 'ref', 'tf3', 'deck_e', 'cis_all.gim'), 'rb') as f:
            cls.tf3_atlas = Atlas(f.read())
        with open(os.path.join(ROOT, 'ref', 'tf3', 'deck_e', 'fileTbl.dat'), 'rb') as f:
            tbl_data = f.read()
            cnt = struct.unpack_from('<H', tbl_data, 0)[0]
            cls.tf3_ids = struct.unpack_from(f'<{cnt}H', tbl_data, 2)
            cls.tf3_id_to_cell = {cid: idx for idx, cid in enumerate(cls.tf3_ids)}

    def test_filetbl_structure(self):
        count = struct.unpack_from('<H', self.filetbl, 0)[0]
        self.assertEqual(count, 3502)
        self.assertEqual(len(self.filetbl), 2 + count * 2)

        entry0 = struct.unpack_from('<H', self.filetbl, 2)[0]
        self.assertEqual(entry0, 0)

        # Base game cards: indices 1 to 2451
        first_card = struct.unpack_from('<H', self.filetbl, 4)[0]
        self.assertEqual(first_card, 4007)
        last_base = struct.unpack_from('<H', self.filetbl, 2 + 2451 * 2)[0]
        self.assertEqual(last_base, 6957)

        # Extended cards: indices 2452 to 3501
        first_ext = struct.unpack_from('<H', self.filetbl, 2 + 2452 * 2)[0]
        self.assertEqual(first_ext, 6958)
        last_ext = struct.unpack_from('<H', self.filetbl, 2 + 3501 * 2)[0]
        self.assertEqual(last_ext, 8007)

    def test_card_thumbnails_match(self):
        # Specific cards:
        # 6958: Fallen Paradise (TF3 3720)
        # 7349: Chrysalis Mole (TF3 7173)
        # 7511: Mad Reloader (TF3 7348)
        # 7512: Dark Bribe (TF3 7349)
        # 8007: Last card (TF3 7993)
        cards = [
            (6958, 3720, 'Fallen Paradise'),
            (7349, 7173, 'Chrysalis Mole'),
            (7511, 7348, 'Mad Reloader'),
            (7512, 7349, 'Dark Bribe'),
            (8007, 7993, 'Last Card'),
        ]
        for gid, tf3id, name in cards:
            cell = bsearch(self.filetbl, gid)
            self.assertGreater(cell, 0, f'Card {name} (gid {gid}) not found in fileTbl.dat')

            # Ensure the top 2 rows (rows 0 and 1) have the authentic card frame and are NOT a black bar
            actual_full = self.atlas.cell_full(cell)
            self.assertGreater(float(actual_full[0].mean()), 20.0, f'{name} row 0 is black bar')
            self.assertGreater(float(actual_full[1].mean()), 40.0, f'{name} row 1 is black bar')

            # Compare against the authentic native TF3 thumbnail cell
            tf3_cell = self.tf3_id_to_cell[tf3id]
            expected_full = self.tf3_atlas.cell_full(tf3_cell)
            diff = float(np.abs(actual_full - expected_full).mean())
            self.assertLess(diff, 5.0, f'Thumbnail mismatch for {name}: diff {diff:.2f}')

if __name__ == '__main__':
    unittest.main()
