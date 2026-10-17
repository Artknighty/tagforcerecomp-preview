#!/bin/sh
# Replays sessions with every swap on and with the swaps whose name starts with <prefix> left out (TF_DECOMP_SKIP), and compares captures: isolates one
# Module's swaps (decomp_compare.sh turns ALL swaps off, which also shows the intended differences of the others).
# Usage: sh tools/decomp_compare_skip.sh <prefix> <last_frame> <step> <session> [session...]      RUN_EXE=gen/run_bg.exe by default (a copy of run.exe)
cd "$(dirname "$0")/.."
PREFIX="$1"; END="$2"; STEP="$3"; shift; shift; shift
SN=$(python -c "print(','.join(str(i) for i in range(100,$END-10,$STEP)))")
for S in "$@"; do
  for M in on off; do
    if [ $M = off ]; then export TF_DECOMP_SKIP="$PREFIX"; else unset TF_DECOMP_SKIP; fi
    RUN_EXE=${RUN_EXE:-gen/run_bg.exe} SNAPS=$SN ENDF=$END TAG=_s$M sh tools/replay.sh $S > /dev/null 2>&1
  done; unset TF_DECOMP_SKIP
  r=gen/replay; n=0; d=0
  for f in $r/${S}_son/gen/snap_*.bmp; do b=$(basename $f); n=$((n+1)); cmp -s $f $r/${S}_soff/gen/$b || { d=$((d+1)); echo "DIFF $b"; }; done
  echo "== $S: $n captures, $d different; swaps exercised: $(grep -c "first call of $PREFIX" $r/${S}_son/log.txt)"
done
