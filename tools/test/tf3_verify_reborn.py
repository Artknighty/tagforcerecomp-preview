"""Special Summon trigger checks through a real Monster Reborn hand activation.

The Graveyard uses unique real Deck-instance tags, not duplicated hand tokens.
This tests the shared Special Summon event adapter and Normal-only exclusion.
"""
import argparse
import json
import os
from pathlib import Path
import struct
import sys
import tf
from tf3_verify_ritual import read, write, set_hand, set_field, SIDE
from tf3_verify_hand_search import read_state


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--card', type=int, choices=(6846, 7809, 5883), default=6846)
    parser.add_argument('--negated', action='store_true')
    args = parser.parse_args()
    pack = Path(os.environ['TF_MODS_FROM']) / 'TF3Cards'
    gids = {int(r[1]): int(r[0]) for line in (pack / 'ported.txt').read_text(encoding='utf-8').splitlines()
            if len(r := line.split(' ', 3)) == 4}
    source = gids.get(args.card, args.card)
    os.environ.update(TF3_TEST_PROBE='1', TF3_FORCE_CARDS='4842', TF3_FORCE_COPIES='3',
                      TF_PRIMEIRO='0', TF_CPUCPU='0', TF_DECK_J1='1')
    try:
        tf.duelo(['--lento']); tf.run('w300'); tf.send('nodraw 1'); tf.RAPIDO = True
        for side in (0, 1):
            for slot in range(5):
                set_field(side, slot, 0)
        if args.negated:
            write(SIDE + 0xfac + 0x30 + 5 * 20, struct.pack('<5I', 5740 | 0x00800000, 0, 1, 0, 0x80))
        count = struct.unpack('<I', read(SIDE + 12, 4))[0]
        words = list(struct.unpack('<%dI' % count, read(SIDE + 0x120, 4 * count)))
        set_hand(0, [4842] + [4844] * (count - 1), words)
        deck_count = struct.unpack('<I', read(SIDE + 16, 4))[0]
        deck = list(struct.unpack('<%dI' % deck_count, read(SIDE + 0x3a0, deck_count * 4)))
        word = (deck.pop(0) & ~0x1fff) | source | 0x4000
        token = (((word >> 16) & 0x3fff) >> 6) * 2 + ((word >> 13) & 1)
        instance = struct.unpack('<I', read(0x09f7bc58 + token * 4, 4))[0]
        write(0x09f7bc58 + token * 4, struct.pack('<I', (instance & ~0x1fff) | source))
        write(SIDE + 0x3a0, struct.pack('<%dI' % (len(deck) + 1), *deck, 0))
        write(SIDE + 16, struct.pack('<I', len(deck)))
        write(SIDE + 0x710, struct.pack('<I', word))
        write(SIDE + 20, struct.pack('<I', 1))
        before = read_state()
        tf.run('DOWN w40 DOWN w40 ' + 'LEFT w30 ' * (count - 1) + 'X w60 shot:menu X w120 shot:r1 X w200 shot:r2 RIGHT w30 X w300 shot:r3')
        def board():
            return [struct.unpack('<I', read(SIDE + 0x30 + 20 * k, 4))[0] & 0x1fff for k in range(5)]
        for step in range(10):
            state = read_state()
            recovered = state['hand'].count(4842) == 1
            draw_done = len(state['deck']) == len(before['deck']) - 1
            done = recovered if args.card == 6846 and not args.negated else draw_done if args.card == 5883 and not args.negated else source in board()
            if source in board() and done:
                break
            tf.run(f'X w200 shot:r{step + 4}')
        tf.run('w300 shot:reborn_end')
        after = read_state()
        recovery = int(args.card == 6846 and not args.negated)
        draws = int(args.card == 5883 and not args.negated)
        passed = source in board() and len(after['deck']) == len(before['deck']) - draws
        passed &= after['hand'].count(4842) == recovery
        passed &= len(after['hand']) == len(before['hand']) - 1 + recovery + draws
        out = dict(card=args.card, source=source, negated=args.negated, before=before, after=after, board=board(), passed=passed)
        (Path(tf.CTL) / 'reborn_results.json').write_text(json.dumps(out, indent=2), encoding='utf-8')
        print(json.dumps(out), flush=True)
        return 0 if passed else 1
    finally:
        tf.stop()


if __name__ == '__main__':
    sys.exit(main())
