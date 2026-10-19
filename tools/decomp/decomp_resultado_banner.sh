#!/bin/sh
# Compara o quadro do resultado com banner/placar (fases 6..9, so em duelos da historia) com a decomp desligada x ligada.
# (resultados dos 3 rounds = vitoria, derrota, empate: poke em start_request+0x36)
# O Free Duel nunca chega la: o script liga o modo "com resultado" (settings+0x119 = 2) e, conforme o argumento, o modo de quatro duelistas (+0x11a = 2).
# Uso: sh tools/decomp/decomp_resultado_banner.sh [2|4]   (saida: gen/cmp_decomp/banner*_*.png, diferencas no fim)
cd "$(dirname "$0")/../.."
TEAM=${1:-2}
P="poke:08a751dd:02 poke:08a7584a:010002000300"; [ "$TEAM" = 4 ] && P="$P poke:08a751de:02"
SCRIPT="w200 X w200 X w200 X w300 $P X w10 shot:b1 w30 shot:b2 w60 shot:b3 w120 shot:b4 w200 shot:b5 w400 shot:b6"
FRAMES="b1 b2 b3 b4 b5 b6"
NAMES=$( { grep -o '"libduel_mgr\.[A-Za-z]*"' decomp/libduel_mgr/swaps.cpp | tr -d '"'; grep -o 'PAGE_SWAP([A-Za-z]*' decomp/libduel_mgr/swaps.cpp | sed 's/PAGE_SWAP(/libduel_mgr./'; } | paste -sd,)
mkdir -p gen/cmp_decomp
guarda() { for f in $FRAMES; do cp "gen/ctl/$f.png" "gen/cmp_decomp/banner${TEAM}_$1_$f.png" 2>/dev/null; done; }
unset TF_DECOMP_VERIFY
TF_DECOMP=off python tools/test/tf.py duelo --auto --fim --lento --depois "$SCRIPT" > gen/cmp_decomp/banner_off.txt 2>&1; guarda off
TF_DECOMP="$NAMES" python tools/test/tf.py duelo --auto --fim --lento --depois "$SCRIPT" > gen/cmp_decomp/banner_on.txt 2>&1; guarda on
TF_DECOMP="$NAMES" TF_DECOMP_VERIFY=1 python tools/test/tf.py duelo --auto --fim --lento --depois "$SCRIPT" > gen/cmp_decomp/banner_verify.txt 2>&1; cp gen/logs_teste/*.log gen/cmp_decomp/banner_verify.log
export FRAMES TEAM
python - <<'PY'
from PIL import Image, ImageChops
import numpy as np
import os
t = os.environ["TEAM"]
for f in os.environ["FRAMES"].split():
    try:
        a = Image.open(f"gen/cmp_decomp/banner{t}_off_{f}.png").convert("RGB"); b = Image.open(f"gen/cmp_decomp/banner{t}_on_{f}.png").convert("RGB")
        n = int((np.array(ImageChops.difference(a, b)).sum(axis=2) > 0).sum())
        print(f, "pixels diferentes:", n)
    except Exception as e:
        print(f, "erro:", e)
PY
echo "MISMATCH no verify: $(grep -a -c MISMATCH gen/cmp_decomp/banner_verify.log)"
echo "CRASH: $(grep -a -c CRASH gen/cmp_decomp/banner_*.txt gen/cmp_decomp/banner_verify.log | tr '\n' ' ')"
echo fim
