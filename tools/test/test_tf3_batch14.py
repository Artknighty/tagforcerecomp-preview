import os
import struct
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PACK = os.path.join(ROOT, 'mods', 'TF3Cards')


class TestTF3Batch14(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(os.path.join(PACK, 'ported.txt'), encoding='utf-8') as f:
            rows = [l.split(' ', 3) for l in f.read().splitlines() if l.strip()]
        cls.gids = {int(r[1]): int(r[0]) for r in rows}
        cls.likes = {int(r[1]): int(r[2]) for r in rows}

    def test_search_and_hand_grave_ss(self):
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

        # 6785 Hero's Bond: zone 4 (hand)
        self.assertEqual(effects[self.gids[6785]]['zone'], 4)
        # 6865 Rose Bud: zone 4 (hand)
        self.assertEqual(effects[self.gids[6865]]['zone'], 4)
        # 7704 Small Piece Golem: zone 4 (hand)
        self.assertEqual(effects[self.gids[7704]]['zone'], 4)
        # 7854 Red-Eyes Zombie Dragon: zone 3 (grave_summon)
        self.assertEqual(effects[self.gids[7854]]['zone'], 3)
        # 7808 Genex Undine: zone 1 (deck)
        self.assertEqual(effects[self.gids[7808]]['zone'], 1)
        # 7874 Morphtronic Celfon: zone 1 (deck)
        self.assertEqual(effects[self.gids[7874]]['zone'], 1)
        # 7596 Wulf, Lightsworn Beast: zone 3 (grave_summon)
        self.assertEqual(effects[self.gids[7596]]['zone'], 3)

    def test_stat_modifiers(self):
        with open(os.path.join(PACK, 'stat.bin'), 'rb') as f:
            data = f.read()
        magic, count = struct.unpack_from('<4sI', data, 0)
        self.assertEqual(magic, b'TF3S')
        self.assertGreaterEqual(count, 30)

    def test_utility_cards(self):
        # 6558 Gorz the Emissary of Darkness -> like 4518
        self.assertEqual(self.likes.get(6558), 4518)
        # 7574 Honest -> like 4518
        self.assertEqual(self.likes.get(7574), 4518)
        # 6980 D.D. Crow -> like 4518
        self.assertEqual(self.likes.get(6980), 4518)
        # 7641 Tour of Doom -> like 4145
        self.assertEqual(self.likes.get(7641), 4145)


if __name__ == '__main__':
    unittest.main()
