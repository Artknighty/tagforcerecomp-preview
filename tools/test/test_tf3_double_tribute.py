"""Regression for truncated Normal Tuner metadata in the tribute target generator."""
import struct
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'mods'))
import tf3_efeitos_param as params
import tf3_cartas


class NormalTargetCategory(unittest.TestCase):
    def test_truncated_normal_tuner_is_not_a_normal_monster_target(self):
        gids = {7213: 7386, 7684: 7800, 9999: 7801}
        raw = {7213: {'w1': 2}, 7684: {'w1': 15 << 1}, 9999: {'w1': 0}}
        props = {4007: {'category': 0, 'attribute': 1}, 7386: {'category': 1, 'attribute': 1},
                 7800: {'category': 0, 'attribute': 1}, 7801: {'category': 0, 'attribute': 1}}
        with patch.object(params, 'propriedades', return_value=props), patch.object(tf3_cartas, 'carregar', return_value=raw):
            data = params.resolver_tributos(gids, {'7213': {'double_tribute_normal': 'light'}})
        self.assertEqual(data[:4], b'TF3B')
        self.assertEqual(struct.unpack_from('<IHHI', data, 4), (1, 7386, 5409, 2))
        self.assertEqual(struct.unpack_from('<2H', data, 16), (4007, 7801))


if __name__ == '__main__':
    unittest.main()
