"""Verify Worm Cartaros's FLIP classification and search after a real attack.

Requires an isolated TF_TAG, TF_TEST_EXE, TF_MODS_FROM and TF3TestProbe.
Only the copied test save/mods are used. The CPU uses the first native duelist
deck and the player starts, making the Set/attack sequence repeatable.
"""
import json
import os
import struct
import sys
import tf
from tf3_probe_client import Probe


def state():
    data = bytes.fromhex(tf.send('peek 09f79cec 512').split()[-1])
    return dict(hand=struct.unpack_from('<I', data, 12)[0], deck=struct.unpack_from('<I', data, 16)[0])


def main():
    os.environ.update(TF3_TEST_PROBE='1', TF3_EVENT_IDS='1', TF3_FORCE_CARDS='7545',
                      TF3_FORCE_COPIES='3', TF_PRIMEIRO='0', TF_DECK_J1='1')
    pack = os.path.join(os.environ['TF_MODS_FROM'], 'TF3Cards')
    with open(os.path.join(pack, 'ported.txt'), encoding='utf-8') as f:
        gids = {int(r[1]): int(r[0]) for line in f if (r := line.split(' ', 3)) and len(r) == 4}
    try:
        tf.duelo(['--lento'])
        tf.run('w300'); tf.send('nodraw 1'); tf.RAPIDO = True
        probe = Probe()
        expected = {5769: 1, 4009: 0, gids[7545]: 1, gids[7892]: 0, gids[7809]: 0}
        kinds = {cid: probe.flip_kind(cid) for cid in expected}
        tf.run('DOWN w40 DOWN w40')
        hand = state()['hand']
        tf.run(' '.join(['LEFT w30'] * (hand - 1)) + ' X w60 shot:card_menu DOWN w30 X w100 shot:set')
        before = state()
        field = bytes.fromhex(tf.send('peek 09f79d1c 100').split()[-1])
        ids = [struct.unpack_from('<H', field, i * 20)[0] & 0x1fff for i in range(5)]
        if gids[7545] not in ids:
            raise RuntimeError('Worm Cartaros was not Set; inspect the card_menu/set captures')
        tf.run('O w60 RIGHT w20 RIGHT w20 RIGHT w20 X w1500 shot:attacked')
        tf.run('X w200 shot:search_prompt X w100 shot:search_list X w60 X w200 shot:resolved')
        after = state()
        result = dict(classification=kinds, before=before, after=after)
        result['passed'] = kinds == expected and after['hand'] == before['hand'] + 1 and after['deck'] == before['deck'] - 1
        with open(os.path.join(tf.CTL, 'flip_results.json'), 'w', encoding='utf-8') as f:
            json.dump(result, f, indent=2)
        print(json.dumps(result), flush=True)
        return 0 if result['passed'] else 1
    finally:
        tf.stop()


if __name__ == '__main__':
    sys.exit(main())
