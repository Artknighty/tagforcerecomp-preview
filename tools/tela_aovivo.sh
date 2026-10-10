#!/bin/sh
# Teste AO VIVO do que chega ao monitor: reproduz uma sessao em tempo real, em tela cheia no monitor principal, com a configuracao
# exata do jogador (gen/tagforce_recomp.ini, so lida) e o pacote de texturas, e grava com tools/tela_gravar.cpp as imagens que o
# Windows compoe a partir do momento em que <texto> aparece no log da reproducao. Para piscadas/engasgos "entre um quadro e outro"
# que nenhuma captura interna mostra. A janela do teste fica em PRIMEIRO PLANO (warn the player; a area de trabalho precisa estar desbloqueada).
# Uso: sh tools/tela_aovivo.sh <sessao> "<texto no log>" [segundos=9] [y0=800 y1=1000]
#   variaveis: TAG (pasta, padrao _aovivo), ENDF (quadro final), e qualquer outra repassada ao jogo (ex.: TF_INTERP_NOLOOP=1 para A/B)
#   Ex.: Duel Result da sessao 182226: TF_IA_LLM=1 TF_IA_SEM=ordem ENDF=21900 sh tools/tela_aovivo.sh sessao_20261006_182226 "DP do duelo"
# Resultado: gen/replay/<sessao><TAG>/tela.{tsv,bin}; analise: python tools/tela_analisar.py gen/replay/<sessao><TAG>/tela
# Se o jogo terminar sem o texto aparecer no log, o gravador e encerrado (nada fica preso esperando).
cd "$(dirname "$0")/.."
S="$1"; TXT="$2"; SECS="${3:-9}"; Y0="${4:-800}"; Y1="${5:-1000}"; TAG="${TAG:-_aovivo}"
[ -n "$S" ] && [ -n "$TXT" ] || { echo "uso: sh tools/tela_aovivo.sh <sessao> \"<texto no log>\" [segundos] [y0 y1]"; exit 1; }
[ -f gen/tela_gravar.exe ] || PATH=/c/msys64/ucrt64/bin:$PATH g++ -O2 -o gen/tela_gravar.exe tools/tela_gravar.cpp -ld3d11 -ldxgi || exit 1
W="gen/replay/$S$TAG"; rm -rf "$W"
( TF_WINDOW="200,150,1000,600,1,0,0" PACE=1 PRIMEIRO_PLANO=1 INI=gen/tagforce_recomp.ini MODDIR=gen/mods RECENT=gen/texturas_recentes.txt \
  TAG="$TAG" SNAPS="${ENDF:-999999999}" sh tools/replay.sh "$S" 100000 1800 > "$W.out" 2>&1 ) &
JOGO=$!
LOG=""; for i in $(seq 1 600); do LOG=$(ls "$W"/logs/sessao_*.log 2>/dev/null | head -1); [ -n "$LOG" ] && break; sleep 1; done
[ -n "$LOG" ] || { echo "a reproducao nao comecou (veja $W.out)"; wait; exit 1; }
mkdir -p "$W"
./gen/tela_gravar.exe "$W/tela" "$SECS" "$Y0" "$Y1" --esperar "$LOG" "$TXT" &
GRAV=$!
wait $JOGO
sleep 2
if kill -0 $GRAV 2>/dev/null; then
  kill $GRAV 2>/dev/null; taskkill //F //IM tela_gravar.exe > /dev/null 2>&1
  echo "o texto \"$TXT\" nao apareceu no log da reproducao: nada gravado"; exit 1
fi
wait $GRAV
echo "gravado em $W/tela.* (analise: python tools/tela_analisar.py $W/tela)"
