import os
import struct
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PACK = os.path.join(ROOT, 'mods', 'TF3Cards')


class TestTF3AccuracyExpansion(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(os.path.join(PACK, 'ported.txt'), encoding='utf-8') as f:
            rows = [l.split(' ', 3) for l in f.read().splitlines() if l.strip()]
        cls.gids = {int(r[1]): int(r[0]) for r in rows}
        cls.likes = {int(r[1]): int(r[2]) for r in rows}

        with open(os.path.join(PACK, 'amount.bin'), 'rb') as f:
            data = f.read()
        magic, count = struct.unpack_from('<4sI', data, 0)
        cls.amounts = {}
        pos = 8
        for _ in range(count):
            cid, kind, pad, self_val, opp_val = struct.unpack_from('<HBBii', data, pos)
            cls.amounts.setdefault(cid, {})[kind] = (self_val, opp_val)
            pos += 12

    def test_parshath_family_donors(self):
        # 7046 Neo-Parshath and 7563 Darknight Parshath must use 5023 (Airknight Parshath)
        self.assertEqual(self.likes.get(7046), 5023, "Neo-Parshath should use donor 5023 (Airknight Parshath)")
        self.assertEqual(self.likes.get(7563), 5023, "Darknight Parshath should use donor 5023 (Airknight Parshath)")

    def test_monarch_family_donors(self):
        # 7117 Raiza, 7516 Caius, 7659 Kuraz must use 5965 (Zaborg the Thunder Monarch) for Tribute Summon trigger
        self.assertEqual(self.likes.get(7117), 5965, "Raiza should use donor 5965 (Zaborg the Thunder Monarch)")
        self.assertEqual(self.likes.get(7516), 5965, "Caius should use donor 5965 (Zaborg the Thunder Monarch)")
        self.assertEqual(self.likes.get(7659), 5965, "Kuraz should use donor 5965 (Zaborg the Thunder Monarch)")
        
        # Caius has -1000 damage on opponent
        self.assertIn(self.gids[7516], self.amounts)
        self.assertEqual(self.amounts[self.gids[7516]][2][1], -1000)

    def test_ancient_gear_family_donors(self):
        # 6932 Ancient Gear Gadjiltron Dragon uses 6315 (Ancient Gear Golem: piercing + attack lock)
        self.assertEqual(self.likes.get(6932), 6315, "Gadjiltron Dragon should use donor 6315 (Ancient Gear Golem)")
        # 6841 Ancient Gear Engineer uses 6317 (Ancient Gear Soldier: attack lock)
        self.assertEqual(self.likes.get(6841), 6317, "Ancient Gear Engineer should use donor 6317 (Ancient Gear Soldier)")

    def test_bounce_and_removal_donors(self):
        # 6770 Neo-Spacian Grand Mole uses 5719 (D.D. Warrior Lady)
        self.assertEqual(self.likes.get(6770), 5719, "Grand Mole should use donor 5719 (D.D. Warrior Lady)")
        # 7207 Phantom Cricket uses 4547 (Hane-Hane)
        self.assertEqual(self.likes.get(7207), 4547, "Phantom Cricket should use donor 4547 (Hane-Hane)")

    def test_lp_amount_overhauls(self):
        # 7088 Tri-Blaze Accelerator: -500 opponent damage
        self.assertIn(self.gids[7088], self.amounts)
        self.assertEqual(self.amounts[self.gids[7088]][2][1], -500)

        # 7229 Destructive Draw: -3000 self damage
        self.assertIn(self.gids[7229], self.amounts)
        self.assertEqual(self.amounts[self.gids[7229]][2][0], -3000)


if __name__ == '__main__':
    unittest.main()
