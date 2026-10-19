#!/bin/sh
# Replays sessions with swaps on and off (TF_DECOMP=off) and compares captures. Usage: sh tools/decomp_compare.sh <last_frame> <session> [session...]
cd "$(dirname "$0")/.."
END="$1"; shift
STEP="${SNAPSTEP:-}"; SN=$(python -c "e=$END;st=int('${STEP:-0}') or max(50,e//12);print(','.join(str(i) for i in range(100,e-10,st)))"); [ -n "$SNAPLIST" ] && SN="$SNAPLIST"
for S in "$@"; do
  for M in on off; do
    if [ $M = off ]; then export TF_DECOMP=off; else unset TF_DECOMP; fi
    RUN_EXE=${RUN_EXE:-gen/pkg/TagForceRecomp/TagForceRecomp_bg.exe} SNAPS=$SN ENDF=$END TAG=_c$M sh tools/replay.sh $S > /dev/null 2>&1
  done; unset TF_DECOMP
  r=gen/replay; n=0; d=0
  for f in $r/${S}_con/gen/snap_*.bmp; do b=$(basename $f); n=$((n+1)); cmp -s $f $r/${S}_coff/gen/$b || { d=$((d+1)); echo "DIFF $b"; }; done
  grep -v "perf\|decomp\|ritmo\|texturas\|^\[res\]\|gpu\|diag" $r/${S}_con/log.txt > gen/cmp_on.txt; grep -v "perf\|decomp\|ritmo\|texturas\|^\[res\]\|gpu\|diag" $r/${S}_coff/log.txt > gen/cmp_off.txt
  echo "== $S: $n captures, $d different; log $(diff -q gen/cmp_on.txt gen/cmp_off.txt >/dev/null && echo identical || echo DIFFERENT); swaps: $(grep -c 'first call' $r/${S}_con/log.txt) exercised"
done
