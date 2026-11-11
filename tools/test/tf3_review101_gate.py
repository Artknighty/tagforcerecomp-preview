"""Track the original 101-card review and the explicitly selected simple expansion.

The roster check alone says nothing about effect correctness. To audit completion,
pass --require-complete --review <local review JSON>. Evidence must be attached to
each checklist item; do not turn historical scenario passes into full-card claims.
"""
import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'mods'))
from tf3_legacy_effects import EFEITO

CHECKS = ('clauses', 'playability', 'activation', 'costs_targets', 'resolution', 'negation', 'identity_interactions')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--review', type=Path)
    parser.add_argument('--require-complete', action='store_true')
    args = parser.parse_args()
    frozen_list = json.loads(Path(__file__).with_name('tf3_review101_ids.json').read_text())
    frozen = set(frozen_list)
    if len(frozen_list) != 101 or len(frozen) != 101:
        raise SystemExit('Invalid frozen 101-card roster.')
    expansion_list = json.loads(Path(__file__).with_name('tf3_simple_expansion_ids.json').read_text())
    expansion = set(expansion_list)
    if len(expansion) != len(expansion_list) or expansion.intersection(frozen):
        raise SystemExit('Invalid simple-effect expansion roster.')
    frozen.update(expansion)
    known = set(EFEITO)
    for name in ('tf3_efeitos.json', 'tf3_efeitos_auto.json', 'tf3_stats.json', 'tf3_stats_auto.json'):
        known.update(map(int, json.loads((ROOT / 'tools' / 'mods' / name).read_text(encoding='utf-8'))))
    if known != frozen:
        print(json.dumps(dict(extra=sorted(known - frozen), missing=sorted(frozen - known))))
        return 1
    print(f'PASS: rule roster has the original 101 cards and {len(expansion)} selected simple additions.')
    if not args.require_complete:
        return 0
    if args.review is None:
        parser.error('--require-complete needs --review; completion cannot be inferred from specs.')
    review = json.loads(args.review.read_text(encoding='utf-8'))
    by_id = {c['id']: c for c in review}
    if len(review) != len(frozen) or set(by_id) != frozen:
        raise SystemExit('Review must contain each original and added card exactly once.')
    pending = []
    for cid in sorted(frozen):
        card = by_id[cid]
        for check in CHECKS:
            item = card.get('checklist', {}).get(check)
            if not isinstance(item, dict) or item.get('status') != 'passed' or not item.get('evidence'):
                pending.append((cid, check))
    print(f'Completion gate: {len(set(cid for cid, _ in pending))} cards still have open review items.')
    return 1 if pending else 0


if __name__ == '__main__':
    sys.exit(main())
