import os
import struct
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PACK = os.path.join(ROOT, 'mods', 'TF3Cards')


class TestTF3Batch7(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        rows = [l.split(' ', 3) for l in open(os.path.join(PACK, 'ported.txt'), encoding='utf-8').read().splitlines() if l.strip()]
        cls.gids = {int(r[1]): int(r[0]) for r in rows}
        cls.likes = {int(r[1]): int(r[2]) for r in rows}

    def test_battle_and_donor_mechanisms(self):
        # 6963 Yariza (direct attack like Jinzo #7 4428)
        self.assertEqual(self.likes.get(6963), 4428)
        # 6964 Nisashi (double attack like Hayabusa Knight 5137)
        self.assertEqual(self.likes.get(6964), 5137)
        # 6969 Irou (face-down destroy like Sasuke Samurai 5412)
        self.assertEqual(self.likes.get(6969), 5412)
        # 7369 Gemini Lancer, 7871 Mad Archfiend, 7258 Malicious Edge (piercing like Mad Sword Beast 5142)
        self.assertEqual(self.likes.get(7369), 5142)
        self.assertEqual(self.likes.get(7871), 5142)
        self.assertEqual(self.likes.get(7258), 5142)

    def test_destruction_rules(self):
        data = open(os.path.join(PACK, 'destroy.bin'), 'rb').read()
        magic, count = struct.unpack_from('<4sI', data, 0)
        self.assertEqual(magic, b'TF3D')
        pos = 8
        rules = {}
        for _ in range(count):
            cid, side, zones, cond, n, count_ids = struct.unpack_from('<HBBBBH', data, pos)
            pos += 8
            ids = struct.unpack_from(f'<{count_ids}H', data, pos)
            pos += 2 * count_ids
            rules[cid] = {'side': side, 'zones': zones, 'cond': cond, 'ids': ids}

        # 6912 Justi-Break (side 3=both, zones 1=monster, cond 9=target_all_not)
        r = rules[self.gids[6912]]
        self.assertEqual(r['side'], 3)
        self.assertEqual(r['zones'], 1)
        self.assertEqual(r['cond'], 9)

        # 6974 Kahkki (side 3=both, zones 1=monster, cond 6=target)
        r = rules[self.gids[6974]]
        self.assertEqual(r['side'], 3)
        self.assertEqual(r['zones'], 1)
        self.assertEqual(r['cond'], 6)

        # 6975 Gren (side 3=both, zones 2=st, cond 6=target)
        r = rules[self.gids[6975]]
        self.assertEqual(r['side'], 3)
        self.assertEqual(r['zones'], 2)
        self.assertEqual(r['cond'], 6)

        # 7120 Magna-Slash Dragon (side 2=opp, zones 2=st, cond 6=target)
        r = rules[self.gids[7120]]
        self.assertEqual(r['side'], 2)
        self.assertEqual(r['zones'], 2)
        self.assertEqual(r['cond'], 6)

        # 7121 Gravi-Crush Dragon (side 2=opp, zones 1=monster, cond 6=target)
        r = rules[self.gids[7121]]
        self.assertEqual(r['side'], 2)
        self.assertEqual(r['zones'], 1)
        self.assertEqual(r['cond'], 6)

        # 7566 Golden Flying Fish (side 3=both, zones 3=all, cond 6=target)
        r = rules[self.gids[7566]]
        self.assertEqual(r['side'], 3)
        self.assertEqual(r['zones'], 3)
        self.assertEqual(r['cond'], 6)

        # 7130 Crystal Raigeki (side 2=opp, zones 3=all, cond 6=target)
        r = rules[self.gids[7130]]
        self.assertEqual(r['side'], 2)
        self.assertEqual(r['zones'], 3)
        self.assertEqual(r['cond'], 6)

        # 6906 Mystical Wind Typhoon (side 3=both, zones 2=st, cond 6=target)
        r = rules[self.gids[6906]]
        self.assertEqual(r['side'], 3)
        self.assertEqual(r['zones'], 2)
        self.assertEqual(r['cond'], 6)

    def test_lp_costs(self):
        data = open(os.path.join(PACK, 'amount.bin'), 'rb').read()
        magic, count = struct.unpack_from('<4sI', data, 0)
        self.assertEqual(magic, b'TF3V')
        pos = 8
        amounts = {}
        for _ in range(count):
            cid, kind, pad, self_val, opp_val = struct.unpack_from('<HBBii', data, pos)
            amounts.setdefault(cid, {})[kind] = (self_val, opp_val)
            pos += 12

        # LP costs (kind 2): self negative
        self.assertEqual(amounts[self.gids[6902]][2], (-500, 0))   # Counter Cleaner
        self.assertEqual(amounts[self.gids[7630]][2], (-800, 0))   # Light of Redemption
        self.assertEqual(amounts[self.gids[7912]][2], (-1000, 0))  # Psychokinesis
        self.assertEqual(amounts[self.gids[7254]][2], (-500, 0))   # Over Limit
        self.assertEqual(amounts[self.gids[7332]][2], (-500, 0))   # Gemini Summoner


if __name__ == '__main__':
    unittest.main()
