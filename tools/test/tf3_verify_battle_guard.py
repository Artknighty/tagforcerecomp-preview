"""Conditional battle survival with real native attacks in isolated games."""
import argparse
import json
import os
from pathlib import Path
import struct
import tf
from tf3_verify_hand_search import read_state, SIDE, P
from tf3_verify_venus_natural import place_from_hand
from tf3_probe_client import Probe


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--card', type=int, required=True)
    parser.add_argument('--native', action='store_true', help='Use native Spirit Reaper as a calculation control')
    parser.add_argument('--case', choices=('protect', 'attack', 'below', 'negated', 'destroy', 'send'), required=True)
    args = parser.parse_args()
    source_card = 5526 if args.native else args.card
    os.environ.update(TF3_TEST_PROBE='1', TF3_TEST_ACTIONS='1', TF3_FORCE_CARDS=str(source_card),
                      TF3_FORCE_COPIES='3', TF_PRIMEIRO='0', TF_DECK_J1='1', TF3_DEBUG_REPLACE='1')
    rows = [l.split(' ', 3) for l in (Path(os.environ['TF_MODS_FROM'])/'TF3Cards/ported.txt').read_text(encoding='utf-8').splitlines()]
    gids = {int(r[1]): int(r[0]) for r in rows}
    gid = source_card if args.native else gids[args.card]
    props = P.propriedades(gids)
    try:
        tf.duelo(['--lento']); tf.run('w300'); tf.send('nodraw 1'); tf.RAPIDO = True
        hand = read_state()['hand']; index = hand.index(gid)
        tf.run('DOWN w40 DOWN w40 ' + 'LEFT w30 '*(len(hand)-1-index) + 'X w60 ' +
               ('' if args.case == 'attack' else 'DOWN w30 ') + 'X w200 shot:placed')
        if args.case == 'attack':
            tf.send(f'poke {SIDE:08x} {struct.pack("<I",8000).hex()}')
        raw = bytes.fromhex(tf.send(f'peek {SIDE+0x30:08x} 100').split()[-1])
        source_slot = next(k for k in range(5) if struct.unpack_from('<I',raw,20*k)[0]&0x1fff == gid)
        if args.case in ('destroy', 'send'):
            removed = Probe().remove_current(0, source_slot, destroyed=args.case == 'destroy')
            tf.run('w120')
            after = read_state()
            result = dict(card=args.card, case=args.case, removed=removed, grave=after['grave'], passed=removed == 1 and gid in after['grave'])
        else:
            # Native opponent summons Sparkman. A real LIGHT Field Spell raises
            # its current ATK to 2100 without altering the opposing hand/instances.
            if args.case != 'below':
                luminous = next(i for i,p in props.items() if p['name']=='Luminous Spark')
                place_from_hand(0, 0, 10, luminous)
            if args.case == 'negated':
                place_from_hand(0, 0, 5, 5740)
            before = read_state()
            tf.run('shot:fixture')
            tf.run('O w60 RIGHT w20 RIGHT w20 RIGHT w20 X w1600 shot:attack_prompt')
            tf.run('RIGHT w20 X w300 '*8 + 'shot:battled')
            first_battle = bytes.fromhex(tf.send('peek 09f77dcc 112').split()[-1])
            after = read_state()
            raw = bytes.fromhex(tf.send(f'peek {SIDE+0x30:08x} 100').split()[-1])
            alive = any(struct.unpack_from('<I',raw,20*k)[0]&0x1fff == gid for k in range(5))
            battle = bytes.fromhex(tf.send('peek 09f77dcc 112').split()[-1])
            expected = args.case in ('protect', 'attack')
            words = list(struct.unpack('<28I',battle))
            first = list(struct.unpack('<28I',first_battle))
            source_record = next((k for k in (0,14) if words[k+4] == gid), None)
            enemy_atk = words[(14 if source_record == 0 else 0)+7]
            lp = int.from_bytes(bytes.fromhex(tf.send(f'peek {SIDE:08x} 4').split()[-1]),'little')
            passed = alive == expected and (gid in after['grave']) != expected and source_record is not None
            threshold = 1900 if args.card == 7686 else 1
            passed &= enemy_atk < threshold if args.case == 'below' else enemy_atk >= threshold
            if args.case == 'attack':
                passed &= lp == 8000 - (enemy_atk - words[source_record+7])
            result = dict(card=args.card, case=args.case, before=before, after=after,
                          alive=alive, expected=expected, battle=list(struct.unpack('<28I',battle)),
                          first_battle=first, enemy_atk=enemy_atk, lp=lp, passed=bool(passed))
        Path(tf.CTL,'battle_guard_results.json').write_text(json.dumps(result,indent=2))
        print(json.dumps(result),flush=True)
        return 0 if result['passed'] else 1
    finally:
        tf.stop()


if __name__ == '__main__':
    raise SystemExit(main())
