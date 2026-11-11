"""Bound the two-clause schema and require exact repeated target names."""
from pathlib import Path
import re
import struct
import sys
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'mods'))
import tf3_efeitos_param as p

class FieldSearchEncoding(unittest.TestCase):
    def test_complete_text(self):
        from tf3_efeitos_texto import PADROES
        pattern=next(rx for kind,rx,donor,zone in PADROES if zone=='deck_field_required')
        text='You can discard this card to the Graveyard to add 1 "Skyscraper" from your Deck to your hand. If this card is on the field but "Skyscraper" is not on the field, destroy this card.'
        self.assertIsNotNone(re.fullmatch(pattern,text))
        for wrong in (text+' Draw 1 card.',text.replace('but "Skyscraper"','but "Umi"'),text.split(' If ')[0]):
            self.assertIsNone(re.fullmatch(pattern,wrong))

    def test_all_three_rules(self):
        root=Path(__file__).resolve().parents[2]
        gids={int(r[1]):int(r[0]) for line in (root/'mods/TF3Cards/ported.txt').read_text(encoding='utf8').splitlines() if len(r:=line.split(' ',3))==4}
        specs=p._carregar(p.SPEC_AUTO,p.SPEC)
        data,likes=p.resolver(gids,{str(cid):specs[str(cid)] for cid in (7105,7109,7110)})
        self.assertEqual(set(likes.values()),{6044})
        self.assertEqual(len(data),38)
        self.assertEqual([struct.unpack_from('<HBBIH',data,off)[1:] for off in (8,18,28)],[(6,0,1,6399),(6,0,1,5982),(6,0,1,5791)])

    def test_rejects_incomplete_or_different_mechanics(self):
        base=dict(like=6044,zone='deck_field_required',filter={'name_is':'target'})
        for extra,prop in [({'like':6764},{'category':13,'icon':2}),({'count':2},{'category':13,'icon':2}),({}, {'category':13,'icon':4}),({}, {'category':1,'icon':2})]:
            with patch.object(p,'propriedades',return_value={5000:dict(name='target',**prop)}),self.assertRaises(ValueError):
                p.resolver({7105:7281},{'7105':dict(base,**extra)})

if __name__=='__main__':unittest.main()
