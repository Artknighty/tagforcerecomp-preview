import os
import unittest

PACK = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), 'mods', 'TF3Cards')

class TestTF3AccuracyBatch2(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        ported_path = os.path.join(PACK, 'ported.txt')
        cls.ported = {}
        with open(ported_path, 'r', encoding='utf-8') as f:
            for line in f:
                parts = line.strip().split()
                if len(parts) >= 3:
                    gid = int(parts[0])
                    tf3 = int(parts[1])
                    like = int(parts[2])
                    cls.ported[tf3] = (gid, like)

    def test_sasuke_samurai_family(self):
        # Anti-defense destroyers without flipping: donor 5521 (Sasuke Samurai)
        self.assertIn(6968, self.ported)
        self.assertEqual(self.ported[6968][1], 5521) # The Six Samurai - Irou
        self.assertIn(7212, self.ported)
        self.assertEqual(self.ported[7212][1], 5521) # Razor Lizard
        self.assertIn(7883, self.ported)
        self.assertEqual(self.ported[7883][1], 5521) # Search Striker

    def test_six_samurai_spell_trap_destroyers(self):
        # Yaichi & Kamon: donor 6600 (Anteatereatingant - ignition destroy Spell/Trap, cannot attack)
        self.assertIn(6966, self.ported)
        self.assertEqual(self.ported[6966][1], 6600) # The Six Samurai - Yaichi
        self.assertIn(6967, self.ported)
        self.assertEqual(self.ported[6967][1], 6600) # The Six Samurai - Kamon

    def test_coin_toss_and_tribute_removal(self):
        # Twin-Barrel Dragon: donor 6045 (Blowback Dragon)
        self.assertIn(7675, self.ported)
        self.assertEqual(self.ported[7675][1], 6045)
        # Offering to the Snake Deity: donor 6709 (Icarus Attack)
        self.assertIn(7476, self.ported)
        self.assertEqual(self.ported[7476][1], 6709)
        # Tiger Dragon & Celestia: donor 6114 (Mobius the Frost Monarch)
        self.assertIn(7896, self.ported)
        self.assertEqual(self.ported[7896][1], 6114)
        self.assertIn(7597, self.ported)
        self.assertEqual(self.ported[7597][1], 6114)
        # Mei-Kou & Golden Flying Fish: donor 5378 (Exiled Force)
        self.assertIn(7116, self.ported)
        self.assertEqual(self.ported[7116][1], 5378)
        self.assertIn(7566, self.ported)
        self.assertEqual(self.ported[7566][1], 5378)

    def test_dark_world_and_chaos_envoys(self):
        # Kahkki & Gren: donor 6505 (Goldd, Wu-Lord of Dark World - discard trigger)
        self.assertIn(6974, self.ported)
        self.assertEqual(self.ported[6974][1], 6505)
        self.assertIn(6975, self.ported)
        self.assertEqual(self.ported[6975][1], 6505)
        # Sky Scourge Enrise & Norleras: donor 5835 (BLS) and 5860 (CED)
        self.assertIn(7112, self.ported)
        self.assertEqual(self.ported[7112][1], 5835)
        self.assertIn(7113, self.ported)
        self.assertEqual(self.ported[7113][1], 5860)

    def test_spells_traps_and_stats(self):
        # Tornado -> 4876 (Dust Tornado)
        self.assertEqual(self.ported[7126][1], 4876)
        # Eliminating the League -> 5464 (Raigeki Break)
        self.assertEqual(self.ported[7008][1], 5464)
        # Cybernetic Hidden Technology -> 5776 (Sakuretsu Armor)
        self.assertEqual(self.ported[7468][1], 5776)
        # Rain Storm -> 4242 (MST)
        self.assertEqual(self.ported[7273][1], 4242)
        # Evolution Burst -> 4856 (Tribute to the Doomed)
        self.assertEqual(self.ported[7556][1], 4856)
        # Diskblade Rider, Cu Chulainn, Prometheus -> 5250 (Bazoo)
        self.assertEqual(self.ported[7608][1], 5250)
        self.assertEqual(self.ported[6992][1], 5250)
        self.assertEqual(self.ported[7148][1], 5250)
        # Dark Red Enchanter, Metal Shooter -> 5655 (Breaker)
        self.assertEqual(self.ported[6555][1], 5655)
        self.assertEqual(self.ported[7018][1], 5655)
        # Desert Twister -> 5810 (Chiron), Frost and Flame Dragon -> 6893 (Snipe Hunter)
        self.assertEqual(self.ported[7211][1], 5810)
        self.assertEqual(self.ported[7210][1], 6893)
        # Lady Heat -> 5974 (Solar Flare Dragon)
        self.assertEqual(self.ported[7024][1], 5974)
        # Morphtronic Radion -> 5379 (Command Knight)
        self.assertEqual(self.ported[7879][1], 5379)
        # Goe Goe the Gallant Ninja -> 4072 (White Magical Hat)
        self.assertEqual(self.ported[7115][1], 4072)

if __name__ == '__main__':
    unittest.main()
