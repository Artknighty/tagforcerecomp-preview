"""Check Twister's LP cost and face-up-only target filter in the real engine.

Requires isolated TF_TAG/TF_TEST_EXE/TF_MODS_FROM and TF3TestProbe.
No natural resolution claim is made by these controlled predicate queries.
"""
import json
import os
import sys
import tf
from tf3_probe_client import Probe, field


def main():
    os.environ.update(TF3_TEST_PROBE='1', TF_PRIMEIRO='0', TF_DECK_J1='1')
    with open(os.path.join(os.environ['TF_MODS_FROM'], 'TF3Cards', 'ported.txt'), encoding='utf-8') as f:
        twister = next(int(r[0]) for line in f if len(r := line.split(' ', 3)) == 4 and int(r[1]) == 6999)
    results = []
    try:
        tf.duelo([])
        probe = Probe()
        for source_side in (0, 1):
            for target_side in (0, 1):
                for label, fixtures, slot, wanted in [
                    ('face-up Spell', [field(target_side, 5, 4909)], 5, 1),
                    ('face-down Spell', [field(target_side, 5, 4909, enabled=0, flags=0x100000)], 5, 0),
                    ('face-up Trap', [field(target_side, 5, 4989)], 5, 1),
                    ('face-down Trap', [field(target_side, 5, 4989, enabled=0, flags=0x100000)], 5, 0),
                    ('Field Spell', [field(target_side, 10, 5533)], 10, 1),
                    ('monster', [field(target_side, 0, 4007)], 0, 0),
                    ('empty', [], 5, 0),
                ]:
                    cost, target, callback = probe.spell_cost_target(fixtures, twister, target_side, slot, source_side)
                    ok = cost == 500 and bool(target) == bool(wanted) and callback != 0
                    results.append(dict(case=label, source_side=source_side, target_side=target_side,
                                        cost=cost, target=target, callback=callback, passed=ok))
                # The original MST must still accept a Set Spell without an LP cost.
                values = probe.spell_cost_target([field(target_side, 5, 4909, enabled=0, flags=0x100000)],
                                                  4909, target_side, source_side=source_side)
                results.append(dict(case='native MST unchanged', values=values,
                                    passed=values[0] == 0 and values[1] != 0 and values[2] == 0))
            for econ_side in (0, 1):
                cost, _, callback = probe.spell_cost_target([field(econ_side, 6, 6051), field(1, 5, 5121)], twister, 1, source_side=source_side)
                results.append(dict(case='Spell Economics', source_side=source_side, economics_side=econ_side,
                                    cost=cost, passed=cost == (0 if econ_side == source_side else 500) and callback != 0))
        with open(os.path.join(tf.CTL, 'twister_results.json'), 'w', encoding='utf-8') as f:
            json.dump(results, f, indent=2)
        for r in results:
            print('PASS' if r['passed'] else 'FAIL', json.dumps(r), flush=True)
        return 0 if all(r['passed'] for r in results) else 1
    finally:
        tf.stop()


if __name__ == '__main__':
    sys.exit(main())
