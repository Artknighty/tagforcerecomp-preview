"""Check Sword of Dark Rites after controlled native tribute and destruction causes."""
import argparse
import json
import os
import struct
import tf
from tf3_probe_client import Probe


def state():
    raw = bytes.fromhex(tf.send('peek 09f79cec 2048').split()[-1])
    count = struct.unpack_from('<I', raw, 12)[0]
    return dict(hand=[struct.unpack_from('<I', raw, 0x120 + i * 4)[0] & 0x1fff for i in range(count)],
                field=[list(struct.unpack_from('<5I', raw, 0x30 + i * 20)) for i in range(11)],
                grave=[struct.unpack_from('<I', raw, 0x710 + i * 4)[0] & 0x1fff
                       for i in range(struct.unpack_from('<I', raw, 20)[0])])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--case', choices=['tribute', 'destroy', 'send', 'banish', 'natural', 'deck', 'hand', 'opposing', 'doubledeck'], default='tribute')
    parser.add_argument('--gladius', action='store_true')
    parser.add_argument('--bamboo', action='store_true')
    parser.add_argument('--native', action='store_true', help='Compare the native Butterfly Dagger - Elma donor')
    parser.add_argument('--native-pendant', action='store_true', help='Compare native mandatory Black Pendant')
    args = parser.parse_args()
    card, gid = (5724, 5724) if args.native else (7152, 7328)
    if args.native_pendant:
        card = gid = 4317
        args.native = True
    normal = 4016 if args.case == 'natural' else 4011
    force_monster = normal
    if args.gladius:
        card, gid, normal, force_monster = 7309, 7473, 7519, 7365
    if args.bamboo:
        card, gid = 7225, 7398
    os.environ.update(TF3_TEST_PROBE='1', TF3_TEST_ACTIONS='1', TF3_DEBUG_TRIG2='1', TF3_DEBUG_FX='1',
                      TF3_FORCE_COPIES='2' if args.case == 'natural' else '3',
                      TF3_FORCE_CARDS=f'{force_monster},{card}' + (',4028' if args.case == 'natural' else ''), TF_PRIMEIRO='0', TF_DECK_J1='1')
    try:
        tf.duelo([])
        tf.run('w300 O w60 DOWN w40 DOWN w40')
        tf.run(' '.join(['LEFT w30'] * (len(state()['hand']) - 1)) + ' X w60 X w200 shot:monster')
        monster = next(i for i, e in enumerate(state()['field'][:5]) if e[0] & 0x1fff == normal)
        base_stats = Probe().current_stats(0, monster) if args.bamboo else None
        tf.run('RIGHT w40 X w60 X w200 X w80 X w200 shot:equipped')
        before = state()
        if not any(e[0] & 0x1fff == gid for e in before['field']):
            raise RuntimeError('Sword was not equipped')
        if args.bamboo and Probe().current_stats(0, monster) != base_stats:
            raise RuntimeError('Broken Bamboo Sword changed the monster stats')
        if args.case == 'doubledeck':
            tf.run('DOWN w40 DOWN w40 X w60 X w200 X w80 X w200 shot:second_equip')
            before = state()
            if sum(e[0] & 0x1fff == gid for e in before['field']) != 2:
                raise RuntimeError('Two equips were not activated')
        target_side = 0
        if args.case == 'opposing':
            target_side = 1
            raw = bytes.fromhex(tf.send('peek %08x 20' % (0x09f79d1c + monster * 20)).split()[-1])
            tf.send('poke %08x %s' % (0x09f79d1c + 0xfac + monster * 20, raw.hex()))
            tf.send('poke %08x %s' % (0x09f79d1c + monster * 20, bytes(20).hex()))
        if args.case == 'natural':
            tf.run('O w60 RIGHT w20 RIGHT w20 RIGHT w20 X w1800 shot:turn_three')
            hand = state()['hand']
            index = hand.index(4028)
            tf.run('DOWN w40 DOWN w40 ' + 'RIGHT w30 ' * ((index + 1) % len(hand)) +
                   'X w60 shot:tribute_menu X w300 shot:tribute_choice X w200 X w500 shot:summoned')
            result = 1
        else:
            destination = 13 if args.case in ('deck', 'doubledeck', 'opposing') else 11 if args.case == 'hand' else 15 if args.case == 'banish' else 14
            result = Probe().remove_current(target_side, monster, destination=destination,
                        effect=0 if args.case == 'tribute' else 4909, actor_side=0,
                        destroyed=args.case == 'destroy', tributed=args.case == 'tribute')
        if args.case in ('tribute', 'natural', 'deck', 'doubledeck') and not args.native:
            tf.run('w300 shot:removed O w60 shot:phase X w1500 shot:pending')
        else:
            tf.run('w300 shot:removed O w60 shot:phase RIGHT w20 RIGHT w20 RIGHT w20 shot:phase_select X w1500 shot:pending')
        pending = state()
        tf.run('X w100 X w300 shot:resolved')
        after = state()
        expected = (2 if args.case == 'doubledeck' else 1) if args.gladius and args.case in ('deck', 'doubledeck') else int(not args.gladius and not args.native_pendant and (args.case in ('tribute', 'natural') or (args.native and args.case == 'destroy')))
        if args.bamboo:
            expected = 0
        output = dict(case=args.case, gladius=args.gladius, result=result, before=before, pending=pending, after=after,
                      passed=result == 1 and after['hand'].count(gid) == before['hand'].count(gid) + expected)
        if args.case == 'natural':
            output['passed'] &= any(e[0] & 0x1fff == 4028 for e in after['field'][:5]) and normal in after['grave']
        if args.bamboo:
            output['bamboo'] = True
            output['base_stats'] = base_stats
            output['passed'] &= gid in after['grave'] and not any(e[0] & 0x1fff == gid for e in after['field'])
        with open(os.path.join(tf.CTL, 'sword_results.json'), 'w', encoding='utf-8') as f:
            json.dump(output, f, indent=2)
        print(json.dumps(output), flush=True)
        return 0 if output['passed'] else 1
    finally:
        tf.stop()


if __name__ == '__main__':
    raise SystemExit(main())
