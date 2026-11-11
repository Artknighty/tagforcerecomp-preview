"""Runs one Tag Force 3 card test per card in parallel (each instance in its own package copy, see TF_TAG in tf.py), reads the duel state from memory and checks it.

Each case forces the card into the opening hand, ends turn 1 so the CPU plays, then runs a script on turn 3 (--rot): activate the first hand card, or summon it and attack.
The state is read with peek (no screenshots): life points, deck size, hand and monster zones of both players, before (`ini`) and after (`fim`) the script.
Results: gen/lote/<card id>/ (state.json, screenshots, the log), gen/lote/folha.png (contact sheet) and one PASS / FAIL line per case when --check is given.

Usage:  python tools/test/tf3_lote.py [-j 8, never above 8: 10 pinned the CPU at 100%] [--mods gen/tf3sum] [--rot ativar|atacar|atacar_sim|<script>]
            [--spec file.json | '{"6960": {...}}'] [--check "fim.me.deck < ini.me.deck"]... [--grave auto|id,id,...] [--hand id,id,...] [--rapido] [--fn 09ebcce8,...] [--debug-fx] ID [ID ...]
  ID           TF3 card id (column 2 of ported.txt) forced into the hand
  --rot        a name of ROTEIROS or a tf.py script (words: X O RIGHT w60 shot:name estado:name ...)
  --spec       temporary effect specs for this run only (a JSON file or inline JSON, same shape as tools/mods/tf3_efeitos.json; {"effects": {...}, "stats": {...}} also accepted);
               every case gets its own pack folder (hard links), the project files are not touched
  --grave      seed your Graveyard before the script: 'auto' = up to 3 original cards that match the card's spec filter, or a list of card ids
  --solo       hand = the forced card (+ the --hand cards, in slots 1..): with the `ativar_r` script (X presses until the state stops changing, no fixed waits)
  --hand       write these card ids over hand slots 3, 4, ... of your hand (slots 0-2 hold the forced card): for discard costs
  --rapido     experimental fast mode (no screenshots): the duel is not the same as in full mode and the waits drift, so the script may not reach turn 3
  --check      python expression over the states ini and fim (attributes: me / op -> lp, deck, hand, n_hand, field); repeatable
               e.g. "fim.op.lp < ini.op.lp"  "fim.me.deck == ini.me.deck - 1"  "fim.me.n_hand > ini.me.n_hand"
TF3_LOTE_FORCE=id,id,...: force these cards (3 copies each, in order) instead of ID alone; ID still names the folders.
Notes: the deck stack at the top left of the screen is the OPPONENT's; `me` is player 0.
"""
import json
import os
import shutil
import struct
import subprocess
import sys
import glob
import threading
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TF = os.path.join(ROOT, 'tools', 'test', 'tf.py')
sys.path.insert(0, os.path.join(ROOT, 'tools', 'mods')); sys.path.insert(0, os.path.join(ROOT, 'tools', 'game'))

# `esperar:turno3` polls the log of the instance for "turno 3 (vez de J0)" (CPUvsCPU plugin monitor): fixed waits drift between fast and full mode.
# The hand cursor starts on the last card and wraps, 6 LEFT presses select slot 0 (the forced card)
# (old note: the hand cursor starts on the last card and wraps, 6 LEFT presses select slot 0 (the forced card)
PRE = "w300 O w60 RIGHT w20 RIGHT w20 RIGHT w20 X w2400 estado:ini shot:d1 DOWN w40 DOWN w40 LEFT w30 LEFT w30 LEFT w30 LEFT w30 LEFT w30 LEFT w30 X w60 shot:d2 "
ROTEIROS = {
    # activate / summon the first hand card and look at the result
    'ativar': PRE + "X w100 shot:d3 X w200 shot:d4 w300 X w200 shot:d5 estado:fim",
    # activate, then answer the selection prompts (OK, pick the first card offered, confirm) a few times; use with --grave auto for Graveyard effects
    'ativar_sel': PRE + "X w100 shot:d3 X w200 shot:d4 X w400 shot:d5 X w300 shot:d6 X w300 shot:d7 estado:fim",
    # same as ativar_sel, dumping the hand / Graveyard words of player 0 before and after (research aid)
    'ativar_dump': PRE + "peek:09f79e0c:30 peek:09f7a3fc:20 peek:09f79cec:30 X w100 shot:d3 X w200 shot:d4 X w400 shot:d5 X w300 shot:d6 X w300 shot:d7 peek:09f79e0c:30 peek:09f7a3fc:20 peek:09f79cec:30 estado:fim",
    # Set the first hand card (a Trap), end the turn (phase bar: RIGHT twice = End Phase), answer YES to the chain prompt of the opponent's attack on turn 4
    'armadilha': PRE + "X w300 shot:t1 O w60 RIGHT w60 RIGHT w60 X w200 w3000 shot:t5 X w400 shot:t6 X w400 shot:t7 X w400 shot:t8 w600 estado:fim",
    # same, then answers the selection prompts of a Graveyard / Deck effect (OK, pick the first card, confirm) and ends with the state
    'armadilha_sel': PRE + "X w300 shot:t1 O w60 RIGHT w60 RIGHT w60 X w200 w3000 shot:t5 X w400 X w400 shot:t7 X w500 shot:t8 X w500 shot:t9 X w500 shot:t10 X w500 X w700 shot:t11 X w500 w900 estado:fim",
    # Set the first hand card (a monster: DOWN picks Set), end the turn, let the opponent attack it, answer YES to the FLIP prompt
    # one card in hand (--solo) and `resolver` (tf.py): presses X until the state stops changing; no fixed waits, so the same script fits any Spell
    'ativar_r': PRE + "X w100 resolver shot:d9 estado:fim",
    'virar': PRE + "DOWN w30 X w300 shot:t1 O w60 RIGHT w60 RIGHT w60 X w200 w3000 shot:t5 X w400 shot:t6 X w400 shot:t7 X w400 shot:t8 w600 estado:fim",
    # summon it, Battle Phase, attack the first target; stops at the "Activate the effect?" prompt if there is one
    'atacar': PRE + "X w300 O w60 RIGHT w20 X w100 X w100 X w100 X w700 shot:d3 estado:fim",
    # same, answers YES, picks the first card / position offered and lets the effect finish
    'atacar_sim': PRE + "X w300 O w60 RIGHT w20 X w100 X w100 X w100 X w700 shot:d3 X w300 w1000 shot:d4 X w400 w400 shot:d5 estado:fim",
}
SOLO = False   # --solo: the forced card plus the --hand cards only (hand slots 1.. instead of 3..)
JOG, PASSO, CASAS = 0x09f79cec, 0xfac, 0x09f79d1c   # player block (life points first), size of a block, first card zone


class Lado:
    def __init__(self, w):
        self.lp = struct.unpack_from('<i', w, 0)[0]
        self.n_hand = struct.unpack_from('<I', w, 12)[0]
        self.deck = struct.unpack_from('<I', w, 16)[0]
        self.hand = [struct.unpack_from('<I', w, 0x120 + i * 4)[0] & 0x1fff for i in range(min(self.n_hand, 12))]
        self.field = [(struct.unpack_from('<I', w, 0x30 + s * 0x14)[0] & 0x1fff, struct.unpack_from('<I', w, 0x34 + s * 0x14)[0]) for s in range(5)]
        self.field = [f for f in self.field if f[0]]
        self.n_grave = struct.unpack_from('<I', w, 20)[0]
        self.grave = [struct.unpack_from('<I', w, 0x710 + i * 4)[0] & 0x1fff for i in range(min(self.n_grave, 12))]

    def dict(self):
        return {'lp': self.lp, 'deck': self.deck, 'n_hand': self.n_hand, 'hand': self.hand, 'field': self.field, 'grave': self.grave}


class Estado:
    def __init__(self, me, op):
        self.me, self.op = me, op

    def dict(self):
        return {'me': self.me.dict(), 'op': self.op.dict()}


def esperar_log(env, texto, max_s=600):
    """Runs 60-frame steps until the session log of this TF_TAG contains `texto`."""
    import time
    t0 = time.time()
    while time.time() - t0 < max_s:
        logs = sorted(glob.glob(os.path.join(ROOT, 'gen', 'logs_teste_' + env['TF_TAG'], '*.log')), key=os.path.getmtime)
        if logs and texto in open(logs[-1], encoding='utf-8', errors='replace').read():
            return True
        subprocess.run([sys.executable, TF, 'run', 'w300'], env=env, capture_output=True)
    return False


def ler_estado(env):
    """Reads both players from the running instance of this TF_TAG."""
    lados = []
    for p in (0, 1):
        o = subprocess.run([sys.executable, TF, 'peek', '%08x' % (JOG + p * PASSO), '2048'], env=env, capture_output=True, text=True).stdout.split()
        lados.append(Lado(bytes.fromhex(o[-1])))
    return Estado(*lados)


_trava = threading.Lock()   # the spec loader reads TF3_SPEC_EXTRA from the environment


def pacote_do_caso(cid, mods, spec):
    """A pack folder for one case: hard links to the big files, fresh copies of the data rewritten with the temporary specs."""
    import tf3_efeitos_param as P
    from tf3_porte_carta import EFEITO
    dest = os.path.join(ROOT, 'gen', 'lotepack', str(cid)); shutil.rmtree(dest, ignore_errors=True)
    src = os.path.join(ROOT, mods)
    reescritos = set(P.RULE_FILES) | {'ported.txt'}
    for raiz, _, arqs in os.walk(src):
        rel = os.path.relpath(raiz, src); os.makedirs(os.path.join(dest, rel), exist_ok=True)
        for a in arqs:
            s, d = os.path.join(raiz, a), os.path.join(dest, rel, a)
            if rel == os.path.join('TF3Cards') and a in reescritos:
                continue
            try:
                os.link(s, d)
            except OSError:
                shutil.copy2(s, d)
    pack = os.path.join(dest, 'TF3Cards'); os.makedirs(pack, exist_ok=True)
    extra = os.path.join(dest, 'extra.json'); json.dump(spec, open(extra, 'w', encoding='utf-8'))
    _trava.acquire(); os.environ['TF3_SPEC_EXTRA'] = extra
    ported = os.path.join(src, 'TF3Cards', 'ported.txt')
    rows = [l.split(' ', 3) for l in open(ported, encoding='utf-8').read().splitlines() if l.strip()]
    gids = {int(r[1]): int(r[0]) for r in rows}
    outputs, likes = P.rule_outputs(gids)
    for nome, dados in outputs:
        open(os.path.join(pack, nome), 'wb').write(dados)
    with open(os.path.join(pack, 'ported.txt'), 'w', encoding='utf-8', newline='\n') as f:
        for r in rows:
            c = int(r[1]); f.write('%s %s %d %s\n' % (r[0], r[1], likes.get(c, EFEITO.get(c, 0)), r[3]))
    os.environ.pop('TF3_SPEC_EXTRA', None); _trava.release()
    return os.path.relpath(dest, ROOT)


def poke_cemiterio(cid, mods, spec, grave):
    """--grave: script words that seed player 0's Graveyard (count at +20, ids at +0x710 of the player block). 'auto' = up to 3 original cards
    that match the filter of the card's spec (the new TF3 cards when no original matches), otherwise a comma list of ids."""
    if grave == 'auto':
        import tf3_efeitos_param as P
        rows = [l.split(' ', 3) for l in open(os.path.join(ROOT, mods, 'TF3Cards', 'ported.txt'), encoding='utf-8').read().splitlines() if l.strip()]
        gids = {int(r[1]): int(r[0]) for r in rows}
        e = ((spec or {}).get('effects') or {}).get(str(cid)) or json.load(open(os.path.join(ROOT, 'tools', 'mods', 'tf3_efeitos.json'), encoding='utf-8')).get(str(cid)) or {}
        prop = P.propriedades(gids)
        ids = sorted(i for i, p in prop.items() if i not in gids.values() and P.combina(p, e.get('filter', {'monster': True})))[:3]
        if not ids:   # no original card matches (e.g. Psychic monsters): use the new TF3 cards
            ids = sorted(i for i, p in prop.items() if i in gids.values() and P.combina(p, e.get('filter', {'monster': True})))[:3]
    else:
        ids = [int(x) for x in grave.split(',')]
    if not ids:
        return ''
    words = ['poke:%08x:%s' % (JOG + 20, struct.pack('<I', len(ids)).hex())]
    words += ['poke:%08x:%s' % (JOG + 0x710 + 4 * k, struct.pack('<I', i).hex()) for k, i in enumerate(ids)]
    return ' '.join(words) + ' '


def um(cid, mods, extra, rot, spec, checks, shots, grave=None, mao=None):
    tag = 'l%s' % cid
    if mao:   # --hand: ids written over hand slots 3, 4, ... (slots 0-2 hold the forced card)
        words = ['poke:%08x:%s' % (JOG + 0x120 + 4 * ((1 if SOLO else 3) + k), struct.pack('<I', int(i)).hex()) for k, i in enumerate(mao.split(','))]
        rot = rot.replace('estado:ini', ' '.join(words) + ' estado:ini', 1)
    if grave:
        rot = rot.replace('estado:ini', poke_cemiterio(cid, mods, spec, grave) + 'estado:ini', 1)
    if spec:
        mods = pacote_do_caso(cid, mods, spec)
    env = dict(os.environ, TF_TAG=tag, TF3_FORCE_CARDS=os.environ.get('TF3_LOTE_FORCE', str(cid)), TF3_FORCE_COPIES='3', TF3_FORCE_DECK='0', TF_MODS_FROM=mods, TF_RNG_SEED='1', **extra)   # fixed shuffle (CPUvsCPU plugin in the mods folder, switched off): the same duel every time
    out = os.path.join(ROOT, 'gen', 'lote', str(cid)); shutil.rmtree(out, ignore_errors=True); os.makedirs(out, exist_ok=True)
    estados = {}
    with open(os.path.join(out, 'saida.txt'), 'w') as f:
        subprocess.run([sys.executable, TF, 'stop'], env=env, stdout=f, stderr=f)
        subprocess.run([sys.executable, TF, 'duelo'] + (['--lento'] if shots else []), env=env, stdout=f, stderr=f, timeout=400)
        seg = []
        for palavra in rot.split():
            if palavra.startswith('esperar:turno'):
                if seg:
                    subprocess.run([sys.executable, TF, 'run', ' '.join(seg)], env=env, stdout=f, stderr=f, timeout=900); seg = []
                f.write('esperar %s: %s' % (palavra, esperar_log(env, 'turno %s (vez de J0)' % palavra[13:])) + chr(10))
            elif palavra.startswith('estado:'):
                if seg:
                    subprocess.run([sys.executable, TF, 'run', ' '.join(seg)], env=env, stdout=f, stderr=f, timeout=900); seg = []
                estados[palavra[7:]] = ler_estado(env)
            else:
                seg.append(palavra)
        if seg:
            subprocess.run([sys.executable, TF, 'run', ' '.join(seg)], env=env, stdout=f, stderr=f, timeout=900)
        subprocess.run([sys.executable, TF, 'stop'], env=env, stdout=f, stderr=f)
    for p in glob.glob(os.path.join(ROOT, 'gen', 'ctl_' + tag, '*.png')):
        shutil.copy(p, out)
    logs = sorted(glob.glob(os.path.join(ROOT, 'gen', 'logs_teste_' + tag, '*.log')), key=os.path.getmtime)
    if logs:
        shutil.copy(logs[-1], os.path.join(out, 'jogo.log'))
    json.dump({k: v.dict() for k, v in estados.items()}, open(os.path.join(out, 'state.json'), 'w'), indent=1)
    res = []
    for c in checks:
        try:
            ok = bool(eval(c, {}, dict(estados)))
        except Exception as e:   # missing state, bad expression
            ok = False; c += '   (%s)' % e
        res.append((c, ok))
    if spec:
        shutil.rmtree(os.path.join(ROOT, 'gen', 'lotepack', str(cid)), ignore_errors=True)
    return cid, estados, res


def folha(ids):
    from PIL import Image, ImageDraw
    tiles = []
    for cid in ids:
        for n in ('d4', 'd5', 'd3'):
            p = os.path.join(ROOT, 'gen', 'lote', str(cid), n + '.png')
            if os.path.exists(p):
                im = Image.open(p).convert('RGB'); ImageDraw.Draw(im).text((4, 4), '%s %s' % (cid, n), fill=(255, 255, 0)); tiles.append(im); break
    if not tiles:
        return
    w, h = tiles[0].size; cols = min(3, len(tiles)); rows = (len(tiles) + cols - 1) // cols
    sheet = Image.new('RGB', (cols * w, rows * h))
    for i, t in enumerate(tiles):
        sheet.paste(t, ((i % cols) * w, (i // cols) * h))
    sheet.save(os.path.join(ROOT, 'gen', 'lote', 'folha.png'))


def resumo(cid, estados):
    if 'ini' not in estados or 'fim' not in estados:
        return '%s: no state read' % cid
    i, f = estados['ini'], estados['fim']
    return '%s: LP me %d>%d op %d>%d | deck me %d>%d op %d>%d | hand me %d>%d op %d>%d | grave me %d>%d | field me %s op %s' % (
        cid, i.me.lp, f.me.lp, i.op.lp, f.op.lp, i.me.deck, f.me.deck, i.op.deck, f.op.deck, i.me.n_hand, f.me.n_hand, i.op.n_hand, f.op.n_hand, i.me.n_grave, f.me.n_grave,
        [x[0] for x in f.me.field], [x[0] for x in f.op.field])


def main():
    global SOLO
    a = sys.argv[1:]
    def opt(nome, padrao=None):
        return a[a.index(nome) + 1] if nome in a else padrao
    j = min(int(opt('-j', 8)), 8)   # Leo's PC: 10 instances = 100% CPU, so 8 is the ceiling
    mods = opt('--mods', 'gen/tf3sum')
    rot = opt('--rot') or os.environ.get('TF3_LOTE_ROTEIRO') or 'ativar'
    rot = ROTEIROS.get(rot, rot)
    if 'estado:fim' not in rot:
        rot += ' estado:fim'
    if 'estado:ini' not in rot:
        rot = rot.replace('shot:d1', 'estado:ini shot:d1', 1) if 'shot:d1' in rot else 'estado:ini ' + rot
    spec = None
    if '--spec' in a:
        v = opt('--spec'); spec = json.load(open(v, encoding='utf-8')) if os.path.exists(v) else json.loads(v)
        if 'effects' not in spec and 'stats' not in spec:
            spec = {'effects': spec}
    if '--solo' in a:   # hand of exactly one card (the forced one): the extra X presses of `resolver` cannot activate a second copy
        SOLO = True
        rot = rot.replace('estado:ini', 'poke:%08x:%s estado:ini' % (JOG + 12, struct.pack('<I', 1 + len((opt('--hand') or '').split(',') if opt('--hand') else [])).hex()), 1)
    shots = '--rapido' not in a   # fast mode (no rasterization) is NOT the same duel as the full mode and its fixed waits drift: experimental
    if not shots:
        rot = ' '.join(w for w in rot.split() if not w.startswith('shot:'))   # fast mode does not rasterize: screenshots would be black
    checks = [a[i + 1] for i, x in enumerate(a) if x == '--check']
    extra = {}
    if '--fn' in a:
        extra['TF3_DEBUG_FN'] = a[a.index('--fn') + 1]; extra['TF3_DEBUG_FN_ALL'] = '1'
    if '--debug-fx' in a:
        extra['TF3_DEBUG_FX'] = '1'
    skip = {'-j', '--mods', '--fn', '--rot', '--spec', '--check', '--grave', '--hand'}
    ids = [x for i, x in enumerate(a) if x.isdigit() and (i == 0 or a[i - 1] not in skip)]
    falhas = 0
    with ThreadPoolExecutor(j) as ex:
        for cid, estados, res in ex.map(lambda c: um(c, mods, extra, rot, spec, checks, shots, opt('--grave'), opt('--hand')), ids):
            print('done', resumo(cid, estados), flush=True)
            for c, ok in res:
                print('   %s  %s' % ('PASS' if ok else 'FAIL', c), flush=True); falhas += not ok
    folha(ids)
    print('gen/lote/folha.png' + ('   %d check(s) failed' % falhas if falhas else ''))
    return 1 if falhas else 0


if __name__ == '__main__':
    sys.exit(main())
