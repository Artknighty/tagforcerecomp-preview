"""Build a read-only TF3 review queue; card text stays in the local gen directory.

This is triage, never effect certification. Native handler equality and short text
do not prove a donor implements a card. No package or rule files are changed.
"""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/game'))
from tf3_efeitos_texto import PADROES, filtro


def normalize(text):
    return re.sub(r'\s+', ' ', text or '').strip()


def rule_content(name, data):
    """Compare keyed tables as the runtime loads them; preserve target ordering.

    Row order in effects/trigger files is irrelevant to their native map loaders.
    Reject duplicates/truncation instead of hiding potentially effective changes.
    Other formats retain byte-for-byte comparison.
    """
    if name not in ('effects.bin', 'trigger.bin'):
        return data
    position, tables = 0, {}
    while position < len(data):
        if position + 8 > len(data):
            raise ValueError('Truncated rule table: ' + name)
        magic, count = data[position:position+4], struct.unpack_from('<I', data, position+4)[0]
        position += 8
        allowed = (b'TF3E',) if name == 'effects.bin' else (b'TF3T', b'TGC1', b'TGR1', b'TRK1')
        if magic not in allowed or magic in tables or count > 65536 or (not tables and magic != allowed[0]):
            raise ValueError('Invalid rule table: ' + name)
        rows = {}
        for _ in range(count):
            size = 8 if name == 'effects.bin' else 4
            if position + size > len(data):
                raise ValueError('Truncated rule row: ' + name)
            if name == 'effects.bin':
                cid, zone, extra, targets = struct.unpack_from('<HBBI', data, position)
                position += size
                if targets > 65536 or position + 2*targets > len(data):
                    raise ValueError('Truncated rule targets: ' + name)
                value = (zone, extra, struct.unpack_from('<%dH' % targets, data, position))
                position += 2*targets
            else:
                cid, value = struct.unpack_from('<HH', data, position)
                position += size
            if cid in rows:
                raise ValueError('Duplicate rule source: ' + name)
            rows[cid] = value
        tables[magic] = rows
    if not tables:
        raise ValueError('Empty rule file: ' + name)
    return tables


def family(card):
    """Only full-clause matches count as existing text-pattern candidates."""
    text = normalize(card.get('text_tf3'))
    for kind, pattern, donor, zone in PADROES:
        match = re.fullmatch(pattern, text)
        if kind == card['kind'] and match:
            target = filtro(match.group('f'))
            if target is not None:
                return 'existing_search_pattern', dict(like=donor, zone=zone, filter=target)
    # These require new work: keep them distinct from supported patterns.
    if re.fullmatch(r'Activate only if you control a "[^"]+" Equip Spell Card\. Draw \d+ cards\.', text):
        return 'conditional_draw_extension', None
    if re.fullmatch(r'If this card attacks or is attacked by a (LIGHT|DARK|WATER|FIRE|EARTH|WIND) monster, it gains \d+ ATK during the Damage Step only\.', text):
        return 'battle_attribute_bonus_extension', None
    if re.fullmatch(r'FLIP: This card gains \d+ ATK and DEF\.', text):
        return 'flip_stat_memory_extension', None
    if text.startswith('FLIP:'):
        return 'flip_manual_review', None
    return 'manual_review', None


def build(cards, raw, ported, rules, reviews, excluded=()):
    old_names = {c['name'].casefold() for c in cards if c.get('tf1_id')}
    old_codes = {c['passcode'] for c in cards if c.get('tf1_id') and c.get('passcode')}
    rows, issues = [], []
    for card in cards:
        cid = card.get('tf3_id')
        if cid not in ported:
            continue
        category = (raw[cid]['w1'] >> 1) & 31
        duplicate = bool(card.get('tf1_id') or card['name'].casefold() in old_names or
                         (card.get('passcode') and card['passcode'] in old_codes))
        group, proposal = family(card)
        blockers = []
        if duplicate:
            blockers.append('existing_tf1_card')
        if cid in excluded:
            blockers.append('explicit_exclusion_roster')
        if category in (15, 16, 17, 18):
            blockers.append('unsupported_tuner_synchro_category')
        elif category != card['category'] and not (category == 11 and card['category'] == 1 and ((raw[cid]['w1'] >> 17) & 31) == 22):
            blockers.append('unexpected_category_conversion')
        if category == 10:
            blockers.append('token')
        if cid in rules:
            state = 'existing_rule_review'
        elif blockers:
            state = 'blocked'
        elif category == 0:
            state = 'normal_metadata_review'
        else:
            state = group
        if blockers:
            issues.append(dict(id=cid, name=card['name'], blockers=blockers,
                               raw_category=category, converted_category=card['category'], has_rule=cid in rules))
        review = reviews.get(cid, {})
        checks = review.get('checklist', {})
        pending = [key for key in ('clauses', 'playability', 'activation', 'costs_targets', 'resolution', 'negation', 'identity_interactions')
                   if not isinstance(checks.get(key), dict) or checks[key].get('status') != 'passed' or not checks[key].get('evidence')]
        rows.append(dict(id=cid, duel_id=ported[cid], name=card['name'], state=state,
                         family=group, blockers=blockers, raw_category=category,
                         text=card.get('text_tf3'), proposal=proposal, rule_sources=rules.get(cid, []),
                         open_checks=pending if cid in rules else [], review_status=review.get('status')))
    order = {'existing_search_pattern': 0, 'conditional_draw_extension': 1,
             'battle_attribute_bonus_extension': 2, 'flip_stat_memory_extension': 3,
             'flip_manual_review': 4, 'existing_rule_review': 5,
             'normal_metadata_review': 6, 'manual_review': 7, 'blocked': 8}
    rows.sort(key=lambda r: (bool(r['blockers']), order[r['state']], r['id']))
    return dict(summary=dict(imported=len(rows), with_rules=sum(bool(r['rule_sources']) for r in rows),
                             states=dict(Counter(r['state'] for r in rows)), blocked=len(issues),
                             exclusion_ids=len(set(excluded)), fully_reviewed=sum(bool(r['rule_sources']) and not r['open_checks'] and not r['blockers'] for r in rows)),
                issues=issues, queue=rows,
                limitations=['TF3-to-TF1 scope includes TF2-era cards; no separate TF2 exclusion is applied.',
                             'Pattern proposals require donor, timing, targets, resolution and negation validation.',
                             'Raw Tuner/Synchro categories block triage even when converted metadata says Normal.',
                             'Existing-rule counts do not certify full card behavior.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pack', type=Path, default=ROOT / 'mods/TF3Cards')
    parser.add_argument('--review', type=Path, default=ROOT / 'gen/tf3_review101.json')
    parser.add_argument('--exclude', type=Path, help='JSON list of TF3 IDs already covered elsewhere or excluded by policy')
    parser.add_argument('--out', type=Path, default=ROOT / 'gen/tf3_review_queue.json')
    args = parser.parse_args()
    # All output contains local card metadata; do not accidentally write it into source.
    if not args.out.resolve().is_relative_to((ROOT / 'gen').resolve()):
        parser.error('--out must stay inside gen/')
    import mapa_cartas
    import tf3_cartas
    from tf3_legacy_effects import EFEITO
    ported = {}
    for line in (args.pack / 'ported.txt').read_text(encoding='utf-8').splitlines():
        gid, cid, _donor, _name = line.split(' ', 3)
        if int(cid) in ported:
            raise ValueError('Duplicate TF3 ID in ported.txt')
        ported[int(cid)] = int(gid)
    if len(set(ported.values())) != len(ported):
        raise ValueError('Duplicate duel ID in ported.txt')
    rules = defaultdict(list)
    mechanisms = defaultdict(set)
    for cid in EFEITO:
        rules[cid].append('legacy')
        mechanisms['legacy'].add(cid)
    for name in ('tf3_efeitos.json', 'tf3_efeitos_auto.json', 'tf3_stats.json', 'tf3_stats_auto.json'):
        for cid, spec in json.loads((Path(__file__).parent / name).read_text(encoding='utf-8')).items():
            rules[int(cid)].append(name)
            if 'stats' in name:
                mechanisms['stats:' + spec.get('scope', 'self')].add(int(cid))
            elif 'zone' in spec:
                mechanisms['search:' + spec['zone']].add(int(cid))
            else:
                mechanisms['donor_record'].add(int(cid))
            for key in ('draw', 'lp', 'destroy', 'trigger_like', 'trigger_record_like', 'trigger_record_kind', 'trigger_condition', 'control_lock', 'attack_protect', 'battle_guard', 'special_summon_lock', 'cannot_special_summon', 'double_tribute_normal'):
                if key in spec:
                    mechanisms[key].add(int(cid))
    reviews = {r['id']: r for r in json.loads(args.review.read_text(encoding='utf-8'))} if args.review.exists() else {}
    excluded = json.loads(args.exclude.read_text(encoding='utf-8')) if args.exclude else []
    if not isinstance(excluded, list) or any(type(cid) is not int for cid in excluded):
        parser.error('--exclude must contain a JSON list of integer TF3 IDs')
    raw = tf3_cartas.carregar()
    installed = []
    for lang in 'efgis':
        data = (args.pack / f'ext_info_{lang}.bin').read_bytes()
        if data[:4] != b'TF3I' or struct.unpack_from('<I', data, 4)[0] != len(ported):
            raise ValueError(f'Invalid imported metadata header: {lang}')
        for index, (cid, gid) in enumerate(ported.items()):
            actual = struct.unpack_from('<II', data, 20 + index * 8)
            w0, w1 = tf3_cartas.para_tf1(raw[cid]['w0'], raw[cid]['w1'])
            if actual != ((w0 & ~0x1fff) | gid, w1 & ~(7 << 20)):
                raise ValueError(f'Installed metadata differs from conversion: {lang}, card {cid}')
        installed.append(lang)
    from tf3_efeitos_param import rule_outputs
    checked_rule_files = []
    for name, expected in rule_outputs(ported)[0]:
        path = args.pack / name
        if not path.exists() and struct.unpack_from('<I', expected, 4)[0] == 0:
            continue
        if not path.is_file() or rule_content(name, path.read_bytes()) != rule_content(name, expected):
            raise ValueError(f'Installed {name} differs from current rule specs; audit the candidate pack instead of counting uninstalled rules')
        checked_rule_files.append(name)
    report = build(mapa_cartas.montar()['cards'], raw, ported, rules, reviews, excluded)
    report['installed_rule_files_checked'] = checked_rule_files
    report['installed_property_languages_checked'] = installed
    report['mechanism_groups'] = {name: sorted(ids.intersection(ported)) for name, ids in sorted(mechanisms.items())}
    if report['summary']['imported'] != len(ported):
        raise ValueError('Imported IDs missing from the cross-game map')
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report['summary'], indent=2))
    print('Local queue:', args.out.resolve().relative_to(ROOT))


if __name__ == '__main__':
    main()
