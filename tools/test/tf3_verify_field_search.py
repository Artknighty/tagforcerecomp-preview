"""Shared continuous survival matrix and real Normal Summon routes for three searches."""
import argparse
import json
import os
from pathlib import Path
import struct
import tf
from tf3_probe_client import Probe, field
from tf3_verify_hand_search import read_state, SIDE, P, TARGETS
from tf3_verify_chrysalis import read, write, board
from tf3_verify_venus_natural import place_from_hand

CARDS=(7105,7109,7110)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--card',type=int,choices=CARDS,default=7105)
    parser.add_argument('--case',choices=('matrix','absent','present','lost','negated','set'),required=True)
    args=parser.parse_args()
    rows=[l.split(' ',3) for l in (Path(os.environ['TF_MODS_FROM'])/'TF3Cards/ported.txt').read_text(encoding='utf8').splitlines()]
    gids={int(r[1]):int(r[0]) for r in rows};props=P.propriedades(gids)
    targets={cid:next(i for i,p in props.items() if p['name']==TARGETS[cid]) for cid in CARDS}
    source=gids[args.card];target=targets[args.card]
    os.environ.update(TF3_TEST_PROBE='1',TF3_FORCE_CARDS=f'{args.card},{target}',TF3_FORCE_COPIES='3',TF_PRIMEIRO='0',TF_CPUCPU='0',TF_DECK_J1='1')
    results=[]
    try:
        tf.duelo(['--lento']);tf.run('w300');tf.send('nodraw 1');tf.RAPIDO=True
        probe=Probe()
        if args.case=='matrix':
            for cid in CARDS:
                for side in (0,1):
                    for slot in range(5):
                        for state in ('active','set','negated'):
                            source_fixture=field(side,slot,gids[cid],enabled=1,flags=0x80 if state=='active' else 0x100002 if state=='set' else 0xa0)
                            for presence in ('absent','own','opposing','set','negated','wrong'):
                                fixtures=[source_fixture]
                                if presence!='absent':
                                    fixtures.append(field(1-side if presence=='opposing' else side,10,
                                        6775 if presence=='wrong' else targets[cid],enabled=presence!='set',
                                        flags=0x100002 if presence=='set' else 0xa0 if presence=='negated' else 0x80))
                                got=probe.required_field_scan(fixtures)
                                hit=state=='active' and presence in ('absent','set','wrong')
                                expected=(int(hit),(1<<(side*16+slot)) if hit else 0)
                                results.append(dict(card=cid,side=side,slot=slot,state=state,presence=presence,got=got,expected=expected,passed=got==expected))
                print(f'Completed {cid}: {len(results)} checks',flush=True)
            # Original Kozaky scan and unrelated imported search stay isolated.
            for fixtures,expected,label in [([field(0,0,6420)],(1,1),'native-absent'),
                ([field(0,0,6420),field(1,1,6020)],(0,0),'native-present'),
                ([field(0,0,gids[7106])],(0,0),'old-search'),
                ([field(0,0,gids[7105]),field(1,1,gids[7109]),field(0,10,targets[7105])],(1,1<<17),'mixed-targets')]:
                got=probe.required_field_scan(fixtures)
                results.append(dict(case=label,got=got,expected=expected,passed=got==expected))
        else:
            if args.case in ('present','lost'):
                hand=read_state()['hand'];place_from_hand(0,hand.index(target),10,target)
            if args.case=='negated':place_from_hand(1,0,5,5740)
            before=read_state();index=before['hand'].index(source)
            legal=probe.hand_search_legal(0,index,source)
            tf.run('DOWN w40 DOWN w40 '+'LEFT w30 '*(len(before['hand'])-1-index)+'X w60 shot:menu')
            # The hand search is the first command when legal; Summon and Set follow it.
            tf.run('DOWN w30 '* (int(bool(legal))+int(args.case=='set'))+'X w500 shot:summoned')
            # Decline optional responses to complete native stabilization.
            tf.run('RIGHT w20 X w300 shot:stabilized')
            summoned=read_state();summoned_board=board()
            if args.case=='lost':
                assert summoned_board.count(source)==1,(summoned_board,summoned)
                write(SIDE+0x30+20*10,bytes(20))
                # The post-summon route opens the next hand Field Spell menu.
                # Set that Spell through the native UI to stabilize the board;
                # its matching name must not count while face-down.
                tf.run('DOWN w30 X w600 shot:field_lost')
            after=read_state();after_board=board()
            should_die=args.case in ('absent','lost')
            passed=(after_board.count(source)==int(not should_die) and
                after['grave'].count(source)==before['grave'].count(source)+int(should_die) and
                after['hand'].count(source)==before['hand'].count(source)-1 and
                len(after['deck'])==len(before['deck']))
            field_id=struct.unpack('<I',read(SIDE+0x30+20*10,4))[0]&0x1fff
            field_flags=struct.unpack('<I',read(SIDE+0x30+20*10+16,4))[0]
            if args.case=='lost':passed &= field_id==target and bool(field_flags&0x100000)
            results.append(dict(field_id=field_id,field_flags=field_flags,card=args.card,case=args.case,source=source,target=target,legal=legal,before=before,
                summoned=summoned,summoned_board=summoned_board,after=after,after_board=after_board,passed=passed))
        result=dict(passed=all(r['passed'] for r in results),checks=len(results),results=results)
        Path(tf.CTL,'field_search_results.json').write_text(json.dumps(result,indent=2))
        print(json.dumps(result),flush=True)
        return 0 if result['passed'] else 1
    finally:tf.stop()

if __name__=='__main__':raise SystemExit(main())
