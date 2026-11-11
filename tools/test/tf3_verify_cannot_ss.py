"""Special Summon restriction probe for candidate cards with 'cannot_special_summon: true'."""
from pathlib import Path
import json
import os
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'test'))
os.environ.setdefault('TF_SEGUNDO_PLANO', '1')
os.environ.setdefault('NO_AUDIO', '1')
import tf
from tf3_probe_client import Probe, field

TARGET_IDS = [6821, 7091, 7092, 7114, 7146, 7265, 7547, 7728, 7983, 7984]

def main():
    pack = Path(os.environ.get('TF_MODS_FROM', 'gen/tf3_cannot_ss_mods')) / 'TF3Cards'
    gids = {int(r[1]): int(r[0]) for line in (pack / 'ported.txt').read_text(encoding='utf-8').splitlines()
            if len(r := line.split(' ', 3)) == 4}
    
    os.environ.update(
        TF3_TEST_PROBE='1',
        TF3_FORCE_CARDS=','.join(str(c) for c in TARGET_IDS[:3]),
        TF3_FORCE_COPIES='3',
        TF_PRIMEIRO='0',
        TF_CPUCPU='0',
        TF_DECK_J1='1'
    )
    
    checks = []
    try:
        tf.duelo(['--lento'])
        tf.run('w300')
        tf.send('nodraw 1')
        tf.RAPIDO = True
        p = Probe()
        
        # Verify native normal monster control can be Special Summoned
        for side in (0, 1):
            control_norm = p.special_summon_paths(side, 4047, proper=True)
            checks.append(dict(name=f'native-control-norm-{side}', got=control_norm, expected=(1, 1, 1, 1), passed=control_norm == (1, 1, 1, 1)))

            # Check all 10 cards under both proper=False and proper=True, empty field and skill drain
            for cid in TARGET_IDS:
                if cid not in gids:
                    checks.append(dict(name=f'{cid}-missing-gid', got=None, expected='present', passed=False))
                    continue
                gid = gids[cid]
                for proper in (False, True):
                    for label, fixtures in [('empty', []), ('skill-drain', [field(1 - side, 5, 5740)])]:
                        got = p.special_summon_paths(side, gid, fixtures, proper=proper)
                        expected = (0, 0, 0, 0)
                        passed = (got == expected)
                        checks.append(dict(name=f'{cid}-{side}-proper{int(proper)}-{label}', got=got, expected=expected, passed=passed))

        out = dict(passed=all(c['passed'] for c in checks), checks=len(checks),
                   failures=[c for c in checks if not c['passed']], results=checks)
        res_file = Path(tf.CTL) / 'cannot_ss_results.json'
        res_file.write_text(json.dumps(out, indent=2), encoding='utf-8')
        print(json.dumps(dict(passed=out['passed'], checks=len(checks), failures=out['failures'])), flush=True)
        return 0 if out['passed'] else 1
    finally:
        tf.stop()

if __name__ == '__main__':
    sys.exit(main())
