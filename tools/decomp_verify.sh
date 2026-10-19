#!/bin/sh
# Replays sessions with swaps on and TF_DECOMP_VERIFY=1 and reports mismatches and the swaps that were exercised.
# Usage: sh tools/decomp_verify.sh <last_frame> <session> [session...]   (uses gen/pkg/TagForceRecomp/TagForceRecomp_bg.exe, the copy of run.exe that the build makes for replays)
cd "$(dirname "$0")/.."
END="$1"; shift
for S in "$@"; do
  TF_DECOMP_VERIFY=1 RUN_EXE=${RUN_EXE:-gen/pkg/TagForceRecomp/TagForceRecomp_bg.exe} TAG=_v ENDF=$END sh tools/replay.sh $S > /dev/null 2>&1
  L=gen/replay/${S}_v/log.txt
  echo "== $S: $(grep -c 'first call of' $L) swaps exercised, $(grep -c MISMATCH $L) mismatches"
  grep MISMATCH $L | sort | uniq -c | sort -rn | head -20 | cut -c1-230
done
