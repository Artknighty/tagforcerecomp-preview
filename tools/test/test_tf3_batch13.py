import os
import struct
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PACK = os.path.join(ROOT, 'mods', 'TF3Cards')


class TestTF3Batch13(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(os.path.join(PACK, 'ported.txt'), encoding='utf-8') as f:
            rows = [l.split(' ', 3) for l in f.read().splitlines() if l.strip()]
        cls.gids = {int(r[1]): int(r[0]) for r in rows}
        cls.likes = {int(r[1]): int(r[2]) for r in rows}

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

        # 3746 Mesozoic Fossil Knight - Skullknight (flags=4)
        self.assertEqual(locks[self.gids[3746]], 4)
        # 6731 Elemental Hero Terra Firma (flags=4)
        self.assertEqual(locks[self.gids[6731]], 4)
        # 6833 Cyberdark Dragon (flags=4)
        self.assertEqual(locks[self.gids[6833]], 4)
        # 7261 Evil Hero Dark Gaia (flags=4)
        self.assertEqual(locks[self.gids[7261]], 4)
        # 7443 Rainbow Neos (flags=4)
        self.assertEqual(locks[self.gids[7443]], 4)
        # 7806 Elemental Hero Divine Neos (flags=4)
        self.assertEqual(locks[self.gids[7806]], 4)

    def test_search_and_grave_hand_ss(self):
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

        # 7209 Neo Space Pathfinder: zone 1 (deck)
        self.assertEqual(effects[self.gids[7209]]['zone'], 1)
        # 7243 Volcanic Rocket: zone 1 (deck)
        self.assertEqual(effects[self.gids[7243]]['zone'], 1)
        # 7359 Mezuki: zone 3 (grave_summon)
        self.assertEqual(effects[self.gids[7359]]['zone'], 3)
        # 7432 Gladiator Beast Darius: zone 3 (grave_summon)
        self.assertEqual(effects[self.gids[7432]]['zone'], 3)
        # 7594 Lumina: zone 3 (grave_summon)
        self.assertEqual(effects[self.gids[7594]]['zone'], 3)
        # 7561 Dark Grepher: zone 4 (hand)
        self.assertEqual(effects[self.gids[7561]]['zone'], 4)
        # 7770 Dark Simorgh: zone 4 (hand)
        self.assertEqual(effects[self.gids[7770]]['zone'], 4)

    def test_stat_modifiers(self):
        with open(os.path.join(PACK, 'stat.bin'), 'rb') as f:
            data = f.read()
        magic, count = struct.unpack_from('<4sI', data, 0)
        self.assertEqual(magic, b'TF3S')
        self.assertGreaterEqual(count, 30)

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

        # 7117 Raiza the Storm Monarch (side opp 2, zones all 3, cond target 6)
        self.assertEqual(rules[self.gids[7117]]['side'], 2)
        self.assertEqual(rules[self.gids[7117]]['zones'], 3)
        self.assertEqual(rules[self.gids[7117]]['cond'], 6)

        # 6988 Elemental Hero Glow Neos (side opp 2, zones all 3, cond target 6)
        self.assertEqual(rules[self.gids[6988]]['side'], 2)
        self.assertEqual(rules[self.gids[6988]]['zones'], 3)
        self.assertEqual(rules[self.gids[6988]]['cond'], 6)

        # 7057 Armoroid Gaidengo (side opp 2, zones st 2, cond target_all 7)
        self.assertEqual(rules[self.gids[7057]]['side'], 2)
        self.assertEqual(rules[self.gids[7057]]['zones'], 2)
        self.assertEqual(rules[self.gids[7057]]['cond'], 7)

    def test_utility_donors(self):
        # 6161 Gold Sarcophagus -> like 4145
        self.assertEqual(self.likes.get(6161), 4145)
        # 7904 Black Garden -> like 4145
        self.assertEqual(self.likes.get(7904), 4145)
        # 7178 Necro Gardna -> like 4518
        self.assertEqual(self.likes.get(7178), 4518)
        # 7078 Crystal Beast Sapphire Pegasus -> like 4518
        self.assertEqual(self.likes.get(7078), 4518)


if __name__ == '__main__':
    unittest.main()
