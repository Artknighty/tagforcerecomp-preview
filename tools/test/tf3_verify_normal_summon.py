"""Check the shared Special Summon path and naturally summon Genex Power Planner."""
import json
import argparse
import os
from pathlib import Path
import sys
import tf
from tf3_probe_client import Probe
from tf3_verify_hand_search import read_state, SIDE
from tf3_verify_venus_natural import place_from_hand


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--case', choices=['planner-normal', 'planner-flip', 'ogre-normal', 'ogre-flip', 'manju-flip', 'planner-empty', 'planner-negated', 'ogre-negated'], default='planner-normal')
    case = parser.parse_args().case
    cid = 5900 if case.startswith('manju') else 6938 if case.startswith('ogre') else 7809
    os.environ.update(TF3_TEST_PROBE='1', TF3_FORCE_CARDS=str(cid), TF3_FORCE_COPIES='3',
                      TF_PRIMEIRO='0', TF_DECK_J1='1', TF3_DEBUG_POST_ALL='1', TF3_DEBUG_FX='1')
    pack = Path(os.environ['TF_MODS_FROM']) / 'TF3Cards'
    gids = {int(r[1]): int(r[0]) for line in (pack / 'ported.txt').read_text(encoding='utf-8').splitlines()
            if len(r := line.split(' ', 3)) == 4}
    try:
        tf.duelo(['--lento'])
        tf.run('w300')
        probe = Probe()
        legality = {f'{side}:{cid}': probe.special_summon_legal(side, gids.get(cid, cid))
                    for side in range(2) for cid in (6938, 7809, 4007)}
        # Retain real instance tags and leave a valid target in the Deck.
        import struct
        before = read_state()
        raw = bytearray.fromhex(tf.send(f'peek {SIDE + 0x3a0:08x} {len(before["deck"]) * 4}').split()[-1])
        for i in range(len(before['deck'])):
            word = struct.unpack_from('<I', raw, i * 4)[0]
            target = 4007 if case == 'planner-empty' else 4802 if cid == 5900 else gids[7808]
            struct.pack_into('<I', raw, i * 4, (word & ~0x1fff) | target)
        tf.send(f'poke {SIDE + 0x3a0:08x} {raw.hex()}')
        source = gids.get(cid, cid)
        if case.endswith('negated'):
            place_from_hand(1, 0, 5, 5740)
        if case.endswith('flip'):
            place_from_hand(0, read_state()['hand'].index(source), 2, source)
            tf.send(f'poke {SIDE + 0x30 + 2 * 20 + 4:08x} {bytes(4).hex()}')
            tf.send(f'poke {SIDE + 0x30 + 2 * 20 + 8:08x} {bytes(4).hex()}')
            tf.send(f'poke {SIDE + 0x30 + 2 * 20 + 16:08x} {struct.pack("<I", 0x100002).hex()}')
            before = read_state()
            tf.run('DOWN w40 UP w40 LEFT w30 LEFT w30 UP w40 X w60 shot:menu X w240 shot:summoned X w160 shot:prompt X w160 shot:targets X w240 shot:resolved')
        else:
            if cid == 6938:
                # Two real hand instances provide the controlled tribute board.
                for slot in (0, 1):
                    index = next(i for i, card in enumerate(read_state()['hand']) if card != source)
                    place_from_hand(0, index, slot, 4007)
            before = read_state()
            index = before['hand'].index(source)
            # Fixture removal retains the old UI hand index; two removals wrap twice.
            steps = len(before['hand']) - 1 - index + (2 if cid == 6938 else 0)
            tf.run('DOWN w40 DOWN w40 ' + 'LEFT w30 ' * steps + 'X w60 shot:menu X w240 shot:summoned')
            if cid == 6938:
                tf.run('X w160 shot:tribute X w160 X w240 shot:tributed')
            tf.run('X w160 shot:prompt X w160 shot:targets X w240 shot:resolved')
        if cid == 5900:
            tf.run('X w300 shot:confirmed')
        after = read_state()
        passed = all(value == ((0, 0, 0) if key.endswith(':6938') else (1, 1, 1))
                     for key, value in legality.items())
        expected = int(case in ('planner-normal', 'ogre-normal', 'manju-flip'))
        field_raw = bytes.fromhex(tf.send(f'peek {SIDE + 0x30:08x} 100').split()[-1])
        field = [struct.unpack_from('<5I', field_raw, i * 20) for i in range(5)]
        passed &= len(after['deck']) == len(before['deck']) - expected
        passed &= any(e[0] & 0x1fff == source and e[2] & 1 for e in field)
        passed &= len(after['hand']) == len(before['hand']) + expected - int(not case.endswith('flip'))
        if cid == 7809:
            passed &= after['hand'].count(gids[7808]) == before['hand'].count(gids[7808]) + expected
        result = dict(case=case, legality=legality, field=field, before=before, after=after, passed=bool(passed))
        Path(tf.CTL, 'normal_summon_results.json').write_text(json.dumps(result, indent=2))
        print(json.dumps(result), flush=True)
        return 0 if passed else 1
    finally:
        tf.stop()


if __name__ == '__main__':
    sys.exit(main())
