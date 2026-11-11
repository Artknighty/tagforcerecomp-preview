import os
import struct
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PACK = os.path.join(ROOT, 'mods', 'TF3Cards')


class TestTF3Batch12(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(os.path.join(PACK, 'ported.txt'), encoding='utf-8') as f:
            rows = [l.split(' ', 3) for l in f.read().splitlines() if l.strip()]
        cls.gids = {int(r[1]): int(r[0]) for r in rows}
        cls.likes = {int(r[1]): int(r[2]) for r in rows}

    def test_multi_attackers_and_piercing(self):
        # 7199 Grasschopper -> like 5381 (Asura Priest)
        self.assertEqual(self.likes.get(7199), 5381)
        # 3766 Zero Sprite -> like 5137 (Hayabusa Knight)
        self.assertEqual(self.likes.get(3766), 5137)
        # 7259 Evil Hero Infernal Gainer -> like 5137
        self.assertEqual(self.likes.get(7259), 5137)
        # 7231 Strike Slash -> like 5142 (Mad Sword Beast)
        self.assertEqual(self.likes.get(7231), 5142)

    def test_cannot_special_summon_flags(self):
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

        # 5097 Masked Beast Des Guardius (flags=4)
        self.assertEqual(locks[self.gids[5097]], 4)
        # 7096 Destiny Hero - Plasma (flags=4)
        self.assertEqual(locks[self.gids[7096]], 4)
        # 7171 Rainbow Dragon (flags=4)
        self.assertEqual(locks[self.gids[7171]], 4)
        # 7406 Rainbow Dark Dragon (flags=4)
        self.assertEqual(locks[self.gids[7406]], 4)
        # 7805 Neos Wiseman (flags=4)
        self.assertEqual(locks[self.gids[7805]], 4)

    def test_search_and_recovery_effects(self):
        with open(os.path.join(PACK, 'effects.bin'), 'rb') as f:
            data = f.read()
        magic, count = struct.unpack_from('<4sI', data, 0)
        self.assertEqual(magic, b'TF3E')
        pos = 8
        effects = {}
        for _ in range(count):
            gid, zone_flags, extra, id_count = struct.unpack_from('<HBBI', data, pos)
            pos += 8
            ids = struct.unpack_from(f'<{id_count}H', data, pos)
            pos += 2 * id_count
            effects[gid] = {'zone': zone_flags & 0x0F, 'extra': extra, 'ids': ids}

        # 7839 Genex Neutron: zone 1 (deck)
        self.assertEqual(effects[self.gids[7839]]['zone'], 1)
        # 6735 Expressroid: zone 2 (grave), extra 1 (count 2)
        self.assertEqual(effects[self.gids[6735]]['zone'], 2)
        self.assertEqual(effects[self.gids[6735]]['extra'], 1)
        # 7004 Skyscraper 2: zone 3 (grave_summon)
        self.assertEqual(effects[self.gids[7004]]['zone'], 3)
        # 7573 Valhalla: zone 4 (hand)
        self.assertEqual(effects[self.gids[7573]]['zone'], 4)

    def test_stat_bonuses(self):
        with open(os.path.join(PACK, 'stat.bin'), 'rb') as f:
            data = f.read()
        magic, count = struct.unpack_from('<4sI', data, 0)
        self.assertEqual(magic, b'TF3S')
        self.assertGreaterEqual(count, 30)

    def test_amount_draw_and_lp(self):
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

        # 7458 Six Samurai United (draw self 2)
        self.assertEqual(amounts[self.gids[7458]][1], (2, 0))
        # 7755 Defense Draw (draw self 1)
        self.assertEqual(amounts[self.gids[7755]][1], (1, 0))
        # 7085 Volcanic Doomfire (burn opp -500)
        self.assertEqual(amounts[self.gids[7085]][2], (0, -500))
        # 7379 Evil Hero Infernal Sniper (burn opp -1000)
        self.assertEqual(amounts[self.gids[7379]][2], (0, -1000))
        # 7370 Imprisoned Queen Archfiend (pay self -1000)
        self.assertEqual(amounts[self.gids[7370]][2], (-1000, 0))

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

        # 6966 Six Samurai - Yaichi (side opp 2, zones st 2, cond target 6)
        self.assertEqual(rules[self.gids[6966]]['side'], 2)
        self.assertEqual(rules[self.gids[6966]]['zones'], 2)
        self.assertEqual(rules[self.gids[6966]]['cond'], 6)

        # 7284 Gladiator Beast Murmillo (side opp 2, zones monster 1, cond target 6)
        self.assertEqual(rules[self.gids[7284]]['side'], 2)
        self.assertEqual(rules[self.gids[7284]]['zones'], 1)
        self.assertEqual(rules[self.gids[7284]]['cond'], 6)

        # 7617 Gladiator Beast Gyzarus (side opp 2, zones all 3, cond target 6)
        self.assertEqual(rules[self.gids[7617]]['side'], 2)
        self.assertEqual(rules[self.gids[7617]]['zones'], 3)
        self.assertEqual(rules[self.gids[7617]]['cond'], 6)

        # 3754 Burst Impact (side both 3, zones all 3, cond target_all 7)
        self.assertEqual(rules[self.gids[3754]]['side'], 3)
        self.assertEqual(rules[self.gids[3754]]['zones'], 3)
        self.assertEqual(rules[self.gids[3754]]['cond'], 7)

    def test_attack_negators(self):
        # 7694 Scrap-Iron Scarecrow -> like 4804 (Negate Attack)
        self.assertEqual(self.likes.get(7694), 4804)
        # 7226 Ambush Fangs -> like 4804
        self.assertEqual(self.likes.get(7226), 4804)
        # 7703 Shield Warrior -> like 4886 (Waboku)
        self.assertEqual(self.likes.get(7703), 4886)


if __name__ == '__main__':
    unittest.main()
