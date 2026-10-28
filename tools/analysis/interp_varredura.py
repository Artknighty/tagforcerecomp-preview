#!/usr/bin/env python3
"""Scan recorded sessions for 120 FPS interpolation defects, without anyone looking at the screen.

For every session it replays the recorded inputs at 120 FPS (tools/test/replay.sh, the player's own save copy, hidden window, muted),
presses an automatic F8 every ~170 game frames (TF_MARCAG_QUADRO) so the black box dumps every presented frame (R = real, M = in-between),
and then measures how the picture moves in every group  R M M M R:

    steps  s0..s3 = mean |difference| between consecutive presented frames, T = difference R -> next R

  frozen   T >= 5, the two middle steps carry < 50 % of an even share, the last step (the real frame) carries > 30 % of the motion
           (map rooms redrawn once per real frame: everything but the sprites stands still, then jumps)
  uneven   T >= 5 and a later step is > 1.8x the mean (an in-between that jumps). [T,0,0,0] = a cut shown early on purpose: skipped
  held     T < 0.6 between two moving pairs (the real frame repeated: game hold or a late frame; informational)

Ground-truth mode (--autoteste): the engine interpolates F0 -> F2 (at double the motion) and compares the result with the REAL in-between frame F1, pixel by pixel
(TF_INTERP_SELFTEST). A pair is flagged when the error is > 1 % of the screen, the real frame F1 differs from F2 (hold > 0.5 %: otherwise the game itself
stood still) and the interpolation is no better than 80 % of "just show F2" (kinds: `candidate` = the game moved, hold >= 2 %, and the error is >= 1.3x hold; `minor` = the rest, only counted).
error == hold means the pair was not interpolated at all (scene change, taint...): counted as `skipped`, listed separately, informational.
The report gives the screen cells (8x4 grid) that are wrong.

A scene cut (T >= 35) is not a defect. Output: gen/varredura/relatorio.md (+ one line per finding on stdout).
The numbers point at WHERE to look (session, mark, frame): send the frame strips to the player or read the [iring]/[iaudit] log.

Usage:
  python tools/analysis/interp_varredura.py --ultimas 5                # the 5 newest sessions
  python tools/analysis/interp_varredura.py sessao_20261027_193849 ...  # named sessions
  python tools/analysis/interp_varredura.py --so-analisar ...          # reuse gen/replay/<sessao>_varr, do not replay again
  python tools/analysis/interp_varredura.py --autoteste --ultimas 8     # ground-truth mode (see below)
  options: --max-quadros N (replay at most N game frames per session, default 4000), --passo N (frames between marks, default 170)
"""
import glob, os, re, shutil, subprocess, sys
import numpy as np
from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT = os.path.join(ROOT, 'gen', 'varredura')


def args_get(name, default=None, cast=str):
    if name in sys.argv:
        i = sys.argv.index(name)
        return cast(sys.argv[i + 1])
    return default


def last_input_frame(sess):
    fn = os.path.join(ROOT, 'logs', sess + '.inputs')
    try:
        txt = open(fn).read().strip()
    except OSError:
        return 0
    nums = re.findall(r'(\d+):', txt)
    return int(nums[-1]) if nums else 0


def make_ini():
    """a private copy of the player's ini at 120 FPS: the player's own ini is never written"""
    os.makedirs(OUT, exist_ok=True)
    src = os.path.join(ROOT, 'tagforce_recomp.ini')
    dst = os.path.join(OUT, 'tagforce_recomp.ini')
    lines = open(src, encoding='utf-8', errors='replace').read().splitlines() if os.path.exists(src) else ['[graficos]']
    out, done = [], False
    for ln in lines:
        if ln.strip().lower().startswith('fps60='):
            ln, done = 'fps60=2', True
        out.append(ln)
    if not done:
        out.append('fps60=2')
    open(dst, 'w', encoding='utf-8').write('\n'.join(out) + '\n')
    return os.path.relpath(dst, ROOT).replace('\\', '/')


def replay(sess, max_frames, step):
    last = last_input_frame(sess)
    end = min(max_frames, last + 600) if last else max_frames
    marks = ','.join(str(q) for q in range(190, end, step))
    env = dict(os.environ, TAG='_varr', ENDF=str(end), INI=make_ini(), PESADO='1', PACE='1', TF_FPS='120',
               TF_MARCAG_QUADRO=marks, TF_MARCA_SEM_NOTA='1', TF_INTERP_NOSUSP='1')   # NOSUSP: the engine's own safety that drops to 30 FPS when late would hide the interpolation
    sh = shutil.which('sh') or 'sh'
    print('replay %s: %d frames, %d marks ...' % (sess, end, len(marks.split(','))), flush=True)
    subprocess.run([sh, 'tools/test/replay.sh', sess, '100000', '900'], cwd=ROOT, env=env,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def load_gray(path):
    im = Image.open(path).convert('L')
    return np.asarray(im.resize((240, 136)), dtype=np.float32)


def analyse_seq(seq_dir):
    """findings of one capture sequence: [(kind, idx, quadro, T, steps)]"""
    idx = os.path.join(seq_dir, 'indice.tsv')
    if not os.path.exists(idx):
        return []
    rows, susp = [], []
    for ln in open(idx):
        p = ln.rstrip('\n').split('\t')
        if len(p) >= 6 and p[0].isdigit():
            rows.append((int(p[0]), p[1], p[3], int(p[5])))
            susp.append(p[4] == "1")
    img = {}
    def get(i):
        if i not in img:
            img[i] = load_gray(os.path.join(seq_dir, rows[i][2]))
        return img[i]
    reals = [k for k, r in enumerate(rows) if r[1] == 'R']
    groups = [(a, b) for a, b in zip(reals, reals[1:]) if b - a == 4]
    out, Ts, St = [], {}, {}
    for a, b in groups:
        if any(susp[a:b + 1]):
            continue   # 60 FPS was suspended (the engine fell back to 30): nothing to measure
        f = [get(k) for k in range(a, b + 1)]
        St[a] = [float(np.abs(f[k + 1] - f[k]).mean()) for k in range(4)]
        Ts[a] = float(np.abs(f[4] - f[0]).mean())
    ordered = [a for a, _ in groups if a in Ts]
    for n, a in enumerate(ordered):
        T, s, quadro = Ts[a], St[a], rows[a][3]
        prev_T = Ts[ordered[n - 1]] if n > 0 else None
        next_T = Ts[ordered[n + 1]] if n + 1 < len(ordered) else None
        if T >= 35:
            continue   # scene cut
        # an isolated change between two quiet pairs is a cut or a fade step, not motion that should have been smooth
        if T >= 5 and prev_T is not None and next_T is not None and prev_T < 0.25 * T and next_T < 0.25 * T:
            continue
        if T >= 5:
            even = T / 4.0
            # [T,0,0,0] (all of it at the first in-between) is a cut shown early, by design: not flagged. The defect is motion that is
            # missing in the middle and arrives at the real frame (s3 big), or a middle step that jumps
            cut_last = (s[3] >= 0.85 * T and max(s[0], s[1], s[2]) < 0.1 * T) or (T >= 12 and s[3] >= 0.6 * T and max(s[0], s[1], s[2]) < 0.2 * T)   # a cut or fade landing on the real frame (sprite motion is T < 12)
            if cut_last:
                continue
            if s[1] < 0.5 * even and s[2] < 0.5 * even and s[3] > 0.3 * T and (s[0] + s[3]) > 0.6 * T:
                out.append(('frozen', rows[a][0], quadro, T, s))
            elif max(s) > 1.8 * (sum(s) / 4.0) and s.index(max(s)) != 0:
                out.append(('uneven', rows[a][0], quadro, T, s))
        elif T < 0.6 and prev_T is not None and next_T is not None and prev_T >= 5 and next_T >= 5:
            out.append(('held', rows[a][0], quadro, T, []))
    return out


def replay_autoteste(sess, max_frames):
    last = last_input_frame(sess)
    end = min(max_frames, last + 600) if last else max_frames
    ini = make_ini()
    env = dict(os.environ, TAG='_at', ENDF=str(end), INI=ini, PESADO='1', PACE='1', TF_FPS='60',
               TF_INTERP_SELFTEST='1', TF_INTERP_SELFTEST_V='1', TF_INTERP_NOSUSP='1')
    print('autotest %s: %d frames ...' % (sess, end), flush=True)
    subprocess.run([shutil.which('sh') or 'sh', 'tools/test/replay.sh', sess, '100000', '900'], cwd=ROOT, env=env,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def analyse_autoteste(sess):
    """[(vb, error %, hold %, hot cells)] flagged from the selftest lines of gen/replay/<sess>_at/log.txt"""
    fn = os.path.join(ROOT, 'gen', 'replay', sess + '_at', 'log.txt')
    out, total = [], 0
    try:
        lines = open(fn, encoding='utf-8', errors='replace').read().splitlines()
    except OSError:
        return out, 0
    pat = re.compile(r'\[selftest\] vb (\d+): erro ([\d.]+)% \(hold ([\d.]+)%\) grade:(.*)')
    for ln in lines:
        m = pat.match(ln)
        if not m:
            continue
        total += 1
        vb, err, hold = int(m.group(1)), float(m.group(2)), float(m.group(3))
        if err > 1.0 and hold > 0.5 and err > 0.8 * hold:
            # skipped: not interpolated (error == hold). candidate: the game moved (hold >= 2 %) and the interpolation is clearly worse than doing nothing
            # (error >= 1.3x hold). minor: everything else (hold < 2 % = the game stood still / moved unevenly, so a linear in-between cannot match; small errors)
            kind = 'skipped' if abs(err - hold) <= 0.03 * hold + 0.05 else ('candidate' if hold >= 2.0 and err >= 1.3 * hold else 'minor')
            rows = [r.split(',') for r in m.group(4).split()]
            cells = sorted(((int(c) if c.isdigit() else 0, ry, rx) for ry, r in enumerate(rows) for rx, c in enumerate(r)), reverse=True)[:3]
            out.append((kind, vb, err, hold, ['col%d,row%d:%d%%' % (rx, ry, v) for v, ry, rx in cells if v]))
    return out, total


def analyse_session(sess):
    base = os.path.join(ROOT, 'gen', 'replay', sess + '_varr')
    found, seen = [], set()
    for d in sorted(glob.glob(os.path.join(base, 'logs', '*_capturas', 'seq_*'))):
        for k, idx, q, T, s in analyse_seq(d):
            if (k, q) in seen:
                continue   # two marks overlap
            seen.add((k, q))
            found.append((k, q, os.path.basename(d), idx, T, s))
    return sorted(found, key=lambda x: x[1])


def runs(found, kind):
    """consecutive findings (quadro step <= 4) merged: [(first, last, count, worstT)]"""
    items = sorted([f for f in found if f[0] == kind], key=lambda x: x[1])
    out = []
    for f in items:
        if out and f[1] - out[-1][1] <= 4:
            out[-1] = (out[-1][0], f[1], out[-1][2] + 1, max(out[-1][3], f[4]), out[-1][4])
        else:
            out.append((f[1], f[1], 1, f[4], f[2] + ':' + str(f[3])))
    return out


def main():
    names = [a for a in sys.argv[1:] if a.startswith('sessao_')]
    n = args_get('--ultimas', 0, int)
    if n:
        allsess = sorted(os.path.basename(p)[:-7] for p in glob.glob(os.path.join(ROOT, 'logs', 'sessao_*.inputs')))
        names += [s for s in allsess[-n:] if s not in names and last_input_frame(s) > 600]
    if not names:
        print(__doc__)
        return 1
    max_frames = args_get('--max-quadros', 4000, int)
    step = args_get('--passo', 170, int)
    os.makedirs(OUT, exist_ok=True)
    rep = ['# 120 FPS interpolation scan', '',
           'frozen = in-betweens stand still and the motion jumps at the real frame; uneven = one step much bigger than the others; held = real frame repeated (informational).', '']
    for sess in names:
        if '--autoteste' in sys.argv:
            if '--so-analisar' not in sys.argv:
                replay_autoteste(sess, max_frames)
            bad, total = analyse_autoteste(sess)
            rep.append('## %s (autotest, %d pairs)' % (sess, total))
            nsk = sum(1 for b in bad if b[0] == 'skipped'); ncand = sum(1 for b in bad if b[0] == 'candidate'); nmin = len(bad) - nsk - ncand
            print('%s: %d pairs measured, %d candidates, %d minor, %d not interpolated (scene changes)' % (sess, total, ncand, nmin, nsk), flush=True)
            rep.append('%d candidates, %d minor, %d not interpolated' % (ncand, nmin, nsk))
            for kind, vb, err, hold, cells in sorted([b for b in bad if b[0] == 'candidate'], key=lambda x: -x[2])[:10]:
                line = '- %s vb %d: error %.1f %% (hold %.1f %%) %s' % (kind, vb, err, hold, ' '.join(cells))
                rep.append(line)
                print('   ' + line)
            rep.append('')
            continue
        if '--so-analisar' not in sys.argv:
            replay(sess, max_frames, step)
        found = analyse_session(sess)
        rep.append('## %s' % sess)
        counts = {k: sum(1 for f in found if f[0] == k) for k in ('frozen', 'uneven', 'held')}
        rep.append('frozen %d, uneven %d, held %d' % (counts['frozen'], counts['uneven'], counts['held']))
        print('%s: frozen %d, uneven %d, held %d' % (sess, counts['frozen'], counts['uneven'], counts['held']), flush=True)
        for kind in ('frozen', 'uneven'):
            for first, last, cnt, worst, where in sorted(runs(found, kind), key=lambda r: -r[3])[:10]:
                line = '- %s game frames %d-%d (%d pairs, worst T %.1f), capture %s' % (kind, first, last, cnt, worst, where)
                rep.append(line)
                print('   ' + line)
        rep.append('')
    open(os.path.join(OUT, 'relatorio.md'), 'w', encoding='utf-8').write('\n'.join(rep) + '\n')
    print('report: gen/varredura/relatorio.md')
    return 0


if __name__ == '__main__':
    sys.exit(main())
