"""Native properly-summoned Ritual checks for all three imported Cyber Angels."""
from pathlib import Path
import json
import os
import sys
import tf
from tf3_probe_client import Probe, field


def main():
    pack = Path(os.environ['TF_MODS_FROM']) / 'TF3Cards'
    gids = {int(r[1]): int(r[0]) for line in (pack / 'ported.txt').read_text(encoding='utf-8').splitlines()
            if len(r := line.split(' ', 3)) == 4}
    os.environ.update(TF3_TEST_PROBE='1', TF3_FORCE_CARDS='6845,6846,6847', TF3_FORCE_COPIES='3',
                      TF_PRIMEIRO='0', TF_CPUCPU='0', TF_DECK_J1='1')
    checks = []
    try:
        tf.duelo(['--lento']); tf.run('w300'); tf.send('nodraw 1'); tf.RAPIDO = True
        p = Probe()
        for side in (0, 1):
            for proper in (False, True):
                expected = (0, 0, int(proper), int(proper))
                control = p.special_summon_paths(side, 4370, proper=proper)
                checks.append(dict(name=f'native-{side}-{proper}', got=control, expected=expected, passed=control == expected))
                for cid in (6845, 6846, 6847):
                    for label, fixtures in [('empty', []), ('skill-drain', [field(1-side, 5, 5740)])]:
                        got = p.special_summon_paths(side, gids[cid], fixtures, proper=proper)
                        checks.append(dict(name=f'{cid}-{side}-{proper}-{label}', got=got, expected=control, passed=got == control))
        out = dict(passed=all(c['passed'] for c in checks), checks=len(checks),
                   failures=[c for c in checks if not c['passed']], results=checks)
        (Path(tf.CTL) / 'ritual_revival_results.json').write_text(json.dumps(out, indent=2), encoding='utf-8')
        print(json.dumps(dict(passed=out['passed'], checks=len(checks), failures=out['failures'])), flush=True)
        return 0 if out['passed'] else 1
    finally:
        tf.stop()


if __name__ == '__main__':
    sys.exit(main())
