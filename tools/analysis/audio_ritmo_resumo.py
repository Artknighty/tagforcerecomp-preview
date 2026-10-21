"""Summary of the per-second audio lines ([audio] por s real ...) of a log written with TF_LAT_DEBUG=1 / diag.txt.
Usage: python tools/analysis/audio_ritmo_resumo.py <log> [skip_seconds=10]"""
import re, sys

def main():
    path = sys.argv[1]; skip = int(sys.argv[2]) if len(sys.argv) > 2 else 10
    lines = open(path, encoding='utf-8', errors='replace').read().splitlines()
    sec = [l for l in lines if 'device:' in l and 'por s real' in l][skip:]
    q = [int(m.group(1)) for l in sec for m in [re.search(r'virtual \d+ us, (\d+) quadros', l)] if m]
    st = [float(m.group(1)) for l in sec for m in [re.search(r'stretch max ([0-9.]+)%', l)] if m]
    vus = [int(m.group(1)) for l in sec for m in [re.search(r'virtual (\d+) us', l)] if m]
    real = [float(m.group(1)) for l in sec for m in [re.search(r'real ([0-9.]+) ms\)', l)] if m]
    speed = sum(vus) / sum(real) / 10.0 if len(real) == len(vus) and real else 0.0
    under = sum('underrun' in l or 'realinhou' in l for l in lines)
    catch = [l for l in lines if 'saidas atrasadas recuperadas' in l]
    if not q: print('no per-second audio lines'); return
    slow = sum(1 for v in q if v <= 59)
    print('seconds %d | frames/s avg %.2f min %d | seconds <= 59 fps: %d | virtual time per real s avg %.1f ms | stretch avg %.2f%% max %.2f%% | game speed %.2f%% of real time | underruns %d'
          % (len(q), sum(q) / len(q), min(q), slow, sum(vus) / len(vus) / 1000.0, sum(st) / len(st), max(st), speed, under))
    smp = [int(m.group(1)) for l in sec for m in [re.search(r'canal0=(\d+) amostras', l)] if m]
    if smp and vus: print('channel 0: %.1f samples per virtual second (44100 = exact)' % (sum(smp) / (sum(vus) / 1e6)))
    for l in catch[-3:]: print(l.split('] ', 1)[-1])
    for l in lines:
        if 'reserve raised' in l: print(l)

main()
