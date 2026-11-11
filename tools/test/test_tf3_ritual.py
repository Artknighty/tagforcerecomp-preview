"""Validate Ritual row compatibility and exact-Level encoding without game data."""
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'mods'))
import tf3_efeitos_param as p


class RitualEncoding(unittest.TestCase):
    def encode(self, ritual):
        props = {7085: {'category': 5, 'level': 6}, 7087: {'category': 5, 'level': 8}}
        with patch.object(p, 'propriedades', return_value=props):
            return p.resolver_rituais({6850: 7088, 6845: 7085, 6847: 7087},
                                     {'6850': {'like': 4676, 'ritual': ritual}})

    def test_legacy_fixed_row_keeps_original_encoding(self):
        self.assertEqual(self.encode({'monster': 6845, 'level': 6}),
                         b'TF3R' + struct.pack('<IHHB', 1, 7088, 7085, 6))

    def test_exact_flag_does_not_change_printed_level(self):
        self.assertEqual(self.encode({'monsters': [6845, 6847], 'level': 'monster', 'exact': True}),
                         b'TF3R' + struct.pack('<I', 2) + struct.pack('<HHBHHB', 7088, 7085, 134, 7088, 7087, 136))

    def test_exact_flag_rejects_truthy_strings(self):
        with self.assertRaises(ValueError):
            self.encode({'monster': 6845, 'level': 6, 'exact': 'false'})



if __name__ == '__main__':
    unittest.main()
