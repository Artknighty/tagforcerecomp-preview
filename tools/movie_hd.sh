#!/bin/sh
# Gera o video substituto HD de um filme do jogo (mod AberturaHD): extrai o .pmf da ISO, amplia os quadros por IA e codifica
# em H.264 (sem B-frames, com delimitadores de AU) em gen/mods/<MOD>/movie/<nome>.h264. Uso:
#   sh tools/movie_hd.sh <nome do pmf sem extensao> [modelo] [escala]       ex.: sh tools/movie_hd.sh opening_a
# Variaveis: MOD (pasta do mod, padrao AberturaHD) · CRF (padrao 14; 17 = arquivo bem menor) · ISO · KEEP=1 (nao apaga os quadros)
# Precisa de: gen/ferramentas_ai/realesrgan (realesrgan-ncnn-vulkan, Real-ESRGAN v0.2.5.0 windows) e FFmpeg do MSYS2.
# O filme original continua mandando no tempo e no audio; o video HD precisa ter 1920x1088 e o MESMO numero de quadros.
set -e
cd "$(dirname "$0")/.."
NAME="${1:?uso: sh tools/movie_hd.sh <nome>}"; MODEL="${2:-realesr-animevideov3}"; SCALE="${3:-4}"
MOD="${MOD:-AberturaHD}"; CRF="${CRF:-14}"
ISO="${ISO:?set ISO to the path of your Tag Force ISO}"
FF=/c/msys64/ucrt64/bin/ffmpeg.exe
W="gen/movie_hd/$NAME"; mkdir -p "$W/frames" "$W/up" "gen/mods/$MOD/movie"
python - "$ISO" "$NAME" "$W/$NAME.pmf" <<'PY'
import sys
sys.path.insert(0, 'tools')
import isofs
cls = [v for v in vars(isofs).values() if isinstance(v, type)][0]
open(sys.argv[3], 'wb').write(cls(sys.argv[1]).ler('PSP_GAME/USRDIR/movie/%s.pmf' % sys.argv[2]))
PY
$FF -hide_banner -loglevel error -y -i "$W/$NAME.pmf" -fps_mode passthrough "$W/frames/f_%05d.png"
echo "quadros: $(ls "$W/frames" | wc -l)"
(cd gen/ferramentas_ai/realesrgan && ./realesrgan-ncnn-vulkan.exe -i "../../../$W/frames" -o "../../../$W/up" -n "$MODEL" -s "$SCALE" -f png -j 2:4:4)
$FF -hide_banner -loglevel error -y -framerate 30000/1001 -i "$W/up/f_%05d.png" -c:v libx264 -preset medium -crf "$CRF" -bf 0 -g 60 -pix_fmt yuv420p \
    -x264-params "keyint=60:nal-hrd=none" -bsf:v h264_metadata=aud=insert -f h264 "gen/mods/$MOD/movie/$NAME.h264"
[ -z "$KEEP" ] && rm -rf "$W/frames" "$W/up"
ls -la "gen/mods/$MOD/movie/$NAME.h264"
