#!/bin/sh
# Smoothness loop over the player's recorded sessions (logs/sessao_*.inputs): replays each one at 120 FPS in real time with the player's config and
# measures (1) cadence of every presentation (tools/analysis/ritmo_sessao.py, game's own [present] log) and (2) flicker on the real monitor output
# (tools/analysis/tela_continua.cpp + piscadas_analisar.py). Two separate passes: the continuous recorder disturbs the cadence.
# Usage: sh tools/test/ciclo_liso.sh [sessao ...]   (default: every session in logs/ not yet listed in gen/ciclo/feitas.txt)
#   FPS=120 (TF_FPS)   SO_RITMO=1 / SO_PISCADAS=1 run only one pass   EXE=path (default ./TagForceRecomp.exe; copied to a private name)
# Output: gen/ciclo/<sessao>.txt (summary). The game window goes fullscreen in the foreground for the duration of each pass.
cd "$(dirname "$0")/../.."
FPS="${FPS:-120}"; PASSO=$(python -c "print(1000/$FPS)")
mkdir -p gen/ciclo gen/logs gen/mods_teste; touch gen/ciclo/feitas.txt
[ -d gen/mods_teste/EnhancedTextures ] || cmd //c "mklink /J gen\mods_teste\EnhancedTextures mods\EnhancedTextures" > /dev/null
for d in mods/*/; do n=$(basename "$d"); [ "$n" = EnhancedTextures ] && continue; [ -f "$d/plugin.dll" ] || continue; rm -rf "gen/mods_teste/$n"; [ -f "$d/disabled" ] && continue
  mkdir -p "gen/mods_teste/$n"; for f in "$d"*; do [ "$(basename "$f")" = llm ] || cp -r "$f" "gen/mods_teste/$n/"; done; done
[ -f gen/tela_continua.exe ] && [ gen/tela_continua.exe -nt tools/analysis/tela_continua.cpp ] || PATH=/c/msys64/ucrt64/bin:$PATH g++ -O2 -o gen/tela_continua.exe tools/analysis/tela_continua.cpp -ld3d11 -ldxgi || exit 1
SRC="${EXE:-./TagForceRecomp.exe}"; cp "$SRC" ./TagForceRecomp_ciclo.exe; EXEC="$(pwd)/TagForceRecomp_ciclo.exe"
SESS="$*"; [ -n "$SESS" ] || for f in logs/sessao_*.inputs; do s=$(basename "$f" .inputs); grep -qx "$s" gen/ciclo/feitas.txt || [ ! -s "$f" ] || SESS="$SESS $s"; done
for s in $SESS; do
  [ -f "logs/$s.log" ] || continue
  OUT=gen/ciclo/$s.txt; : > "$OUT"
  cp "logs/$s.log" "logs/$s.inputs" gen/logs/; rm -rf "gen/logs/${s}_ms0"; cp -r "logs/${s}_ms0" gen/logs/ 2>/dev/null
  LAST=$(tr ',' '\n' < "logs/$s.inputs" | grep : | tail -1 | cut -d: -f1); ENDF=$((LAST + 600))
  GAMEFR=$(grep -o 'sessao: [0-9]* quadros' "logs/$s.log" | grep -o '[0-9]*'); [ -n "$GAMEFR" ] && ENDF=$((GAMEFR - 5))
  run() { TF_FPS=$FPS TF_WINDOW="0,0,1920,1080,1,0,0" PACE=1 PRIMEIRO_PLANO=1 INI=tagforce_recomp.ini MODDIR=gen/mods_teste RECENT=gen/texturas_recentes.txt \
          TF_TEX_CACHE_DIR="$(pwd -W)/texcache" TF_PRESENT_LOG=1 RUN_EXE="$EXEC" ENDF=$ENDF SNAPS=$ENDF TAG="$1" sh tools/test/replay.sh "$s" 100000 > "gen/ciclo/run$1.out" 2>&1; }
  if [ -z "$SO_PISCADAS" ]; then
    run _cr; echo "== RITMO $s ($FPS FPS, passo $PASSO ms)" >> "$OUT"
    python tools/analysis/ritmo_sessao.py "gen/replay/$s""_cr" --passo-ms "$PASSO" --bloco 1500 >> "$OUT" 2>&1
  fi
  if [ -z "$SO_RITMO" ]; then
    D="gen/ciclo/grav_$s"; rm -rf "$D"; mkdir -p "$D"; ./gen/tela_continua.exe "$D" --manter 30 2> "$D/gravador.log" &
    sleep 2; run _cp; sleep 2; taskkill //F //IM tela_continua.exe > /dev/null 2>&1
    echo "== PISCADAS $s" >> "$OUT"; python tools/analysis/piscadas_analisar.py --continuo "$D" --png "gen/ciclo/png_$s" --max-pares 40 >> "$OUT" 2>&1
    rm -f "$D"/seg_*.bin
  fi
  [ -n "$KEEP" ] || rm -rf "gen/replay/$s""_cr" "gen/replay/$s""_cp"
  echo "$s" >> gen/ciclo/feitas.txt; cat "$OUT"
done
rm -f ./TagForceRecomp_ciclo.exe
