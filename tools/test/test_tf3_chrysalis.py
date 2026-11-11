"""Keep the named hand/Deck schema bounded to the shared Chrysalis record."""
from pathlib import Path
import struct
import sys
import unittest
import re
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'mods'))
import tf3_efeitos_param as p

class ChrysalisEncoding(unittest.TestCase):
    def test_complete_text_pattern(self):
        from tf3_efeitos_texto import PADROES, filtro
        pattern = next(rx for kind, rx, donor, zone in PADROES if zone == 'hand_deck_summon')
        text = 'You can Tribute this card while "Neo Space" is on the field to Special Summon 1 "Neo-Spacian Grand Mole" from your hand or Deck.'
        self.assertEqual(filtro(re.match(pattern,text).group('f')), {'name_is':'Neo-Spacian Grand Mole'})
        self.assertIsNone(re.match(pattern,text+' Then draw 1 card.'))
        self.assertIsNone(re.match(pattern,text.replace('"Neo Space"','"Umi"')))

    def test_all_five_targets(self):
        spec = p._carregar(p.SPEC_AUTO, p.SPEC)
        root = Path(__file__).resolve().parents[2]
        gids = {int(r[1]):int(r[0]) for line in (root/'mods/TF3Cards/ported.txt').read_text(encoding='utf-8').splitlines() if len(r:=line.split(' ',3))==4}
        selected = {str(cid):spec[str(cid)] for cid in (7155,7156,7157,7172,7173)}
        data, likes = p.resolver(gids, selected)
        self.assertEqual(len(selected),5)
        self.assertEqual(set(likes.values()),{6764})
        self.assertEqual(struct.unpack_from('<I',data,4)[0],5)
        self.assertEqual(len(data),58)
        targets=[]
        for offset in range(8,len(data),10):
            source,zone,extra,count,target=struct.unpack_from('<HBBIH',data,offset)
            self.assertEqual((zone,extra,count),(5,0,1));targets.append(target)
        self.assertEqual(len(set(targets)),5)

    def test_rejects_other_mechanics(self):
        base={'like':6764,'zone':'hand_deck_summon','filter':{'name_is':'target'}}
        for extra in ({'like':6044},{'count':2},{'then_draw':1},{'filter':{'monster':True}}):
            with self.assertRaises(ValueError), patch.object(p,'propriedades',return_value={}):
                p.resolver({7155:7300},{'7155':dict(base,**extra)})

if __name__=='__main__':unittest.main()
