#!/bin/sh
# coleta com o MODELO decidindo + exploracao, em tempo real (PACE=1: a GPU nao satura e o servidor responde rapido). uso: dados_modelo.sh <rotulo> <ini> <fim> <pacote> [paralelo]
cd "$(dirname "$0")/.."
R=$1; A=$2; B=$3; M=$4; P=${5:-5}
export TF_IA_DADOS=1 TF_IA_EXPLORAR=${EXPL:-30} PACE=1 TIMEOUT=3000; unset TF_ESPELHO TF_IA_LLM_LADO
s=$A
while [ $s -le $B ]; do
  n=0
  while [ $n -lt $P ] && [ $s -le $B ]; do TF_IA_SEMENTE=$s sh tools/arena.sh $R $s -1 "" $M > /dev/null 2>&1 & s=$((s+1)); n=$((n+1)); sleep 5; done
  wait
done
echo fim > gen/arena/lote_${R}_fim.txt
