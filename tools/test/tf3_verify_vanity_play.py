"""Normal/Tribute Summon each continuous summon-lock source through the native UI."""
import argparse
import json
import os
from pathlib import Path
import struct
import sys
import tf
from tf3_probe_client import Probe
from tf3_verify_hand_search import read_state, SIDE
from tf3_verify_venus_natural import place_from_hand


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('card', type=int, choices=(6881, 6888))
    cid = parser.parse_args().card
    os.environ.update(TF3_TEST_PROBE='1', TF3_FORCE_CARDS=str(cid), TF3_FORCE_COPIES='3',
                      TF_PRIMEIRO='0', TF_DECK_J1='1')
    pack = Path(os.environ['TF_MODS_FROM']) / 'TF3Cards'
    gids = {int(r[1]): int(r[0]) for line in (pack / 'ported.txt').read_text(encoding='utf-8').splitlines()
            if len(r := line.split(' ', 3)) == 4}
    source, tributes = gids[cid], 1 if cid == 6881 else 2
    try:
        tf.duelo(['--lento']); tf.run('w300'); tf.send('nodraw 1'); tf.RAPIDO = True
        for slot in range(tributes):
            index = next(i for i, card in enumerate(read_state()['hand']) if card != source)
            place_from_hand(0, index, slot, 4007)
        before = read_state()
        steps = len(before['hand']) - 1 - before['hand'].index(source) + tributes
        tf.run('DOWN w40 DOWN w40 ' + 'LEFT w30 ' * steps + 'X w60 shot:menu X w240 shot:summoning')
        tf.run('X w160 ' * (tributes + 3) + 'shot:summoned')
        after = read_state()
        raw = bytes.fromhex(tf.send(f'peek {SIDE + 0x30:08x} 100').split()[-1])
        board = [struct.unpack_from('<5I', raw, i * 20) for i in range(5)]
        probe = Probe()
        legality = [probe.current_special_summon_legal(side, 4007) for side in range(2)]
        expected = [(0, 0, 0), (0, 0, 0)] if cid == 6881 else [(1, 1, 1), (0, 0, 0)]
        passed = sum((e[0] & 0x1fff) == source and bool(e[2] & 1) for e in board) == 1
        passed &= len(after['hand']) == len(before['hand']) - 1
        passed &= after['grave'].count(4007) - before['grave'].count(4007) == tributes
        passed &= legality == expected
        result = dict(card=cid, tributes=tributes, before=before, after=after, field=board,
                      legality=legality, expected=expected, passed=bool(passed))
        Path(tf.CTL, 'vanity_play_results.json').write_text(json.dumps(result, indent=2))
        print(json.dumps(result), flush=True)
        return 0 if passed else 1
    finally:
        tf.stop()


if __name__ == '__main__':
    sys.exit(main())
