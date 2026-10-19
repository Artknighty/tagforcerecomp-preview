#!/bin/sh
# Compare a decomp build against the baseline captures of a Module (docs/DECOMP.md).
# Usage: sh tools/decomp_check.sh [module=libduelrecord] [session=sessao_20261011_014117]
# Builds gen/pkg/TagForceRecomp/run_decomp.exe, replays the session with TF_DECOMP_VERIFY=1 (swaps on) and with TF_DECOMP=off, and reports
# which swaps were exercised, any MISMATCH, and whether every capture is identical to gen/decomp_baseline/<module>/.
cd "$(dirname "$0")/.."
M="${1:-libduelrecord}"; S="${2:-sessao_20261011_014117}"; B=gen/decomp_baseline/$M
SN=1250,1350,1450,1550,1650,1750; END=1900
OUT=gen/pkg/TagForceRecomp/run_decomp.exe sh tools/build.sh > gen/decomp_build.log 2>&1 || { echo "build failed, see gen/decomp_build.log"; grep -n "error" gen/decomp_build.log | head; exit 1; }
for MODE in verify on off; do
  T=_chk_$MODE; unset TF_DECOMP TF_DECOMP_VERIFY
  case $MODE in off) export TF_DECOMP=off;; verify) export TF_DECOMP_VERIFY=1;; esac
  RUN_EXE=gen/pkg/TagForceRecomp/run_decomp.exe SNAPS=$SN ENDF=$END TAG=$T sh tools/replay.sh $S > gen/replay/chk_$MODE.txt 2>&1
  unset TF_DECOMP TF_DECOMP_VERIFY
  W=gen/replay/$S$T; echo "== swaps $MODE"
  cat gen/replay/chk_$MODE.txt $W/logs/*.log 2>/dev/null | grep "\[decomp\]" | grep -v "first call" | sort -u
  # verify mode runs functions several times and perturbs timing, so only the on/off runs are compared with the captures
  [ $MODE = verify ] && { echo "calls checked: $(cat gen/replay/chk_$MODE.txt $W/logs/*.log 2>/dev/null | grep -c 'first call') swaps exercised"; continue; }
  for f in $(echo $SN | tr ',' ' '); do cmp -s $B/snap_$f.bmp $W/gen/snap_$f.bmp && echo -n "$f=ok " || echo -n "$f=DIFF "; done; echo
done
