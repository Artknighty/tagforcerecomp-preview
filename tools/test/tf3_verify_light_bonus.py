"""Shared native Damage Step amount matrix and actual LIGHT battles."""
import argparse
import json
import os
from pathlib import Path
import struct
import tf
from tf3_probe_client import Probe, field
from tf3_verify_hand_search import read_state, SIDE, P
from tf3_verify_venus_natural import place_from_hand

CARDS = {7541:200, 7542:700}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--case', choices=('matrix','light','wrong','negated','native'), required=True)
    parser.add_argument('--card',type=int,choices=CARDS,default=7541)
    args=parser.parse_args()
    if args.case!='matrix' and not (Path(os.environ['TF_MODS_FROM'])/'CPUvsCPU/plugin.dll').is_file():
        parser.error('Native opponent deck cases require the CPUvsCPU test DLL (TF_CPUCPU stays disabled)')
    source=6125 if args.case=='native' else args.card
    os.environ.update(TF3_TEST_PROBE='1',TF3_TEST_ACTIONS='1',TF3_FORCE_CARDS=str(source),
                      TF3_FORCE_COPIES='3',TF_PRIMEIRO='0',TF_CPUCPU='0',TF_DECK_J1='0' if args.case=='wrong' else '1')
    rows=[l.split(' ',3) for l in (Path(os.environ['TF_MODS_FROM'])/'TF3Cards/ported.txt').read_text(encoding='utf-8').splitlines()]
    gids={int(r[1]):int(r[0]) for r in rows}; props=P.propriedades(gids)
    results=[]
    try:
        tf.duelo(['--lento']);tf.run('w300');tf.send('nodraw 1');tf.RAPIDO=True
        probe=Probe()
        if args.case=='matrix':
            configs=[(side,slot,negated,None,False) for side in (0,1) for slot in range(5) for negated in (False,True)]
            configs += [(side,0,False,equip,reverse) for side in (0,1) for equip in (None,4310,4663) for reverse in (False,True)]
            for side,slot,negated,equip,reverse in configs:
                for current,lp in ((side,1000),(1-side,2000)):
                    tf.send(f'poke {SIDE+current*0xfac:08x} {struct.pack("<I",lp).hex()}')
                def sample(cid):
                    fixtures=[field(side,slot,cid,flags=0xa0 if negated else 0x80)]
                    if equip:fixtures.append(field(side,5,equip,slot+1))
                    # Reverse Trap is a controlled resolved-state marker in
                    # the probe's global zone, not a Trap activation claim.
                    plain,_=probe.battle_bonus_stats(fixtures,side,slot,reverse=reverse,active=False)
                    boosted,restored=probe.battle_bonus_stats(fixtures,side,slot,reverse=reverse)
                    return tuple(boosted[i]-plain[i] for i in (0,1)),restored,plain,boosted
                native=sample(6125)
                for cid,bonus in CARDS.items():
                    delta,restored,plain,boosted=sample(gids[cid])
                    expected=tuple(v*bonus//1000 for v in native[0])
                    result=dict(card=cid,side=side,slot=slot,negated=negated,equip=equip,reverse=reverse,
                                native_delta=native[0],actual=delta,expected=expected,restored=restored,
                                passed=delta==expected and restored==(6125,0))
                    results.append(result);print(json.dumps(result),flush=True)
        else:
            gid=source if source==6125 else gids[source]
            tribute=props[gid]['level']>4
            if tribute:
                hand=read_state()['hand']
                place_from_hand(0,next(i for i,cid in enumerate(hand) if cid!=gid),0,4007)
            hand=read_state()['hand'];index=hand.index(gid)
            tf.run('DOWN w40 DOWN w40 '+'LEFT w30 '*(len(hand)-1-index+int(tribute))+
                   'X w60 DOWN w30 X w200 shot:set_menu')
            if tribute:tf.run('X w160 '*3+'shot:tribute_set O w60')
            raw=bytes.fromhex(tf.send(f'peek {SIDE+0x30:08x} 100').split()[-1])
            slot=next(k for k in range(5) if struct.unpack_from('<I',raw,20*k)[0]&0x1fff==gid)
            if tribute:
                # Reveal the Defense fixture so native AI can evaluate its DEF.
                # This does not certify a full natural Tribute Set route.
                entry=SIDE+0x30+20*slot
                tf.send(f'poke {entry+4:08x} {struct.pack("<HHI",0,1,1).hex()}')
                tf.send(f'poke {entry+16:08x} {struct.pack("<I",0x80).hex()}')
            if args.case=='negated':place_from_hand(0,0,5,5740)
            before=probe.current_stats(0,slot)
            tf.run('O w60 RIGHT w20 RIGHT w20 RIGHT w20 X w1600 shot:attack_prompt')
            tf.run('RIGHT w20 X w300 '*8+'shot:battled')
            words=list(struct.unpack('<28I',bytes.fromhex(tf.send('peek 09f77dcc 112').split()[-1])))
            index=next((k for k in (0,14) if words[k+4]==gid),None)
            raw=bytes.fromhex(tf.send(f'peek {SIDE+0x30+20*slot:08x} 4').split()[-1])
            alive=int.from_bytes(raw,'little')&0x1fff==gid
            after=probe.current_stats(0,slot) if alive else None
            bonus=0 if args.case in ('negated','wrong') else 1000 if source==6125 else CARDS[args.card]
            enemy=words[(14 if index==0 else 0)+4] if index is not None else 0
            correct_attribute=enemy in props and ((props[enemy]['attribute']!=1) if args.case=='wrong' else props[enemy]['attribute']==1)
            result=dict(card=source,case=args.case,before=before,after=after,battle=words,alive=alive,
                        field=[tf.send(f'peek {SIDE+s*0xfac+0x30:08x} 100').split()[-1] for s in (0,1)],
                        passed=index is not None and correct_attribute and words[index+7]==before[0]+bonus and (not alive or after==before))
            results.append(result);print(json.dumps(result),flush=True)
        output=dict(passed=all(r['passed'] for r in results),checks=len(results),results=results)
        Path(tf.CTL,'light_bonus_results.json').write_text(json.dumps(output,indent=2))
        return 0 if output['passed'] else 1
    finally:
        tf.stop()


if __name__=='__main__':raise SystemExit(main())
