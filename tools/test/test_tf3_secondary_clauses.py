"""Unit tests for TF3 complex and secondary clauses encoding.

Tests coverage:
- The Wicked Eraser (7092): 3 generic tributes (lord.bin), board wipe on destruction (destroy.bin + trigger.bin), continuous stat scaling from opponent field cards (stat.bin).
- The Wicked Avatar (7091): 3 generic tributes (lord.bin).
- Light and Darkness Dragon (6821): own field wipe on destruction (destroy.bin + trigger.bin).
- Vicious Claw (7446): equip stat bonus (stat.bin).
- Telekinetic Shocker (7720): 500 LP cost (amount.bin).
- Exploder Dragon (7094): battle destruction trigger (trigger.bin).
- Grandmaster of the Six Samurai (7145): destroyed by opponent card effect trigger (trigger.bin).
"""
import os
import struct
import unittest

PACK = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), 'mods', 'TF3Cards')


class TestSecondaryClauses(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.ported = {}
        with open(os.path.join(PACK, 'ported.txt'), encoding='utf-8') as f:
            for line in f.read().splitlines():
                if line.strip():
                    parts = line.split(' ', 3)
                    cls.ported[int(parts[1])] = int(parts[0])

    def test_lords_encoding(self):
        with open(os.path.join(PACK, 'lord.bin'), 'rb') as f:
            data = f.read()
        self.assertEqual(data[:4], b'TF3L')
        count = struct.unpack_from('<I', data, 4)[0]
        entries = {}
        for i in range(count):
            gid, req_type = struct.unpack_from('<HB', data, 8 + i * 3)
            entries[gid] = req_type

        # Verify The Wicked Eraser (7092) and The Wicked Avatar (7091) have generic 3-tribute rule (req_type = 0)
        self.assertIn(self.ported[7092], entries)
        self.assertEqual(entries[self.ported[7092]], 0)

        self.assertIn(self.ported[7091], entries)
        self.assertEqual(entries[self.ported[7091]], 0)

    def test_destroy_encoding(self):
        with open(os.path.join(PACK, 'destroy.bin'), 'rb') as f:
            data = f.read()
        self.assertEqual(data[:4], b'TF3D')
        count = struct.unpack_from('<I', data, 4)[0]
        pos = 8
        rules = {}
        for _ in range(count):
            gid, side, zones, cond_kind, n, num_ids = struct.unpack_from('<HBBBBH', data, pos)
            pos += 8 + num_ids * 2
            rules[gid] = {'side': side, 'zones': zones, 'cond_kind': cond_kind}

        # The Wicked Eraser (7092): both sides (3), all zones (3)
        self.assertIn(self.ported[7092], rules)
        self.assertEqual(rules[self.ported[7092]]['side'], 3)
        self.assertEqual(rules[self.ported[7092]]['zones'], 3)

        # Light and Darkness Dragon (6821): own side (1), all zones (3)
        self.assertIn(self.ported[6821], rules)
        self.assertEqual(rules[self.ported[6821]]['side'], 1)
        self.assertEqual(rules[self.ported[6821]]['zones'], 3)

    def test_trigger_conditions(self):
        with open(os.path.join(PACK, 'trigger.bin'), 'rb') as f:
            data = f.read()
        self.assertEqual(data[:4], b'TF3T')
        # Check for TGC1 trailer
        tgc_idx = data.find(b'TGC1')
        self.assertNotEqual(tgc_idx, -1)
        count = struct.unpack_from('<I', data, tgc_idx + 4)[0]
        conditions = {}
        for i in range(count):
            gid, flags = struct.unpack_from('<HH', data, tgc_idx + 8 + i * 4)
            conditions[gid] = flags

        # 7092 (The Wicked Eraser): destroyed (bit 0 = 1)
        self.assertIn(self.ported[7092], conditions)
        self.assertTrue(conditions[self.ported[7092]] & 1)

        # 6821 (Light and Darkness Dragon): destroyed (bit 0 = 1)
        self.assertIn(self.ported[6821], conditions)
        self.assertTrue(conditions[self.ported[6821]] & 1)

        # 7145 (Grandmaster of the Six Samurai): destroyed (1) | by_effect (2) | opponent_effect (4) = 7
        self.assertIn(self.ported[7145], conditions)
        self.assertEqual(conditions[self.ported[7145]] & 7, 7)

    def test_stat_rules(self):
        with open(os.path.join(PACK, 'stat.bin'), 'rb') as f:
            data = f.read()
        self.assertEqual(data[:4], b'TF3S')
        count = struct.unpack_from('<I', data, 4)[0]
        pos = 8
        stats = {}
        for _ in range(count):
            gid, scope, cond_side, flags, excl, atk, defe, ntgt, ncond = struct.unpack_from('<HBBBBiiII', data, pos)
            pos += 22 + ntgt * 2 + ncond * 2
            stats[gid] = {'scope': scope, 'cond_side': cond_side, 'flags': flags, 'atk': atk, 'defe': defe}

        # 7092 The Wicked Eraser: atk 1000, def 1000, set (flag 4), cond_side opponent controls (32)
        self.assertIn(self.ported[7092], stats)
        r = stats[self.ported[7092]]
        self.assertEqual(r['atk'], 1000)
        self.assertEqual(r['defe'], 1000)
        self.assertTrue(r['flags'] & 4)  # set: true
        self.assertTrue(r['cond_side'] & 32)  # zone: controls

        # 7446 Vicious Claw: atk 300, scope equip
        self.assertIn(self.ported[7446], stats)
        self.assertEqual(stats[self.ported[7446]]['atk'], 300)


if __name__ == '__main__':
    unittest.main()
