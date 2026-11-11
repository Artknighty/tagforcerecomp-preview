"""Reuse Windstorm's native position changes through Gravitic Orb's real FLIP event."""
import argparse
import json
import os
from pathlib import Path
import struct
import tf
from tf3_verify_hand_search import read_state, SIDE
from tf3_verify_venus_natural import place_from_hand
from tf3_probe_client import Probe


def entry(side, slot):
    return SIDE + side*0xfac + 0x30 + slot*20


def position(side, slot):
    raw = bytes.fromhex(tf.send(f'peek {entry(side,slot):08x} 20').split()[-1])
    flags = struct.unpack_from('<I', raw, 16)[0]
    return (struct.unpack_from('<I', raw)[0] & 0x1fff, 2 if flags & 2 else struct.unpack_from('<H', raw, 6)[0])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--case', choices=['mixed', 'empty', 'negated', 'plain', 'native', 'battle', 'immunity', 'native-immunity'], required=True)
    case = parser.parse_args().case
    native = case in ('native', 'native-immunity')
    cid = 5001 if native else 7206
    os.environ.update(TF3_TEST_PROBE='1', TF3_TEST_ACTIONS='1', TF3_FORCE_CARDS=str(cid), TF3_FORCE_COPIES='3', TF_PRIMEIRO='0', TF_DECK_J1='1')
    rows = [line.split(' ', 3) for line in (Path(os.environ['TF_MODS_FROM']) / 'TF3Cards/ported.txt').read_text(encoding='utf-8').splitlines()]
    gids = {int(r[1]): int(r[0]) for r in rows}
    gid = 5001 if cid == 5001 else gids[cid]
    try:
        tf.duelo([] if case == 'battle' else ['--lento']); tf.run('w300')
        tf.send('nodraw 1'); tf.RAPIDO = True
        if case == 'battle':
            tf.run('O w60 DOWN w40 DOWN w40')
            hand = read_state()['hand']
            tf.run('LEFT w30 '*(len(hand)-1) + 'X w60 DOWN w30 X w120 shot:set')
            before = read_state()
            tf.run('O w60 RIGHT w20 RIGHT w20 RIGHT w20 X w1500 shot:attacked X w240 X w240 shot:resolved')
            after = read_state()
            opponents = [position(1, slot) for slot in range(5)]
            source = [position(0, slot) for slot in range(5)]
            import cartas
            spark = next(c.id for c in cartas.CARTAS.values() if c.nome == 'Elemental Hero Sparkman')
            passed = (spark, 1) in opponents and len(after['deck']) == len(before['deck'])-1 and len(after['hand']) == len(before['hand'])+1
            passed &= gid in after['grave'] or any(card == gid for card, _ in source)
            result = dict(case=case, before=before, after=after, opponents=opponents, source=source, passed=bool(passed))
        else:
            if case != 'plain':
                place_from_hand(0, read_state()['hand'].index(gid), 7 if native else 2, gid)
                source = entry(0, 7 if native else 2)
                tf.send(f'poke {source+4:08x} {bytes(8).hex()}')
                if not native:
                    tf.send(f'poke {source+6:08x} {struct.pack("<H",1).hex()}')
                else:
                    # The engine's first turn is zero; zero would mean Set this turn.
                    tf.send(f'poke {source+4:08x} {struct.pack("<H",0xffff).hex()}')
                tf.send(f'poke {source+16:08x} {struct.pack("<I",0x100000 if native else 0x100002).hex()}')
            place_from_hand(0, next(i for i, card in enumerate(read_state()['hand']) if card != gid), 0, 4007)
            if case != 'empty':
                place_from_hand(1, 0, 0, 4011)
                place_from_hand(1, 0, 1, 4007)
                tf.send(f'poke {entry(1,1)+6:08x} {struct.pack("<H",1).hex()}')
                place_from_hand(1, 0, 2, 4016)
                tf.send(f'poke {entry(1,2)+8:08x} {bytes(4).hex()}')
                tf.send(f'poke {entry(1,2)+6:08x} {struct.pack("<H",1).hex()}')
                tf.send(f'poke {entry(1,2)+16:08x} {struct.pack("<I",0x100002).hex()}')
            if case == 'negated':
                place_from_hand(1, 0, 6, 5740)
            if case in ('immunity', 'native-immunity'):
                place_from_hand(1, 0, 3, 6478)
            before = dict(own=[position(0,k) for k in range(5)], opp=[position(1,k) for k in range(5)])
            hand_before = read_state()
            if case == 'plain':
                hand = read_state()['hand']; index = hand.index(gid)
                tf.run('DOWN w40 DOWN w40 ' + 'LEFT w30 '*(len(hand)-1-index) + 'X w60 X w240 shot:summoned')
            elif native:
                assert Probe().resolve_windstorm(0, 7), 'Native Windstorm record is missing'
                tf.run('w300 shot:native_resolved')
            else:
                tf.run('DOWN w40 UP w40 LEFT w30 LEFT w30 ' + ('' if native else 'UP w40 ') +
                       'X w60 shot:menu X w240 X w160 X w240 shot:resolved')
            after = dict(own=[position(0,k) for k in range(5)], opp=[position(1,k) for k in range(5)])
            expected = before['opp'].copy()
            if case not in ('negated', 'plain', 'empty'):
                expected[0] = (4011, 1); expected[1] = (4007, 0)
            if case == 'immunity':
                expected[3] = (6478, 1)
            passed = after['opp'] == expected and after['own'][0] == before['own'][0]
            # No Kotetsu Equip search may leak into this resolution.
            final = read_state()
            passed &= len(final['deck']) == len(hand_before['deck'])
            if case != 'plain' and not native:
                passed &= after['own'][2] == (gid, 0) and len(final['hand']) == len(hand_before['hand'])
            result = dict(case=case, before=before, after=after, expected=expected, passed=bool(passed))
        Path(tf.CTL, 'orb_results.json').write_text(json.dumps(result, indent=2))
        print(json.dumps(result), flush=True)
        return 0 if result['passed'] else 1
    finally:
        tf.stop()


if __name__ == '__main__':
    raise SystemExit(main())
