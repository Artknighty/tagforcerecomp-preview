#!/bin/sh
# The ONE build of the project: incremental build of the generic runtime into the dev package folder
#   gen/pkg/TagForceRecomp/TagForceRecomp.exe   (same layout as the release zip: tcc/, tools/, include/, mods/, DLLs next to the exe)
# plus gen/pkg/TagForceRecomp/TagForceRecomp_bg.exe, a copy that replays and tests use, so the exe you play with can stay open.
# The first run (or a missing folder) also assembles the rest of the package with tools/package_release.py --no-build.
# Usage: sh tools/build.sh      Variables: OPT=-O1 (optimization, default -O2), OUT=other.exe, JOBS=n
set -e
export PATH=/c/msys64/ucrt64/bin:$PATH
PKG=gen/pkg/TagForceRecomp
mkdir -p $PKG
python tools/build/spv2h.py   # Vulkan shaders (GLSL -> SPIR-V, gen/gpu_spv.h)
echo '1 24 "runtime/app.manifest"' > gen/pkg/app.rc
[ -f runtime/app.ico ] && echo '1 ICON "runtime/app.ico"' >> gen/pkg/app.rc
windres gen/pkg/app.rc -O coff -o gen/pkg/app_res.o
CPPSRCS=$(ls runtime/*/*.cpp decomp/decomp.cpp decomp/*/*.cpp)   # every runtime and decomp source (tools/package_release.py builds the same set)
# incremental: each .cpp becomes a .o in gen/pkg/obj (tools/build/cc_one.sh, in parallel); only what changed (source or a header listed in the .d) is rebuilt
OD=gen/pkg/obj; mkdir -p $OD
GAMEID=ULES-00600
STAMP="${OPT:--O2} $GAMEID"
[ "$(cat $OD/flags.txt 2>/dev/null)" = "$STAMP" ] || { rm -f $OD/*.o $OD/*.d; echo "$STAMP" > $OD/flags.txt; }
export CXXOPT="${OPT:--O2}" GAMEID
TODO=""; CPPOBJS=""
for s in $CPPSRCS; do
  o=$OD/$(echo "${s%.cpp}" | tr '/' '_').o; CPPOBJS="$CPPOBJS $o"
  need=0
  if [ ! -f "$o" ] || [ ! -f "${o%.o}.d" ] || [ "$s" -nt "$o" ]; then need=1
  else for h in $(sed -e 's/^[^:]*://' -e 's/\\$//' "${o%.o}.d" | tr ' ' '\n' | grep .); do
    if [ ! -e "$h" ] || [ "$h" -nt "$o" ]; then need=1; break; fi
  done; fi
  [ $need = 1 ] && TODO="$TODO $s:$o"
done
if [ -n "$TODO" ]; then printf '%s\n' $TODO | tr ':' ' ' | xargs -P ${JOBS:-$(nproc)} -L1 sh tools/build/cc_one.sh; fi
EXE="${OUT:-$PKG/TagForceRecomp.exe}"
g++ -o "$EXE" $CPPOBJS gen/pkg/app_res.o -static -mwindows -ldwmapi -lgdi32 -luser32 -lcomdlg32 -lshell32 -lwinmm -lsynchronization -ld3d11 -ldxgi -ld3dcompiler -lole32 -loleaut32 -luuid -lwindowscodecs -lgdiplus
if [ -z "$OUT" ]; then
  [ -d $PKG/tcc ] || python tools/package_release.py --no-build --version dev --out gen/pkg   # first time: tcc, tools/python, DLLs, mods, docs
  cp $PKG/TagForceRecomp.exe $PKG/TagForceRecomp_bg.exe 2>/dev/null || echo "note: TagForceRecomp_bg.exe is in use (a replay is running?): replays keep the previous build"
  # the repo root is also a ready-to-run install (plug and play): exe plus the runtime pieces the release zip carries (all git-ignored)
  for d in tcc include tools/python; do [ -d $PKG/$d ] && mkdir -p ./$d && cp -ru $PKG/$d/. ./$d/; done
  cp -u $PKG/*.dll ./ 2>/dev/null
  cp -u assets/branding/yugioh.png ./ 2>/dev/null   # title-screen thumbnail the launcher loads next to the exe
  cp -u $PKG/tools/translator_version.txt ./tools/ 2>/dev/null
  cp $PKG/TagForceRecomp.exe ./TagForceRecomp.exe 2>/dev/null || echo "note: ./TagForceRecomp.exe is in use; root copy not updated"
  # the game data (jogo/) and the translated code (gen/) are made once from your ISO: sh tools/install_dev.sh
fi
echo "built: $EXE"
