#!/bin/sh
# Torneio de decks na arena (CPU x CPU, regras do plugin). uso: torneio.sh <rotulo> <arquivo_de_pares> <pasta_mods> [paralelo]
# arquivo de pares: uma linha "deckJ0 deckJ1 semente" por duelo. Resultado em gen/arena/res_<rotulo>_<a>x<b>_<semente>.txt (formato da arena)
cd "$(dirname "$0")/../.."
R=$1; PAR=$2; M=$3; P=${4:-6}
export TF_IA_LLM=0 TF_IA_DADOS=1 TF_IA_EXPLORAR=${EXPL:-0} TIMEOUT=2400; unset TF_ESPELHO TF_IA_LLM_LADO
n=0
while read a b s; do
  TF_DECK_J0=$a TF_DECK_J1=$b sh tools/test/arena.sh ${R}_${a}x${b} $s -1 "" $M > /dev/null 2>&1 &
  n=$((n+1)); sleep 4
  if [ $((n % P)) -eq 0 ]; then wait; fi
done < $PAR
wait
echo fim > gen/arena/torneio_${R}_fim.txt
