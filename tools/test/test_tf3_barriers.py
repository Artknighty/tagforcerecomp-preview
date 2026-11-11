"""The six-card family shares one bounded Attribute field and old flags."""
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'mods'))
import tf3_efeitos_param as p

class BarrierEncoding(unittest.TestCase):
    def test_one_schema_for_all_six(self):
        attributes=('light','dark','water','fire','earth','wind')
        spec={str(6882+i):{'special_summon_except':attribute} for i,attribute in enumerate(attributes)}
        gids={6882+i:7000+i for i in range(6)}
        expected=b'TF3S'+struct.pack('<I',6)+b''.join(struct.pack('<HH',7000+i,(i+1)<<8) for i in range(6))
        self.assertEqual(p.resolver_invocacoes(gids,spec),expected)

    def test_vanity_flags_unchanged(self):
        spec={'6881':{'special_summon_lock':'both','cannot_special_summon':True},
              '6888':{'special_summon_lock':'opponent','cannot_special_summon':True}}
        self.assertEqual(p.resolver_invocacoes({6881:7001,6888:7002},spec),b'TF3S'+struct.pack('<IHHHH',2,7001,5,7002,6))

    def test_rejects_ambiguous_and_unsupported_rules(self):
        for rule in ({'special_summon_except':'divine'}, {'special_summon_except':'invalid'},
                     {'special_summon_except':'light','special_summon_lock':'both'},
                     {'special_summon_except':'dark','cannot_special_summon':True}):
            with self.assertRaises(ValueError):p.resolver_invocacoes({6882:7000},{'6882':rule})

if __name__=='__main__':unittest.main()
