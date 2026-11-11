import os
import struct
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PACK = os.path.join(ROOT, 'mods', 'TF3Cards')


class TestDarkBribeAndCounterTraps(unittest.TestCase):
    def test_dark_bribe_ported_configuration(self):
        """Verify Counter Traps have valid slots and Table 0 donors."""
        ported_path = os.path.join(PACK, 'ported.txt')
        self.assertTrue(os.path.exists(ported_path))
        with open(ported_path, 'r', encoding='utf-8') as f:
            lines = [l.strip().split(None, 3) for l in f if l.strip()]

        cards = {int(parts[1]): (int(parts[0]), int(parts[2]), parts[3]) for parts in lines}

        # Dark Bribe (TF3 7349)
        self.assertIn(7349, cards)
        slot, like, name = cards[7349]
        self.assertEqual(slot, 7512)
        self.assertEqual(like, 5263, "Dark Bribe must use Counter Trap donor 5263 (Riryoku Field)")
        self.assertEqual(name, 'Dark Bribe')

        # Counter Counter (TF3 7236)
        self.assertIn(7236, cards)
        self.assertEqual(cards[7236][1], 5263)

        # Snake Deity's Command (TF3 7168)
        self.assertIn(7168, cards)
        self.assertEqual(cards[7168][1], 4862)  # Magic Jammer

        # Disarm (TF3 7320)
        self.assertIn(7320, cards)
        self.assertEqual(cards[7320][1], 4862)  # Magic Jammer

        # Parry (TF3 7321)
        self.assertIn(7321, cards)
        self.assertEqual(cards[7321][1], 4863)  # Seven Tools

        # Gladiator Beast War Chariot (TF3 7763) -> Divine Wrath 6217
        self.assertIn(7763, cards)
        self.assertEqual(cards[7763][1], 6217)

        # Pulling the Rug (TF3 7016) -> Divine Wrath 6217
        self.assertIn(7016, cards)
        self.assertEqual(cards[7016][1], 6217)

        # Dark Illusion (TF3 7470), Destruction Jammer (TF3 7645), Hero's Rule 2 (TF3 7383)
        self.assertIn(7470, cards)
        self.assertEqual(cards[7470][1], 5263)
        self.assertIn(7645, cards)
        self.assertEqual(cards[7645][1], 5263)
        self.assertIn(7383, cards)
        self.assertEqual(cards[7383][1], 5263)

    def test_no_bogus_destroy_rules_for_counter_traps(self):
        """Counter Traps must NOT exist in destroy.bin (which causes prompt spamming for field targeting)."""
        destroy_path = os.path.join(PACK, 'destroy.bin')
        self.assertTrue(os.path.exists(destroy_path))
        with open(destroy_path, 'rb') as f:
            data = f.read()

        magic, count = struct.unpack_from('<4sI', data, 0)
        self.assertEqual(magic, b'TF3D')

        # Read all IDs in destroy.bin: each record is (cid: u16, b: 4 bytes, n: u16, ids: n*u16)
        destroy_ids = set()
        offset = 8
        for _ in range(count):
            cid, b0, b1, b2, b3, n = struct.unpack_from('<H4BH', data, offset)
            destroy_ids.add(cid)
            offset += 8 + n * 2

        # None of these Counter Traps should be in destroy.bin!
        counter_trap_slots = [7512, 7409, 7344, 7484, 7485, 7874, 7183, 7617, 7772, 7543]
        for slot in counter_trap_slots:
            self.assertNotIn(slot, destroy_ids, f"Slot {slot} must not be in destroy.bin")

    def test_cleaned_destroy_rules_and_revival_effects(self):
        """Verify revival cards are wired to grave_summon (zone 3) and bogus destroy rules are removed."""
        ported_path = os.path.join(PACK, 'ported.txt')
        with open(ported_path, 'r', encoding='utf-8') as f:
            lines = [l.strip().split(None, 3) for l in f if l.strip()]
        cards = {int(parts[1]): (int(parts[0]), int(parts[2]), parts[3]) for parts in lines}

        # 1. Revival cards must use Call of the Haunted donor 4989
        revival_ids = [7007, 7256, 7636, 7644, 7647, 7671]
        for cid in revival_ids:
            self.assertIn(cid, cards)
            self.assertEqual(cards[cid][1], 4989, f"Card {cid} ({cards[cid][2]}) must use donor 4989")

        # 2. Check effects.bin for zone 3 (grave_summon)
        fx_path = os.path.join(PACK, 'effects.bin')
        with open(fx_path, 'rb') as f:
            fx_data = f.read()
        magic, count = struct.unpack_from('<4sI', fx_data, 0)
        self.assertEqual(magic, b'TF3E')
        fx_map = {}
        offset = 8
        for _ in range(count):
            slot, zone_byte, extra, n = struct.unpack_from('<HBBI', fx_data, offset)
            offset += 8
            ids = struct.unpack_from(f'<{n}H', fx_data, offset)
            offset += n * 2
            fx_map[slot] = (zone_byte & 15, extra, ids)

        for cid in revival_ids:
            slot = cards[cid][0]
            self.assertIn(slot, fx_map, f"Card {cid} slot {slot} must be in effects.bin")
            zone, extra, ids = fx_map[slot]
            self.assertEqual(zone, 3, f"Card {cid} slot {slot} must be in zone 3 (grave_summon)")
            self.assertGreater(len(ids), 0, f"Card {cid} slot {slot} must have valid target candidate IDs")

        # 3. Cleaned cards must NOT be in destroy.bin
        destroy_path = os.path.join(PACK, 'destroy.bin')
        with open(destroy_path, 'rb') as f:
            d_data = f.read()
        magic, count = struct.unpack_from('<4sI', d_data, 0)
        self.assertEqual(magic, b'TF3D')
        destroy_ids = set()
        offset = 8
        for _ in range(count):
            slot, b0, b1, b2, b3, n = struct.unpack_from('<H4BH', d_data, offset)
            destroy_ids.add(slot)
            offset += 8 + n * 2

        cleaned_cids = [
            3781, 3788, 5696, 5926, 6743, 6790, 6841, 6844, 6870, 6871, 6872,
            6944, 6971, 6972, 6990, 7007, 7017, 7048, 7056, 7102, 7127,
            7149, 7150, 7179, 7256, 7308, 7317, 7438, 7451, 7465, 7471, 7519, 7528,
            7538, 7636, 7644, 7647, 7650, 7670, 7671, 7709, 7756, 7758, 7761, 7907
        ]
        for cid in cleaned_cids:
            if cid in cards:
                slot = cards[cid][0]
                self.assertNotIn(slot, destroy_ids, f"Card {cid} slot {slot} must NOT be in destroy.bin")

        # 4. Attack-response traps and continuous trap donor checks
        self.assertEqual(cards[7350][1], 4886, "Chaos Burst must use attack-trigger donor 4886")
        self.assertEqual(cards[7465][1], 4886, "Hate Buster must use attack-trigger donor 4886")
        self.assertEqual(cards[7758][1], 4886, "Synchro Deflector must use attack-trigger donor 4886")
        self.assertEqual(cards[7520][1], 4145, "By Order of the Emperor must use donor 4145")
        self.assertEqual(cards[5926][1], 4145, "Brain Crusher must use donor 4145")
        self.assertEqual(cards[7451][1], 4145, "Cybernetic Zone must use donor 4145")

    def test_native_donor_table0_record(self):
        """Donors 5263 and 6217 must exist in libduel_eng Table 0."""
        prx_path = os.path.join(ROOT, 'jogo', 'PSP_GAME', 'USRDIR', 'gmodule', 'libduel_eng.prx')
        if not os.path.exists(prx_path):
            self.skipTest("libduel_eng.prx not available")

        # Load image
        import sys
        sys.path.insert(0, os.path.join(ROOT, 'tools', 'translator'))
        from pspmod import load_module
        base = 0x09e6b800
        image = load_module(prx_path, base)[0]

        # Table 0: 0x09f6be44, 764 records
        table0_addr = 0x09f6be44
        table0_count = 764
        table0_map = {}
        for i in range(table0_count):
            rec = struct.unpack_from('<6I', image, table0_addr - base + i * 24)
            table0_map[rec[0]] = rec

        # Donor 5263 (Riryoku Field)
        self.assertIn(5263, table0_map)
        card_id, on_resolve, target_filter, timing_cond, cost_fn, extra = table0_map[5263]
        self.assertEqual(on_resolve, 0x09ee55c8, "Word 1 must be native negate & destroy")
        self.assertEqual(timing_cond, 0x09ed49e0, "Word 3 must be Counter Trap timing predicate")
        self.assertEqual(cost_fn, 0x0, "Word 4 must be 0 (no activation cost)")

        # Donor 6217 (Divine Wrath)
        self.assertIn(6217, table0_map)
        card_id, on_resolve, target_filter, timing_cond, cost_fn, extra = table0_map[6217]
        self.assertEqual(on_resolve, 0x09f07b94, "Word 1 must be native monster effect negate & destroy")
        self.assertEqual(timing_cond, 0x09edec94, "Word 3 must be Divine Wrath monster effect timing predicate")


if __name__ == '__main__':
    unittest.main()
