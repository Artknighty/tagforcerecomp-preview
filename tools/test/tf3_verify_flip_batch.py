"""New Charmer reuse and regression scenarios for two original FLIP imports."""
import argparse
import json
import os
from pathlib import Path
import struct
import sys
import tf
from tf3_probe_client import Probe
from tf3_verify_hand_search import P, read_state, SIDE
from tf3_verify_venus_natural import place_from_hand

CASES = ['dharc-valid', 'dharc-wrong', 'dharc-down', 'dharc-negated', 'dharc-lifetime', 'native-charmer', 'native-lifetime',
         'ducker-valid', 'ducker-empty', 'ducker-negated', 'ducker-multiple',
         'mosquito-mixed', 'mosquito-negated', 'mosquito-empty']


def board(side):
    raw = bytes.fromhex(tf.send(f'peek {SIDE + side*0xfac + 0x30:08x} 100').split()[-1])
    return [struct.unpack_from('<5I', raw, 20*i) for i in range(5)]


def write(address, fmt, *values):
    tf.send(f'poke {address:08x} {struct.pack(fmt, *values).hex()}')


def set_down(side, slot):
    entry = SIDE + side*0xfac + 0x30 + 20*slot
    write(entry+4, '<HHI', 0, 1, 0)
    write(entry+16, '<I', 0x100002)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--case', choices=CASES, required=True)
    case = parser.parse_args().case
    cid = 6335 if case.startswith('native-') else 7664 if case.startswith('dharc') else 7610 if case.startswith('ducker') else 7439
    os.environ.update(TF3_TEST_PROBE='1', TF3_TEST_ACTIONS='1', TF3_FORCE_CARDS=str(cid),
                      TF3_FORCE_COPIES='3', TF_PRIMEIRO='0', TF_DECK_J1='1')
    pack = Path(os.environ['TF_MODS_FROM']) / 'TF3Cards'
    gids = {int(r[1]): int(r[0]) for line in (pack / 'ported.txt').read_text(encoding='utf-8').splitlines()
            if len(r := line.split(' ', 3)) == 4}
    source = gids.get(cid, cid)
    props = P.propriedades(gids)
    assert props[4385]['attribute'] == props[4041]['attribute'] == 2
    assert props[4538]['attribute'] == props[6335]['attribute'] == 5
    assert props[gids[7664]]['attribute'] == 2
    assert props[6313]['level'] == 4 and props[4007]['level'] == 8
    assert props[5121]['category'] == 13
    assert props[6155]['type'] == props[6390]['type'] == 7
    try:
        tf.duelo(['--lento']); tf.run('w300'); tf.send('nodraw 1'); tf.RAPIDO = True
        probe = Probe()
        place_from_hand(0, read_state()['hand'].index(source), 2, source); set_down(0, 2)
        target = 4385  # La Jinn: DARK, with no continuous negation effect.
        if cid in (6335, 7664):
            if cid == 6335: target = 4538  # Gemini Elf: native EARTH comparison.
            if case == 'dharc-wrong': target = 4007
            place_from_hand(1, 0, 0, target)
            place_from_hand(0, 0, 4, 4041)
            if case == 'dharc-down': set_down(1, 0)
        elif cid == 7610:
            targets = [4007, 5121] if case == 'ducker-empty' else [6313, 4007]
            if case == 'ducker-multiple': targets.append(gids[7546])
            for card in targets:
                place_from_hand(0, 0, 0, card)
                # Build one explicit Graveyard fixture. Repeated native removals
                # share an action driver and are not independent setup calls.
                word = board(0)[0][0]
                count = len(read_state()['grave'])
                write(SIDE+0x710+4*count, '<I', word)
                write(SIDE+20, '<I', count+1)
                write(SIDE+0x30, '<5I', 0, 0, 0, 0, 0)
            assert read_state()['grave'] == targets
        else:
            place_from_hand(0, 0, 4, 4007)
            if case != 'mosquito-empty':
                place_from_hand(0, 0, 0, 6155)
                place_from_hand(1, 0, 0, 6390)
                place_from_hand(1, 0, 1, 6155); set_down(1, 1)
                place_from_hand(1, 0, 3, 4007)
        if case.endswith('negated'): place_from_hand(1, 0, 5, 5740)
        before = dict(own=read_state(), boards=[board(0), board(1)])
        tf.run('DOWN w40 UP w40 LEFT w30 LEFT w30 UP w40 X w60 shot:menu X w240 shot:flipped X w160 shot:prompt X w200 X w200 X w240 shot:resolved')
        after = dict(own=read_state(), boards=[board(0), board(1)])
        ids = lambda side: [e[0] & 0x1fff for e in after['boards'][side]]
        passed = probe.flip_kind(source) == 1 and any(e[0] & 0x1fff == source and e[2] & 1 for e in after['boards'][0])
        if cid in (6335, 7664):
            controlled = case in ('dharc-valid', 'dharc-lifetime', 'native-charmer', 'native-lifetime')
            # An own DARK monster must never be selected or moved.
            passed &= (after['boards'][0][4][0] & 0x1fff) == 4041
            passed &= ids(0).count(target) == int(controlled)
            passed &= ids(1).count(target) == int(not controlled)
            if case.endswith('lifetime'):
                slot = next(i for i, e in enumerate(after['boards'][0]) if e[0] & 0x1fff == source)
                probe.remove_current(0, slot, effect=0, destroyed=False)
                # The explicit removal is not a full UI action. Advance a native
                # phase so normal ownership reconciliation runs, for both donors.
                tf.run('O w60 RIGHT w20 RIGHT w20 RIGHT w20 X w900 shot:source_removed')
                after['returned_boards'] = [board(0), board(1)]
                passed &= sum(e[0] & 0x1fff == target for e in after['returned_boards'][0]) == 0
                passed &= sum(e[0] & 0x1fff == target for e in after['returned_boards'][1]) == 1
        elif cid == 7610:
            recovered = case not in ('ducker-empty', 'ducker-negated')
            passed &= len(after['own']['hand']) == len(before['own']['hand']) + int(recovered)
            passed &= len(after['own']['grave']) == len(before['own']['grave']) - int(recovered)
            if recovered:
                valid = {6313, gids[7546]}
                passed &= sum(card in valid for card in after['own']['hand']) == sum(card in valid for card in before['own']['hand']) + 1
            passed &= after['own']['grave'].count(4007) == before['own']['grave'].count(4007)
        else:
            expected = case == 'mosquito-negated'
            if case != 'mosquito-empty':
                passed &= (after['boards'][0][0][0] & 0x1fff) == (6155 if expected else 0)
                passed &= (after['boards'][1][0][0] & 0x1fff) == (6390 if expected else 0)
                passed &= (after['boards'][1][1][0] & 0x1fff) == 6155
                passed &= (after['boards'][1][3][0] & 0x1fff) == 4007
            passed &= (after['boards'][0][4][0] & 0x1fff) == 4007
        result = dict(case=case, source=source, before=before, after=after, passed=bool(passed))
        Path(tf.CTL, 'flip_batch_results.json').write_text(json.dumps(result, indent=2))
        print(json.dumps(result), flush=True)
        return 0 if passed else 1
    finally:
        tf.stop()


if __name__ == '__main__':
    sys.exit(main())
