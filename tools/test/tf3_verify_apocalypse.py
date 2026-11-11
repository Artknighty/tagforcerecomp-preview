"""Flip Worm Apocalypse through the native menu with controlled target fixtures."""
import argparse
import json
import os
from pathlib import Path
import struct
import tf
from tf3_verify_hand_search import read_state, SIDE
from tf3_verify_venus_natural import place_from_hand


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--case', choices=['spell', 'trap', 'set', 'own', 'empty', 'negated', 'choice'], default='spell')
    case = parser.parse_args().case
    os.environ.update(TF3_FORCE_CARDS='7543', TF3_FORCE_COPIES='3', TF_PRIMEIRO='0', TF_DECK_J1='1')
    rows = [line.split(' ', 3) for line in (Path(os.environ['TF_MODS_FROM']) / 'TF3Cards/ported.txt').read_text(encoding='utf-8').splitlines()]
    gid = next(int(r[0]) for r in rows if int(r[1]) == 7543)
    side = int(case != 'own')
    target = 4863 if case == 'trap' else 5121
    base = SIDE + side * 0xfac
    try:
        tf.duelo(['--lento']); tf.run('w300')
        place_from_hand(0, read_state()['hand'].index(gid), 2, gid)
        entry = SIDE + 0x30 + 2 * 20
        tf.send(f'poke {entry + 4:08x} {bytes(8).hex()}')
        tf.send(f'poke {entry + 16:08x} {struct.pack("<I", 0x100002).hex()}')
        if case != 'empty':
            place_from_hand(side, 0, 5, target)
            if case == 'set':
                tf.send(f'poke {base + 0x30 + 5 * 20 + 8:08x} {bytes(4).hex()}')
                tf.send(f'poke {base + 0x30 + 5 * 20 + 16:08x} {struct.pack("<I", 0x100000).hex()}')
        if case == 'negated':
            place_from_hand(1, 0, 6, 5740)
        if case == 'choice':
            place_from_hand(1, 0, 6, 4960)
        # A monster is an illegal target and must survive every case.
        place_from_hand(1, 0, 1, 4007)
        before = read_state()
        tf.run('DOWN w40 UP w40 LEFT w30 LEFT w30 UP w40 X w60 shot:menu X w240 shot:flipped X w160 shot:prompt')
        if case == 'choice':
            tf.run('LEFT w30 shot:second_target')
        tf.run('X w160 shot:target X w200 X w300 shot:resolved')
        own = bytes.fromhex(tf.send(f'peek {entry:08x} 20').split()[-1])
        target_raw = bytes.fromhex(tf.send(f'peek {base + 0x30 + 5 * 20:08x} 20').split()[-1])
        monster = int.from_bytes(bytes.fromhex(tf.send(f'peek {SIDE + 0xfac + 0x30 + 20:08x} 2').split()[-1]), 'little') & 0x1fff
        remaining = struct.unpack_from('<I', target_raw)[0] & 0x1fff
        expected = target if case in ('negated', 'choice') else 0
        source_id = struct.unpack_from('<I', own)[0] & 0x1fff
        passed = remaining == expected and source_id == gid and struct.unpack_from('<I', own, 8)[0] & 1 and monster == 4007
        if case == 'choice':
            other = int.from_bytes(bytes.fromhex(tf.send(f'peek {base + 0x30 + 6 * 20:08x} 2').split()[-1]), 'little') & 0x1fff
            passed &= other == 0
        result = dict(case=case, target=target, target_remaining=remaining, source=source_id, monster=monster,
                      before=before, after=read_state(), passed=bool(passed))
        Path(tf.CTL, 'apocalypse_results.json').write_text(json.dumps(result, indent=2))
        print(json.dumps(result), flush=True)
        return 0 if passed else 1
    finally:
        tf.stop()


if __name__ == '__main__':
    raise SystemExit(main())
