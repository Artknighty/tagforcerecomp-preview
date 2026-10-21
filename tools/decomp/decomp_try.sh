#!/bin/sh
# Quick check of one swap selection: sh tools/decomp/decomp_try.sh "<TF_DECOMP list>" [session]  (uses RUN_EXE, default gen/pkg/TagForceRecomp/TagForceRecomp_bg.exe; no rebuild)
# Prints the [decomp]/CRASH lines and whether the captures match gen/decomp_baseline/libduelrecord/.
cd "$(dirname "$0")/../.."
L="$1"; S="${2:-sessao_20261011_014117}"; B=gen/decomp_baseline/libduelrecord; SN=1250,1350,1450,1550,1650,1750
export TF_DECOMP="$L"; [ -n "$VERIFY" ] && export TF_DECOMP_VERIFY=1
RUN_EXE=${RUN_EXE:-gen/pkg/TagForceRecomp/TagForceRecomp_bg.exe} SNAPS=$SN ENDF=1900 TAG=_try sh tools/test/replay.sh $S > gen/replay/try.txt 2>&1
W=gen/replay/${S}_try
grep -h "MISMATCH\|CRASH\|no import" gen/replay/try.txt $W/logs/*.log 2>/dev/null | sort -u | head -5
for f in $(echo $SN | tr ',' ' '); do cmp -s $B/snap_$f.bmp $W/gen/snap_$f.bmp && echo -n "$f=ok " || echo -n "$f=DIFF "; done; echo
