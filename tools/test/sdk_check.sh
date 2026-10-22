#!/bin/sh
# Guards the plugin SDK: after ANY change to plugins/sdk/ or to the runtime's plugin loader, run this to know nothing broke the mods.
#   1. the ABI of TfModApi is frozen (plugins/sdk/abi_check.cpp: every field keeps its offset)
#   2. every mod of plugins/src/ and plugins/examples/ still compiles against the SDK (the same flags as the release)
#   3. with --run: the real game loads every mod and runs to the first frame (needs the installed game data, see tools/test/tf.py)
# Usage: sh tools/test/sdk_check.sh [--run]
cd "$(dirname "$0")/../.."
export PATH=/c/msys64/ucrt64/bin:$PATH
fail=0
g++ -std=c++17 -fsyntax-only -Iplugins/sdk plugins/sdk/abi_check.cpp && echo "abi: ok" || { echo "abi: BROKEN"; fail=1; }
OUT=gen/sdk_check_mods
for d in plugins/src/* plugins/examples/*; do
  [ -d "$d" ] || continue
  srcs=$(ls "$d"/*.cpp "$d"/*.c 2>/dev/null)
  [ -n "$srcs" ] || continue
  m=$(basename "$d"); mkdir -p "$OUT/$m"; cp "$d/mod.txt" "$OUT/$m/" 2>/dev/null
  if g++ -std=c++17 -O1 -Wall -Wno-misleading-indentation -shared -static -s -Iplugins/sdk -o "$OUT/$m/plugin.dll" $srcs -lwinhttp -lws2_32 2> "$OUT/$m/build.log"; then
    echo "build: $m ok"
  else echo "build: $m FAILED"; cat "$OUT/$m/build.log"; fail=1; fi
done
if [ "$1" = "--run" ] && [ $fail = 0 ]; then
  # every mod switched on at once (they only load; no duel runs, so the deck-reader mods do not fight), the game started hidden: each must
  # report 'plugin carregado' and nothing may crash
  M=gen/sdk_check_run; python - "$OUT" "$M" <<'PY'
import os, shutil, sys
src, dst = sys.argv[1:3]
shutil.rmtree(dst, ignore_errors=True); os.makedirs(dst)
for m in os.listdir(src):
    if not os.path.isdir(os.path.join(src, m)): continue
    shutil.copytree(os.path.join(src, m), os.path.join(dst, m))
    for n in ('disabled', 'build.log'):
        p = os.path.join(dst, m, n)
        if os.path.exists(p): os.remove(p)
PY
  TF_MODS_FROM="$M" python tools/test/tf.py start > gen/sdk_check_run.log 2>&1
  L=$(ls -t gen/logs_teste/*.log 2>/dev/null | head -1)
  python tools/test/tf.py stop > /dev/null 2>&1
  for d in "$M"/*; do
    n=$(basename "$d"); [ -f "$d/mod.txt" ] || continue
    nm=$(sed -n 's/^name=//p' "$d/mod.txt" | head -1 | tr -d '\r')
    grep -aq "\[mods\] $nm.*plugin carregado" "$L" && echo "run: $n loaded" || { echo "run: $n NOT LOADED"; fail=1; }
  done
  grep -aq "CRASH" "$L" && { echo "run: CRASH in the log"; fail=1; }
fi
rm -rf gen/sdk_check_run
[ $fail = 0 ] && echo "SDK CHECK: OK" || echo "SDK CHECK: FAILED"
exit $fail
