import os
import struct
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PACK = os.path.join(ROOT, 'mods', 'TF3Cards')


class TestTF3Batch6(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        rows = [l.split(' ', 3) for l in open(os.path.join(PACK, 'ported.txt'), encoding='utf-8').read().splitlines() if l.strip()]
        cls.gids = {int(r[1]): int(r[0]) for r in rows}
        cls.likes = {int(r[1]): int(r[2]) for r in rows}

    def test_harpie_queen_donor_and_search(self):
        # 7111 Harpie Queen borrows King of the Swamp 6044
        self.assertEqual(self.likes.get(7111), 6044)
        gid = self.gids[7111]
        data = open(os.path.join(PACK, 'effects.bin'), 'rb').read()
        magic, count = struct.unpack_from('<4sI', data, 0)
        self.assertEqual(magic, b'TF3E')
        pos = 8
        found = False
        for _ in range(count):
            cid, zone, extra, n = struct.unpack_from('<HBBI', data, pos)
            pos += 8
            targets = struct.unpack_from(f'<{n}H', data, pos)
            pos += 2 * n
            if cid == gid:
                found = True
                self.assertEqual(zone & 15, 1)  # deck zone
                self.assertEqual(n, 1)          # Harpies' Hunting Ground
                self.assertEqual(targets[0], 6207)  # Harpies' Hunting Ground TF1 id
                break
        self.assertTrue(found, 'Harpie Queen not found in effects.bin')

    def test_rampaging_rhynos_role_bonus(self):
        # 6895 Rampaging Rhynos has 500 battle role bonus in BAR1 trailer
        gid = self.gids[6895]
        data = open(os.path.join(PACK, 'stat.bin'), 'rb').read()
        bar1_idx = data.find(b'BAR1')
        self.assertNotEqual(bar1_idx, -1, 'BAR1 trailer missing from stat.bin')
        count = struct.unpack_from('<I', data, bar1_idx + 4)[0]
        pos = bar1_idx + 8
        bonuses = {}
        for _ in range(count):
            cid, bonus = struct.unpack_from('<HH', data, pos)
            bonuses[cid] = bonus
            pos += 4
        self.assertEqual(bonuses.get(gid), 500)

    def test_batch6_amounts(self):
        # amount.bin checks for burn and draw
        data = open(os.path.join(PACK, 'amount.bin'), 'rb').read()
        magic, count = struct.unpack_from('<4sI', data, 0)
        self.assertEqual(magic, b'TF3V')
        pos = 8
        amounts = {}
        for _ in range(count):
            cid, kind, pad, self_val, opp_val = struct.unpack_from('<HBBii', data, pos)
            amounts.setdefault(cid, {})[kind] = (self_val, opp_val)
            pos += 12

        # Draw: kind 1
        # 6916 Accumulated Fortune (self: 2)
        self.assertEqual(amounts[self.gids[6916]][1], (2, 0))
        # 7095 Disk Commander (self: 2)
        self.assertEqual(amounts[self.gids[7095]][1], (2, 0))
        # 7198 Lucky Pied Piper (self: 1)
        self.assertEqual(amounts[self.gids[7198]][1], (1, 0))
        # 7080 Rare Value (self: 2)
        self.assertEqual(amounts[self.gids[7080]][1], (2, 0))
        # 7469 Dark Spirit Art (self: 2)
        self.assertEqual(amounts[self.gids[7469]][1], (2, 0))
        # 7161 Fifth Hope (self: 2)
        self.assertEqual(amounts[self.gids[7161]][1], (2, 0))

        # LP: kind 2
        # 7887 Iron Chain Blaster (opp: -800)
        self.assertEqual(amounts[self.gids[7887]][2], (0, -800))
        # 7884 Pursuit Chaser (opp: -500)
        self.assertEqual(amounts[self.gids[7884]][2], (0, -500))
        # 7533 Flamvell Dragnov (opp: -500)
        self.assertEqual(amounts[self.gids[7533]][2], (0, -500))
        # 6880 Blast Asmodian (opp: -500)
        self.assertEqual(amounts[self.gids[6880]][2], (0, -500))
        # 7416 Destiny Hero - Dunker (opp: -500)
        self.assertEqual(amounts[self.gids[7416]][2], (0, -500))
        # 7876 Morphtronic Datatron (opp: -600)
        self.assertEqual(amounts[self.gids[7876]][2], (0, -600))
        # 7103 Volcanic Slicer (opp: -500)
        self.assertEqual(amounts[self.gids[7103]][2], (0, -500))
        # 7203 King Pyron (opp: -1000)
        self.assertEqual(amounts[self.gids[7203]][2], (0, -1000))


if __name__ == '__main__':
    unittest.main()
