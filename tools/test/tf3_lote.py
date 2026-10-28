"""Runs one Tag Force 3 card test per card in parallel (each instance in its own package copy, see TF_TAG in tf.py) and writes a contact sheet.

Each case forces the card into the opening hand, ends turn 1 so the CPU plays, then opens the hand on turn 3 and activates / summons the first card.
Results: gen/lote/<card id>/ (screenshots + the log) and gen/lote/folha.png (last shots of every case side by side, labelled).
Usage:  python tools/test/tf3_lote.py [-j 8] [--mods gen/tf3sum] [--fn 09ebcce8,...] [--debug-fx] ID [ID ...]
"""
import os
import shutil
import subprocess
import sys
import glob
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TF = os.path.join(ROOT, 'tools', 'test', 'tf.py')
ROTEIRO = ("w300 O w60 RIGHT w20 RIGHT w20 RIGHT w20 X w2400 shot:d1 DOWN w40 DOWN w40 LEFT w30 LEFT w30 LEFT w30 LEFT w30 LEFT w30 X w60 shot:d2 "
           "X w100 shot:d3 X w200 shot:d4 w300 X w200 shot:d5")


def um(cid, mods, extra):
    tag = 'l%s' % cid
    env = dict(os.environ, TF_TAG=tag, TF3_FORCE_CARDS=str(cid), TF3_FORCE_COPIES='3', TF3_FORCE_DECK='0', TF_MODS_FROM=mods, **extra)
    out = os.path.join(ROOT, 'gen', 'lote', str(cid)); shutil.rmtree(out, ignore_errors=True); os.makedirs(out)
    with open(os.path.join(out, 'saida.txt'), 'w') as f:
        subprocess.run([sys.executable, TF, 'stop'], env=env, stdout=f, stderr=f)
        subprocess.run([sys.executable, TF, 'duelo', '--lento'], env=env, stdout=f, stderr=f, timeout=400)
        subprocess.run([sys.executable, TF, 'run', ROTEIRO], env=env, stdout=f, stderr=f, timeout=900)
        subprocess.run([sys.executable, TF, 'stop'], env=env, stdout=f, stderr=f)
    for p in glob.glob(os.path.join(ROOT, 'gen', 'ctl_' + tag, '*.png')):
        shutil.copy(p, out)
    logs = sorted(glob.glob(os.path.join(ROOT, 'gen', 'logs_teste_' + tag, '*.log')), key=os.path.getmtime)
    if logs:
        shutil.copy(logs[-1], os.path.join(out, 'jogo.log'))
    return cid


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


def main():
    a = sys.argv[1:]
    j = int(a[a.index('-j') + 1]) if '-j' in a else 8
    mods = a[a.index('--mods') + 1] if '--mods' in a else 'gen/tf3sum'
    extra = {}
    if '--fn' in a:
        extra['TF3_DEBUG_FN'] = a[a.index('--fn') + 1]; extra['TF3_DEBUG_FN_ALL'] = '1'
    if '--debug-fx' in a:
        extra['TF3_DEBUG_FX'] = '1'
    skip = {'-j', '--mods', '--fn'}
    ids = [x for i, x in enumerate(a) if x.isdigit() and (i == 0 or a[i - 1] not in skip)]
    with ThreadPoolExecutor(j) as ex:
        for cid in ex.map(lambda c: um(c, mods, extra), ids):
            print('done', cid, flush=True)
    folha(ids)
    print('gen/lote/folha.png')


if __name__ == '__main__':
    main()
