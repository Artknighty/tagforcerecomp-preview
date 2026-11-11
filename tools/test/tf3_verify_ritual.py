"""Validate imported specific-target Ritual Spells (ritual.bin) against the native Ritual table.

matrix: native hand-activation legality (probe operation 15, same path the hand menu uses) for each imported Ritual Spell and its native
control, over Ritual Monster / Level combinations, both controllers, hand and field Tributes. It also asserts that the native Ritual table
and the scan table behind it are byte-identical after every query (the plugin extends the table only while the native functions run).
"""
import argparse
import json
import os
import struct
import sys

import tf
from tf3_probe_client import Probe
from tf3_verify_hand_search import SIDE, P, read_state

TABLE = 0x09f767ac
# (Ritual Spell TF3 id, Ritual Monster TF3 id, total Levels) as written in tools/mods/tf3_efeitos.json
IMPORTED = {6997: [(6991, 6)], 6998: [(6992, 4)]}
# The multi-target family is opt-in via --cyber, keeping the original 82-check
# installed-pair regression stable. Its excess-Level checks require exact sums.
NATIVE = {4676: [(4370, 8)], 4703: [(4722, 7)], 5601: [(5600, 4)]}   # Black Luster Ritual, Garma Sword Oath, White Dragon Ritual
CYBER = {6850: [(6845, 6), (6846, 6), (6847, 8)]}


def read(address, size):
    return bytes.fromhex(tf.send(f'peek {address:08x} {size}').split()[-1])


def write(address, data):
    # The runtime reads a command through a 4096-byte line buffer. Keep even
    # cleanup/restoration writes below it after expanding bytes to hex.
    for offset in range(0, len(data), 1024):
        tf.send(f'poke {address + offset:08x} {data[offset:offset + 1024].hex()}')


def set_hand(side, ids, saved_words):
    """Hand = ids; keeps each slot's native instance tag and rewrites the instance table so the engine sees the id."""
    base = SIDE + side * 0xfac
    write(base + 12, struct.pack('<I', len(ids)))
    for k, cid in enumerate(ids):
        word = saved_words[k]
        write(base + 0x120 + 4 * k, struct.pack('<I', (word & ~0x1fff) | cid))
        token = (((word >> 16) & 0x3fff) >> 6) * 2 + ((word >> 13) & 1)
        instance = struct.unpack('<I', read(0x09f7bc58 + 4 * token, 4))[0]
        write(0x09f7bc58 + 4 * token, struct.pack('<I', (instance & ~0x1fff) | cid))


def set_field(side, slot, cid):
    base = SIDE + side * 0xfac + 0x30 + 20 * slot
    write(base, struct.pack('<5I', (cid | 0x00800000) if cid else 0, 0, 1 if cid else 0, 0, 0x80 if cid else 0))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--case', choices=('matrix', 'play'), default='matrix')
    parser.add_argument('--card', type=int, choices=sorted(IMPORTED) + sorted(NATIVE) + [6850], default=6997)
    parser.add_argument('--keys', default='')
    parser.add_argument('--cyber', action='store_true', help='Include the multi-target exact-Level Ritual family in the matrix')
    parser.add_argument('--choices', action='store_true', help='Offer all three Cyber Angels in the same real hand-menu summon')
    parser.add_argument('--monster', type=int, default=0, help='TF3 id of the Ritual Monster to summon (default: the first row)')
    parser.add_argument('--idaten-effect', choices=('recover', 'negated'), help='Check Idaten recovering the paid Ritual Spell after its summon')
    args = parser.parse_args()
    pack = os.path.join(os.environ['TF_MODS_FROM'], 'TF3Cards')
    gids = {int(r[1]): int(r[0]) for line in open(os.path.join(pack, 'ported.txt'), encoding='utf-8').read().splitlines()
            if len(r := line.split(' ', 3)) == 4}
    props = P.propriedades(gids)
    def normal(level, avoid=()):
        return next(i for i, p in sorted(props.items()) if i < 5000 and p['category'] == 0 and p['level'] == level and i not in avoid)
    lv = {n: normal(n) for n in (1, 2, 3, 4, 5, 6, 8)}
    os.environ.update(TF3_TEST_PROBE='1', TF3_TEST_ACTIONS='1', TF3_FORCE_CARDS=','.join(str(x) for x in (6997, 6998, 6991, 6992)),
                      TF3_FORCE_COPIES='3', TF_PRIMEIRO='0', TF_CPUCPU='0', TF_DECK_J1='1')
    results = []
    try:
        tf.duelo(['--lento']); tf.run('w300'); tf.send('nodraw 1'); tf.RAPIDO = True
        probe = Probe()
        saved = read(SIDE, 2 * 0xfac)
        hands = {side: [struct.unpack('<I', read(SIDE + side * 0xfac + 0x120 + 4 * k, 4))[0] for k in range(5)] for side in (0, 1)}
        if args.case == 'play':
            spell_tf3 = args.card
            entries = IMPORTED.get(spell_tf3) or CYBER.get(spell_tf3) or NATIVE[spell_tf3]
            monster_tf3, need = next((m, n) for m, n in entries if not args.monster or m == args.monster)
            spell = gids.get(spell_tf3, spell_tf3); monster = gids.get(monster_tf3, monster_tf3)
            tribute = [lv[need]] if need in lv else [lv[need - 2], lv[2]]
            for s_ in (0, 1):
                for k in range(5):
                    set_field(s_, k, 0)
            if args.idaten_effect == 'negated':
                write(SIDE + 0xfac + 0x30 + 5 * 20, struct.pack('<5I', 5740 | 0x00800000, 0, 1, 0, 0x80))
            size = struct.unpack('<I', read(SIDE + 12, 4))[0]
            full = [struct.unpack('<I', read(SIDE + 0x120 + 4 * k, 4))[0] for k in range(size)]
            set_hand(0, ([spell] + tribute + [4844] * 8)[:size - 1] + [monster], full)   # monster last: the Tribute prompt starts on the first card   # the hand UI keeps its slots; Pot of Greed pads
            if args.choices:
                assert spell_tf3 in CYBER and size == 6, (spell_tf3, size)
                set_hand(0, [spell, lv[6], lv[2]] + [gids[m] for m, _ in CYBER[spell_tf3]], full)
            before = read_state(); board0 = lambda: [struct.unpack('<I', read(SIDE + 0x30 + 20 * k, 4))[0] & 0x1fff for k in range(5)]
            tf.run('shot:ritual_hand')
            route = 'X w120 shot:s1 X w200 shot:s2 RIGHT w30 X w400 shot:s3 X w300 shot:s4 X w300 shot:s5 X w300 shot:s6'
            if spell_tf3 in CYBER:
                if args.choices:
                    steps = [m for m, _ in CYBER[spell_tf3]].index(monster_tf3)
                    route = route.replace('shot:s4 X', 'shot:s4 ' + 'RIGHT w40 ' * steps + 'X')
                route += ' X w300 shot:s7 X w400 shot:s8 X w400 shot:s9'
            tf.run('DOWN w40 DOWN w40 ' + 'LEFT w30 ' * (size - 1) + 'X w60 shot:menu ' + (args.keys or route))
            for _ in range(100):   # the summon animation may still be running
                if monster in board0():
                    break
                tf.run('w100')
            tf.run('w200 shot:end')
            if args.idaten_effect:
                assert monster_tf3 == 6846
                if args.idaten_effect == 'recover':
                    for step in range(5):
                        if read_state()['hand'].count(spell) == before['hand'].count(spell):
                            break
                        tf.run(f'X w200 shot:e{step + 1}')
            after = read_state()
            out = dict(spell=spell, monster=monster, before=before, after=after, board=board0(), passed=monster in board0())
            if args.idaten_effect:
                recovered = after['hand'].count(spell) == before['hand'].count(spell) and spell not in after['grave']
                out.update(effect=args.idaten_effect, recovered=recovered)
                out['passed'] &= recovered == (args.idaten_effect == 'recover')
            with open(os.path.join(tf.CTL, 'ritual_play.json'), 'w', encoding='utf-8') as f:
                json.dump(out, f, indent=1)
            print(json.dumps(out), flush=True)
            return 0 if out['passed'] else 1
        tail_before = read(TABLE + 4 * 24, 4 * 8)
        hands = {side: [struct.unpack('<I', read(SIDE + side * 0xfac + 0x120 + 4 * k, 4))[0] for k in range(5)] for side in (0, 1)}
        assert all(len(set(h)) == 5 for h in hands.values()), hands

        def legal(side, hand, field=()):
            # Batch only the field arrays (100 bytes each). The control reader
            # has a 4096-byte command buffer; whole-side hex writes are truncated.
            fields = [bytearray(100), bytearray(100)]
            for k, cid in enumerate(field):
                struct.pack_into('<5I', fields[side], 20 * k,
                                 cid | 0x00800000, 0, 1, 0, 0x80)
            for s in (0, 1):
                write(SIDE + s * 0xfac + 0x30, fields[s])
            set_hand(side, hand, hands[side])
            return probe.hand_spell_legal(side, 0)

        def check(name, side, got, expected):
            results.append(dict(name=name, side=side, got=got, expected=expected, passed=got == expected))

        imports = dict(IMPORTED)
        if args.cyber:
            imports.update(CYBER)
        rows = [(gids[s], gids[m], n, 'imported') for s, ms in imports.items() for m, n in ms] + [(s, m, n, 'native') for s, ms in NATIVE.items() for m, n in ms]
        for side in (0, 1):
            for spell, monster, need, kind in rows:
                other = next(m2 for s2, m2, n2, k2 in rows if s2 != spell)
                # exact Levels, hand Tributes: one card when a single Level fits, otherwise a pair
                exact = [lv[need]] if need in lv else [lv[need - 2], lv[2]]
                short = [lv[need - 1]] if need - 1 in lv else [lv[need - 3], lv[2]]
                exact_required = args.cyber and spell == gids[6850]
                over = [lv[5], lv[4]] if exact_required and need == 8 else [lv[8]]
                tag = f'{kind}-{spell}-{monster}'
                check(f'{tag}-exact', side, legal(side, [spell, monster] + exact), 1)
                check(f'{tag}-over', side, legal(side, [spell, monster] + over), 0 if exact_required else 1)
                check(f'{tag}-short', side, legal(side, [spell, monster] + short), 0)
                check(f'{tag}-no-monster', side, legal(side, [spell] + exact + [lv[3]]), 0)
                check(f'{tag}-wrong-monster', side, legal(side, [spell, other] + exact), 0)
                check(f'{tag}-field-tribute', side, legal(side, [spell, monster], field=[lv[need]] if need in lv else [lv[need - 2], lv[2]]), 1)
                check(f'{tag}-field-short', side, legal(side, [spell, monster], field=short), 0)
                check(f'{tag}-mixed-tribute', side, legal(side, [spell, monster, lv[1]], field=[lv[need - 1]] if need - 1 in lv else [lv[need - 3], lv[2]]), 1)
            # tables unchanged after all queries
            check('table-restored', side, read(TABLE + 4 * 24, 4 * 8) == tail_before, True)
        write(SIDE, saved)
        passed = all(r['passed'] for r in results)
        out = dict(passed=passed, checks=len(results), failures=[r for r in results if not r['passed']], results=results)
        with open(os.path.join(tf.CTL, 'ritual_results.json'), 'w', encoding='utf-8') as f:
            json.dump(out, f, indent=1)
        print(json.dumps(dict(passed=passed, checks=len(results), failures=out['failures'][:12])), flush=True)
        return 0 if passed else 1
    finally:
        tf.stop()


if __name__ == '__main__':
    sys.exit(main())
