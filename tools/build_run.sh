#!/bin/sh
# Recompila os modulos (se preciso) e gera gen/run.exe (OUT=gen/run2.exe: outro nome, ex. com o jogo aberto). Uso: sh tools/build_run.sh [--regen]
set -e
export PATH=/c/msys64/ucrt64/bin:$PATH
# TF_VER=ULUS10136 (por exemplo) compila OUTRA versao do jogo: codigo em gen/ULUS10136/, executavel gen/run_ULUS10136.exe. Sem TF_VER: a europeia (gen/, gen/run.exe).
GV=gen${TF_VER:+/$TF_VER}
mkdir -p $GV
[ "$1" = "--regen" ] && python tools/build_modules.py
[ -f $GV/module_index.cpp ] || python tools/build_modules.py
if [ -n "$TF_VER" ]; then GAMEID=$(python -c "import sys;v=sys.argv[1];print(v[:4]+'-'+v[4:])" "$TF_VER"); EXE_DEF=gen/run_$TF_VER.exe; else GAMEID=ULES-00600; EXE_DEF=gen/run.exe; fi
OBJS=""
for f in $GV/*.c; do
  o="${f%.c}.o"
  if [ ! "$o" -nt "$f" ] || [ runtime/psp_rt.h -nt "$o" ]; then
    echo "gcc $f"; gcc -std=gnu11 -O1 -w -DPSP_TRACE $GCCX -Iruntime -c "$f" -o "$o" &
  fi
  OBJS="$OBJS $o"
done
wait
python tools/spv2h.py   # sombreadores do Vulkan (GLSL -> SPIR-V, gen/gpu_spv.h)
# icone do .exe/janela/barra de tarefas: gen/app.ico (python tools/make_icon.py; arte do jogo, fora do git); sem ele o .exe fica sem icone
# manifesto (runtime/app.manifest: controles comuns v6 = estilo atual do Windows e tema escuro dos controles) sempre; icone se houver
echo '1 24 "runtime/app.manifest"' > gen/app.rc
[ -f gen/app.ico ] && echo '1 ICON "gen/app.ico"' >> gen/app.rc
RES=""; windres gen/app.rc -O coff -o gen/app_res.o && RES=gen/app_res.o
g++ -std=c++17 ${OPT:--O2} -g -DPSP_TRACE "-DTF_GAME_ID=\"$GAMEID\"" -Wall -Wno-misleading-indentation -Iruntime -o "${OUT:-$EXE_DEF}" runtime/main_run.cpp runtime/gameid.cpp runtime/host.cpp runtime/kernel.cpp runtime/iso.cpp runtime/isofolder.cpp runtime/install.cpp runtime/hle_misc.cpp runtime/hle_io.cpp runtime/hle_ge.cpp runtime/hle_audio.cpp runtime/modules.cpp runtime/vfpu.cpp runtime/hle_utility.cpp runtime/display.cpp runtime/hle_mpeg.cpp runtime/hle_font.cpp runtime/launcher.cpp runtime/text_input.cpp runtime/diag.cpp runtime/audio_out.cpp runtime/raster_pool.cpp runtime/ffmpeg_dec.cpp runtime/menu.cpp runtime/mouse_ui.cpp runtime/gfx.cpp runtime/imgio.cpp runtime/texrepl.cpp runtime/plugins.cpp runtime/modbox.cpp runtime/gpu.cpp runtime/interp.cpp runtime/pacing.cpp runtime/hdfont.cpp runtime/jit.cpp runtime/gpu_d3d11.cpp runtime/gpu_vulkan.cpp runtime/pc_text.cpp runtime/mods.cpp runtime/util.cpp runtime/prepare.cpp runtime/dark_ui.cpp runtime/slider_dialog.cpp $GV/module_index.cpp $OBJS $RES -static -mwindows -ldwmapi -lgdi32 -luser32 -lcomdlg32 -lshell32 -lwinmm -lsynchronization -ld3d11 -ldxgi -ld3dcompiler -lole32 -loleaut32 -luuid -lwindowscodecs -lgdiplus
# os replays (tools/replay.sh) usam gen/run_bg.exe (o gen/run.exe pode estar aberto pelo jogador): no build padrao ele acompanha o run.exe
if [ -z "$OUT" ] && [ -z "$TF_VER" ]; then cp gen/run.exe gen/run_bg.exe 2>/dev/null || echo "aviso: gen/run_bg.exe em uso (replay rodando?): os replays continuam com o build anterior"; fi
python tools/ffmpeg_dlls.py gen   # DLLs do FFmpeg ao lado do run.exe (ATRAC: musica, vozes, efeitos do duelo)
sh tools/build_plugins.sh   # mods de comportamento (mods_src/<Mod> -> gen/mods/<Mod>/plugin.dll; API em mods_sdk/tfmod.h)
