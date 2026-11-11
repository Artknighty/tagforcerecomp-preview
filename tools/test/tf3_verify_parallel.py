"""Run TF3 engine suites with separate packages, saves and logs (at most eight at once)."""
import sys as _sys, pathlib as _pl
_sys.path.insert(0, str(_pl.Path(__file__).resolve().parents[1] / 'mods')); _sys.path.insert(0, str(_pl.Path(__file__).resolve().parents[1] / 'game'))
from tf3_efeitos_param import RULE_FILES
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
JOBS = [
    ('stats', 'tf3_verify_stats.py', []),
    ('attacks', 'tf3_verify_attacks.py', []),
    ('targets', 'tf3_verify_equips.py', []),
    ('flip', 'tf3_verify_flip.py', []),
    ('replacement', 'tf3_verify_replacement.py', ['--case', 'destroy']),
    ('battle', 'tf3_verify_replacement.py', ['--case', 'battle']),
    ('send', 'tf3_verify_replacement.py', ['--case', 'send']),
    ('negated', 'tf3_verify_replacement.py', ['--case', 'negated']),
]
HAND_SEARCH_JOBS = [(str(cid), 'tf3_verify_hand_search.py', [str(cid)])
                    for cid in (7106,7107,7108,7572,6044)] + [
    (str(cid)+'_missing', 'tf3_verify_hand_search.py', [str(cid),'--missing'])
    for cid in (7106,7107,7572)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', required=True, type=Path)
    parser.add_argument('--mods', required=True, type=Path)
    parser.add_argument('--tag', required=True)
    parser.add_argument('--workers', type=int, choices=range(1, 9), default=4,
                        help='Concurrent game processes; lower this when startup/rendering contention outweighs parallelism')
    parser.add_argument('--cases', nargs='+', help='Run only these named cases from the selected suite')
    parser.add_argument('--suite', choices=['regression', 'replacement', 'mass', 'final', 'sword', 'sword-final', 'gladius', 'gladius-final', 'sabre', 'sabre-double', 'sabre-destination', 'venus', 'venus-effect', 'venus-continuous', 'venus-natural', 'venus-prohibition', 'venus-natural-negative', 'venus-jammer', 'venus-targeted', 'venus-phoenix', 'venus-rod', 'venus-immunity', 'jammer-set', 'venus-targeted-negative', 'venus-trap', 'hand-search', 'twister', 'twister-interactions', 'normal-summon', 'simple-effects', 'continuous-stats', 'continuous-final', 'golden', 'golden-final'], default='regression')
    parser.add_argument('--dimikles', action='store_true', help='Add resolved-FLIP bonus scenarios to the selected suite')
    parser.add_argument('--orb', action='store_true', help='Add shared Windstorm/FLIP position scenarios to the selected suite')
    parser.add_argument('--summon-batch', action='store_true', help='Add one session covering the summon-lock family and ordinary Normal cards')
    parser.add_argument('--flip-review', action='store_true', help='Add the Charmer inclusion and original FLIP review batch')
    parser.add_argument('--effigy', action='store_true', help='Add the shared Normal-Monster double-Tribute batch')
    parser.add_argument('--battle-guard', action='store_true', help='Add shared conditional battle-survival scenarios')
    parser.add_argument('--light-bonus', action='store_true', help='Add shared LIGHT Damage Step bonus scenarios')
    parser.add_argument('--role-bonus', action='store_true', help='Add signed attacking/defending Damage Step bonus scenarios')
    parser.add_argument('--barriers',action='store_true',help='Add the six-card shared Attribute summon-restriction batch')
    parser.add_argument('--field-search', action='store_true', help='Add complete named Field Spell search/survival family')
    parser.add_argument('--ritual', action='store_true', help='Add imported specific-target Ritual Spell legality matrix with native controls')
    parser.add_argument('--cyber-ritual', action='store_true', help='Add candidate Cyber Angel Ritual selection scenarios')
    parser.add_argument('--idaten', action='store_true', help='Add mandatory Special Summon Spell recovery checks')
    parser.add_argument('--chrysalis', action='store_true', help='Add the five-card named hand/Deck summon family')
    parser.add_argument('--hand-search', action='store_true', help='Include the prior named-search regression family in one batch')
    args = parser.parse_args()
    if (args.mods/'TF3Cards/disabled').exists():
        parser.error('TF3Cards is disabled in the test copy; keep live enablement unchanged and enable only the candidate')
    if args.suite in ('golden', 'golden-final'):
        if args.suite == 'golden':
            JOBS[:] = []
        JOBS.extend([('golden-' + case, 'tf3_verify_golden.py', ['--case', case]) for case in
                     ['predicates', 'valid', 'missing', 'opposing', 'set', 'wrong', 'equip-negated', 'negated', 'native', 'natural-equip']])
        if args.suite == 'golden-final':
            JOBS.extend([('cup-' + case, 'tf3_verify_cup.py', ['--case', case]) for case in ['heads', 'tails', 'native', 'negated']])
            JOBS.extend([('bamboo-' + case, 'tf3_verify_sword.py', ['--bamboo', '--case', case]) for case in ['destroy', 'send']])
    if args.suite in ('continuous-stats', 'continuous-final'):
        if args.suite == 'continuous-stats':
            JOBS[:] = []
        JOBS.extend([(case, 'tf3_verify_continuous_stats.py', ['--case', case]) for case in
                     ['predicates', 'calculator', 'dweller', 'calculator-negated', 'dweller-negated']])
    elif args.suite == 'simple-effects':
        JOBS[:] = [('apocalypse-' + case, 'tf3_verify_apocalypse.py', ['--case', case])
                   for case in ['spell', 'trap', 'set', 'own', 'empty', 'negated', 'choice']]
        JOBS.extend([('bamboo-' + case, 'tf3_verify_sword.py', ['--bamboo', '--case', case]) for case in ['destroy', 'send']])
        JOBS.extend([('cup-' + case, 'tf3_verify_cup.py', ['--case', case]) for case in ['heads', 'tails', 'native', 'negated']])
    elif args.suite == 'normal-summon':
        JOBS[:] = [(case, 'tf3_verify_normal_summon.py', ['--case', case]) for case in
                   ['planner-normal', 'planner-flip', 'ogre-normal', 'ogre-flip', 'manju-flip', 'planner-empty', 'planner-negated', 'ogre-negated']]
    elif args.suite == 'twister-interactions':
        JOBS[:] = [(case, 'tf3_verify_twister_play.py', ['--case', case]) for case in ['lost', 'replaced', 'set_after']]
    elif args.suite == 'twister':
        JOBS[:] = [('predicates', 'tf3_verify_twister.py', [])]
        JOBS.extend([(case, 'tf3_verify_twister_play.py', ['--case', case])
                     for case in ['normal', 'economics', 'low', 'exact', 'set', 'native', 'negated']])
    elif args.suite == 'hand-search':
        JOBS[:] = HAND_SEARCH_JOBS
    elif args.suite == 'replacement':
        JOBS[:] = [(case, 'tf3_verify_replacement.py', ['--case', case])
                   for case in ['destroy', 'battle', 'send', 'negated', 'mark', 'marked', 'double', 'unrelated']]
    elif args.suite == 'mass':
        JOBS[4:] = [(case, 'tf3_verify_replacement.py', ['--case', case])
                    for case in ['massmonster', 'massboth', 'double', 'unrelated']]
    elif args.suite == 'final':
        JOBS[4:] = [(case, 'tf3_verify_replacement.py', ['--case', case])
                    for case in ['natural', 'massboth', 'double', 'unrelated']]
    elif args.suite == 'sword':
        JOBS[4:] = [(case, 'tf3_verify_sword.py', ['--case', case])
                    for case in ['tribute', 'destroy', 'send', 'banish']]
    elif args.suite == 'sword-final':
        JOBS[3:] = [('sword_' + case, 'tf3_verify_sword.py', ['--case', case])
                    for case in ['natural', 'destroy', 'send', 'banish']]
        JOBS.append(('ebon_natural', 'tf3_verify_replacement.py', ['--case', 'natural']))
    elif args.suite == 'gladius':
        JOBS[:] = [(case, 'tf3_verify_sword.py', ['--gladius', '--case', case])
                   for case in ['deck', 'doubledeck', 'destroy', 'send', 'hand', 'banish', 'opposing']]
        JOBS.append(('sword_natural', 'tf3_verify_sword.py', ['--case', 'natural']))
    elif args.suite == 'gladius-final':
        JOBS[3:] = [(case, 'tf3_verify_sword.py', ['--gladius', '--case', case])
                    for case in ['deck', 'doubledeck', 'opposing']]
        JOBS.extend([('sword_natural', 'tf3_verify_sword.py', ['--case', 'natural']),
                     ('ebon_natural', 'tf3_verify_replacement.py', ['--case', 'natural'])])
    elif args.suite == 'sabre':
        JOBS[3:] = [(case, 'tf3_verify_sabre.py', ['--case', case])
                    for case in ['valid', 'wrong', 'opponent', 'decline']]
        JOBS.append(('sword_natural', 'tf3_verify_sword.py', ['--case', 'natural']))
    elif args.suite == 'sabre-double':
        JOBS[:] = [('double' + str(i), 'tf3_verify_sabre.py', ['--case', 'double']) for i in range(4)]
        JOBS.extend([(case, 'tf3_verify_sabre.py', ['--case', case]) for case in ['valid', 'wrong', 'opponent', 'decline']])
    elif args.suite == 'sabre-destination':
        JOBS[:] = [(case + str(i), 'tf3_verify_sabre.py', ['--case', case])
                   for case in ['macro', 'fissure'] for i in range(4)]
    elif args.suite == 'venus':
        JOBS[:] = [(case, 'tf3_verify_venus.py', ['--case', case]) for case in
                   ['spell', 'trap', 'other', 'opposing', 'disabled', 'facedown', 'absent', 'side1']]
    elif args.suite == 'venus-effect':
        JOBS[:] = [(case, 'tf3_verify_venus_effect.py', ['--case', case]) for case in
                   ['own0', 'own1', 'opp0', 'opp1', 'disabled', 'facedown', 'absent', 'no_jinzo']]
    elif args.suite == 'venus-continuous':
        JOBS[:] = [(source + '_' + state, 'tf3_verify_venus_effect.py', ['--case', source + '_' + state])
                   for source in ['jinzo', 'decree', 'canceller', 'order'] for state in ['on', 'off']]
    elif args.suite == 'venus-natural':
        JOBS[:] = [(case + str(i), 'tf3_verify_venus_natural.py', ['--case', case])
                   for case in ['protected', 'negated', 'plain', 'venus_only'] for i in range(2)]
    elif args.suite == 'venus-prohibition':
        JOBS[:] = [(case + str(i), 'tf3_verify_venus_natural.py', ['--case', case])
                   for case in ['canceller_venus', 'canceller_only'] for i in range(4)]
    elif args.suite == 'venus-natural-negative':
        JOBS[:] = [(case + str(i), 'tf3_verify_venus_natural.py', ['--case', case])
                   for case in ['protected', 'opposing', 'disabled', 'facedown'] for i in range(2)]
    elif args.suite == 'venus-jammer':
        JOBS[:] = [(case + str(i), 'tf3_verify_venus_natural.py', ['--case', case])
                   for case in ['jammer_venus', 'jammer_only'] for i in range(4)]
    elif args.suite == 'venus-targeted':
        JOBS[:] = [(case + str(i), 'tf3_verify_venus_targeted.py', ['--case', case])
                   for case in ['protected', 'unprotected'] for i in range(4)]
    elif args.suite == 'venus-phoenix':
        JOBS[:] = [(case + str(i), 'tf3_verify_venus_targeted.py', ['--case', case, '--source', 'phoenix'])
                   for case in ['protected', 'unprotected'] for i in range(4)]
    elif args.suite == 'venus-rod':
        JOBS[:] = [(case + str(i), 'tf3_verify_venus_targeted.py', ['--case', case, '--source', 'rod'])
                   for case in ['protected', 'unprotected'] for i in range(4)]
    elif args.suite == 'venus-immunity':
        JOBS[:] = [(case + str(i), 'tf3_verify_venus_immunity.py', ['--case', case])
                   for case in ['venus', 'baseline'] for i in range(4)]
    elif args.suite == 'jammer-set':
        JOBS[:] = [('set' + str(i), 'tf3_inspect_jammer_set.py', []) for i in range(8)]
    elif args.suite == 'venus-targeted-negative':
        JOBS[:] = [(case + str(i), 'tf3_verify_venus_targeted.py', ['--case', case])
                   for case in ['protected', 'unprotected', 'disabled', 'opposing'] for i in range(2)]
    elif args.suite == 'venus-trap':
        JOBS[:] = [(case + str(i), 'tf3_verify_venus_trap.py', ['--case', case])
                   for case in ['protected', 'unprotected'] for i in range(4)]
    if args.hand_search and args.suite != 'hand-search':
        JOBS.extend([('search-'+name,script,extra) for name,script,extra in HAND_SEARCH_JOBS])
    if args.field_search:
        JOBS.append(('field-matrix','tf3_verify_field_search.py',['--case','matrix']))
        JOBS.extend([(f'field-{cid}-{case}','tf3_verify_field_search.py',['--card',str(cid),'--case',case])
                     for cid in (7105,7109,7110) for case in ('absent','present','lost','negated','set')])
        JOBS.extend([(f'field-search-{cid}'+('-missing' if missing else ''),'tf3_verify_hand_search.py',
                      [str(cid)]+(['--missing'] if missing else []))
                     for cid in (7105,7109,7110) for missing in (False,True)])
    if args.idaten:
        JOBS.append(('ritual-revival', 'tf3_verify_ritual_revival.py', []))
        JOBS.extend([(f'reborn-{cid}', 'tf3_verify_reborn.py', ['--card', str(cid)]) for cid in (6846, 7809, 5883)])
        JOBS.append(('reborn-idaten-negated', 'tf3_verify_reborn.py', ['--card', '6846', '--negated']))
        JOBS.extend([(f'idaten-{case}', 'tf3_verify_ritual.py', ['--case', 'play', '--card', '6850', '--monster', '6846', '--idaten-effect', case]) for case in ('recover', 'negated')])
    if args.cyber_ritual:
        JOBS.append(('cyber-matrix', 'tf3_verify_ritual.py', ['--case', 'matrix', '--cyber']))
        JOBS.extend([(f'cyber-play-{monster}', 'tf3_verify_ritual.py', ['--case', 'play', '--card', '6850', '--monster', str(monster)]) for monster in (6845, 6846, 6847)])
        JOBS.extend([(f'cyber-choice-{monster}', 'tf3_verify_ritual.py', ['--case', 'play', '--card', '6850', '--monster', str(monster), '--choices']) for monster in (6845, 6846, 6847)])
    if args.ritual:
        JOBS.append(('ritual-matrix', 'tf3_verify_ritual.py', ['--case', 'matrix']))
        JOBS.extend([(f'ritual-play-{cid}', 'tf3_verify_ritual.py', ['--case', 'play', '--card', str(cid)]) for cid in (6997, 6998, 4676, 4703)])
    if args.chrysalis:
        JOBS.append(('chrysalis-matrix', 'tf3_verify_chrysalis.py', ['--case', 'matrix']))
        JOBS.extend([(f'chrysalis-{cid}-{case}', 'tf3_verify_chrysalis.py', ['--card', str(cid), '--case', case])
                     for cid in (7155,7156,7157,7172,7173,6764) for case in ('deck','hand')])
        JOBS.extend([(f'chrysalis-{case}', 'tf3_verify_chrysalis.py', ['--case', case])
                     for case in ('missing','no-field','opposing','negated','blocked','lost-field')])
        JOBS.append(('chrysalis-native-lost-field', 'tf3_verify_chrysalis.py', ['--card','6764','--case','lost-field']))
    if args.summon_batch:
        JOBS.append(('summon-batch', 'tf3_verify_summon_batch.py', []))
        JOBS.extend([('vanity-' + str(cid), 'tf3_verify_vanity_play.py', [str(cid)]) for cid in (6881, 6888)])
    if args.flip_review:
        from tf3_verify_flip_batch import CASES
        JOBS.extend([(case, 'tf3_verify_flip_batch.py', ['--case', case]) for case in CASES])
        if not args.summon_batch:
            JOBS.append(('summon-batch', 'tf3_verify_summon_batch.py', []))
    if args.effigy:
        JOBS.append(('effigy-matrix', 'tf3_verify_effigy.py', ['--case', 'matrix']))
        JOBS.extend([(f'effigy-{cid}-{case}', 'tf3_verify_effigy.py', ['--card', str(cid), '--case', case])
                     for cid in (7213, 7214, 7367, 7368) for case in ('summon', 'set-source', 'effect-target', 'native', 'native-effect-target')])
    if args.battle_guard:
        JOBS.extend([(f'guard-{cid}-{case}', 'tf3_verify_battle_guard.py', ['--card', str(cid), '--case', case])
                     for cid in (7686,) for case in ('protect', 'attack', 'below', 'negated', 'destroy', 'send')])
        JOBS.append(('guard-native', 'tf3_verify_battle_guard.py', ['--card', '7686', '--case', 'protect', '--native']))
    if args.light_bonus:
        JOBS.append(('light-bonus-matrix','tf3_verify_light_bonus.py',['--case','matrix']))
        JOBS.extend([(f'light-bonus-{cid}-{case}','tf3_verify_light_bonus.py',['--card',str(cid),'--case',case])
                     for cid in (7541,7542) for case in ('light','wrong','negated')])
        JOBS.append(('light-bonus-native','tf3_verify_light_bonus.py',['--case','native']))
    if args.role_bonus:
        JOBS.extend([('role-bonus-'+case,'tf3_verify_role_bonus.py',['--case',case]) for case in
                     ('matrix','defend','attack','direct','negated','native-defend','native-attack')])
    if args.barriers:
        JOBS.append(('barrier-gates','tf3_verify_barrier_batch.py',['--case','gates']))
        JOBS.append(('barrier-matrix','tf3_verify_barrier_batch.py',['--case','matrix']))
        JOBS.extend([(f'barrier-{cid}-{case}','tf3_verify_barrier_batch.py',['--case',case,'--card',str(cid)]) for cid in range(6882,6888) for case in ('play','inherent')])
        JOBS.extend([(f'barrier-vanity-{cid}','tf3_verify_barrier_batch.py',['--case','inherent','--card',str(cid)]) for cid in (6881,6888)])
        JOBS.extend([(f'barrier-cpu-{cid}','tf3_verify_barrier_batch.py',['--case','cpu','--card',str(cid)]) for cid in (6882,6887)])
    if args.dimikles:
        JOBS.extend([('dimikles-' + case, 'tf3_verify_dimikles.py', ['--case', case]) for case in
                     ['predicates', 'plain', 'flip', 'negated', 'after-negated', 'native', 'battle', 'modifiers', 'lifecycle']])
    if args.orb:
        JOBS.extend([('orb-' + case, 'tf3_verify_orb.py', ['--case', case]) for case in
                     ['mixed', 'empty', 'negated', 'plain', 'native', 'battle', 'immunity', 'native-immunity']])
    if len({job[0] for job in JOBS}) != len(JOBS):
        parser.error('Suite case names must be unique before preparing packages')
    if args.cases:
        unknown = set(args.cases) - {job[0] for job in JOBS}
        if unknown:
            parser.error('Unknown case names: ' + ', '.join(sorted(unknown)))
        JOBS[:] = [job for job in JOBS if job[0] in args.cases]
    if not args.tag.replace('_', '').replace('-', '').isalnum():
        parser.error('tag must contain only letters, numbers, underscores or hyphens')
    template = args.exe.resolve()
    if not template.is_file() or template.parent == ROOT:
        parser.error('exe must be an existing isolated test executable')
    output = ROOT / 'gen' / ('parallel_' + args.tag)
    output.mkdir(exist_ok=False)
    # Freeze the tested build once. Later edits to a candidate must never make
    # one batch silently contain several DLL/data versions.
    mod_snapshot=output/'_mods'
    shutil.copytree(args.mods.resolve(),mod_snapshot)
    fingerprints={str(p.relative_to(mod_snapshot)).replace('\\','/'):hashlib.sha256(p.read_bytes()).hexdigest()
                  for p in mod_snapshot.rglob('*') if p.is_file() and
                  (p.name=='plugin.dll' or p.parent.name=='TF3Cards' and p.name in
                   ('ported.txt',*RULE_FILES))}
    started = time.monotonic()
    # Complete all package copies before any process can modify a copied save/cache.
    packages = []
    for name, _, _ in JOBS:
        package = output / name
        shutil.copytree(template.parent, package,
                        ignore=shutil.ignore_patterns('jogo', 'ms0', 'logs', 'texcache', '__pycache__'))
        packages.append(package)
    print(f'Prepared {len(JOBS)} independent packages; launching {len(JOBS)} game suites with {args.workers} workers.', flush=True)

    def run(index):
        name, script, extra = JOBS[index]
        tag = args.tag + '_' + name
        env = dict(os.environ, TF_TAG=tag, TF_SAVE_FROM=str(ROOT / 'ms0'),
                   TF_MODS_FROM=str(mod_snapshot), TF_RES='1',
                   TF_SEGUNDO_PLANO='1', NO_AUDIO='1',
                   TF_TEST_DATA=str((ROOT / os.environ['TF_TEST_DATA']).resolve()) if os.environ.get('TF_TEST_DATA') else str(template.parent / 'jogo'),
                   TF_TEST_EXE=str(packages[index] / template.name))
        begin = time.monotonic()
        with (output / (name + '.log')).open('w', encoding='utf-8') as log:
            process = subprocess.Popen([sys.executable, str(ROOT / 'tools' / 'test' / script), *extra],
                                       cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT,
                                       creationflags=subprocess.CREATE_NO_WINDOW)
            print(f'{name}: started controller {process.pid}', flush=True)
            code = process.wait()
        result = dict(name=name, tag=tag, exit_code=code, seconds=round(time.monotonic() - begin, 1))
        print(json.dumps(result), flush=True)
        return result

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
        results = list(pool.map(run, range(len(JOBS))))
    summary = dict(seconds=round(time.monotonic() - started, 1), results=results,mod_sha256=fingerprints,
                   passed=all(item['exit_code'] == 0 for item in results))
    (output / 'results.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
    # The controllers stop their game in finally; retain evidence, not bulky copies.
    # A remaining pid file means cleanup cannot be assumed safe.
    for (name, _, _), package in zip(JOBS, packages):
        tag = args.tag + '_' + name
        if (ROOT / 'gen' / ('ctl_' + tag) / 'pid').exists():
            print(f'{name}: retaining files because a game may still be running', flush=True)
            continue
        shutil.rmtree(package)
        shutil.rmtree(ROOT / 'gen' / ('plugins_teste_' + tag), ignore_errors=True)
    print(json.dumps(summary), flush=True)
    if all(not (ROOT/'gen'/('ctl_'+args.tag+'_'+name)/'pid').exists() for name,_,_ in JOBS):
        assert mod_snapshot.resolve().parent==output.resolve()
        shutil.rmtree(mod_snapshot)
    return 0 if summary['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
