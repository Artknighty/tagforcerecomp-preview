#!/bin/sh
# Teste AO VIVO do que chega ao monitor: reproduz uma sessao em tempo real, em tela cheia no monitor principal, com a configuracao
# exata do jogador (gen/tagforce_recomp.ini, so lida) e o pacote de texturas, e grava com tools/analysis/tela_gravar.cpp as imagens que o
# Windows compoe a partir do momento em que <texto> aparece no log da reproducao. Para piscadas/engasgos "entre um quadro e outro"
# que nenhuma captura interna mostra, e para o ritmo do 60/120 FPS. A janela do teste fica em PRIMEIRO PLANO (avise o jogador; a area
# de trabalho precisa estar desbloqueada).
# Uso: sh tools/analysis/tela_aovivo.sh <sessao> "<texto no log>" [segundos=9] [y0=800 y1=1000]
#   variaveis: OSK="texto1|texto2" (o que digitar nas caixas de texto do jogo, como o 4o argumento do replay.sh), TAG (pasta, padrao _aovivo), ENDF (quadro final), RUN_EXE (outro executavel), SO_TEMPOS=1 (grava so os horarios das imagens:
#   para uma sessao inteira; analise com tools/analysis/fluidez_analisar.py), e qualquer outra repassada ao jogo
#   (ex.: TF_FPS=120, TF_RITMO_FASE=0.5, TF_INTERP_NOLOOP=1 para A/B)
#   Ex.: tela Versus da sessao 205658 (fundo animado): TF_FPS=120 ENDF=3400 sh tools/analysis/tela_aovivo.sh sessao_20261014_205658 DrawPulsingPanel 10 300 700
# Resultado: gen/replay/<sessao><TAG>/tela.{tsv,bin} e log.txt (com TF_PRESENT_LOG=1: hora de cada apresentacao)
# Analise: python tools/analysis/ritmo_analisar.py gen/replay/<sessao><TAG> [--ciclos]   (ritmo)   |   python tools/analysis/tela_analisar.py .../tela (piscadas)
# Mods: gen/mods_teste (EnhancedTextures com textures -> o pacote real; TexturasPC desligado), NUNCA gen/mods: a reproducao grava os
# arquivos disabled quando a sessao troca Textures e mudaria a configuracao do jogador.
# Se o jogo terminar sem o texto aparecer no log, o gravador e encerrado (nada fica preso esperando).
cd "$(dirname "$0")/../.."
S="$1"; TXT="$2"; SECS="${3:-9}"; Y0="${4:-800}"; Y1="${5:-1000}"; TAG="${TAG:-_aovivo}"
[ -n "$S" ] && [ -n "$TXT" ] || { echo "uso: sh tools/analysis/tela_aovivo.sh <sessao> \"<texto no log>\" [segundos] [y0 y1]"; exit 1; }
[ -f gen/tela_gravar.exe ] && [ gen/tela_gravar.exe -nt tools/analysis/tela_gravar.cpp ] || PATH=/c/msys64/ucrt64/bin:$PATH g++ -O2 -o gen/tela_gravar.exe tools/analysis/tela_gravar.cpp -ld3d11 -ldxgi || exit 1
[ -d gen/mods_teste/EnhancedTextures ] || { echo "falta gen/mods_teste/EnhancedTextures"; exit 1; }
rm -f gen/mods_teste/EnhancedTextures/disabled; mkdir -p gen/mods_teste/TexturasPC; [ -f gen/mods_teste/TexturasPC/disabled ] || echo off > gen/mods_teste/TexturasPC/disabled
# os plugins de comportamento ligados do jogador (como o replay.sh sem MODDIR): sem eles um duelo reproduzido diverge
for d in gen/mods/*/; do n=$(basename "$d"); [ -f "$d/plugin.dll" ] || continue; rm -rf "gen/mods_teste/$n"; [ -f "$d/disabled" ] && continue
  mkdir -p "gen/mods_teste/$n"; for f in "$d"*; do [ "$(basename "$f")" = llm ] || cp -r "$f" "gen/mods_teste/$n/"; done; done
W="gen/replay/$S$TAG"; rm -rf "$W"
( TF_WINDOW="0,0,1920,1080,1,0,0" PACE=1 PRIMEIRO_PLANO=1 INI="${INI:-gen/tagforce_recomp.ini}" MODDIR=gen/mods_teste RECENT=gen/texturas_recentes.txt \
  TF_PRESENT_LOG=1 TF_LAT_DEBUG=1 TAG="$TAG" SNAPS="${ENDF:-999999999}" sh tools/test/replay.sh "$S" 100000 1800 "$OSK" > "$W.out" 2>&1 ) &
JOGO=$!
LOG=""; for i in $(seq 1 600); do LOG=$(ls "$W"/logs/sessao_*.log 2>/dev/null | head -1); [ -n "$LOG" ] && break; sleep 1; done
[ -n "$LOG" ] || { echo "a reproducao nao comecou (veja $W.out)"; wait; exit 1; }
mkdir -p "$W"
./gen/tela_gravar.exe "$W/tela" "$SECS" "$Y0" "$Y1" --esperar "$LOG" "$TXT" ${SO_TEMPOS:+--so-tempos} &
GRAV=$!
wait $JOGO
sleep 2
if kill -0 $GRAV 2>/dev/null; then
  kill $GRAV 2>/dev/null; taskkill //F //IM tela_gravar.exe > /dev/null 2>&1
  # gravacao mais longa que o resto da sessao: o que foi gravado vale (o gravador grava o .tsv aos poucos)
  grep -qF "$TXT" "$LOG" || { echo "o texto \"$TXT\" nao apareceu no log da reproducao: nada gravado"; exit 1; }
else wait $GRAV; fi
# o cache de texturas decodificadas (~700 MB) e as capturas da reproducao nao servem para a analise: sem apagar, cada teste deixava ~1 GB e o
# disco C: encheu (2026-10-15). TELA_BIN=0 apaga tambem a gravacao (tela.bin, ~300 MB; o ritmo_analisar so usa tela.tsv e o log)
rm -rf "$W/texcache" "$W/gen"; [ "${TELA_BIN:-1}" = 0 ] && rm -f "$W/tela.bin"
echo "gravado em $W/tela.* (analise: python tools/analysis/ritmo_analisar.py $W  |  python tools/analysis/tela_analisar.py $W/tela)"
