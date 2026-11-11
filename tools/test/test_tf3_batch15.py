import os
import struct
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PACK = os.path.join(ROOT, 'mods', 'TF3Cards')


class TestTF3Batch15(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(os.path.join(PACK, 'ported.txt'), encoding='utf-8') as f:
            rows = [l.split(' ', 3) for l in f.read().splitlines() if l.strip()]
        cls.gids = {int(r[1]): int(r[0]) for r in rows}
        cls.likes = {int(r[1]): int(r[2]) for r in rows}

    def test_cannot_special_summon(self):
        with open(os.path.join(PACK, 'summon.bin'), 'rb') as f:
            data = f.read()
        magic, count = struct.unpack_from('<4sI', data, 0)
        self.assertEqual(magic, b'TF3S')
        pos = 8
        summons = {}
        for _ in range(count):
            gid, flags = struct.unpack_from('<HH', data, pos)
            pos += 4
            summons[gid] = flags

        # Inherent cannot special summon: flag 4
        for cid in (7342, 7386, 7403, 7419):
            gid = self.gids[cid]
            self.assertIn(gid, summons, f"Card {cid} missing from summon.bin")
            self.assertEqual(summons[gid] & 4, 4, f"Card {cid} should have cannot_special_summon flag")

    def test_stat_modifiers(self):
        with open(os.path.join(PACK, 'stat.bin'), 'rb') as f:
            data = f.read()
        magic, count = struct.unpack_from('<4sI', data, 0)
        self.assertEqual(magic, b'TF3S')
        self.assertGreaterEqual(count, 30)

    def test_attack_negators_and_battle_traps(self):
        # 7400 Dimensional Prison -> like 4886 (Sakuretsu Armor)
        self.assertEqual(self.likes.get(7400), 4886)
        # 7170 Mirror Gate -> like 4886
        self.assertEqual(self.likes.get(7170), 4886)

    def test_gemini_and_fusion_cards(self):
        # Geminis -> like 4518
        for cid in (7292, 7293, 7294, 7339):
            self.assertEqual(self.likes.get(cid), 4518, f"Gemini {cid} donor mismatch")
        # Fusions -> like 4507
        for cid in (7175, 7177, 7275, 7301, 7302):
            self.assertEqual(self.likes.get(cid), 4507, f"Fusion {cid} donor mismatch")

    def test_utility_monsters_and_spells(self):
        # Armageddon Knight, Prisma, Test Tiger, Sea Koala -> like 4518
        for cid in (7423, 7491, 7482, 7437, 7351, 7404):
            self.assertEqual(self.likes.get(cid), 4518, f"Monster {cid} donor mismatch")
        # Double Summon, Super Poly, Veil of Darkness, Arms Hole, Demise of the Land -> like 4145
        for cid in (7219, 7445, 7488, 7493, 7620, 7262, 7381, 7454):
            self.assertEqual(self.likes.get(cid), 4145, f"Spell/Trap {cid} donor mismatch")


if __name__ == '__main__':
    unittest.main()
