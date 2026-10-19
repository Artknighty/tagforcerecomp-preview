#!/bin/sh
# Which functions of a Module run in a recorded session? Usage: sh tools/decomp/decomp_cobertura.sh <session> <Module> [last_frame]
# Replays with TF_HOOK_TRACE on every function of the Module and prints "address calls first_frame last_frame" (sorted).
# The Windows environment limit (~32 KB) allows about 3000 addresses per run.
cd "$(dirname "$0")/../.."
S="$1"; M="$2"; END="${3:-1000}"
export TF_HOOK_TRACE=$(tail -n +2 gen/ferramentas_decomp/export/$M/_funcoes.tsv | cut -f1 | paste -sd,)
RUN_EXE=${RUN_EXE:-gen/pkg/TagForceRecomp/TagForceRecomp_bg.exe} TAG=_cov$M ENDF=$END sh tools/test/replay.sh "$S" > /dev/null 2>&1
grep '^\[hook\] quadro' "gen/replay/${S}_cov$M/log.txt" | sed -E 's/\[hook\] quadro ([0-9]+): ([0-9a-f]{8})\(.*/\2 \1/' \
 | awk '{c[$1]++; if(!($1 in f)) f[$1]=$2; l[$1]=$2} END{for(a in c) print a, c[a], f[a], l[a]}' | sort
