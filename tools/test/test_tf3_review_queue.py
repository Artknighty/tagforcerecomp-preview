"""Triage must reject duplicate identities, unsupported categories and partial clauses."""
import sys
from pathlib import Path
import unittest
import struct

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'mods'))
from tf3_review_queue import build, family, rule_content


class ReviewQueueTests(unittest.TestCase):
    def test_keyed_row_order_is_not_a_rule_change(self):
        header=b'TF3E'+struct.pack('<I',2)
        a=struct.pack('<HBBIH',7000,1,0,1,4007)
        b=struct.pack('<HBBIH',7001,2,0,1,4009)
        self.assertEqual(rule_content('effects.bin',header+a+b),rule_content('effects.bin',header+b+a))
        self.assertNotEqual(rule_content('effects.bin',header+a+b),rule_content('effects.bin',header+a+b[:-2]+struct.pack('<H',4010)))
        with self.assertRaises(ValueError):
            rule_content('effects.bin',header+a+a)
        with self.assertRaises(ValueError):
            rule_content('effects.bin',header+a+b[:-1])

    def test_trigger_order_and_donor_changes(self):
        header=b'TF3T'+struct.pack('<I',2)
        a,b=struct.pack('<HH',7000,4507),struct.pack('<HH',7001,5769)
        self.assertEqual(rule_content('trigger.bin',header+a+b),rule_content('trigger.bin',header+b+a))
        self.assertNotEqual(rule_content('trigger.bin',header+a+b),rule_content('trigger.bin',header+a+struct.pack('<HH',7001,4546)))
    def card(self, cid=1, **values):
        return dict(dict(tf3_id=cid, tf1_id=None, name='Test card', passcode=123,
                         kind='spell', category=13, text_tf3='Add 1 Warrior-Type monster from your Deck to your hand.'), **values)

    def test_complete_pattern_only(self):
        card = self.card()
        self.assertEqual(family(card)[0], 'existing_search_pattern')
        card['text_tf3'] += ' Then discard 1 card.'
        self.assertEqual(family(card)[0], 'manual_review')

    def test_category_truncation_blocks_normal_and_existing_rules(self):
        for raw_category in (15, 16, 17, 18):
            card = self.card(kind='monster', category=raw_category & 15)
            report = build([card], {1: {'w1': raw_category << 1}}, {1: 7000}, {1: ['stat']}, {})
            self.assertIn('unsupported_tuner_synchro_category', report['issues'][0]['blockers'])
            self.assertEqual(report['summary']['fully_reviewed'], 0)

    def test_duplicate_names_and_passcodes(self):
        for old in (self.card(cid=None, tf1_id=9, passcode=999),
                    self.card(cid=None, tf1_id=9, name='Other name')):
            report = build([old, self.card()], {1: {'w1': 26}}, {1: 7000}, {}, {})
            self.assertEqual(report['queue'][0]['state'], 'blocked')
            self.assertIn('existing_tf1_card', report['issues'][0]['blockers'])

    def test_explicit_exclusion_and_normal(self):
        card = self.card(kind='monster', category=0)
        self.assertEqual(build([card], {1: {'w1': 0}}, {1: 7000}, {}, {})['queue'][0]['state'], 'normal_metadata_review')
        self.assertEqual(build([card], {1: {'w1': 0}}, {1: 7000}, {}, {}, [1])['queue'][0]['state'], 'blocked')

    def test_extensions_are_not_existing_support(self):
        for text, expected in (
            ('Activate only if you control a "Bamboo Sword" Equip Spell Card. Draw 2 cards.', 'conditional_draw_extension'),
            ('If this card attacks or is attacked by a LIGHT monster, it gains 200 ATK during the Damage Step only.', 'battle_attribute_bonus_extension'),
            ('FLIP: This card gains 300 ATK and DEF.', 'flip_stat_memory_extension')):
            self.assertEqual(family(self.card(text_tf3=text))[0], expected)

    def test_divine_beast_conversion_is_intentional(self):
        card = self.card(kind='monster', category=1)
        report = build([card], {1: {'w1': (11 << 1) | (22 << 17)}}, {1: 7000}, {}, {})
        self.assertEqual(report['issues'], [])
        report = build([card], {1: {'w1': 2 << 1}}, {1: 7000}, {}, {})
        self.assertEqual(report['issues'][0]['blockers'], ['unexpected_category_conversion'])


if __name__ == '__main__':
    unittest.main()
