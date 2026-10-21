#!/bin/sh
# Audio/pacing check at the title screen (Press Start), no input: runs the dev package with the real limiter and the audio path on (device muted),
# then prints per-second game speed and audio stretch. Usage:
#   sh tools/test/audio_ritmo.sh <tag> [seconds]      env: TF_FPS (30/60/120), TF_GPU (1 = D3D11, 2 = Vulkan), TF_SIM_ATRASO="p,ms", TF_RITMO_RECUPERA=0
# The window is visible (a hidden window blocks on present and distorts pacing); the save is a fresh copy, the .ini and logs are the test's own.
cd "$(dirname "$0")/../.."
TAG="${1:-t}"; SECS="${2:-40}"; D="gen/audio_ritmo/$TAG"
rm -rf "$D"; mkdir -p "$D"; cp -r gen/ms0 "$D/ms0" 2>/dev/null
W=$(cygpath -m "$PWD/$D")
export TF_KEEP_CWD=1 TF_LOG_DIR="$W/logs" TF_INI="$W/teste.ini" TF_LAT_DEBUG=1 TF_AUDIO_MUDO=1 TF_NO_MOVIES=1 TF_CONFIRM_AUTO=0
export TF_MODS_DIR="$W/mods"; mkdir -p "$D/mods"
cp gen/pkg/TagForceRecomp/TagForceRecomp.exe gen/pkg/TagForceRecomp/TagForceRecomp_bg.exe || { echo "TagForceRecomp_bg.exe in use: close the running test first"; exit 1; }
EXE="$PWD/gen/pkg/TagForceRecomp/TagForceRecomp_bg.exe"; DATA=$(cygpath -m "$PWD/gen/pkg/TagForceRecomp/jogo")
( cd "$D" && EXIT_FRAME=$((SECS * 60)) timeout "$((SECS + 30))" "$EXE" --dados "$DATA" > /dev/null 2>&1 )
python tools/analysis/audio_ritmo_resumo.py "$D"/logs/*.log
