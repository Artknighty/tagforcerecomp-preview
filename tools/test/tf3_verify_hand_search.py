"""Exercise imported discard-to-search monsters from the actual hand menu.

Requires TF_TAG, TF_TEST_EXE and TF_MODS_FROM pointing at isolated copies.
The source is dealt normally; a controlled Deck fixture supplies/removes its
target. Results and captures stay under the test controller's gen/ folder.
"""
import argparse
import json
import os
import struct
import sys

import tf
from tf3_probe_client import Probe

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'mods'))
import tf3_efeitos_param as P

SIDE = 0x09f79cec
TARGETS = {7105: 'Skyscraper', 7109: 'The Sanctuary in the Sky', 7110: 'Pandemonium', 7106: 'Necrovalley', 7107: 'A Legendary Ocean',
           7108: 'Jurassic World', 7572: 'Valhalla, Hall of the Fallen',
           6044: 'Polymerization'}


def read_state():
    data = bytes.fromhex(tf.send(f'peek {SIDE:08x} 2048').split()[-1])
    def cards(count, offset):
        return [struct.unpack_from('<I', data, offset + 4 * i)[0] & 0x1fff
                for i in range(struct.unpack_from('<I', data, count)[0])]
    return dict(hand=cards(12, 0x120), deck=cards(16, 0x3a0), grave=cards(20, 0x710))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('card', type=int, choices=TARGETS)
    parser.add_argument('--missing', action='store_true')
    parser.add_argument('--menu-only', action='store_true')
    args = parser.parse_args()
    os.environ.update(TF3_TEST_PROBE='1', TF3_FORCE_CARDS=str(args.card), TF3_FORCE_COPIES='3',
                      TF_PRIMEIRO='0', TF_DECK_J1='1')
    with open(os.path.join(os.environ['TF_MODS_FROM'], 'TF3Cards', 'ported.txt'), encoding='utf-8') as f:
        gids = {int(r[1]): int(r[0]) for line in f if len(r := line.split(' ', 3)) == 4}
    prop = P.propriedades(gids)
    target = next(i for i, p in prop.items() if p['name'] == TARGETS[args.card])
    source = gids.get(args.card, args.card)
    try:
        tf.duelo(['--lento'])
        tf.run('w300')
        initial = read_state()
        # Preserve native Deck instance tags; change only the low card-ID bits.
        raw = bytearray.fromhex(tf.send(f'peek {SIDE + 0x3a0:08x} {len(initial["deck"]) * 4}').split()[-1])
        for i in range(len(initial['deck'])):
            word = struct.unpack_from('<I', raw, i * 4)[0]
            cid = word & 0x1fff
            replacement = 4007 if args.missing and cid == target else target if not args.missing and i == 0 else cid
            struct.pack_into('<I', raw, i * 4, (word & ~0x1fff) | replacement)
        tf.send(f'poke {SIDE + 0x3a0:08x} {raw.hex()}')
        before = read_state()
        index = before['hand'].index(source)
        legal = Probe().hand_search_legal(0, index, source)
        tf.run('DOWN w40 DOWN w40 ' + 'LEFT w30 ' * (len(before['hand']) - 1 - index) + 'X w60 shot:menu')
        if args.menu_only:
            print('Menu captured; activation not attempted.', flush=True)
            return 0
        if not args.missing:
            tf.run('X w120 shot:activate X w200 shot:chain_prompt RIGHT w30 X w600 shot:resolved')
        after = read_state()
        if args.missing:
            passed = legal == 0 and before == after
        else:
            passed = (legal == 1 and after['hand'].count(target) == before['hand'].count(target) + 1 and
                      after['hand'].count(source) == before['hand'].count(source) - 1 and
                      after['grave'].count(source) == before['grave'].count(source) + 1 and
                      len(after['deck']) == len(before['deck']) - 1)
        result = dict(card=args.card, missing=args.missing, source=source, target=target, legal=legal,
                      before=before, after=after, passed=passed)
        with open(os.path.join(tf.CTL, 'hand_search_results.json'), 'w', encoding='utf-8') as f:
            json.dump(result, f, indent=2)
        print(json.dumps(result), flush=True)
        return 0 if passed else 1
    finally:
        tf.stop()


if __name__ == '__main__':
    sys.exit(main())
