"""One shared Attribute-restriction matrix and native play routes for six Statues."""
import argparse
import json
import os
from pathlib import Path
import struct
import tf
from tf3_probe_client import Probe,field
from tf3_verify_hand_search import read_state,SIDE,P
from tf3_verify_venus_natural import place_from_hand

CARDS={6882:2,6883:3,6884:4,6885:6,6886:5,6887:1}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--case',choices=('matrix','play','inherent','gates','cpu'),required=True)
    parser.add_argument('--card',type=int,choices=(*CARDS,6881,6888),default=6882)
    args=parser.parse_args();cid=args.card
    os.environ.update(TF3_TEST_PROBE='1',TF3_TEST_ACTIONS='1',TF3_FORCE_CARDS=f'{cid},6390' if args.case=='inherent' else str(cid),
                      TF3_FORCE_COPIES='3',TF_PRIMEIRO='0',TF_CPUCPU='0')
    if args.case=='cpu':os.environ.update(TF3_FORCE_CARDS=f'{cid},6390',TF3_FORCE_OPP='1',TF_DECK_J1='0')
    pack=Path(os.environ['TF_MODS_FROM'])/'TF3Cards'
    gids={int(r[1]):int(r[0]) for line in (pack/'ported.txt').read_text(encoding='utf-8').splitlines() if len(r:=line.split(' ',3))==4}
    props=P.propriedades(gids)
    targets={a:next(c for c,p in props.items() if c<6958 and p['category']==0 and p['attribute']==a and p['level']<=4) for a in range(1,7)}
    results=[]
    try:
        tf.duelo(['--lento']);tf.run('w300');tf.send('nodraw 1');tf.RAPIDO=True;probe=Probe()
        if args.case=='cpu':
            count=int.from_bytes(bytes.fromhex(tf.send(f'peek {SIDE+0xfac+12:08x} 4').split()[-1]),'little')
            hand=bytes.fromhex(tf.send(f'peek {SIDE+0xfac+0x120:08x} {4*count}').split()[-1])
            forced=sum(struct.unpack_from('<I',hand,4*k)[0]&0x1fff==6390 for k in range(count))
            assert forced>0
            # Include both card kinds in the forced opposing Deck so native
            # duel resources are loaded, then perform a real own Normal Summon.
            word=int.from_bytes(bytes.fromhex(tf.send(f'peek {SIDE+0x120:08x} 4').split()[-1]),'little')
            token=(((word>>16)&0x3fff)>>6)*2+((word>>13)&1)
            tf.send(f'poke {SIDE+0x120:08x} {struct.pack("<I",(word&~0x1fff)|gids[cid]).hex()}')
            instance=int.from_bytes(bytes.fromhex(tf.send(f'peek {0x09f7bc58+4*token:08x} 4').split()[-1]),'little')
            tf.send(f'poke {0x09f7bc58+4*token:08x} {struct.pack("<I",(instance&~0x1fff)|gids[cid]).hex()}')
            own=read_state()['hand']
            tf.run('DOWN w40 DOWN w40 '+'LEFT w30 '*(len(own)-1)+'X w60 X w240 shot:normal_summon')
            tf.send(f'poke {SIDE:08x} {struct.pack("<I",8000).hex()}')
            tf.run('shot:fixture O w60 RIGHT w20 RIGHT w20 RIGHT w20 X w1600 shot:cpu_prompt')
            tf.run('RIGHT w20 X w300 '*8+'shot:cpu_finished')
            board=bytes.fromhex(tf.send(f'peek {SIDE+0xfac+0x30:08x} 100').split()[-1])
            present=any(struct.unpack_from('<I',board,20*k)[0]&0x1fff==6390 for k in range(5))
            results.append(dict(card=cid,case='native CPU Cyber Dragon summon',forced=forced,present=present,
                                expected=CARDS[cid]==1,passed=present==(CARDS[cid]==1)))
        elif args.case=='gates':
            for attribute,target in targets.items():
                fixtures=[field(0,0,gids[cid])]
                absent=probe.toon_world_present(1,fixtures);present=probe.toon_world_present(1,fixtures+[field(1,5,4798)])
                results.append(dict(attribute=attribute,absent=absent,present=present,passed=absent==0 and present==1))
        elif args.case=='matrix':
            baselines={}
            def check(label,side,target,fixtures,blocked,**info):
                key=(side,target)
                if key not in baselines:baselines[key]=probe.special_summon_paths(side,target)
                baseline=baselines[key]
                got=probe.special_summon_paths(side,target,fixtures)
                expected=(0,0,0,0) if blocked else baseline
                results.append(dict(case=label,side=side,target=target,actual=got,expected=expected,passed=got==expected,**info))
                if len(results)%100==0:print(f'Completed {len(results)} shared checks',flush=True)
            for card,allowed in CARDS.items():
                for source in (0,1):
                    for side in (0,1):
                        for attribute,target in targets.items():
                            check('all Attributes',side,target,[field(source,0,gids[card])],attribute!=allowed,card=card,source=source)
                    for slot in range(5):
                        for state,enabled,flags in [('active',1,0x80),('negated',0,0xa0),('set',0,0x100002),('absent',0,0)]:
                            fixtures=[] if state=='absent' else [field(source,slot,gids[card],enabled=enabled,flags=flags)]
                            for attribute in (allowed,allowed%6+1):
                                check('all slots/states',1-source,targets[attribute],fixtures,state=='active' and attribute!=allowed,card=card,source=source,slot=slot,state=state)
                for attribute,target in targets.items():
                    fixtures=[field(0,0,gids[card])]
                    absent=probe.toon_world_present(1,fixtures)
                    present=probe.toon_world_present(1,fixtures+[field(1,5,4798)])
                    results.append(dict(case='native Toon World predicate unchanged',card=card,attribute=attribute,absent=absent,present=present,passed=absent==0 and present==1))
                # Compare native legality, preserving proper-Summon restrictions.
                for category in (3,5):
                    for attribute in range(1,7):
                        choices=[c for c,p in props.items() if c<6958 and p['category']==category and p['attribute']==attribute]
                        if choices:check('Fusion/Ritual candidate',0,choices[0],[field(1,0,gids[card])],attribute!=allowed,card=card,category=category)
            cards=list(CARDS)
            for i,first in enumerate(cards):
                for second in cards[i+1:]:
                    for side in (0,1):
                        for target in targets.values():check('conflicting Statues',side,target,[field(0,0,gids[first]),field(1,4,gids[second])],True)
            for side in (0,1):
                check('same Attribute stacks',side,targets[1],[field(0,0,gids[6887]),field(1,4,gids[6887])],False)
                check('one source negated',side,targets[1],[field(0,0,gids[6882],enabled=0),field(1,4,gids[6887])],False)
                check('Vanity overrides permitted Attribute',side,targets[1],[field(0,0,gids[6887]),field(1,4,gids[6881])],True)
        elif args.case=='play':
            hand=read_state()['hand'];index=hand.index(gids[cid])
            tf.run('DOWN w40 DOWN w40 '+'LEFT w30 '*(len(hand)-1-index)+'X w60 X w240 shot:summoned')
            board=bytes.fromhex(tf.send(f'peek {SIDE+0x30:08x} 100').split()[-1])
            slot=next(k for k in range(5) if struct.unpack_from('<I',board,20*k)[0]&0x1fff==gids[cid])
            before=[probe.current_special_summon_legal(side,target) for side in (0,1) for target in targets.values()]
            expected=[(int(attribute==CARDS[cid]),)*3 for side in (0,1) for attribute in targets]
            removed=probe.remove_current(0,slot,destroyed=True);tf.run('w120')
            after=[probe.current_special_summon_legal(side,target) for side in (0,1) for target in targets.values()]
            results.append(dict(card=cid,before=before,expected=expected,after=after,removed=removed,
                                passed=before==expected and removed==1 and all(row==(1,1,1) for row in after)))
        else:
            # Only the opponent field is controlled; the native hand menu and
            # native Cyber Dragon summon own the candidate and resolution.
            place_from_hand(1,0,2,gids[cid])
            hand=read_state()['hand'];index=hand.index(6390)
            tf.run('DOWN w40 DOWN w40 '+'LEFT w30 '*(len(hand)-1-index)+'X w60 shot:menu X w240 X w160 X w160 shot:resolved')
            board=bytes.fromhex(tf.send(f'peek {SIDE+0x30:08x} 100').split()[-1])
            present=any(struct.unpack_from('<I',board,20*k)[0]&0x1fff==6390 for k in range(5))
            results.append(dict(card=cid,case='native Cyber Dragon hand summon',present=present,expected=CARDS.get(cid)==1,passed=present==(CARDS.get(cid)==1)))
        output=dict(passed=all(r['passed'] for r in results),checks=len(results),results=results)
        Path(tf.CTL,'barrier_results.json').write_text(json.dumps(output,indent=2))
        print(json.dumps({k:v for k,v in output.items() if k!='results'}),flush=True)
        for r in results:
            if not r['passed']:print(json.dumps(r),flush=True)
        return 0 if output['passed'] else 1
    finally:tf.stop()

if __name__=='__main__':raise SystemExit(main())
