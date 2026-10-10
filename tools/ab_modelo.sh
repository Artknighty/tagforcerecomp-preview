#!/bin/sh
# A/B pareado do MODELO de linguagem: o modelo decide so no jogador <lado>, as regras do plugin no outro; decks forcados por TF_DECK_J0/J1.
# uso: ab_modelo.sh <rotulo> <arquivo_pares> <pasta_mods> [paralelo]; linha do arquivo: "deckJ0 deckJ1 semente lado". Resultado: gen/arena/res_<rotulo><lado>_<a>x<b>_<semente>.txt
cd "$(dirname "$0")/.."
R=$1; PAR=$2; M=$3; P=${4:-6}
export TIMEOUT=2400; unset TF_ESPELHO TF_IA_LLM TF_IA_DADOS TF_IA_EXPLORAR TF_IA_VALOR
n=0
while read a b s l; do
  TF_IA_LLM_LADO=$l TF_DECK_J0=$a TF_DECK_J1=$b sh tools/arena.sh ${R}${l}_${a}x${b} $s -1 "" $M > /dev/null 2>&1 &
  n=$((n+1)); sleep 3
  if [ $((n % P)) -eq 0 ]; then wait; fi
done < $PAR
wait
echo fim > gen/arena/abm_${R}_fim.txt
