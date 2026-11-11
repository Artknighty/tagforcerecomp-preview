"""Battle protection is an auxiliary rule, never an additive ATK bonus."""
import struct
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'mods'))
import tf3_efeitos_param as p
import tf3_cartas


class BattleGuardEncoding(unittest.TestCase):
    def setUp(self):
        cards_mock = {7686: {'w1': 2}, 7812: {'w1': 32}, 7052: {'w1': 2}, 7264: {'w1': 2}}
        raw = patch.object(tf3_cartas, 'carregar', return_value=cards_mock)
        raw.start()
        self.addCleanup(raw.stop)
    def test_bounded_thresholds_without_stat_rows(self):
        with patch.object(p, 'propriedades', return_value={}):
            data = p.resolver_stats({7686: 7798}, {
                '7686': {'battle_guard': {'stat': 'atk', 'min': 1900}}})
        self.assertEqual(data[:8], b'TF3S'+struct.pack('<I',0))
        self.assertEqual(data[8:], b'BGP1'+struct.pack('<IHHI',1,7798,1,1900))

    def test_unconditional_battle_guard_min_one(self):
        with patch.object(p, 'propriedades', return_value={}):
            data = p.resolver_stats({7052: 7245, 7264: 7429}, {
                '7052': {'battle_guard': {'stat': 'atk', 'min': 1}},
                '7264': {'battle_guard': {'stat': 'atk', 'min': 1}},
            })
        self.assertEqual(data[:8], b'TF3S'+struct.pack('<I',0))
        self.assertEqual(data[8:16], b'BGP1'+struct.pack('<I',2))
        self.assertEqual(data[16:24], struct.pack('<HHI', 7245, 1, 1))
        self.assertEqual(data[24:32], struct.pack('<HHI', 7429, 1, 1))

    def test_rejects_ambiguous_rules(self):
        for rule in ({'battle_guard': {'stat': 'def', 'min': 4}},
                     {'battle_guard': {'stat': 'level', 'min': 0}},
                     {'battle_guard': {'stat': 'atk', 'min': 1900}, 'atk': 200}):
            with patch.object(p, 'propriedades', return_value={}), self.assertRaises(ValueError):
                p.resolver_stats({7686: 7798}, {'7686':rule})

    def test_rejects_unsupported_tuner(self):
        with patch.object(p, 'propriedades', return_value={}), self.assertRaises(ValueError):
            p.resolver_stats({7812:7894}, {'7812':{'battle_guard':{'stat':'atk','min':1900}}})


if __name__ == '__main__':
    unittest.main()
