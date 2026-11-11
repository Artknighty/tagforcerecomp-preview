"""Conditional Damage Step amounts must not become permanent stat rows."""
import struct
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'mods'))
import tf3_efeitos_param as p
import tf3_cartas


class LightBonusEncoding(unittest.TestCase):
    def setUp(self):
        for target, name, value in ((p, 'propriedades', {}),
                                    (tf3_cartas, 'carregar', {7433:{'w1':2},7541:{'w1':2},7542:{'w1':2},7812:{'w1':32}})):
            mock = patch.object(target, name, return_value=value)
            mock.start()
            self.addCleanup(mock.stop)

    def test_shared_amounts_without_passive_rows(self):
        data = p.resolver_stats({7541:7665,7542:7666},
                               {'7542':{'battle_light_bonus':700},'7541':{'battle_light_bonus':200}})
        self.assertEqual(data, b'TF3S'+struct.pack('<I',0)+b'BAP1'+struct.pack('<IHHHH',2,7665,200,7666,700))

    def test_rejects_invalid_or_combined_amounts(self):
        for value in (0,-200,201,10000,True,200.0):
            with self.assertRaises(ValueError):
                p.resolver_stats({7541:7665},{'7541':{'battle_light_bonus':value}})
        with self.assertRaises(ValueError):
            p.resolver_stats({7541:7665},{'7541':{'battle_light_bonus':200,'atk':200}})

    def test_rejects_unsupported_tuner(self):
        with self.assertRaises(ValueError):
            p.resolver_stats({7812:7894},{'7812':{'battle_light_bonus':700}})

    def test_role_magnitude_without_passive_rows(self):
        self.assertEqual(p.resolver_stats({7433:7581},{'7433':{'battle_role_bonus':400}}),
                         b'TF3S'+struct.pack('<I',0)+b'BAR1'+struct.pack('<IHH',1,7581,400))

    def test_rejects_invalid_role_rules(self):
        for value in (0,-400,401,10000,True,400.0):
            with self.assertRaises(ValueError):
                p.resolver_stats({7433:7581},{'7433':{'battle_role_bonus':value}})
        for cid,rule in ((7433,{'battle_role_bonus':400,'atk':400}),(7812,{'battle_role_bonus':400})):
            with self.assertRaises(ValueError):
                p.resolver_stats({cid:7581},{str(cid):rule})


if __name__ == '__main__':
    unittest.main()
