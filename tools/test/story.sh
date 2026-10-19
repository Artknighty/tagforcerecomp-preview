#!/bin/sh
# Roteiro padrao ate a historia (com save existente em ./ms0): titulo -> Continue -> Story Mode -> Part 1 -> dialogos
# (X a cada 60 quadros ate o quadro $1). Uso: sh tools/test/story.sh <ate_quadro> "<capturas>" [segundos] [extra_AUTO_PRESS]
cd "$(dirname "$0")/../.."
C="300:4000,306:0,900:8,906:0,1100:4000,1106:0,1600:4000,1606:0,2000:4000,2006:0,2300:4000,2306:0"
X=$(python -c "print(','.join('%d:4000,%d:0' % (f, f + 6) for f in range(2800, $1, 60)))")
EXTRA="${4:+,$4}"
sh tools/test/play.sh "$C,$X$EXTRA" "$2" "${3:-500}"
grep -A6 watchdog gen/log_play.txt | head -20
