"""Audit trigger/resolution donor record compatibility using the user's TF1 data.

This checks native table presence, not timing, callback semantics or card completion.
No game data is written. Run from a development checkout with extracted game data.
"""
import argparse
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools' / 'mods'), str(ROOT / 'tools' / 'translator')]
import tf3_efeitos_param as P
from pspmod import load_module

TABLES = ((0x09f6be44, 764), (0x09f705fc, 454), (0x09f730a4, 175), (0x09f74124, 213))


def audit(module, specs):
    base = 0x09e6b800
    image = load_module(str(module), base)[0]
    records = {}
    for kind, (address, count) in enumerate(TABLES):
        for index in range(count):
            card = struct.unpack_from('<I', image, address - base + index * 24)[0]
            records.setdefault(card, set()).add(kind)
    checked, issues = [], []
    for card, rule in specs.items():
        if not rule.get('trigger_like'):
            continue
        trigger = rule['trigger_like']
        resolution = rule.get('trigger_record_like', rule['like'])
        trigger_kinds = records.get(trigger, set())
        resolution_kinds = records.get(resolution, set())
        # A donor can have unrelated equip and Graveyard records. Require at
        # least one shared kind; exact event-to-kind coverage needs runtime tests.
        missing = trigger_kinds if not trigger_kinds.intersection(resolution_kinds) else set()
        if 'trigger_record_kind' in rule:
            kind = rule['trigger_record_kind']
            missing = ({1} if 1 not in trigger_kinds else set()) | ({kind} if kind not in resolution_kinds else set())
        item = dict(card=int(card), trigger=trigger, resolution=resolution,
                    trigger_kinds=sorted(trigger_kinds), resolution_kinds=sorted(resolution_kinds),
                    missing_kinds=sorted(missing))
        checked.append(item)
        if not trigger_kinds or missing:
            issues.append(item)
    return dict(checked=len(checked), issues=issues, passed=not issues)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--module', type=Path, default=ROOT / 'jogo/PSP_GAME/USRDIR/gmodule/libduel_eng.prx')
    args = parser.parse_args()
    result = audit(args.module, P._carregar(P.SPEC_AUTO, P.SPEC))
    print(json.dumps(result, indent=2))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
