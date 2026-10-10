"""Resolve o arquivo do profiler (PROFILE=arquivo, ver display.cpp) em funcoes/linhas.
Uso: python tools/prof.py <arquivo> [exe=gen/run.exe] [topo=25]
Mostra (1) a funcao externa (onde o codigo inlined foi parar) e (2) a linha-fonte mais interna, com % das amostras."""
import subprocess, sys, collections, os

prof = sys.argv[1]
exe = sys.argv[2] if len(sys.argv) > 2 else 'gen/run.exe'
top = int(sys.argv[3]) if len(sys.argv) > 3 else 25
os.environ['PATH'] = r'C:\msys64\ucrt64\bin;' + os.environ['PATH']
rows = [l.split() for l in open(prof) if l.strip()]
addrs = [int(a, 16) for a, _ in rows]
counts = [int(c) for _, c in rows]
total = sum(counts)
out = subprocess.run(['addr2line', '-f', '-i', '-C', '-e', exe] + ['%x' % a for a in addrs], capture_output=True, text=True).stdout.splitlines()
# saida: para cada endereco, pares (funcao, arquivo:linha) do mais interno ao mais externo; o marcador de fim nao existe,
# entao resolvemos endereco a endereco
res = []
for a in addrs:
    o = subprocess.run(['addr2line', '-f', '-i', '-C', '-e', exe, '%x' % a], capture_output=True, text=True).stdout.splitlines()
    frames = [(o[i], o[i + 1]) for i in range(0, len(o) - 1, 2)]
    res.append(frames)
outer = collections.Counter(); inner = collections.Counter()
for frames, c in zip(res, counts):
    if not frames:
        outer['??'] += c; inner['??'] += c; continue
    outer[frames[-1][0].split('(')[0]] += c
    f, loc = frames[0]
    inner['%s  %s' % (f.split('(')[0], os.path.basename(loc))] += c
print('amostras:', total)
print('--- por funcao externa')
for k, v in outer.most_common(top): print('%5.1f%%  %s' % (100.0 * v / total, k))
print('--- por linha mais interna')
for k, v in inner.most_common(top): print('%5.1f%%  %s' % (100.0 * v / total, k))
