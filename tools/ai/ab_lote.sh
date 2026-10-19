#!/bin/sh
# uso: ab_lote.sh <rotulo> <lado> <pasta_mods> <semente_ini> <semente_fim> [paralelo]  (TF_IA_LLM=0 no ambiente = so as regras do plugin)
cd "$(dirname "$0")/../.."
R=$1; L=$2; M=$3; A=$4; B=$5; P=${6:-5}; unset TF_ESPELHO TF_IA_EXPLORAR; [ -z "$DADOS" ] && unset TF_IA_DADOS; [ -n "$DADOS" ] && export TF_IA_DADOS=1
s=$A
while [ $s -le $B ]; do
  n=0
  while [ $n -lt $P ] && [ $s -le $B ]; do TIMEOUT=1800 sh tools/test/arena.sh $R $s $L "" $M > /dev/null 2>&1 & s=$((s+1)); n=$((n+1)); sleep 4; done
  wait
done
echo fim > gen/arena/lote_${R}_fim.txt
