#!/bin/sh
# Which LINES of the decomp C++ of a module does a recorded session run? (decomp_cobertura.sh is about the game's functions; this one is about our swaps.)
# Usage: sh tools/decomp_linhas.sh <session> <last_frame> <decomp dir> [<decomp dir> ...]      e.g.  sh tools/decomp_linhas.sh sessao_20261012_190727 3520 decomp/libfield
# Builds a separate, instrumented gen/run_cov.exe (gcc --coverage, -O0; the runtime objects come from gen/obj, so run tools/build_run.sh first), replays the
# session with it, and writes gen/cov/out/<file>.cpp.gcov (a "#####" in the first column = never ran). Prints executed / not executed lines per file.
# The exe is only a test tool: it is deleted at the end (gen/run_cov.exe, gen/cov), because the only build is gen/run.exe.
# Windows quirk: the game quits with ExitProcess, which skips the atexit that writes the .gcda files, so runtime/display.cpp is compiled from a patched COPY
# (a call to __gcov_dump before ExitProcess); the real file is not touched.
cd "$(dirname "$0")/.." || exit 1
export PATH=/c/msys64/ucrt64/bin:$PATH
S="$1"; END="$2"; shift 2
if [ -z "$S" ] || [ -z "$END" ] || [ $# -eq 0 ]; then echo "usage: sh tools/decomp_linhas.sh <session> <last_frame> <decomp dir>..."; exit 2; fi
if tasklist 2>/dev/null | grep -i -q -E '^run(_[a-z0-9]+)?\.exe'; then echo "a run*.exe is open (the game or a replay): wait or close it first"; exit 2; fi
C=gen/cov; rm -rf $C; mkdir -p $C/out
PAT=""
for d in "$@"; do
  for f in $d/*.cpp; do
    b=$(echo "${f%.cpp}" | tr '/' '_'); PAT="$PAT|$b"
    g++ -std=c++17 -O0 -g --coverage -DPSP_TRACE '-DTF_GAME_ID="ULES-00600"' -Iruntime -c "$f" -o $C/$b.o &
  done
done
wait
sed 's/diag_flush(); ExitProcess(0); }/diag_flush(); __gcov_dump(); ExitProcess(0); }/' runtime/display.cpp > runtime/display_cov_tmp.cpp
sed -i '1i extern "C" void __gcov_dump(void);' runtime/display_cov_tmp.cpp
g++ -std=c++17 -O1 -DPSP_TRACE '-DTF_GAME_ID="ULES-00600"' -Iruntime -c runtime/display_cov_tmp.cpp -o $C/zz_display.o; rm -f runtime/display_cov_tmp.cpp
OTHERS=$(ls gen/obj/*.o | grep -v -E "runtime_display\.o${PAT}")
g++ -o gen/run_cov.exe $OTHERS $C/*.o gen/*.o --coverage -static -mwindows -ldwmapi -lgdi32 -luser32 -lcomdlg32 -lshell32 -lwinmm -lsynchronization -ld3d11 -ldxgi -ld3dcompiler -lole32 -loleaut32 -luuid -lwindowscodecs -lgdiplus || { echo "link failed"; exit 1; }
RUN_EXE=gen/run_cov.exe TAG=_cov ENDF=$END sh tools/replay.sh "$S" > /dev/null 2>&1
rm -rf gen/replay/${S}_cov
for f in $C/*.gcda; do gcov -o $C "$f" > /dev/null 2>&1; done
mv *.gcov $C/out/ 2>/dev/null
for g in $C/out/*.cpp.gcov; do
  exe=$(grep -c '^ *[1-9][0-9]*\*\?:' "$g"); miss=$(grep -c '^ *#####' "$g")
  echo "$(basename "$g" .gcov): ran $exe lines, never ran $miss"
done
rm -rf gen/linhas_ultimo; cp -r $C/out gen/linhas_ultimo 2>/dev/null
rm -rf gen/run_cov.exe $C
echo "details: gen/linhas_ultimo/<file>.cpp.gcov (look for #####)"
