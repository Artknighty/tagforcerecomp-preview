"""Compare Dimikles's resolved-FLIP state with native Slate Warrior in isolated games."""
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
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--case', choices=['predicates', 'modifiers', 'lifecycle', 'plain', 'flip', 'negated', 'after-negated', 'native', 'battle'], required=True)
    case = parser.parse_args().case
    cid = 5037 if case == 'native' else 7546
    os.environ.update(TF3_TEST_PROBE='1', TF3_TEST_ACTIONS='1', TF3_FORCE_CARDS=str(cid), TF3_FORCE_COPIES='3', TF_PRIMEIRO='0', TF_DECK_J1='1')
    rows = [line.split(' ', 3) for line in (Path(os.environ['TF_MODS_FROM']) / 'TF3Cards/ported.txt').read_text(encoding='utf-8').splitlines()]
    gids = {int(r[1]): int(r[0]) for r in rows}
    gid = 5037 if cid == 5037 else gids[cid]
    results = []
    try:
        tf.duelo([] if case == 'battle' else ['--lento']); tf.run('w300')
        tf.send('nodraw 1'); tf.RAPIDO = True; probe = Probe()
        if case == 'predicates':
            for side in (0, 1):
                for enabled in (0, 1):
                    for flags in (0x80, 0x90, 0x100002):
                        actual = probe.stats([field(side, 0, gid, enabled=enabled, flags=flags)], side)
                        native = probe.stats([field(side, 0, 5037, enabled=enabled, flags=flags)], side)
                        boosted = native[0] == 2400
                        expected = (2000, 1700) if boosted else (1700, 1400)
                        results.append(dict(case='native-state comparison', side=side, enabled=enabled, flags=flags,
                                            actual=actual, native=native, expected=expected, passed=actual[:2] == expected))
        elif case == 'modifiers':
            for side in (0, 1):
                for lp in (1000, 2000, 3000):
                    for current, value in ((side, lp), (1-side, 2000)):
                        tf.send(f'poke {SIDE + current*0xfac:08x} {struct.pack("<I",value).hex()}')
                    for equip in (4310, 4663):
                        def delta(card):
                            plain = probe.stats([field(side, 0, card), field(side, 5, equip, 1)], side)
                            boosted = probe.stats([field(side, 0, card, flags=0x90), field(side, 5, equip, 1)], side)
                            return tuple(b-a for a, b in zip(plain[:2], boosted[:2]))
                        actual, native = delta(gid), delta(5037)
                        expected = tuple(value*3//5 for value in native)
                        results.append(dict(case='native modifier delta', side=side, lp=lp, equip=equip,
                                            actual=actual, native=native, expected=expected, passed=actual == expected))
        elif case == 'battle':
            tf.run('O w60 DOWN w40 DOWN w40')
            hand = read_state()['hand']
            tf.run('LEFT w30 ' * (len(hand)-1) + 'X w60 DOWN w30 X w120 shot:set')
            before = read_state()
            tf.run('O w60 RIGHT w20 RIGHT w20 RIGHT w20 X w1500 shot:attacked X w240 X w240 shot:resolved')
            raw = bytes.fromhex(tf.send(f'peek {SIDE+0x30:08x} 100').split()[-1])
            slots = [i for i in range(5) if struct.unpack_from('<I', raw, 20*i)[0] & 0x1fff == gid]
            # A 1600-ATK attack destroys the 1400-DEF monster before its FLIP resolves.
            # No inherited Kotetsu Equip search or Slate battle-destruction debuff may occur.
            after = read_state()
            opponent = bytes.fromhex(tf.send(f'peek {SIDE+0xfac+0x30:08x} 100').split()[-1])
            import cartas
            spark_id = next(c.id for c in cartas.CARTAS.values() if c.nome == 'Elemental Hero Sparkman')
            sparkman = next((i for i in range(5) if struct.unpack_from('<I', opponent, 20*i)[0] & 0x1fff == spark_id), None)
            attacker = probe.current_stats(1, sparkman) if sparkman is not None else None
            results.append(dict(case=case, slots=slots, grave=after['grave'], attacker=attacker, before=before, after=after,
                                passed=not slots and gid in after['grave'] and attacker is not None and attacker[0] == 1600
                                and len(after['deck']) == len(before['deck'])-1 and len(after['hand']) == len(before['hand'])+1))
        elif case == 'plain':
            hand = read_state()['hand']; index = hand.index(gid)
            tf.run('DOWN w40 DOWN w40 ' + 'LEFT w30 ' * (len(hand)-1-index) + 'X w60 X w240 shot:summoned')
            raw = bytes.fromhex(tf.send(f'peek {SIDE+0x30:08x} 100').split()[-1])
            slot = next(i for i in range(5) if struct.unpack_from('<I', raw, 20*i)[0] & 0x1fff == gid)
            actual = probe.current_stats(0, slot)
            results.append(dict(case=case, actual=actual, passed=actual[:2] == (1700, 1400)))
        else:
            place_from_hand(0, read_state()['hand'].index(gid), 2, gid)
            entry = SIDE + 0x30 + 40
            tf.send(f'poke {entry+4:08x} {bytes(8).hex()}')
            tf.send(f'poke {entry+16:08x} {struct.pack("<I",0x100002).hex()}')
            if case == 'negated':
                place_from_hand(1, 0, 6, 5740)
            tf.run('DOWN w40 UP w40 LEFT w30 LEFT w30 UP w40 X w60 shot:menu X w240 shot:flipped X w160 X w240 shot:resolved')
            actual = probe.current_stats(0, 2)
            expected = (2400, 900) if cid == 5037 else (1700, 1400) if case == 'negated' else (2000, 1700)
            results.append(dict(case=case, actual=actual, expected=expected, passed=actual[:2] == expected))
            if case == 'after-negated':
                # Controlled native disabled-state transition, not a Trap activation claim.
                tf.send(f'poke {entry+8:08x} {bytes(4).hex()}')
                actual = probe.current_stats(0, 2)
                results.append(dict(case='disabled state after resolution', actual=actual, passed=actual[:2] == (1700, 1400)))
                tf.send(f'poke {entry+8:08x} {struct.pack("<I",1).hex()}')
                actual = probe.current_stats(0, 2)
                results.append(dict(case='enabled state restored', actual=actual, passed=actual[:2] == (2000, 1700)))
            if case == 'lifecycle':
                removed = probe.remove_current(0, 2, destroyed=False)
                tf.run('w100')
                place_from_hand(0, read_state()['hand'].index(gid), 2, gid)
                actual = probe.current_stats(0, 2)
                results.append(dict(case='new instance in same slot', removed=removed, actual=actual,
                                    passed=removed == 1 and actual[:2] == (1700, 1400)))
        Path(tf.CTL, 'dimikles_results.json').write_text(json.dumps(results, indent=2))
        for result in results:
            print(json.dumps(result), flush=True)
        return 0 if all(r['passed'] for r in results) else 1
    finally:
        tf.stop()


if __name__ == '__main__':
    raise SystemExit(main())
