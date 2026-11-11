import os
import struct
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PACK = os.path.join(ROOT, 'mods', 'TF3Cards')


class TestTF3Batch10(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(os.path.join(PACK, 'ported.txt'), encoding='utf-8') as f:
            rows = [l.split(' ', 3) for l in f.read().splitlines() if l.strip()]
        cls.gids = {int(r[1]): int(r[0]) for r in rows}
        cls.likes = {int(r[1]): int(r[2]) for r in rows}

    def test_double_attack_and_piercing(self):
        # Double Attack (5137): 6892, 6893, 6965, 7288, 7877
        for cid in (6892, 6893, 6965, 7288, 7877):
            self.assertEqual(self.likes.get(cid), 5137, f"Card {cid} expected like 5137 (double attack)")

        # Piercing (5142 / 6315): 7680, 6932
        self.assertEqual(self.likes.get(7680), 5142, "Card 7680 expected like 5142 (piercing)")
        # 6932 Ancient Gear Gadjiltron Dragon upgraded to 6315 (Ancient Gear Golem: piercing + attack lock)
        self.assertEqual(self.likes.get(6932), 6315)

        # Direct Attacker (4428): 7606
        self.assertEqual(self.likes.get(7606), 4428)

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

        # 3782 Attribute Bomb (-1000 opp)
        self.assertEqual(amounts[self.gids[3782]][2], (0, -1000))
        # 7693 Synchronized Realm (-500 opp)
        self.assertEqual(amounts[self.gids[7693]][2], (0, -500))
        # 7724 Mecha Bunny (-500 opp)
        self.assertEqual(amounts[self.gids[7724]][2], (0, -500))
        # 7846 Zombie Mammoth (-1000 opp)
        self.assertEqual(amounts[self.gids[7846]][2], (0, -1000))
        # 5399 Royal Oppression (-800 self)
        self.assertEqual(amounts[self.gids[5399]][2], (-800, 0))
        # 7134 Backs to the Wall (-1000 self)
        self.assertEqual(amounts[self.gids[7134]][2], (-1000, 0))
        # 7353 Dark Cure (1000 self)
        self.assertEqual(amounts[self.gids[7353]][2], (1000, 0))

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

        # 3721 Slash Draw (draw self 1)
        self.assertEqual(amounts[self.gids[3721]][1], (1, 0))
        # 7087 Soul of Fire (draw opp 1)
        self.assertEqual(amounts[self.gids[7087]][1], (0, 1))
        # 7229 Destructive Draw (draw self 1)
        self.assertEqual(amounts[self.gids[7229]][1], (1, 0))
        # 5449 Pharaoh's Treasure (draw self 1)
        self.assertEqual(amounts[self.gids[5449]][1], (1, 0))

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

        # 3737 Jurassic Impact: side both (3), zones monster (1), cond target_all (7)
        self.assertEqual(rules[self.gids[3737]]['side'], 3)
        self.assertEqual(rules[self.gids[3737]]['zones'], 1)
        self.assertEqual(rules[self.gids[3737]]['cond'], 7)

        # 3738 Last Machine Acid Virus: side opp (2), zones monster (1), cond target_all (7)
        self.assertEqual(rules[self.gids[3738]]['side'], 2)
        self.assertEqual(rules[self.gids[3738]]['zones'], 1)

        # 6784 Stratos: side opp (2), zones st (2), cond target (6)
        self.assertEqual(rules[self.gids[6784]]['side'], 2)
        self.assertEqual(rules[self.gids[6784]]['zones'], 2)
        self.assertEqual(rules[self.gids[6784]]['cond'], 6)

        # 7595 Ryko: side both (3), zones all (3), cond target (6)
        self.assertEqual(rules[self.gids[7595]]['side'], 3)
        self.assertEqual(rules[self.gids[7595]]['zones'], 3)
        self.assertEqual(rules[self.gids[7595]]['cond'], 6)


if __name__ == '__main__':
    unittest.main()
