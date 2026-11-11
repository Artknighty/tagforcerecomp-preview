import os
import struct
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PACK = os.path.join(ROOT, 'mods', 'TF3Cards')


class TestTF3Batch9(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(os.path.join(PACK, 'ported.txt'), encoding='utf-8') as f:
            rows = [l.split(' ', 3) for l in f.read().splitlines() if l.strip()]
        cls.gids = {int(r[1]): int(r[0]) for r in rows}
        cls.likes = {int(r[1]): int(r[2]) for r in rows}

    def test_piercing_monsters(self):
        # 3787 Satellite Laser X-06S, Balsam
        # 6835 Super Vehicroid - Stealth Union
        # 7253 Paleozoic Fossil Knight - Skullking
        # 7263 Paleozoic Fossil Dragon - Skullgeoth
        for cid in (3787, 6835, 7253, 7263):
            self.assertEqual(self.likes.get(cid), 5142, f"Card {cid} expected like 5142 (piercing)")
        # 7046 Neo-Parshath, the Sky Paladin upgraded to 5023 (Airknight Parshath: piercing + battle damage draw)
        self.assertEqual(self.likes.get(7046), 5023)

    def test_burn_and_cost_amounts(self):
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

        # Burn:
        # 6904 Chain Strike (-400 opp)
        self.assertEqual(amounts[self.gids[6904]][2], (0, -400))
        # 7227 Venom Burn (-700 opp)
        self.assertEqual(amounts[self.gids[7227]][2], (0, -700))
        # 7516 Caius the Shadow Monarch (-1000 opp)
        self.assertEqual(amounts[self.gids[7516]][2], (0, -1000))
        # 7578 Volcanic Queen (-1000 opp)
        self.assertEqual(amounts[self.gids[7578]][2], (0, -1000))

        # Pay LP (kind 2, self negative):
        # 3760 Ojamandala (-1000 self)
        self.assertEqual(amounts[self.gids[3760]][2], (-1000, 0))
        # 7082 Volcanic Shell (-500 self)
        self.assertEqual(amounts[self.gids[7082]][2], (-500, 0))
        # 7599 Judgment Dragon (-1000 self)
        self.assertEqual(amounts[self.gids[7599]][2], (-1000, 0))
        # 7911 Teleport (-800 self)
        self.assertEqual(amounts[self.gids[7911]][2], (-800, 0))

    def test_draw_amounts(self):
        with open(os.path.join(PACK, 'amount.bin'), 'rb') as f:
            data = f.read()
        magic, count = struct.unpack_from('<4sI', data, 0)
        pos = 8
        amounts = {}
        for _ in range(count):
            cid, kind, pad, self_val, opp_val = struct.unpack_from('<HBBii', data, pos)
            amounts.setdefault(cid, {})[kind] = (self_val, opp_val)
            pos += 12

        # 3720 Fallen Paradise (draw self 2)
        self.assertEqual(amounts[self.gids[3720]][1], (2, 0))
        # 7159 Convert Contact (draw self 2)
        self.assertEqual(amounts[self.gids[7159]][1], (2, 0))
        # 7487 Royal Firestorm Guards (draw self 2)
        self.assertEqual(amounts[self.gids[7487]][1], (2, 0))
        # 7746 Psi-Impulse (draw opp 3)
        self.assertEqual(amounts[self.gids[7746]][1], (0, 3))
        # 7081 Last Resort (draw opp 1)
        self.assertEqual(amounts[self.gids[7081]][1], (0, 1))

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

        # 7421 Dark Armed Dragon: side both (3), zones all (3), cond target (6)
        r = rules[self.gids[7421]]
        self.assertEqual(r['side'], 3)
        self.assertEqual(r['zones'], 3)
        self.assertEqual(r['cond'], 6)

        # 7592 Lyla: side opp (2), zones st (2), cond target (6)
        r = rules[self.gids[7592]]
        self.assertEqual(r['side'], 2)
        self.assertEqual(r['zones'], 2)
        self.assertEqual(r['cond'], 6)

        # 7346 Beast King Barbaros: side opp (2), zones all (3), cond target_all (7)
        r = rules[self.gids[7346]]
        self.assertEqual(r['side'], 2)
        self.assertEqual(r['zones'], 3)
        self.assertEqual(r['cond'], 7)

        # 7562 Darklord Zerato: side opp (2), zones monster (1), cond target_all (7)
        r = rules[self.gids[7562]]
        self.assertEqual(r['side'], 2)
        self.assertEqual(r['zones'], 1)
        self.assertEqual(r['cond'], 7)

        # 7935 Giant Trap Hole: side both (3), zones monster (1), cond None (0)
        r = rules[self.gids[7935]]
        self.assertEqual(r['side'], 3)
        self.assertEqual(r['zones'], 1)
        self.assertEqual(r['cond'], 0)


if __name__ == '__main__':
    unittest.main()
