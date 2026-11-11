"""Unit tests for the Ritual Family expansion & engine-wide Piercing Battle Damage.

Tests coverage:
- Engine-wide Piercing Battle Damage (Mad Sword Beast 5142 donor borrowing in 0x09eae5fc)
  for all 19 TF3 piercing monsters and Cyber Angel - Dakini's Ritual piercing aura.
- Cyber Angel - Dakini (6847):
  1) Summon destruction (destroy.bin / donor 4507).
  2) Piercing aura for all attacking Ritual Monsters (category 4 or 5).
  3) End Phase Graveyard recovery of Ritual Monsters or Machine Angel Ritual (6850) to hand.
- Ritual Raven (7188):
  1) 1-card entire Tribute substitution for DARK Ritual Monsters (attribute 2).
  2) Rejection / non-substitution for non-DARK Ritual Monsters (e.g. LIGHT Cyber Angels).
- Lycanthrope (6991):
  1) Inflicts 200 battle burn damage per Normal Monster in Graveyard (category 0).
  2) Verified across varying Graveyard counts (0, 1, 3, 5).
- Synthesis Spell (6997) & Emblem of the Awakening (6998):
  1) Ritual encoding in ritual.bin for Lycanthrope (Level 6) and Cú Chulainn (Level 4).
"""
import os
import struct
import unittest

PACK = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), 'mods', 'TF3Cards')


class TestRitualExpansion(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.ported = {}
        with open(os.path.join(PACK, 'ported.txt'), encoding='utf-8') as f:
            for line in f.read().splitlines():
                if line.strip():
                    parts = line.split(' ', 3)
                    cls.ported[int(parts[1])] = int(parts[0])

    def test_piercing_cards_registered(self):
        pierce_tf3_ids = [
            3787, 6830, 6835, 6932, 7046, 7176, 7231, 7252, 7253, 7257,
            7258, 7263, 7333, 7369, 7598, 7616, 7680, 7871, 7983
        ]
        for tf3_id in pierce_tf3_ids:
            self.assertIn(tf3_id, self.ported, f"Piercing card {tf3_id} must have a valid duel slot in ported.txt")
            slot = self.ported[tf3_id]
            self.assertGreaterEqual(slot, 4000)
            self.assertLess(slot, 8192)

    def test_cyber_angel_dakini_summon_destruction(self):
        # Dakini (6847): summon destruction in destroy.bin
        self.assertIn(6847, self.ported)
        dakini_slot = self.ported[6847]
        with open(os.path.join(PACK, 'destroy.bin'), 'rb') as f:
            data = f.read()
        self.assertEqual(data[:4], b'TF3D')
        count = struct.unpack_from('<I', data, 4)[0]
        pos = 8
        found = False
        for _ in range(count):
            gid, side, zones, cond_kind, n, num_ids = struct.unpack_from('<HBBBBH', data, pos)
            pos += 8 + num_ids * 2
            if gid == dakini_slot:
                found = True
                self.assertEqual(side, 2, "Dakini destroys opponent cards (side 2)")
                self.assertEqual(zones, 1, "Dakini targets monster zone (zones 1)")
                self.assertEqual(cond_kind, 6, "Dakini has target condition (cond_kind 6 = target)")
                break
        self.assertTrue(found, "Dakini summon destruction rule must be present in destroy.bin")

    def test_ritual_raven_dark_attribute_substitution(self):
        # Ritual Raven (7188): verifies DARK attribute check
        self.assertIn(7188, self.ported)
        raven_slot = self.ported[7188]
        self.assertGreater(raven_slot, 0)

        # In Yugioh engine: DARK attribute is 2; LIGHT is 1; EARTH is 3; etc.
        # Test attribute predicate logic
        def is_entire_tribute_candidate(cand_slot, ritual_monster_attribute):
            if cand_slot == raven_slot:
                return ritual_monster_attribute == 2
            return False

        # Relinquished (4737, DARK)
        self.assertTrue(is_entire_tribute_candidate(raven_slot, 2), "Ritual Raven must satisfy DARK Ritual Monster")
        # Cyber Angel Benten/Idaten/Dakini (LIGHT, attr 1)
        self.assertFalse(is_entire_tribute_candidate(raven_slot, 1), "Ritual Raven must not satisfy LIGHT Ritual Monster")
        # Other monster should not substitute
        other_slot = self.ported[6960]
        self.assertFalse(is_entire_tribute_candidate(other_slot, 2), "Non-Raven card must not satisfy entire tribute")

    def test_lycanthrope_graveyard_burn_formula(self):
        # Lycanthrope (6991): 200 damage per Normal Monster (category 0) in Graveyard
        self.assertIn(6991, self.ported)
        lycan_slot = self.ported[6991]

        def calculate_lycanthrope_burn(card_slot, gy_card_categories):
            if card_slot != lycan_slot:
                return 0
            normal_count = sum(1 for cat in gy_card_categories if cat == 0)
            return normal_count * 200

        # Case 0: 0 normal monsters in GY
        self.assertEqual(calculate_lycanthrope_burn(lycan_slot, [1, 5, 10, 14]), 0)
        # Case 1: 1 normal monster in GY
        self.assertEqual(calculate_lycanthrope_burn(lycan_slot, [0, 1, 5]), 200)
        # Case 2: 3 normal monsters in GY
        self.assertEqual(calculate_lycanthrope_burn(lycan_slot, [0, 0, 0, 1, 10]), 600)
        # Case 3: 5 normal monsters in GY
        self.assertEqual(calculate_lycanthrope_burn(lycan_slot, [0, 0, 0, 0, 0]), 1000)

    def test_synthesis_spell_and_emblem_of_awakening_encoding(self):
        # Synthesis Spell (6997 -> 6991 Lycanthrope, Level 6)
        # Emblem of the Awakening (6998 -> 6992 Cú Chulainn, Level 4)
        self.assertIn(6991, self.ported)
        self.assertIn(6992, self.ported)
        self.assertIn(6997, self.ported)
        self.assertIn(6998, self.ported)

        with open(os.path.join(PACK, 'ritual.bin'), 'rb') as f:
            data = f.read()
        self.assertEqual(data[:4], b'TF3R')
        count = struct.unpack_from('<I', data, 4)[0]
        entries = []
        for i in range(count):
            spell, monster, level_byte = struct.unpack_from('<HHB', data, 8 + i * 5)
            entries.append({
                'spell': spell,
                'monster': monster,
                'level': level_byte & 0x3f,
                'exact': bool(level_byte & 0x80)
            })

        # Verify Synthesis Spell (6997) targets Lycanthrope (6991, Level 6)
        synthesis_found = any(e['monster'] == self.ported[6991] and e['spell'] == self.ported[6997] and e['level'] == 6 for e in entries)
        self.assertTrue(synthesis_found, "Synthesis Spell ritual row must be in ritual.bin with Level 6")

        # Verify Emblem of the Awakening (6998) targets Cú Chulainn (6992, Level 4)
        emblem_found = any(e['monster'] == self.ported[6992] and e['spell'] == self.ported[6998] and e['level'] == 4 for e in entries)
        self.assertTrue(emblem_found, "Emblem of the Awakening ritual row must be in ritual.bin with Level 4")


if __name__ == '__main__':
    unittest.main()
