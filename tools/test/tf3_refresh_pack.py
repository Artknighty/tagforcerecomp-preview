"""Rewrite the data files of a test copy of the TF3 Cards mod from the current specs (effects / trigger / stat / amount / destroy / summon / tribute .bin and the `like` column of
ported.txt), as tools/test/tf3_lote.py does for its own packs.  Stale files make the engine suites fail (e.g. no LVL1 trailer).

  python tools/test/tf3_refresh_pack.py gen/tf3sum
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'mods')); sys.path.insert(0, os.path.join(ROOT, 'tools', 'game'))
import tf3_efeitos_param as P

pack = os.path.join(sys.argv[1], 'TF3Cards')
rows = [l.split(' ', 3) for l in open(os.path.join(pack, 'ported.txt'), encoding='utf-8').read().splitlines() if l.strip()]
gids = {int(r[1]): int(r[0]) for r in rows}
outputs, likes = P.rule_outputs(gids)
for name, data in outputs:
    open(os.path.join(pack, name), 'wb').write(data)
    print(name, len(data))
with open(os.path.join(pack, 'ported.txt'), 'w', encoding='utf-8', newline='\n') as f:
    for r in rows:
        f.write('%s %s %d %s\n' % (r[0], r[1], likes.get(int(r[1]), int(r[2])), r[3]))
