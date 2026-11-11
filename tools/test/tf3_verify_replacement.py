"""Play Ebon Steed normally, then request native destruction in an isolated duel."""
import argparse
import json
import os
import struct
import tf
from tf3_probe_client import Probe


def board():
    raw = bytes.fromhex(tf.send('peek 09f79d1c 220').split()[-1])
    return [list(struct.unpack_from('<5I', raw, i * 20)) for i in range(11)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--case', choices=['destroy', 'battle', 'send', 'negated', 'mark', 'marked', 'double', 'unrelated', 'massmonster', 'massboth', 'natural'], default='destroy')
    args = parser.parse_args()
    os.environ.update(TF3_TEST_PROBE='1', TF3_TEST_ACTIONS='1',
                      TF3_FORCE_COPIES='3', TF3_FORCE_CARDS='7276,7000', TF_PRIMEIRO='0', TF_DECK_J1='1')
    try:
        tf.duelo(['--lento'])
        tf.run('w300'); tf.send('nodraw 1'); tf.RAPIDO = True
        tf.run('DOWN w40 DOWN w40')
        hand = int.from_bytes(bytes.fromhex(tf.send('peek 09f79cf8 4').split()[-1]), 'little')
        tf.run(' '.join(['LEFT w30'] * (hand - 1)) + ' X w60 X w200 shot:monster')
        probe = Probe()
        monster = next(i for i, e in enumerate(board()[:5]) if e[0] & 0x1fff == 7440)
        base = probe.current_stats(0, monster)
        tf.run('RIGHT w40 X w60 X w200 X w80 X w200 shot:equipped')
        before = board()
        equipped = probe.current_stats(0, monster)
        equip = next(i for i, e in enumerate(before) if e[0] & 0x1fff == 7213)
        if args.case in ('mark', 'marked'):
            mask = 1 << equip if args.case == 'marked' else 0
            marked = probe.destruction_marks(0, monster, mask=mask)
            output = dict(case=args.case, mask=mask, marked=marked, monster=monster, equip=equip,
                          passed=marked == ((1 << equip) | ((1 << monster) if mask else 0)))
            with open(os.path.join(tf.CTL, 'replacement_results.json'), 'w', encoding='utf-8') as f:
                json.dump(output, f, indent=2)
            print(json.dumps(output), flush=True)
            return 0 if output['passed'] else 1
        if args.case in ('double', 'unrelated'):
            tf.run('DOWN w40 DOWN w40 X w60 X w200 X w80 X w200 shot:second_equip')
            second = [i for i, e in enumerate(board()) if e[0] & 0x1fff == 7213 and i != equip]
            if len(second) != 1:
                raise RuntimeError('Second Steed was not equipped normally')
            if args.case == 'unrelated':
                # Controlled interaction fixture: the second attachment becomes native Axe of Despair.
                address = 0x09f79d1c + second[0] * 20
                word = board()[second[0]][0]
                tf.send('poke %08x %s' % (address, struct.pack('<I', (word & ~0x1fff) | 4310).hex()))
            before = board()
        if args.case == 'negated':
            tf.send('poke %08x %s' % (0x09f79d1c + equip * 20 + 16,
                                     struct.pack('<I', before[equip][4] | 2).hex()))
        if args.case == 'natural':
            lp_before = int.from_bytes(bytes.fromhex(tf.send('peek 09f79cec 4').split()[-1]), 'little')
            tf.run('O w60 RIGHT w20 RIGHT w20 RIGHT w20 X w1500 shot:damage_prompt')
            # Decline optional hand effects (for example Kuriboh), then let the
            # native battle and automatic Equip replacement finish completely.
            tf.run('RIGHT w20 X w300 ' * 8 + 'shot:natural_battle')
            lp_after = int.from_bytes(bytes.fromhex(tf.send('peek 09f79cec 4').split()[-1]), 'little')
            result = 0
        else:
            result = (probe.destroy_group((1 << monster) | ((1 << equip) if args.case == 'massboth' else 0))
                  if args.case.startswith('mass') else
                  probe.remove_current(0, monster, destroyed=args.case != 'send',
                                       effect=0 if args.case == 'battle' else 4909, battle=args.case == 'battle'))
        tf.run('w120 shot:replacement')
        after = board()
        output = dict(case=args.case, base=base, equipped=equipped, before=before, after=after, removal=result)
        output['passed'] = tuple(a-b for a, b in zip(equipped, base)) == (200, 200, 0)
        if args.case == 'natural':
            output.update(lp_before=lp_before, lp_after=lp_after)
            output['passed'] &= lp_after < lp_before
        if args.case in ('destroy', 'battle', 'double', 'unrelated', 'massmonster', 'natural'):
            output['passed'] &= (args.case == 'massmonster' or result == 0) and after[monster][0] & 0x1fff == 7440 and after[equip][0] & 0x1fff == 0
            if args.case in ('double', 'unrelated'):
                output['passed'] &= after[second[0]][0] & 0x1fff == (7213 if args.case == 'double' else 4310)
            output['second_removal'] = probe.remove_current(0, monster)
            tf.run('w120')
            if args.case == 'double':
                output['passed'] &= output['second_removal'] == 0 and board()[monster][0] & 0x1fff == 7440
                output['third_removal'] = probe.remove_current(0, monster)
                tf.run('w120')
                output['passed'] &= output['third_removal'] == 1 and board()[monster][0] & 0x1fff == 0
            else:
                output['passed'] &= output['second_removal'] == 1 and board()[monster][0] & 0x1fff == 0
        elif args.case == 'massboth':
            output['passed'] &= after[monster][0] & 0x1fff == 0 and after[equip][0] & 0x1fff == 0
        else:
            output['passed'] &= result == 1 and after[monster][0] & 0x1fff == 0
        with open(os.path.join(tf.CTL, 'replacement_results.json'), 'w', encoding='utf-8') as f:
            json.dump(output, f, indent=2)
        print(json.dumps(output), flush=True)
        return 0 if output['passed'] else 1
    finally:
        tf.stop()


if __name__ == '__main__':
    raise SystemExit(main())
