"""Check Golden Bamboo activation requirements and native two-card draw in isolated games."""
import argparse
import json
import os
from pathlib import Path
import struct
import tf
from tf3_probe_client import Probe, field
from tf3_verify_cup import counts
from tf3_verify_hand_search import read_state, SIDE
from tf3_verify_venus_natural import place_from_hand
from tf3_verify_sabre import write


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--case', choices=['predicates', 'valid', 'missing', 'opposing', 'set', 'wrong', 'equip-negated', 'negated', 'native', 'natural-equip'], required=True)
    case = parser.parse_args().case
    cid = 4844 if case == 'native' else 7635
    os.environ.update(TF3_TEST_PROBE='1', TF3_FORCE_CARDS=str(cid), TF3_FORCE_COPIES='3', TF_PRIMEIRO='0', TF_DECK_J1='1')
    if case == 'natural-equip':
        os.environ.update(TF3_FORCE_CARDS='4011,7225,7635', TF3_FORCE_COPIES='2')
    rows = [line.split(' ', 3) for line in (Path(os.environ['TF_MODS_FROM']) / 'TF3Cards/ported.txt').read_text(encoding='utf-8').splitlines()]
    gids = {int(r[1]): int(r[0]) for r in rows}
    gid = 4844 if case == 'native' else gids[cid]
    results = []
    try:
        tf.duelo(['--lento']); tf.run('w300')
        tf.send('nodraw 1'); tf.RAPIDO = True
        probe = Probe()
        if case == 'predicates':
            for side in (0, 1):
                for label, fixtures, expected in [
                    ('missing', [], 0),
                    ('own equip', [field(side, 5, gids[7225])], 1),
                    ('opposing equip', [field(1-side, 5, gids[7225])], 0),
                    ('set equip', [field(side, 5, gids[7225], enabled=0, flags=0x100000)], 0),
                    ('negated equip counts', [field(side, 9, gids[7225], enabled=33)], 1),
                    ('wrong spell', [field(side, 5, 5121)], 0),
                    ('monster zone excluded', [field(side, 0, gids[7225])], 0),
                    ('field zone excluded', [field(side, 10, gids[7225])], 0),
                ]:
                    actual = probe.draw_legal(fixtures, side, gid)
                    results.append(dict(case=label, side=side, actual=actual, expected=expected, passed=actual == expected))
                actual = probe.draw_legal([], side, 4844)
                results.append(dict(case='native unchanged', side=side, actual=actual, passed=actual == 1))
        else:
            if case == 'natural-equip':
                hand = read_state()['hand']
                tf.run('DOWN w40 DOWN w40 ' + 'LEFT w30 ' * (len(hand) - 1) + 'X w60 shot:summon_menu X w240 shot:monster')
                monsters = bytes.fromhex(tf.send(f'peek {SIDE + 0x30:08x} 100').split()[-1])
                assert any(struct.unpack_from('<I', monsters, 20 * slot)[0] & 0x1fff == 4011 for slot in range(5)), 'Normal Summon did not occur'
                tf.run('RIGHT w40 X w60 X w200 X w80 X w200 shot:equipped')
                raw = bytes.fromhex(tf.send(f'peek {SIDE + 0x30 + 100:08x} 100').split()[-1])
                assert any(struct.unpack_from('<I', raw, 20 * slot)[0] & 0x1fff == gids[7225] for slot in range(5)), 'Broken Bamboo was not naturally equipped'
            elif case not in ('missing', 'native'):
                side = int(case == 'opposing')
                hand = read_state()['hand']
                index = next(i for i, card in enumerate(hand) if card != gid) if not side else 0
                place_from_hand(side, index, 5, 5121 if case == 'wrong' else gids[7225])
                if case in ('set', 'equip-negated'):
                    entry = SIDE + side * 0xfac + 0x30 + 100
                    write(entry + 8, struct.pack('<I', 0 if case == 'set' else 33))
                    if case == 'set':
                        write(entry + 16, struct.pack('<I', 0x100000))
            if case == 'negated':
                place_from_hand(1, 0, 6, 4960)
            hand = read_state()['hand']; index = hand.index(gid)
            before = counts()
            legal = probe.hand_spell_legal(0, index)
            should_play = case not in ('missing', 'opposing', 'set', 'wrong')
            if should_play:
                navigation = 'DOWN w40 DOWN w40 ' if case != 'natural-equip' else 'DOWN w40 shot:down_one DOWN w40 shot:down_two '
                # After equipping, Down selects the second dealt Golden copy.
                selection = '' if case == 'natural-equip' else 'LEFT w30 ' * (len(hand) - 1 - index)
                tf.run(navigation + selection + 'X w60 shot:menu X w1400 shot:resolved')
            else:
                tf.run('shot:blocked')
            after = counts(); grave = read_state()['grave']
            draws = 2 if should_play and case != 'negated' else 0
            expected = [(h + (draws - int(should_play) if side == 0 else 0), d - (draws if side == 0 else 0)) for side, (h, d) in enumerate(before)]
            results.append(dict(case=case, legal=legal, before=before, after=after, expected=expected,
                                passed=bool(legal) == should_play and after == expected and ((gid in grave) == should_play)))
        Path(tf.CTL, 'golden_results.json').write_text(json.dumps(results, indent=2))
        for result in results:
            print(json.dumps(result), flush=True)
        return 0 if all(r['passed'] for r in results) else 1
    finally:
        tf.stop()


if __name__ == '__main__':
    raise SystemExit(main())
