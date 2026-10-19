#!/bin/sh
# Benchmark do rasterizador + regressao visual. Uso: sh tools/test/bench.sh [ref]
#  - duelo: replays a recorded session (144753) sem limitador; mede o trabalho medio por quadro dos quadros >= 6000
#  - cut-ins: demo de duelo do titulo (quadros >= 5400)
#  - com "ref": grava as capturas atuais como referencia (gen/bench_ref$SUF); sem: compara com ela (deve dar 0 em refatoracoes
#    puras) e imprime o tempo. Custo = media ponderada das linhas [perf] do log da sessao.
cd "$(dirname "$0")/../.."
S=sessao_20260929_144753
MODE="${1:-cmp}"
export TF_RES_BUDGET_MS=0   # sem resolucao dinamica: imagens deterministicas
export TF_GPU="${TF_GPU:-0}"   # mede o rasterizador por SOFTWARE (o .ini do usuario pode ter GPU como padrao)
SUF=""; [ -n "$TF_RES" ] && [ "$TF_RES" != "1" ] && SUF="_r$TF_RES"   # TF_RES=2 sh tools/test/bench.sh: mede/compara a resolucao interna 2x (referencias separadas)
mkdir -p gen/bench_cur$SUF; rm -f gen/bench_cur$SUF/*.bmp
avg() {   # avg <log> <quadro_inicial>
  python - "$1" "$2" <<'PY'
import re, sys
n = t = 0.0
for l in open(sys.argv[1], errors='replace'):
    m = re.match(r'\[perf\] quadros (\d+)-(\d+): trabalho medio ([\d.]+) ms', l)
    if m and int(m.group(1)) >= int(sys.argv[2]):
        c = int(m.group(2)) - int(m.group(1)) + 1; n += c; t += c * float(m.group(3))
print('%.2f ms/quadro (%d quadros)' % (t / n, n) if n else 'sem dados')
PY
}
# jogo PURO: sem uma pasta de mods vazia, replay.sh/play.sh carregam os mods de COMPORTAMENTO ligados (IA, decks, cheats...), que mudam
# o duelo e a demo do titulo, e as capturas deixam de bater com a referencia (o mesmo que a regressao.sh corrigiu em 2026-10-07)
mkdir -p gen/regressao_sem_mods
MODDIR=gen/regressao_sem_mods sh tools/test/replay.sh $S 1000 900 > /dev/null 2>&1
echo "duelo   : $(avg gen/replay/$S/log.txt 6000)"
for f in 6000 7000 8000 9000 10000 11000; do cp gen/replay/$S/gen/snap_$f.bmp gen/bench_cur$SUF/duelo_$f.bmp; done
TF_MODS_DIR="$(cygpath -m "$PWD/gen/regressao_sem_mods" 2>/dev/null || echo "$PWD/gen/regressao_sem_mods")" sh tools/test/play.sh "300:4000,306:0" "3000,4000,5000,5400,6200,7000" 400 > /dev/null 2>&1
echo "cut-ins : $(avg gen/log_play.txt 5400)"
for f in 3000 4000 5000 5400 6200 7000; do cp gen/snap_$f.bmp gen/bench_cur$SUF/demo_$f.bmp; done
if [ "$MODE" = "ref" ]; then rm -rf gen/bench_ref$SUF; cp -r gen/bench_cur$SUF gen/bench_ref$SUF; echo "referencia gravada em gen/bench_ref$SUF"; exit 0; fi
SUF=$SUF python - <<'PY'
import os
import numpy as np
from PIL import Image
r, c = 'gen/bench_ref' + os.environ.get('SUF', ''), 'gen/bench_cur' + os.environ.get('SUF', '')
if not os.path.isdir(r): print('sem referencia (rode: sh tools/test/bench.sh ref)'); raise SystemExit
worst = 0
for f in sorted(os.listdir(c)):
    a = np.asarray(Image.open(os.path.join(r, f)).convert('RGB'), dtype=int); b = np.asarray(Image.open(os.path.join(c, f)).convert('RGB'), dtype=int)
    d = np.abs(a - b); n = int((d.sum(2) > 0).sum()); m = int(d.max())
    if n: print('  %-16s %6d pixels diferentes, diferenca max %d' % (f, n, m))
    worst = max(worst, m)
print('imagens: IDENTICAS' if worst == 0 else 'imagens: DIFERENTES (max %d)' % worst)
PY
