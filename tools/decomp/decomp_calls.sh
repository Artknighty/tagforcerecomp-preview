#!/bin/sh
# Compares what a Module does at its boundaries with its swaps on and with them left out: every call into the engine (import stubs, with all argument
# registers) and every entry of the Module's own functions (four argument registers). The generated code and the swaps must produce the same sequence;
# the first difference shows where they stop agreeing (the Ghidra pseudo-C hides arguments, so this is the real check).
# Usage: sh tools/decomp/decomp_calls.sh <session> <last_frame> <Module> <skip prefix> <stub range lo-hi (hex)> [first_frame=0] [tag_suffix]
#   RUN_EXE=gen/pkg/TagForceRecomp/TagForceRecomp_bg.exe by default. tag_suffix makes unique output folders (e.g. _r2 -> _cn_r2 / _cf_r2).
#   libfield: sh tools/decomp/decomp_calls.sh sessao_20261012_190727 3520 libfield libfield. 091ed540-091efc30 0 _r1
# decomp/<Module>/trace_arity.txt ("address ints floats" per line) corrects the argument counts the tool derives from Ghidra.
# Functions listed (one hex address per line) in decomp/<Module>/trace_skip.txt are not traced: the ones the swaps inline instead of calling.
# NORUN=1 compares the traces of the last run again (after changing this filter). Stack addresses (09f..) are masked; frame numbers are left out. The full traces stay in gen/replay/<session>_cn<suffix>/calls.txt (swaps on) and _cf<suffix>/calls.txt.
cd "$(dirname "$0")/../.."
S="$1"; END="$2"; MOD="$3"; PREFIX="$4"; RANGE="$5"; FROM="${6:-0}"; TAG_SUFFIX="${7:-}"
case "$TAG_SUFFIX" in *[!A-Za-z0-9_-]*) echo "tag_suffix may contain only letters, digits, _ and -"; exit 2;; esac
CN_TAG="_cn${TAG_SUFFIX}"; CF_TAG="_cf${TAG_SUFFIX}"
CN_DIR="gen/replay/${S}${CN_TAG}"; CF_DIR="gen/replay/${S}${CF_TAG}"
if [ -z "$NORUN" ]; then
  if [ -e "$CN_DIR" ] || [ -e "$CF_DIR" ]; then
    echo "refusing to replace existing replay output: $CN_DIR or $CF_DIR (choose a new tag_suffix)"; exit 2
  fi
fi
python tools/decomp/decomp_arity.py $MOD > /dev/null
SKIPFILE=decomp/$MOD/trace_skip.txt; [ -f $SKIPFILE ] || SKIPFILE=/dev/null
ADDRS=$(tail -n +2 gen/ferramentas_decomp/export/$MOD/_funcoes.tsv | cut -f1 | grep -v -i -x -f $SKIPFILE | paste -sd,)
norm() { python tools/decomp/decomp_calls_norm.py "$1" gen/arity_$MOD.txt "$FROM" 091ed5a0,091ed5b0,091ed5b8,091ed5c0,091ed5e0,091ed5e8,091ed5f0,091ed5f8,091ed608 decomp/$MOD/trace_arity.txt $SKIPFILE > "$2"; }
export TF_CALL_TRACE="$RANGE"; export TF_HOOK_TRACE="$ADDRS"
[ -z "$NORUN" ] && for M in n f; do
  if [ $M = f ]; then export TF_DECOMP_SKIP="$PREFIX"; else unset TF_DECOMP_SKIP; fi
  TAG="${CN_TAG}"; [ $M = f ] && TAG="${CF_TAG}"
  RUN_EXE=${RUN_EXE:-gen/pkg/TagForceRecomp/TagForceRecomp_bg.exe} ENDF=$END TAG="$TAG" SNAPS=$END sh tools/test/replay.sh $S > /dev/null 2>&1 &
done
wait
unset TF_DECOMP_SKIP
r=gen/replay
norm $CN_DIR/log.txt $CN_DIR/calls.txt; norm $CF_DIR/log.txt $CF_DIR/calls.txt
a=$(wc -l < $CN_DIR/calls.txt); b=$(wc -l < $CF_DIR/calls.txt); n=$a; [ $b -lt $n ] && n=$b
echo "events: swaps on $a, swaps off $b (comparing the first $n); swaps exercised: $(grep -c "first call of $PREFIX" $CN_DIR/log.txt)"
head -n $n $CN_DIR/calls.txt > $CN_DIR/calls_cut.txt; head -n $n $CF_DIR/calls.txt > $CF_DIR/calls_cut.txt
if cmp -s $CN_DIR/calls_cut.txt $CF_DIR/calls_cut.txt; then echo "IDENTICAL"; else
  echo "DIFFERENT, first differences (< swaps on, > swaps off):"; diff $CN_DIR/calls_cut.txt $CF_DIR/calls_cut.txt | head -${LINES_SHOWN:-30}
  echo "first different line: $(cmp $CN_DIR/calls_cut.txt $CF_DIR/calls_cut.txt | head -1)"
fi
