"""Activate Twister from the real hand against a controlled Spell/Trap fixture."""
import argparse
import json
import os
import struct
import tf
from tf3_verify_sabre import read, write
from tf3_verify_venus_natural import place_from_hand
from tf3_probe_client import Probe

SIDE = 0x09f79cec


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--case', choices=['normal', 'economics', 'low', 'exact', 'set', 'native', 'negated', 'lost', 'replaced', 'set_after'], default='normal')
    parser.add_argument('--native', action='store_true', help='Use native MST as a matched control for a target-mutation case.')
    args = parser.parse_args()
    native = args.case == 'native' or args.native
    os.environ.update(TF3_TEST_PROBE='1', TF3_FORCE_COPIES='3', TF3_FORCE_CARDS='4909' if native else '6999', TF_PRIMEIRO='0', TF_DECK_J1='1')
    if args.case in ('lost', 'replaced', 'set_after'):
        os.environ['TF3_TEST_ACTIONS'] = '1'
    with open(os.path.join(os.environ['TF_MODS_FROM'], 'TF3Cards', 'ported.txt'), encoding='utf-8') as f:
        source = 4909 if native else next(int(r[0]) for line in f if len(r := line.split(' ', 3)) == 4 and int(r[1]) == 6999)
    fixture_result = None
    fixture_state = None
    try:
        tf.duelo(['--lento'])
        tf.run('w300')
        place_from_hand(1, 0, 5, 5121)  # Infinite Cards: no negation or target protection.
        if args.case == 'set':
            write(SIDE + 0xfac + 0x30 + 100 + 8, struct.pack('<I', 0))
            write(SIDE + 0xfac + 0x30 + 100 + 16, struct.pack('<I', 0x100000))
        if args.case in ('economics', 'negated'):
            place_from_hand(0, 1, 6, 6051 if args.case == 'economics' else 4960)
        lp = 499 if args.case == 'low' else 500 if args.case == 'exact' else 2000
        write(SIDE, struct.pack('<I', lp))
        count = struct.unpack('<I', read(SIDE + 12, 4))[0]
        hand = struct.unpack('<' + 'I' * count, read(SIDE + 0x120, count * 4))
        index = next(i for i, word in enumerate(hand) if word & 0x1fff == source)
        legal = Probe().hand_spell_legal(0, index)
        # Removing a hand fixture leaves the old cursor index; wrap once more.
        steps = count - 1 - index + int(args.case in ('economics', 'negated'))
        tf.run('DOWN w40 DOWN w40 ' + 'LEFT w30 ' * steps + 'X w60 shot:menu')
        if args.case not in ('low', 'exact', 'set'):
            tf.run('X w240 shot:prompt X w160 shot:target')
            if args.case in ('lost', 'replaced', 'set_after'):
                Probe().arm_selected_destruction(1, 5, source, {'set_after': 1, 'lost': 2, 'replaced': 3}[args.case])
            tf.run('X w200 shot:after_pick')
            tf.run('X w200 RIGHT w30 X w600 shot:resolved')
            if args.case in ('lost', 'replaced', 'set_after'):
                fixture_result = Probe().selected_destruction_fixture_result()
                fixture_state = Probe().selected_destruction_fixture_state()
        final_lp = struct.unpack('<I', read(SIDE, 4))[0]
        target = struct.unpack('<I', read(SIDE + 0xfac + 0x30 + 100, 4))[0] & 0x1fff
        n = struct.unpack('<I', read(SIDE + 20, 4))[0]
        grave = [w & 0x1fff for w in struct.unpack('<' + 'I' * n, read(SIDE + 0x710, n * 4))] if n else []
        if args.case in ('low', 'exact', 'set'):
            passed = legal == 0 and final_lp == lp and target == 5121 and source not in grave
        else:
            passed = legal != 0 and final_lp == lp - (0 if native or args.case == 'economics' else 500) and source in grave
            # With an own face-up card the native cursor selects it first.
            # Destroying Spell Economics itself still uses the free activation cost.
            passed = passed and (target == 5121 if args.case in ('negated', 'economics', 'replaced', 'set_after') else target == 0)
            if args.case == 'economics':
                passed = passed and 6051 in grave
            if args.case == 'negated':
                passed = passed and (struct.unpack('<I', read(SIDE + 0x30 + 120, 4))[0] & 0x1fff) == 4960
            if fixture_result is not None:
                passed = passed and fixture_result == (5121, {'set_after': 1, 'lost': 2, 'replaced': 3}[args.case])
                passed = passed and fixture_state[5] == 6
                if args.case in ('lost', 'replaced'):
                    passed = passed and fixture_state[8] == 0
                if args.case == 'replaced':
                    def token(word):
                        return (((word >> 16) & 0x3fff) >> 6) * 2 + ((word >> 13) & 1)
                    passed = passed and token(fixture_state[2]) != token(fixture_state[3])
        result = dict(case=args.case, source=source, legal=legal, lp_before=lp, lp_after=final_lp, target=target, grave=grave, fixture_result=fixture_result, fixture_state=fixture_state, passed=passed)
        with open(os.path.join(tf.CTL, 'twister_play_results.json'), 'w', encoding='utf-8') as f:
            json.dump(result, f, indent=2)
        print(json.dumps(result), flush=True)
        return 0 if passed else 1
    finally:
        tf.stop()


if __name__ == '__main__':
    raise SystemExit(main())
