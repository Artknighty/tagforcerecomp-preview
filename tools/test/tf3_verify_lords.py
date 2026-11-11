"""Duel checks of the Lords ("must be Tribute Summoned by Tributing 3 <Type>-Type monsters") through tf3_lote.py, hidden, one isolated duel per case.

Monsters are poked into your zones (game ids, see LORDS; Sangan 4054 = Fiend is the wrong Type for all six);
the Lord is forced into the hand. The plugin debug log (TF3_DEBUG_SLIFER=1) names each pick and its Type.
  ok-<id>  three Tributes of the right Type: Summon asks for 3 Tributes, all 3 go to the Graveyard, the Lord lands
  wrong-<id> a Fiend first, then three right Tributes: the Fiend pick is refused (log) and the Fiend stays on the field, the 3 right ones are Tributed
  few-<id> two right Tributes + a Fiend: the hand menu offers no Summon / Set, nothing moves
Run (no -I): TF_TEST_EXE=<absolute full package exe> TF_TEST_DATA=<absolute game data> python tools/test/tf3_verify_lords.py [--mods DIR] [case ...]
"""
import glob
import json
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MODS = os.environ.get('TF_MODS_FROM', 'gen/tf3_lords_mods')
SIDE0 = 0x09f79d1c   # first monster zone of side 0
FIEND = 4054   # Sangan: the wrong Type for every Lord
# TF3 id -> native Tribute monster of its Type. Native ids: Dark Magician Spellcaster, Celtic Guardian Warrior, Griffore Beast, Oscillo Hero #2 Thunder,
# Spirit of the Harp Fairy, Ground Attacker Bugroth Machine.
LORDS = {6958: 4041, 6959: 4047, 7374: 4052, 7375: 4051, 7992: 4176, 7993: 4281}


def zone(addr, card):   # poke words that put a face-up monster into a field zone (entry address, game id)
    return 'poke:%08x:%s poke:%08x:0100 poke:%08x:80000000' % (addr, (card | 0x800000).to_bytes(4, 'little').hex(), addr + 8, addr + 16)


def start(cards):
    return ('w300 estado:ini ' + ' '.join(zone(SIDE0 + 20 * k, c) for k, c in enumerate(cards)) +
            ' w30 DOWN w40 DOWN w40 LEFT w30 LEFT w30 LEFT w30 LEFT w30 LEFT w30 X w100 ')


SUMMON = 'X w300 X w300 '   # Summon, YES
OK_ROT = SUMMON + 'X w300 X w300 X w300 w600 estado:fim'
# the cursor is physical: after a refused pick it stays on the Fiend (slot 0), after a good one it returns to slot 0, so each good pick needs one more RIGHT
WRONG_ROT = SUMMON + 'X w300 RIGHT w200 X w400 RIGHT w200 RIGHT w200 X w400 RIGHT w200 RIGHT w200 RIGHT w200 X w400 w600 estado:fim'
CASES = {}
for lord, tribute in LORDS.items():
    CASES['ok-%d' % lord] = dict(lord=lord, cards=[tribute] * 3, rot=OK_ROT, log=['lord: pick 3 is side 0 slot', 'god: third Tribute'], grave_n=3)
    CASES['wrong-%d' % lord] = dict(lord=lord, cards=[FIEND] + [tribute] * 3, rot=WRONG_ROT, log=['Type 3 (needs'], grave_n=3, grave_has_not=FIEND)
    CASES['few-%d' % lord] = dict(lord=lord, cards=[tribute, tribute, FIEND], rot='X w300 X w300 w300 estado:fim', log=[], grave_n=0, hand_same=True)


def run(name, case, mods):
    env = dict(os.environ, TF_MODS_FROM=mods, TF3_DEBUG_SLIFER='1', TF_SEGUNDO_PLANO='1', NO_AUDIO='1')
    subprocess.run([sys.executable, 'tools/test/tf3_lote.py', '-j', '1', '--mods', mods, '--rot', start(case['cards']) + case['rot'], str(case['lord'])],
                   cwd=ROOT, env=env, capture_output=True, text=True, timeout=240)
    logs = sorted(glob.glob(os.path.join(ROOT, 'gen', 'logs_teste_l%d' % case['lord'], 'sessao_*.log')), key=os.path.getmtime)
    text = open(logs[-1], encoding='utf-8', errors='replace').read() if logs else ''
    state = json.load(open(os.path.join(ROOT, 'gen', 'lote', str(case['lord']), 'state.json'), encoding='utf-8'))
    ok = all(s in text for s in case['log'])
    grave = state['fim']['me']['grave']
    ok = ok and len(grave) - len(state['ini']['me']['grave']) == case['grave_n']
    if case.get('hand_same'):
        ok = ok and state['fim']['me']['n_hand'] == state['ini']['me']['n_hand']
    if case.get('grave_has_not'):
        ok = ok and case['grave_has_not'] not in grave
    print('PASS' if ok else 'FAIL', name, flush=True)
    return ok


if __name__ == '__main__':
    args = sys.argv[1:]
    mods = MODS
    if args[:1] == ['--mods']:
        mods, args = args[1], args[2:]
    wanted = args or list(CASES)
    results = [run(n, CASES[n], mods) for n in wanted]
    sys.exit(0 if all(results) else 1)
