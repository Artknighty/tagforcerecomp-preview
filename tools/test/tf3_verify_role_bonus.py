"""Steamroid-family signed modifier math and native opponent battles."""
import argparse
import json
import os
from pathlib import Path
import struct
import tf
from tf3_probe_client import Probe, field
from tf3_verify_hand_search import read_state, SIDE
from tf3_verify_venus_natural import place_from_hand


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--case',choices=('matrix','defend','attack','direct','negated','native-defend','native-attack'),required=True)
    args=parser.parse_args(); attacking=args.case.endswith('attack') or args.case=='direct'; native=args.case.startswith('native')
    pack=Path(os.environ['TF_MODS_FROM'])
    if args.case!='matrix' and not (pack/'CPUvsCPU/plugin.dll').is_file():parser.error('CPUvsCPU test DLL required for native deck override')
    source=6386 if native else 7433
    os.environ.update(TF3_TEST_PROBE='1',TF3_TEST_ACTIONS='1',TF3_FORCE_CARDS=str(source),TF3_FORCE_COPIES='3',
                      TF_PRIMEIRO='1' if attacking else '0',TF_CPUCPU='0',TF_DECK_J1='0')
    gids={int(r[1]):int(r[0]) for line in (pack/'TF3Cards/ported.txt').read_text(encoding='utf-8').splitlines() if len(r:=line.split(' ',3))==4}
    gid=6386 if native else gids[7433]; results=[]
    try:
        tf.duelo(['--lento']);tf.run('w1600' if attacking else 'w300');tf.send('nodraw 1');tf.RAPIDO=True
        probe=Probe()
        if args.case=='matrix':
            configs=[(side,slot,negated,None,False) for side in (0,1) for slot in range(5) for negated in (False,True)]
            configs += [(side,0,False,equip,reverse) for side in (0,1) for equip in (None,4310,4663) for reverse in (False,True)]
            for side,slot,negated,equip,reverse in configs:
                for current,lp in ((side,1000),(1-side,2000)):tf.send(f'poke {SIDE+current*0xfac:08x} {struct.pack("<I",lp).hex()}')
                for attack in (False,True):
                    def sample(cid):
                        fixtures=[field(side,slot,cid,flags=0xa0 if negated else 0x80)]
                        if equip:fixtures.append(field(side,5,equip,slot+1))
                        plain,_=probe.battle_bonus_stats(fixtures,side,slot,reverse=reverse,active=False,role=attack)
                        changed,restored=probe.battle_bonus_stats(fixtures,side,slot,reverse=reverse,role=attack)
                        return tuple(changed[i]-plain[i] for i in (0,1)),restored
                    reference,_=sample(6386); delta,restored=sample(gid); expected=tuple(v*4//5 for v in reference)
                    results.append(dict(side=side,slot=slot,negated=negated,equip=equip,reverse=reverse,attack=attack,
                                        actual=delta,expected=expected,restored=restored,passed=delta==expected and restored==(6386,int(attack))))
        else:
            hand=read_state()['hand']; index=hand.index(gid)
            tf.run('DOWN w40 DOWN w40 '+'LEFT w30 '*(len(hand)-1-index)+'X w60 '+('' if attacking else 'DOWN w30 ')+'X w200 shot:placed')
            board=bytes.fromhex(tf.send(f'peek {SIDE+0x30:08x} 100').split()[-1])
            slot=next(k for k in range(5) if struct.unpack_from('<I',board,20*k)[0]&0x1fff==gid)
            if args.case=='negated':place_from_hand(0,0,5,5740)
            before=probe.current_stats(0,slot)
            enemy_lp_before=int.from_bytes(bytes.fromhex(tf.send(f'peek {SIDE+0xfac:08x} 4').split()[-1]),'little')
            if args.case=='direct':
                enemy=bytes.fromhex(tf.send(f'peek {SIDE+0xfac+0x30:08x} 100').split()[-1])
                for k in range(5):
                    if struct.unpack_from('<I',enemy,20*k)[0]&0x1fff:assert probe.remove_current(1,k,destroyed=False)==1
                tf.run('w120')
            if attacking:
                tf.run('O w60 RIGHT w20 X w100 X w100 X w100 X w700 shot:attack_prompt')
            else:tf.run('O w60 RIGHT w20 RIGHT w20 RIGHT w20 X w1600 shot:attack_prompt')
            tf.run('RIGHT w20 X w300 '*8+'shot:battled')
            words=list(struct.unpack('<28I',bytes.fromhex(tf.send('peek 09f77dcc 112').split()[-1])))
            index=next((k for k in (0,14) if words[k+4]==gid),None)
            alive=int.from_bytes(bytes.fromhex(tf.send(f'peek {SIDE+0x30+20*slot:08x} 4').split()[-1]),'little')&0x1fff==gid
            after=probe.current_stats(0,slot) if alive else None
            bonus=0 if args.case in ('negated','direct') else (500 if native else 400)*(1 if attacking else -1)
            direct=int.from_bytes(bytes.fromhex(tf.send('peek 09f77da8 4').split()[-1]),'little')
            enemy_lp_after=int.from_bytes(bytes.fromhex(tf.send(f'peek {SIDE+0xfac:08x} 4').split()[-1]),'little')
            direct_evidence=index is not None and words[(14 if index==0 else 0)+4]==0 and enemy_lp_before-enemy_lp_after==before[0]
            results.append(dict(case=args.case,before=before,after=after,battle=words,alive=alive,
                direct_marker=direct,enemy_lp_before=enemy_lp_before,enemy_lp_after=enemy_lp_after,
                passed=index is not None and words[index+7]==before[0]+bonus and (not alive or after==before) and (args.case!='direct' or direct_evidence)))
        output=dict(passed=all(r['passed'] for r in results),checks=len(results),results=results)
        Path(tf.CTL,'role_bonus_results.json').write_text(json.dumps(output,indent=2))
        print(json.dumps(output),flush=True)
        return 0 if output['passed'] else 1
    finally:tf.stop()


if __name__=='__main__':raise SystemExit(main())
