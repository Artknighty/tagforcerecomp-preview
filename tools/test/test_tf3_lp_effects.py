"""Unit tests for TF3 LP effect encoding (burn, heal, battle triggers, tribute burn)."""
import struct
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'mods'))
import tf3_efeitos_param as p


class TestLpEffectsEncoding(unittest.TestCase):
    def setUp(self):
        self.gids = {
            3722: 6960,
            6915: 7141,
            6936: 7153,
            7245: 7412,
            7426: 7574,
            7435: 7583,
            7885: 7951,
        }

    def test_lp_amounts_encoding(self):
        spec = {
            '3722': {'like': 5408, 'trigger_like': 5408, 'trigger_event': 'battle', 'lp': {'opp': -700}},
            '6915': {'like': 4865, 'lp': {'opp': -500}},
            '6936': {'like': 4317, 'trigger_like': 4317, 'trigger_condition': {'destroyed': True}, 'lp': {'self': 800, 'opp': 800}},
            '7245': {'like': 5408, 'trigger_like': 5408, 'trigger_event': 'battle', 'lp': {'opp': 'destroyed_atk'}},
            '7426': {'like': 4518, 'lp': {'opp': -800}},
            '7435': {'like': 5195, 'lp': {'opp': -1500}},
            '7885': {'like': 5408, 'trigger_like': 5408, 'trigger_event': 'battle', 'lp': {'opp': -300}},
        }
        data = p.resolver_valores(self.gids, spec)
        self.assertTrue(data.startswith(b'TF3V'))
        count = struct.unpack_from('<I', data, 4)[0]
        self.assertEqual(count, 7)

        # Parse each entry (u16 gid, u8 kind=2, u8 0, i32 self, i32 opp)
        offset = 8
        encoded = {}
        for _ in range(count):
            gid, kind, pad, self_amt, opp_amt = struct.unpack_from('<HBBii', data, offset)
            self.assertEqual(kind, 2)  # 2 = LP amount
            encoded[gid] = (self_amt, opp_amt)
            offset += 12

        self.assertEqual(encoded[6960], (0, -700))
        self.assertEqual(encoded[7141], (0, -500))
        self.assertEqual(encoded[7153], (800, 800))
        # destroyed_atk maps to INT32_MIN + 2
        self.assertEqual(encoded[7412], (0, -0x80000000 + 2))
        self.assertEqual(encoded[7574], (0, -800))
        self.assertEqual(encoded[7583], (0, -1500))
        self.assertEqual(encoded[7951], (0, -300))

    def test_triggers_encoding(self):
        spec = {
            '3722': {'like': 5408, 'trigger_like': 5408, 'trigger_event': 'battle', 'lp': {'opp': -700}},
            '6936': {'like': 4317, 'trigger_like': 4317, 'trigger_condition': {'destroyed': True}, 'lp': {'self': 800, 'opp': 800}},
            '7245': {'like': 5408, 'trigger_like': 5408, 'trigger_event': 'battle', 'lp': {'opp': 'destroyed_atk'}},
            '7885': {'like': 5408, 'trigger_like': 5408, 'trigger_event': 'battle', 'lp': {'opp': -300}},
        }
        trg = p.resolver_gatilhos(self.gids, spec)
        self.assertTrue(trg.startswith(b'TF3T'))
        count = struct.unpack_from('<I', trg, 4)[0]
        self.assertEqual(count, 4)

        # 5408 with trigger_event: battle has bit 14 set (0x4000)
        # 4317 has no battle bit
        offset = 8
        for _ in range(count):
            gid, donor_flags = struct.unpack_from('<HH', trg, offset)
            if gid in (6960, 7412, 7951):
                self.assertEqual(donor_flags, 5408 | 0x4000)
            elif gid == 7153:
                self.assertEqual(donor_flags, 4317)
            offset += 4


if __name__ == '__main__':
    unittest.main()
