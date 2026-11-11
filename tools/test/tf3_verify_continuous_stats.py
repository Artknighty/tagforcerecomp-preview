"""Check level-sum and Continuous Spell counts through the actual engine."""
import argparse
import json
import os
from pathlib import Path
import struct
import tf
from tf3_probe_client import Probe, field
from tf3_verify_hand_search import P, read_state, SIDE
from tf3_verify_venus_natural import place_from_hand


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--case', choices=['predicates', 'calculator', 'dweller', 'calculator-negated', 'dweller-negated'], required=True)
    case = parser.parse_args().case
    cid = 7436 if case.startswith('calculator') else 7119
    os.environ.update(TF3_TEST_PROBE='1', TF3_TEST_ACTIONS='1', TF3_FORCE_CARDS=str(cid),
                      TF3_FORCE_COPIES='3', TF_PRIMEIRO='0', TF_DECK_J1='1')
    pack = Path(os.environ['TF_MODS_FROM']) / 'TF3Cards'
    rows = [line.split(' ', 3) for line in (pack / 'ported.txt').read_text(encoding='utf-8').splitlines()]
    gids = {int(r[1]): int(r[0]) for r in rows}
    props = P.propriedades(gids)
    named = lambda name: next(i for i, p in props.items() if p['name'] == name)
    results = []
    try:
        tf.duelo(['--lento']); tf.run('w300')
        tf.send('nodraw 1'); tf.RAPIDO = True
        probe = Probe()
        if case == 'predicates':
            for side in range(2):
                calc = field(side, 0, gids[7436])
                dweller = field(side, 0, gids[7119])
                down = field(side, 1, 4007, enabled=0, flags=0x100002)
                scenarios = [
                    ('calc alone', [calc], 600),
                    ('calc own level 8', [calc, field(side, 1, 4007)], 3000),
                    ('calc opposing level 8', [calc, field(1-side, 1, 4007)], 600),
                    ('calc face-down excluded', [calc, down], 600),
                    ('calc current other level', [calc, field(side, 1, 4007), field(side, 5, gids[7691], 2)], 3300),
                    ('calc own changed level', [calc, field(side, 5, gids[7691], 1)], 900),
                    ('calc disabled', [field(side, 0, gids[7436], enabled=0), field(side, 1, 4007)], 0),
                    ('dweller alone', [dweller], 1500),
                    ('dweller own continuous', [dweller, field(side, 5, 5121)], 1800),
                    ('dweller two continuous', [dweller, field(side, 5, 5121), field(side, 6, 5121)], 2100),
                    ('dweller opposing excluded', [dweller, field(1-side, 5, 5121)], 1500),
                    ('dweller set excluded', [dweller, field(side, 5, 5121, enabled=0, flags=0x100000)], 1500),
                    ('dweller normal spell excluded', [dweller, field(side, 5, 4844)], 1500),
                    ('dweller equip excluded', [dweller, field(side, 5, gids[7225], 1)], 1500),
                    ('dweller continuous trap excluded', [dweller, field(side, 5, named('Royal Decree'))], 1500),
                    ('dweller field excluded', [dweller, field(side, 10, named('Neo Space'))], 1500),
                    ('dweller negated spell still counts', [dweller, field(side, 5, 5121, enabled=33)], 1800),
                    ('dweller spell state bit still counts', [dweller, field(side, 5, 5121, enabled=33, flags=0xa0)], 1800),
                    ('dweller disabled', [field(side, 0, gids[7119], enabled=0), field(side, 5, 5121)], 1500),
                ]
                for label, fixtures, expected in scenarios:
                    actual = probe.stats(fixtures, side)
                    result = dict(case=label, side=side, actual=actual, expected=expected, passed=actual[0] == expected)
                    results.append(result)
                    print(json.dumps(result), flush=True)
        else:
            if case.endswith('negated'):
                place_from_hand(1, 0, 5, 5740)
            hand = read_state()['hand']; index = hand.index(gids[cid])
            tf.run('DOWN w40 DOWN w40 ' + 'LEFT w30 ' * (len(hand) - 1 - index) + 'X w60 shot:menu X w240 shot:summoned')
            raw = bytes.fromhex(tf.send(f'peek {SIDE + 0x30:08x} 100').split()[-1])
            slot = next(i for i in range(5) if struct.unpack_from('<I', raw, 20*i)[0] & 0x1fff == gids[cid])
            initial = probe.current_stats(0, slot)
            hand = read_state()['hand']
            item = next(i for i, card in enumerate(hand) if card != gids[cid])
            fixture_slot, fixture_id = (1 if slot != 1 else 0, 4007) if cid == 7436 else (5, 5121)
            place_from_hand(0, item, fixture_slot, fixture_id)
            active = probe.current_stats(0, slot)
            probe.remove_current(0, fixture_slot, destroyed=False)
            restored = probe.current_stats(0, slot)
            baseline = (0 if cid == 7436 else 1500) if case.endswith('negated') else 600 if cid == 7436 else 1500
            boosted = baseline if case.endswith('negated') else 3000 if cid == 7436 else 1800
            results.append(dict(case=case, initial=initial, active=active, restored=restored,
                                passed=initial[0] == baseline and active[0] == boosted and restored[0] == baseline))
            tf.run('shot:final')
        Path(tf.CTL, 'continuous_stats_results.json').write_text(json.dumps(results, indent=2))
        print(json.dumps(results[-1]), flush=True)
        return 0 if all(r['passed'] for r in results) else 1
    finally:
        tf.stop()


if __name__ == '__main__':
    raise SystemExit(main())
