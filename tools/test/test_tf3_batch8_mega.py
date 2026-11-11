import os
import struct
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PACK = os.path.join(ROOT, 'mods', 'TF3Cards')


class TestTF3Batch8Mega(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        rows = [l.split(' ', 3) for l in open(os.path.join(PACK, 'ported.txt'), encoding='utf-8').read().splitlines() if l.strip()]
        cls.gids = {int(r[1]): int(r[0]) for r in rows}
        cls.likes = {int(r[1]): int(r[2]) for r in rows}

    def test_direct_attackers(self):
        # 7071 Amethyst Cat, 7093 Spell Striker, 7295 Goblin Black Ops, 7579 Jinzo Returner, 7602 Unifrog
        for cid in (7071, 7093, 7295, 7579, 7602, 6863, 6897, 6831):
            self.assertEqual(self.likes.get(cid), 4428, f"Card {cid} expected like 4428")

    def test_piercing_and_double_attack(self):
        # Piercing: 7333 Ocean's Keeper, 7176 Darkbright, 7252 Plasma Vice, 7257 Inferno Wing, 7616 Ultimate Ancient Gear Golem, 6830 Cyberdark Horn
        for cid in (7333, 7176, 7252, 7257, 7616, 6830):
            self.assertEqual(self.likes.get(cid), 5142, f"Card {cid} expected like 5142")
        # Double Attack: 7196 Chthonian Emperor Dragon
        self.assertEqual(self.likes.get(7196), 5137)

    def test_battle_guard(self):
        # 7838 Sphere of Chaos, 7601 Substitoad
        data = open(os.path.join(PACK, 'stat.bin'), 'rb').read()
        bgp1_idx = data.find(b'BGP1')
        self.assertNotEqual(bgp1_idx, -1)
        count = struct.unpack_from('<I', data, bgp1_idx + 4)[0]
        pos = bgp1_idx + 8
        guards = {}
        for _ in range(count):
            cid, stat, minimum = struct.unpack_from('<HHI', data, pos)
            guards[cid] = (stat, minimum)
            pos += 8
        self.assertIn(self.gids[7838], guards)
        self.assertIn(self.gids[7601], guards)

    def test_burn_and_heal_amounts(self):
        data = open(os.path.join(PACK, 'amount.bin'), 'rb').read()
        magic, count = struct.unpack_from('<4sI', data, 0)
        self.assertEqual(magic, b'TF3V')
        pos = 8
        amounts = {}
        for _ in range(count):
            cid, kind, pad, self_val, opp_val = struct.unpack_from('<HBBii', data, pos)
            amounts.setdefault(cid, {})[kind] = (self_val, opp_val)
            pos += 12

        # 7490 Fire Trooper (-1000 opp)
        self.assertEqual(amounts[self.gids[7490]][2], (0, -1000))
        # 7083 Volcanic Scattershot (-500 opp)
        self.assertEqual(amounts[self.gids[7083]][2], (0, -500))
        # 7723 Jenis Lightsworn Mender (500 self, -500 opp)
        self.assertEqual(amounts[self.gids[7723]][2], (500, -500))
        # 7047 Meltiel (1000 self)
        self.assertEqual(amounts[self.gids[7047]][2], (1000, 0))
        # 6900 Ritual Foregone (-1000 self)
        self.assertEqual(amounts[self.gids[6900]][2], (-1000, 0))
        # 7570 Allure of Darkness (draw 2 self)
        self.assertEqual(amounts[self.gids[7570]][1], (2, 0))
        # 7452 The Beginning of the End (draw 3 self)
        self.assertEqual(amounts[self.gids[7452]][1], (3, 0))

    def test_destruction_records(self):
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
            rules[cid] = {'side': side, 'zones': zones, 'cond': cond}

        # 7285 Gladiator Beast Bestiari (side 3=both, zones 2=st, cond 6=target)
        r = rules[self.gids[7285]]
        self.assertEqual(r['zones'], 2)
        self.assertEqual(r['cond'], 6)
        # 7420 Dark Nephthys (side 3=both, zones 2=st, cond 6=target)
        r = rules[self.gids[7420]]
        self.assertEqual(r['zones'], 2)
        self.assertEqual(r['cond'], 6)


if __name__ == '__main__':
    unittest.main()
