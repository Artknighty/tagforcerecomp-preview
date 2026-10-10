#!/bin/sh
# A/B da funcao de valor: uso: ab_valor.sh <rotulo> <arquivo_pares> <pasta_mods> [paralelo]; linha do arquivo: "deckJ0 deckJ1 semente lado" (lado = jogador que usa a nota aprendida, o outro usa as regras)
cd "$(dirname "$0")/.."
R=$1; PAR=$2; M=$3; P=${4:-6}
export TF_IA_LLM=0 TF_IA_VALOR=1 TIMEOUT=2400; unset TF_ESPELHO TF_IA_LLM_LADO TF_IA_DADOS TF_IA_EXPLORAR
n=0
while read a b s l; do
  TF_IA_VALOR_LADO=$l TF_DECK_J0=$a TF_DECK_J1=$b sh tools/arena.sh ${R}${l}_${a}x${b} $s -1 "" $M > /dev/null 2>&1 &
  n=$((n+1)); sleep 3
  if [ $((n % P)) -eq 0 ]; then wait; fi
done < $PAR
wait
echo fim > gen/arena/abv_${R}_fim.txt
