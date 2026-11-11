"""Unit test for cannot_special_summon encoding across all 10 candidate cards."""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'mods'))
import tf3_efeitos_param as p

TARGET_IDS = [6821, 7091, 7092, 7114, 7146, 7265, 7547, 7728, 7983, 7984]

class CannotSSEncodingTest(unittest.TestCase):
    def test_cannot_special_summon_flags(self):
        spec = {str(cid): {'cannot_special_summon': True} for cid in TARGET_IDS}
        gids = {cid: 7000 + i for i, cid in enumerate(TARGET_IDS)}
        expected = b'TF3S' + struct.pack('<I', len(TARGET_IDS)) + b''.join(
            struct.pack('<HH', 7000 + i, 4) for i in range(len(TARGET_IDS))
        )
        self.assertEqual(p.resolver_invocacoes(gids, spec), expected)

    def test_rejects_non_boolean_cannot_special_summon(self):
        for invalid in ('yes', 1, None, []):
            with self.assertRaises(ValueError):
                p.resolver_invocacoes({6821: 7000}, {'6821': {'cannot_special_summon': invalid}})

if __name__ == '__main__':
    unittest.main()
