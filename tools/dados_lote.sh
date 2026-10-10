#!/bin/sh
# uso: dados_lote.sh <rotulo> <semente_ini> <semente_fim> [paralelo=6]  -> coleta decisoes (exploracao) em duelos CPU x CPU; ia_decisoes.jsonl em cada pasta de replay
cd "$(dirname "$0")/.."
R=$1; A=$2; B=$3; P=${4:-6}
export TF_IA_DADOS=1 TF_IA_EXPLORAR=${EXPL:-25} TF_IA_LLM=0
s=$A
while [ $s -le $B ]; do
  n=0
  while [ $n -lt $P ] && [ $s -le $B ]; do
    unset TF_ESPELHO   # os duelos com TF_ESPELHO (deck espelhado) caem no quadro ~5100 mesmo com a IA original
    TF_IA_SEMENTE=$s sh tools/arena.sh $R $s -1 "" gen/mods_v1 > /dev/null 2>&1 &
    s=$((s+1)); n=$((n+1)); sleep 4
  done
  wait
done
echo fim > gen/arena/lote_${R}_fim.txt
