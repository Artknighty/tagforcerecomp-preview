"""Unit tests for TF3 continuous stats & equip expansion."""
import struct
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'mods'))
import tf3_efeitos_param as p
import tf3_cartas


class TestStatExpansion(unittest.TestCase):
    def setUp(self):
        # Mock raw cards so that category checks pass
        raw_mock = {
            6993: {'w1': 13 << 1},
            7000: {'w1': 13 << 1},
            7076: {'w1': 1 << 1},
            7390: {'w1': 13 << 1},
            7446: {'w1': 13 << 1},
            7591: {'w1': 1 << 1},
            7598: {'w1': 1 << 1},
            7603: {'w1': 1 << 1},
            7691: {'w1': 13 << 1},
            7822: {'w1': 1 << 1},
            7886: {'w1': 13 << 1},
        }
        patcher = patch.object(tf3_cartas, 'carregar', return_value=raw_mock)
        patcher.start()
        self.addCleanup(patcher.stop)

        self.gids = {
            6993: 7206,
            7000: 7213,
            7076: 7257,
            7390: 7543,
            7446: 7593,
            7591: 7715,
            7598: 7722,
            7603: 7727,
            7691: 7803,
            7822: 7904,
            7886: 7952,
        }

    def test_encoding_equips_and_role_bonuses(self):
        spec = {
            '6993': {'scope': 'equip', 'atk': 800},
            '7000': {'scope': 'equip', 'atk': 200, 'def': 200},
            '7076': {'battle_role_bonus': 400},
            '7390': {'scope': 'equip', 'atk': 800},
            '7446': {'scope': 'equip', 'atk': 300},
            '7591': {'battle_role_bonus': 300},
            '7691': {'scope': 'equip', 'atk': 500, 'level': 1},
            '7822': {'battle_role_bonus': 300},
            '7886': {'scope': 'equip', 'atk': -800, 'def': -800},
        }
        with patch.object(p, 'propriedades', return_value={}):
            data = p.resolver_stats(self.gids, spec)

        self.assertTrue(data.startswith(b'TF3S'))
        # 6 equip rows
        row_count = struct.unpack_from('<I', data, 4)[0]
        self.assertEqual(row_count, 6)

        # LVL1 trailer for Synchro Boost (7803, level 1)
        lvl_pos = data.find(b'LVL1')
        self.assertGreater(lvl_pos, 0)
        lvl_count = struct.unpack_from('<I', data, lvl_pos + 4)[0]
        self.assertEqual(lvl_count, 1)
        cid, lvl = struct.unpack_from('<Hh', data, lvl_pos + 8)
        self.assertEqual((cid, lvl), (7803, 1))

        # BAR1 trailer for 3 battle role bonus monsters
        bar_pos = data.find(b'BAR1')
        self.assertGreater(bar_pos, 0)
        bar_count = struct.unpack_from('<I', data, bar_pos + 4)[0]
        self.assertEqual(bar_count, 3)

        encoded_bar = {}
        for i in range(3):
            cid, bonus = struct.unpack_from('<HH', data, bar_pos + 8 + i * 4)
            encoded_bar[cid] = bonus

        self.assertEqual(encoded_bar[7257], 400) # Topaz Tiger
        self.assertEqual(encoded_bar[7715], 300) # Jain
        self.assertEqual(encoded_bar[7904], 300) # Galahad


if __name__ == '__main__':
    unittest.main()
