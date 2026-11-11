"""Exercise a shared summon-lock family and ordinary Normal metadata in one engine session."""
import json
import os
from pathlib import Path
import sys
import tf
from tf3_probe_client import Probe, field


def main():
    os.environ.update(TF3_TEST_PROBE='1', TF_PRIMEIRO='0', TF_DECK_J1='1')
    pack = Path(os.environ['TF_MODS_FROM']) / 'TF3Cards'
    gids = {int(r[1]): int(r[0]) for line in (pack / 'ported.txt').read_text(encoding='utf-8').splitlines()
            if len(r := line.split(' ', 3)) == 4}
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'mods'))
    import tf3_efeitos_param as params
    props = params.propriedades(gids)
    queue = json.loads(Path('gen/tf3_review_queue.json').read_text(encoding='utf-8'))
    normals = [r for r in queue['queue'] if r['state'] == 'normal_metadata_review' and not r['blockers']]
    results = []
    try:
        tf.duelo(['--lento']); tf.run('w300'); tf.send('nodraw 1'); tf.RAPIDO = True
        probe = Probe()
        for cid in (6881, 6888):
            for source_side in range(2):
                for target_side in range(2):
                    for slot in range(5):
                        for state, enabled, flags in [('active', 1, 0x80), ('negated', 0, 0x80), ('set', 0, 0x100002), ('absent', 0, 0)]:
                            fixtures = [] if state == 'absent' else [field(source_side, slot, gids[cid], enabled=enabled, flags=flags)]
                            blocked = state == 'active' and (cid == 6881 or source_side != target_side)
                            got = probe.special_summon_paths(target_side, 4007, fixtures)
                            expected = (int(not blocked),) * 4
                            results.append(dict(card=cid, source=source_side, target=target_side, slot=slot, state=state,
                                                actual=got, expected=expected, passed=got == expected))
            for side in range(2):
                got = probe.special_summon_paths(side, gids[cid])
                results.append(dict(card=cid, side=side, case='inherent prohibition', actual=got, passed=got == (0, 0, 0, 0)))
        # Distinct instances compose: negating one must not disable the other.
        for side in range(2):
            got = probe.special_summon_paths(side, 4007,
                [field(0, 0, gids[6881], enabled=0), field(1, 4, gids[6888])])
            expected = (int(side == 1),) * 4
            results.append(dict(case='two sources', side=side, actual=got, expected=expected, passed=got == expected))
        for row in normals:
            cid, gid = row['id'], row['duel_id']
            p = props[gid]
            for side in range(2):
                got = probe.stats([field(side, 0, gid)], side)
                expected = (p['atk'], p['defe'], p['level'])
                legal = probe.special_summon_paths(side, gid)
                results.append(dict(card=cid, name=row['name'], case='normal stats and summon predicates', side=side,
                                    actual=got, expected=expected, special=legal,
                                    passed=got == expected and legal == (1, 1, 1, 1)))
        output = dict(passed=all(r['passed'] for r in results), checks=len(results), normals=len(normals), results=results,
                      scope='Controlled native predicates and stat queries; Normal/Tribute Summon UI and complete duels require separate evidence.')
        Path(tf.CTL, 'summon_batch_results.json').write_text(json.dumps(output, indent=2), encoding='utf-8')
        print(json.dumps({k: v for k, v in output.items() if k != 'results'}), flush=True)
        for result in results:
            if not result['passed']: print(json.dumps(result), flush=True)
        return 0 if output['passed'] else 1
    finally:
        tf.stop()


if __name__ == '__main__':
    sys.exit(main())
