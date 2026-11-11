import os
import struct
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PACK = os.path.join(ROOT, 'mods', 'TF3Cards')


class TestTF3Batch11(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(os.path.join(PACK, 'ported.txt'), encoding='utf-8') as f:
            rows = [l.split(' ', 3) for l in f.read().splitlines() if l.strip()]
        cls.gids = {int(r[1]): int(r[0]) for r in rows}
        cls.likes = {int(r[1]): int(r[2]) for r in rows}

    def test_multi_attackers(self):
        # 7760 Machine Lord Ür, 7826 Ally of Justice - Thousand Arms -> like 4966 (Asura Priest)
        for cid in (7760, 7826):
            self.assertIn(self.likes.get(cid), (4966, 5381), f"Card {cid} expected like 4966 or 5381")

    def test_summon_locks(self):
        with open(os.path.join(PACK, 'summon.bin'), 'rb') as f:
            data = f.read()
        magic, count = struct.unpack_from('<4sI', data, 0)
        self.assertEqual(magic, b'TF3S')
        pos = 8
        locks = {}
        for _ in range(count):
            gid, flags = struct.unpack_from('<HH', data, pos)
            locks[gid] = flags
            pos += 4

        # 7397 Thunder King Rai-Oh (flags=1 both)
        self.assertEqual(locks[self.gids[7397]], 1)
        # 7405 Fossil Dyna Pachycephalo (flags=1 both)
        self.assertEqual(locks[self.gids[7405]], 1)
        # 7633 Deck Lockdown (flags=1 both)
        self.assertEqual(locks[self.gids[7633]], 1)
        # 3745 Skullger (flags=4 cannot special summon)
        self.assertEqual(locks[self.gids[3745]], 4)
        # 7334 Thousand-Eyes Jellyfish (flags=4)
        self.assertEqual(locks[self.gids[7334]], 4)
        # 7712 Montage Dragon (flags=4)
        self.assertEqual(locks[self.gids[7712]], 4)

    def test_stat_bonuses(self):
        with open(os.path.join(PACK, 'stat.bin'), 'rb') as f:
            data = f.read()
        magic, count = struct.unpack_from('<4sI', data, 0)
        self.assertEqual(magic, b'TF3S')

    def test_burn_and_lp_amounts(self):
        with open(os.path.join(PACK, 'amount.bin'), 'rb') as f:
            data = f.read()
        magic, count = struct.unpack_from('<4sI', data, 0)
        self.assertEqual(magic, b'TF3V')
        pos = 8
        amounts = {}
        for _ in range(count):
            cid, kind, pad, self_val, opp_val = struct.unpack_from('<HBBii', data, pos)
            amounts.setdefault(cid, {})[kind] = (self_val, opp_val)
            pos += 12

        # 3772 Volcanic Wall (-500 opp)
        self.assertEqual(amounts[self.gids[3772]][2], (0, -500))
        # 7352 Nurse Reficule (-1000 opp)
        self.assertEqual(amounts[self.gids[7352]][2], (0, -1000))
        # 6453 Trap of Darkness (-1000 self)
        self.assertEqual(amounts[self.gids[6453]][2], (-1000, 0))
        # 6908 Degenerate Circuit (-500 self)
        self.assertEqual(amounts[self.gids[6908]][2], (-500, 0))
        # 7132 Terrible Deal (-1000 self)
        self.assertEqual(amounts[self.gids[7132]][2], (-1000, 0))
        # 7869 Healing Wave Generator (1000 self)
        self.assertEqual(amounts[self.gids[7869]][2], (1000, 0))

    def test_destructions(self):
        with open(os.path.join(PACK, 'destroy.bin'), 'rb') as f:
            data = f.read()
        magic, count = struct.unpack_from('<4sI', data, 0)
        self.assertEqual(magic, b'TF3D')
        pos = 8
        rules = {}
        for _ in range(count):
            cid, side, zones, cond, n, count_ids = struct.unpack_from('<HBBBBH', data, pos)
            pos += 8
            ids = struct.unpack_from(f'<{count_ids}H', data, pos)
            pos += 2 * count_ids
            rules[cid] = {'side': side, 'zones': zones, 'cond': cond, 'n': n, 'ids': ids}

        # 7325 Cell Explosion Virus: side opp (2), zones monster (1), cond target_all (7)
        self.assertEqual(rules[self.gids[7325]]['side'], 2)
        self.assertEqual(rules[self.gids[7325]]['zones'], 1)
        self.assertEqual(rules[self.gids[7325]]['cond'], 7)

        # 7646 Froggy Forcefield: side opp (2), zones monster (1), cond target_all (7)
        self.assertEqual(rules[self.gids[7646]]['side'], 2)
        self.assertEqual(rules[self.gids[7646]]['zones'], 1)
        self.assertEqual(rules[self.gids[7646]]['cond'], 7)

        # 7675 Twin-Barrel Dragon: side opp (2), zones all (3), cond target (6)
        self.assertEqual(rules[self.gids[7675]]['side'], 2)
        self.assertEqual(rules[self.gids[7675]]['zones'], 3)
        self.assertEqual(rules[self.gids[7675]]['cond'], 6)

        # 7825 Ally Bomb: side both (3), zones all (3), cond target (6)
        self.assertEqual(rules[self.gids[7825]]['side'], 3)
        self.assertEqual(rules[self.gids[7825]]['zones'], 3)
        self.assertEqual(rules[self.gids[7825]]['cond'], 6)


if __name__ == '__main__':
    unittest.main()
