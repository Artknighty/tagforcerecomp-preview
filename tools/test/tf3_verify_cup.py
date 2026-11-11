"""Activate Cup of Ace from hand; assert the recipient and both Deck/hand deltas."""
import argparse
import json
import os
from pathlib import Path
import struct
import tf
from tf3_verify_hand_search import read_state, SIDE
from tf3_verify_venus_natural import place_from_hand


def counts():
    result = []
    for side in range(2):
        raw = bytes.fromhex(tf.send(f'peek {SIDE + 0xfac * side:08x} 24').split()[-1])
        result.append(struct.unpack_from('<II', raw, 12))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--case', choices=['heads', 'tails', 'native', 'negated'], required=True)
    case = parser.parse_args().case
    cid = 4844 if case == 'native' else 7623
    os.environ.update(TF3_FORCE_CARDS=str(cid), TF3_FORCE_COPIES='3', TF_PRIMEIRO='0', TF_DECK_J1='1',
                      TF3_FORCE_COIN='0' if case == 'tails' else '1')
    rows = [line.split(' ', 3) for line in (Path(os.environ['TF_MODS_FROM']) / 'TF3Cards/ported.txt').read_text(encoding='utf-8').splitlines()]
    gid = cid if cid == 4844 else next(int(r[0]) for r in rows if int(r[1]) == cid)
    try:
        tf.duelo(['--lento']); tf.run('w300')
        tf.send('nodraw 1'); tf.RAPIDO = True
        if case == 'negated':
            place_from_hand(1, 0, 5, 4960)
        hand = read_state()['hand']; index = hand.index(gid)
        before = counts()
        tf.run('DOWN w40 DOWN w40 ' + 'LEFT w30 ' * (len(hand) - 1 - index) +
               'X w60 shot:menu X w1400 shot:resolved')
        after = counts(); grave = read_state()['grave']
        draws = (0, 0) if case == 'negated' else (0, 2) if case == 'tails' else (2, 0)
        expected = [(h + draws[side] - int(side == 0), d - draws[side]) for side, (h, d) in enumerate(before)]
        result = dict(case=case, before=before, after=after, expected=expected, grave=grave,
                      passed=after == expected and gid in grave)
        Path(tf.CTL, 'cup_results.json').write_text(json.dumps(result, indent=2))
        print(json.dumps(result), flush=True)
        return 0 if result['passed'] else 1
    finally:
        tf.stop()


if __name__ == '__main__':
    raise SystemExit(main())
