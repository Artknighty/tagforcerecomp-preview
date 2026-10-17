#!/bin/sh
# Compara a tela de resultado do duelo (paginas, slide, pergunta) com a decomp desligada x ligada e roda a verificacao dos swaps.
# Uso: sh tools/decomp_resultado.sh   (saida: gen/cmp_decomp/*.png, diferencas no fim, gen/cmp_decomp/verify.log)
cd "$(dirname "$0")/.."
# pokectx/pokeptr ligam as paginas 2 e 3 com entradas reais (recordes: numero, tempo e numero grande; desafios); RIGHT/LEFT passam pelas 4 paginas, com capturas no meio do slide
SCRIPT="w200 X w200 X w200 X w300 shot:r3 pokectx:c:14000000 pokectx:10:1e000000 pokectx:3a:0300 pokeptr:3c:0:000025000300 pokeptr:40:0:010001000100 pokeptr:50:0:88130000 pokeptr:50:c:f0490200 pokeptr:50:94:7d000000 pokectx:46:0200 pokeptr:48:0:00000100 pokeptr:4c:0:01000100 RIGHT w8 shot:m1 w60 shot:p1 RIGHT w8 shot:m2 w60 shot:p2 RIGHT w60 shot:p3 DOWN w20 shot:p3d UP w20 LEFT w60 shot:q2 LEFT w60 shot:q1 LEFT w60 shot:q0 X w200 shot:s5 w200 shot:s6 X w300 shot:e1 w600 shot:e2"
FRAMES="r3 m1 p1 m2 p2 p3 p3d q2 q1 q0 s5 s6 e1 e2"
# os nomes dos swaps: os escritos por extenso e os das macros PAGE_SWAP(Nome, ...)
NAMES=$( { grep -o '"libduel_mgr\.[A-Za-z]*"' decomp/libduel_mgr/swaps.cpp | tr -d '"'; grep -o 'PAGE_SWAP([A-Za-z]*' decomp/libduel_mgr/swaps.cpp | sed 's/PAGE_SWAP(/libduel_mgr./'; } | paste -sd,)
echo "swaps: $(echo "$NAMES" | tr ',' '
' | wc -l)"
mkdir -p gen/cmp_decomp
guarda() { for f in $FRAMES; do cp "gen/ctl/$f.png" "gen/cmp_decomp/$1_$f.png" 2>/dev/null; done; }
unset TF_DECOMP_VERIFY
TF_DECOMP=off python tools/tf.py duelo --auto --fim --lento --depois "$SCRIPT" > gen/cmp_decomp/off.txt 2>&1; guarda off
TF_DECOMP="$NAMES" python tools/tf.py duelo --auto --fim --lento --depois "$SCRIPT" > gen/cmp_decomp/on.txt 2>&1; guarda on
TF_DECOMP="$NAMES" TF_DECOMP_VERIFY=1 python tools/tf.py duelo --auto --fim --lento --depois "$SCRIPT" > gen/cmp_decomp/verify.txt 2>&1; cp gen/logs_teste/*.log gen/cmp_decomp/verify.log
export FRAMES
python - <<'PY'
from PIL import Image, ImageChops
import numpy as np
import os
for f in os.environ["FRAMES"].split():
    try:
        a = Image.open(f"gen/cmp_decomp/off_{f}.png").convert("RGB"); b = Image.open(f"gen/cmp_decomp/on_{f}.png").convert("RGB")
        n = int((np.array(ImageChops.difference(a, b)).sum(axis=2) > 0).sum())
        print(f, "pixels diferentes:", n)
    except Exception as e:
        print(f, "erro:", e)
PY
echo "MISMATCH no verify: $(grep -a -c MISMATCH gen/cmp_decomp/verify.log)"
echo "CRASH: $(grep -a -c CRASH gen/cmp_decomp/*.txt gen/cmp_decomp/verify.log | tr '\n' ' ')"
echo fim
