#!/bin/sh
# A/B de UMA regra do plugin Better AI: o jogador <lado> roda com a regra DESLIGADA (TF_IA_SEM=<regra>), o outro com o plugin completo; decks forcados por TF_DECK_J0/J1.
# uso: ab_regra.sh <rotulo> <regra> <arquivo_pares> <pasta_mods> [paralelo]; linha do arquivo: "deckJ0 deckJ1 semente lado". regras: invocar posicao segurar monstro darkhole extras
# Resultado: gen/arena/res_<rotulo><lado>_<a>x<b>_<semente>.txt. Se o lado SEM a regra ganha mais de ~55 %, a regra atrapalha; abaixo de ~45 %, ela ajuda.
cd "$(dirname "$0")/.."
R=$1; REGRA=$2; PAR=$3; M=$4; P=${5:-6}
export TIMEOUT=2400 TF_IA_LLM=0; unset TF_ESPELHO TF_IA_LLM_LADO TF_IA_DADOS TF_IA_EXPLORAR TF_IA_VALOR
n=0
while read a b s l; do
  TF_IA_SEM=$REGRA TF_IA_SEM_LADO=$l TF_DECK_J0=$a TF_DECK_J1=$b sh tools/arena.sh ${R}${l}_${a}x${b} $s -1 "" $M > /dev/null 2>&1 &
  n=$((n+1)); sleep 3
  if [ $((n % P)) -eq 0 ]; then wait; fi
done < $PAR
wait
