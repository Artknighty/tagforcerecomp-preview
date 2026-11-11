"""Validate the five named Chrysalis effects through one family and native play routes."""
import argparse
import json
import os
from pathlib import Path
import struct
import tf
from tf3_probe_client import Probe, field
from tf3_verify_hand_search import read_state, SIDE, P
from tf3_verify_venus_natural import place_from_hand

CARDS = {7155: 'Neo-Spacian Air Hummingbird', 7156: 'Neo-Spacian Dark Panther',
         7157: 'Neo-Spacian Glow Moss', 7172: 'Neo-Spacian Flare Scarab',
         7173: 'Neo-Spacian Grand Mole', 6764: 'Neo-Spacian Aqua Dolphin'}


def read(address, size):
    return bytes.fromhex(tf.send(f'peek {address:08x} {size}').split()[-1])


def write(address, data):
    tf.send(f'poke {address:08x} {data.hex()}')


def board(side=0):
    return [struct.unpack_from('<I', read(SIDE+side*0xfac+0x30+20*k, 4))[0] & 0x1fff for k in range(5)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--card', type=int, choices=CARDS, default=7155)
    parser.add_argument('--case', choices=('matrix', 'deck', 'hand', 'missing', 'no-field', 'opposing', 'negated', 'blocked', 'lost-field'), required=True)
    parser.add_argument('--inspect-navigation', action='store_true')
    args = parser.parse_args()
    pack = Path(os.environ['TF_MODS_FROM'])/'TF3Cards'
    gids = {int(r[1]): int(r[0]) for line in (pack/'ported.txt').read_text(encoding='utf-8').splitlines() if len(r := line.split(' ', 3)) == 4}
    props = P.propriedades(gids)
    target = next(i for i, p in props.items() if p['name'] == CARDS[args.card])
    source = gids.get(args.card, args.card)
    # Include the target in the forced Deck so native models and rule resources
    # exist even when a hand/Deck fixture changes the initial distribution.
    target_tf3 = next((cid for cid, gid in gids.items() if gid == target), target)
    os.environ.update(TF3_TEST_PROBE='1', TF3_TEST_ACTIONS='1', TF3_FORCE_CARDS=f'{args.card},{target_tf3},6775',
                      TF3_FORCE_COPIES='3', TF_PRIMEIRO='0', TF_CPUCPU='0', TF_DECK_J1='1')
    results = []
    try:
        tf.duelo(['--lento']); tf.run('w300'); tf.send('nodraw 1'); tf.RAPIDO = True
        probe = Probe()
        if args.case == 'matrix':
            saved = read(SIDE, 2*0xfac)
            for cid in CARDS:
                if cid == 6764:
                    continue
                gid = gids[cid]
                wanted = next(i for i, p in props.items() if p['name'] == CARDS[cid])
                attr = props[wanted]['attribute']
                for side in (0, 1):
                    base = SIDE+side*0xfac
                    for location in ('hand', 'deck', 'both', 'missing', 'wrong', 'grave'):
                        write(SIDE, saved)
                        for owner in (0, 1):
                            b = SIDE+owner*0xfac
                            for count, off in ((12, 0x120), (16, 0x3a0), (20, 0x710)):
                                write(b+count, struct.pack('<I', 1))
                                write(b+off, struct.pack('<I', 4007))
                        for off in ([0x120] if location == 'hand' else [0x3a0] if location == 'deck' else
                                    [0x120, 0x3a0] if location == 'both' else [0x710] if location == 'grave' else []):
                            write(base+off, struct.pack('<I', wanted))
                        if location == 'wrong':
                            write(base+0x120, struct.pack('<I', 6768))
                        for neo_side in (side, 1-side, None):
                            for state in ('active', 'set', 'negated'):
                                fixtures = [field(side, 2, gid)]
                                if neo_side is not None:
                                    fixtures.append(field(neo_side, 10, 6775, enabled=state != 'set', flags=0x80 if state == 'active' else 0x100002 if state == 'set' else 0xa0))
                                got = probe.chrysalis_legal(fixtures, side, 2, gid)
                                count = 2 if location == 'both' else int(location in ('hand', 'deck'))
                                expected = (count, int(count > 0 and neo_side is not None and state != 'set'))
                                results.append(dict(card=cid, side=side, location=location, neo_side=neo_side, state=state,
                                                    got=got, expected=expected, passed=got == expected))
                    # Target availability must obey the previous batch's gates.
                    write(SIDE, saved)
                    write(base+12, struct.pack('<I', 1)); write(base+0x120, struct.pack('<I', wanted))
                    write(base+16, struct.pack('<I', 0))
                    for statue, allowed in ((6882, 2), (6887, 1)):
                        for enabled, flags in ((1, 0x80), (0, 0xa0), (0, 0x100002)):
                            fixtures = [field(side, 2, gid), field(side, 10, 6775), field(1-side, 0, gids[statue], enabled=enabled, flags=flags)]
                            got = probe.chrysalis_legal(fixtures, side, 2, gid)
                            expected = (0, 0) if enabled and attr != allowed else (1, 1)
                            results.append(dict(card=cid, side=side, statue=statue, enabled=enabled, flags=flags,
                                                got=got, expected=expected, passed=got == expected))
                print(f'Completed {cid}: {len(results)} checks', flush=True)
            write(SIDE, saved)
        else:
            hand = read_state()['hand']
            assert source in hand, hand
            # The source is Normal Summoned using the real hand menu.
            if args.case != 'no-field':
                neo_side = int(args.case == 'opposing')
                neo_hand = read_state()['hand'] if not neo_side else []
                index = neo_hand.index(6775) if 6775 in neo_hand else 0
                place_from_hand(neo_side, index, 10, 6775)
            if args.case == 'negated':
                place_from_hand(1, 0, 5, 5740)
            if args.case == 'blocked':
                # Choose a Statue of an Attribute different from this target.
                statue = 6882 if props[target]['attribute'] != 2 else 6887
                place_from_hand(1, 0, 0, gids[statue])
            # Preserve Deck tags and update instance IDs for controlled targets.
            before = read_state()
            for count, off in ((12, 0x120), (16, 0x3a0)):
                n = struct.unpack('<I', read(SIDE+count, 4))[0]
                raw = bytearray(read(SIDE+off, n*4))
                for k in range(n):
                    word = struct.unpack_from('<I', raw, 4*k)[0]
                    if word & 0x1fff == target:
                        struct.pack_into('<I', raw, 4*k, (word & ~0x1fff) | 4007)
                write(SIDE+off, raw)
            location = 'hand' if args.case == 'hand' else 'deck'
            if args.case != 'missing':
                offset = 0x120 if location == 'hand' else 0x3a0
                hand = read_state()['hand']
                index = next(k for k, card in enumerate(hand) if card != source) if location == 'hand' else 0
                word = struct.unpack('<I', read(SIDE+offset+4*index, 4))[0]
                write(SIDE+offset+4*index, struct.pack('<I', (word & ~0x1fff) | target))
                token = (((word >> 16) & 0x3fff) >> 6)*2+((word >> 13) & 1)
                instance = struct.unpack('<I', read(0x09f7bc58+4*token, 4))[0]
                write(0x09f7bc58+4*token, struct.pack('<I', (instance & ~0x1fff) | target))
            hand = read_state()['hand']; index = hand.index(source)
            tf.run('DOWN w40 DOWN w40 '+'LEFT w30 '*(len(hand)-1-index)+'X w60 X w240 shot:normal_summon RIGHT w20 X w180')
            assert source in board(), board()
            initial = read_state(); initial_board = board()
            slot = initial_board.index(source)
            fixtures = [[side, k, *struct.unpack('<5I', read(SIDE+side*0xfac+0x30+20*k,20))]
                        for side in (0,1) for k in range(11)]
            legal = probe.chrysalis_legal(fixtures,0,slot,source)
            if args.inspect_navigation:
                tf.run('shot:nav_initial UP w40 shot:nav_up UP w40 shot:nav_up2 UP w40 shot:nav_up3 X w60 shot:nav_menu')
                return 0
            # Return from the hand area to the Summoned monster's field slot.
            tf.run('UP w40 UP w40 '+'RIGHT w30 '*max(slot-1,0)+'X w60 shot:effect_menu')
            if args.case not in ('missing', 'no-field', 'blocked'):
                tf.run('X w180 shot:activation')
                if args.case == 'lost-field':
                    assert source in read_state()['grave'], 'Tribute must be paid before changing the field fixture'
                    write(SIDE+0x30+10*20,bytes(20))
                tf.run('X w240 RIGHT w20 X w300 shot:selection X w240 X w300 shot:resolution')
            final = read_state(); final_board = board()
            success = args.case in ('deck', 'hand', 'opposing', 'negated')
            passed = (target in final_board and source in final['grave'] and source not in final_board) if success else final_board == initial_board and final == initial
            if success:
                passed &= final_board.count(target) == 1 and sum(bool(card) for card in final_board) == 1
                passed &= final['grave'].count(source) == initial['grave'].count(source)+1
                passed &= len(final['hand']) == len(initial['hand'])-int(location == 'hand')
                passed &= len(final['deck']) == len(initial['deck'])-int(location == 'deck')
            if args.case == 'lost-field':
                # Neo Space must still be face-up at resolution, as the native
                # donor and official card FAQ specify. The Tribute stays paid.
                passed = (not any(final_board) and source in final['grave'] and
                          final['deck'] == initial['deck'] and final['hand'] == initial['hand'])
            passed &= legal == ((0,0) if args.case in ('missing','blocked') else (1,0) if args.case == 'no-field' else (1,1))
            results.append(dict(card=args.card, case=args.case, source=source, target=target, before=initial, after=final,
                                before_board=initial_board, after_board=final_board, legal=legal, passed=bool(passed)))
        output = dict(passed=all(r['passed'] for r in results), checks=len(results), results=results)
        Path(tf.CTL, 'chrysalis_results.json').write_text(json.dumps(output, indent=2), encoding='utf-8')
        print(json.dumps(dict(passed=output['passed'], checks=len(results), failures=[r for r in results if not r['passed']])), flush=True)
        return 0 if output['passed'] else 1
    finally:
        tf.stop()


if __name__ == '__main__':
    raise SystemExit(main())
