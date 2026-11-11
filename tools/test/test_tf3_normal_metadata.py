import os
import struct
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PACK = os.path.join(ROOT, 'mods', 'TF3Cards')

NORMAL_IDS = [
    6412, # Mad Lobster
    6960, # Gene-Warped Warwolf
    6961, # Frostosaurus
    6962, # Spiral Serpent
    7190, # Alien Shocktrooper
    7191, # Volcanic Rat
    7192, # Renge, Gatekeeper of Dark World
    7193, # Hunter Dragon
    7194, # Venom Cobra
    7276, # Chamberlain of the Six Samurai
    7277, # Cloudian - Smoke Ball
    7365, # Gladiator Beast Andal
    7366, # Atlantean Pikeman
    7540, # Ally of Justice - Clausolas
    7815, # Watchkeeper of Mist Valley
    7821, # X-Saber Anapelera
]


class TestTF3NormalMetadata(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(os.path.join(PACK, 'ported.txt'), encoding='utf-8') as f:
            rows = [l.split(' ', 3) for l in f.read().splitlines() if l.strip()]
        cls.gids = {int(r[1]): int(r[0]) for r in rows}
        cls.names = {int(r[1]): r[3] for r in rows}
        cls.donors = {int(r[1]): int(r[2]) for r in rows}

    def test_all_16_normal_monsters_present_with_vanilla_donor(self):
        self.assertEqual(len(NORMAL_IDS), 16)
        for cid in NORMAL_IDS:
            self.assertIn(cid, self.gids, f"Normal monster {cid} missing from ported.txt")
            # In Tag Force 1, Normal monsters have no effect records (donor 0)
            self.assertEqual(self.donors[cid], 0, f"Normal monster {cid} should have donor 0")

    def test_metadata_in_all_languages(self):
        for lang in ('e', 'f', 'g', 'i', 's'):
            path = os.path.join(PACK, f'ext_info_{lang}.bin')
            self.assertTrue(os.path.exists(path), f"ext_info_{lang}.bin missing")
            with open(path, 'rb') as f:
                data = f.read()
            self.assertEqual(data[:4], b'TF3I')
            count = struct.unpack_from('<I', data, 4)[0]
            self.assertEqual(count, 1050)

    def test_collection_trunk_copies(self):
        path = os.path.join(PACK, 'collection_ext.bin')
        self.assertTrue(os.path.exists(path), "collection_ext.bin missing")
        with open(path, 'rb') as f:
            data = f.read()
        self.assertEqual(len(data), 1050 * 4)
        for cid in NORMAL_IDS:
            gid = self.gids[cid]
            idx = gid - 6958
            record = struct.unpack_from('<BBBB', data, idx * 4)
            # 0x02 = new, 3 copies owned, 3 copy limit, 0 reserved
            self.assertEqual(record[1], 3, f"Card {cid} (slot {gid}) should have 3 copies in trunk")


if __name__ == '__main__':
    unittest.main()
