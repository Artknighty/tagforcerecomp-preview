"""Battle-burn probe (hidden, own duel): a Level 4 test monster (Masked Chopper 7039, +3000 ATK in the test pack only) gets a temporary effect spec,
is summoned on turn 1, and on turn 3 attacks the CPU's monster. Prints the opponent's life points before / after and the CPU's field / Graveyard.
Run (no -I): TF_TEST_EXE=<absolute full package exe> TF_TEST_DATA=<absolute game data> python tools/test/tf3_verify_benten.py --spec '{"7039": {...}}' [--mods DIR] [--card 7039]
"""
import json
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'test'))
os.environ.setdefault('TF_SEGUNDO_PLANO', '1'); os.environ.setdefault('NO_AUDIO', '1')
import tf
import tf3_lote


def block(p):
    return bytes.fromhex(tf.send('peek %08x 2048' % (0x09f79cec + p * 0xfac)).split()[-1])


def lp(p):
    return struct.unpack_from('<i', block(p), 0)[0]


def field(p):
    w = block(p)
    return [(struct.unpack_from('<I', w, 0x30 + s * 20)[0] & 0x1fff, struct.unpack_from('<I', w, 0x34 + s * 20)[0]) for s in range(5)]


def grave(p):
    w = block(p)
    return [struct.unpack_from('<I', w, 0x710 + i * 4)[0] & 0x1fff for i in range(min(struct.unpack_from('<I', w, 20)[0], 12))]


def main():
    a = sys.argv[1:]
    opt = lambda n, d=None: a[a.index(n) + 1] if n in a else d
    card = int(opt('--card', '7039'))
    spec = json.loads(opt('--spec'))
    spec = spec if 'effects' in spec or 'stats' in spec else {'effects': spec}
    spec.setdefault('stats', {})[str(card)] = {'scope': 'self', 'atk': 3000, 'def': 0}
    mods = tf3_lote.pacote_do_caso('benten', opt('--mods', 'gen/tf3_lords_mods'), spec)
    rows = [l.split(' ', 3) for l in open(os.path.join(ROOT, mods, 'TF3Cards', 'ported.txt'), encoding='utf-8').read().splitlines() if l.strip()]
    gid = {int(r[1]): int(r[0]) for r in rows}[card]
    os.environ.update(TF_MODS_FROM=mods, TF3_FORCE_CARDS=str(card), TF3_FORCE_COPIES='3', TF_PRIMEIRO='0', TF_DECK_J1='1', TF_RNG_SEED='1')
    try:
        tf.duelo([])
        tf.run('w300 O w60 DOWN w40 DOWN w40')
        raw = bytes.fromhex(tf.send('peek 09f79cec 512').split()[-1])
        n = struct.unpack_from('<I', raw, 12)[0]
        hand = [struct.unpack_from('<H', raw, 0x120 + i * 4)[0] & 0x1fff for i in range(n)]
        tf.run(' '.join(['LEFT w30'] * (n - 1 - hand.index(gid))) + ' X w60 X w300 shot:summoned')
        tf.send('poke 09f79cec %s' % struct.pack('<i', 8000).hex()); tf.send('poke 09f7ac98 %s' % struct.pack('<i', 8000).hex())
        tf.run('O w60 RIGHT w20 RIGHT w20 RIGHT w20 X w1500 shot:cpu_turn')
        tf.run('O w300 w900 O w300 w900')   # NO to a chain prompt, if the CPU turn raised one
        tf.run('w1500 shot:turn3')
        before = lp(1)
        tf.run('O w60 RIGHT w20 X w200 X w60 X w60 X w900 shot:attacked')
        tf.run('X w300 w1000 shot:after')
        print(json.dumps(dict(me=field(0), op=field(1), op_grave=grave(1), op_lp_before=before, op_lp_after=lp(1), damage=before - lp(1))))
    finally:
        tf.stop()
        import shutil; shutil.rmtree(os.path.join(ROOT, 'gen', 'lotepack', 'benten'), ignore_errors=True)


if __name__ == '__main__':
    main()
