"""One shared Effigy matrix and native Normal/Tribute Summon UI scenarios."""
import argparse
import json
import os
from pathlib import Path
import struct
import sys
import tf
from tf3_probe_client import Probe, field
from tf3_verify_hand_search import P, read_state, SIDE
from tf3_verify_venus_natural import place_from_hand

CARDS = {7213: (1, 5409), 7214: (2, 6040), 7367: (5, 6121), 7368: (6, 6259)}
EFFECT_TARGETS = {1: 'The Creator', 2: 'Dark Magician of Chaos', 5: 'Ancient Gear Golem', 6: "Harpie's Pet Dragon"}


def normal_target(props, attribute):
    return max((cid for cid, p in props.items() if cid < 6958 and p['category'] == 0 and p['attribute'] == attribute),
               key=lambda cid: (props[cid]['level'], props[cid]['atk'] or 0, -cid))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--card', type=int, choices=CARDS, default=7213)
    parser.add_argument('--case', choices=['matrix', 'summon', 'set-source', 'effect-target', 'native', 'native-effect-target'], default='matrix')
    args = parser.parse_args()
    rows = [l.split(' ', 3) for l in (Path(os.environ['TF_MODS_FROM']) / 'TF3Cards/ported.txt').read_text(encoding='utf-8').splitlines()]
    gids = {int(r[1]): int(r[0]) for r in rows}; props = P.propriedades(gids)
    attr, donor = CARDS[args.card]
    target = normal_target(props, attr)
    effect_target = lambda attribute: next(cid for cid, p in props.items() if cid < 6958 and p['name'] == EFFECT_TARGETS[attribute])
    if args.case.endswith('effect-target'):
        target = effect_target(attr)
    source_cid = donor if args.case.startswith('native') else args.card
    source = gids.get(source_cid, source_cid)
    os.environ.update(TF3_TEST_PROBE='1', TF3_FORCE_CARDS=f'{source_cid},{target}', TF3_FORCE_COPIES='2',
                      TF_PRIMEIRO='0', TF_DECK_J1='1')
    results = []
    try:
        tf.duelo(['--lento']); tf.run('w300'); tf.send('nodraw 1'); tf.RAPIDO = True
        probe = Probe()
        if args.case == 'matrix':
            for cid, (attribute, native) in CARDS.items():
                valid = normal_target(props, attribute)
                wrong = normal_target(props, 2 if attribute != 2 else 1)
                effect = effect_target(attribute)
                for side in range(2):
                    for state, enabled, flags in [('face-up', 1, 0x80), ('Set', 0, 0x100002), ('negated', 0, 0xa0)]:
                        for label, wanted in [('normal', valid), ('wrong Attribute', wrong), ('effect', effect)]:
                            actual = probe.double_tribute([field(side, 0, gids[cid], enabled=enabled, flags=flags)], side, wanted, side)
                            reference = probe.double_tribute([field(side, 0, native, enabled=enabled, flags=flags)], side, wanted, side)
                            expected = reference[0] if label == 'normal' else 0
                            results.append(dict(card=cid, side=side, state=state, target_kind=label, target=wanted,
                                                actual=actual[0], expected=expected, reference=reference[0],
                                                passed=actual[0] == expected and actual[1] == actual[2] and reference[1] == reference[2]))
                    for slot in range(5):
                        actual = probe.double_tribute([field(side, slot, gids[cid])], side, valid, side, slot)
                        results.append(dict(card=cid, side=side, slot=slot, case='all source slots', passed=actual[0] == 1 and actual[1] == actual[2]))
                    for enabled, flags in [(1, 0x80), (0, 0x100002)]:
                        actual = probe.double_tribute([field(1-side, 0, gids[cid], enabled=enabled, flags=flags)], side, valid, 1-side)
                        reference = probe.double_tribute([field(1-side, 0, native, enabled=enabled, flags=flags)], side, valid, 1-side)
                        results.append(dict(card=cid, side=side, case='opposing material predicate', flags=flags,
                                            passed=actual[0] == reference[0] and actual[1] == actual[2]))
            for native, attribute in [(5409, 1), (6040, 2), (6120, 3), (6260, 4), (6121, 5), (6259, 6)]:
                for side in range(2):
                    wanted = normal_target(props, attribute)
                    actual = probe.double_tribute([field(side, 0, native)], side, wanted, side)
                    results.append(dict(native=native, side=side, case='unchanged native donor', passed=actual[0] == 1 and actual[1] == actual[2]))
            passed = all(r['passed'] for r in results)
            result = dict(case=args.case, passed=passed, checks=len(results), results=results)
        else:
            assert props[target]['attribute'] == attr
            index = read_state()['hand'].index(source)
            place_from_hand(0, index, 0, source)
            if args.case == 'set-source':
                write = lambda address, data: tf.send(f'poke {address:08x} {data.hex()}')
                write(SIDE+0x30+4, struct.pack('<HHI', 0, 1, 0)); write(SIDE+0x30+16, struct.pack('<I', 0x100002))
            before = read_state()
            steps = len(before['hand']) - 1 - before['hand'].index(target) + 1
            tf.run('DOWN w40 DOWN w40 ' + 'LEFT w30 '*steps + 'X w60 shot:menu X w240 shot:summoning X w160 X w160 X w200 X w200 shot:summoned')
            after = read_state()
            raw = bytes.fromhex(tf.send(f'peek {SIDE+0x30:08x} 100').split()[-1])
            board = [struct.unpack_from('<5I', raw, 20*i) for i in range(5)]
            present = [e[0] & 0x1fff for e in board]
            if args.case == 'effect-target':
                passed = source in present and target not in present and after == before
            else:
                passed = target in present and source not in present
                passed &= after['grave'].count(source) == before['grave'].count(source) + 1
                passed &= len(after['hand']) == len(before['hand']) - 1
            result = dict(card=args.card, case=args.case, source=source, target=target, target_name=props[target]['name'],
                          target_level=props[target]['level'], before=before, after=after, field=board, passed=bool(passed))
        Path(tf.CTL, 'effigy_results.json').write_text(json.dumps(result, indent=2))
        print(json.dumps({k:v for k,v in result.items() if k != 'results'}), flush=True)
        for item in results:
            if not item['passed']: print(json.dumps(item), flush=True)
        return 0 if result['passed'] else 1
    finally:
        tf.stop()


if __name__ == '__main__':
    sys.exit(main())
