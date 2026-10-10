#!/bin/sh
# Arena de IA: duelos CPU x CPU (mod CPUvsCPU) para medir a IA. Roda o Free Duel gravado em gen/arena/inputs_fd_shift.txt (deck de um lado x deck do outro) com semente
# de sorteio fixa e imprime o resultado. Uso: sh tools/arena.sh <rotulo> <semente> <lado_melhorado> [primeiro=0|1] [pasta_mods]
#   lado_melhorado: -1 = IA Aprimorada nos dois lados, 0 ou 1 = so nesse jogador (o outro usa a IA original do jogo), n = IA original nos dois
#   primeiro: quem comeca o duelo (padrao: sorteio da semente)
# Resultado: gen/arena/res_<rotulo>_<semente>.txt com a linha "FIM ..." (quem venceu), ou "sem fim". Detalhes de cada jogada: gen/replay/<...>/log.txt
# (python tools/ia_monitor.py <log>).
cd "$(dirname "$0")/.."
S=$(cat gen/arena/sessao.txt 2>/dev/null); [ -n "$S" ] || { echo "gen/arena/sessao.txt must name a recorded Free Duel session (see docs/TESTING.md)"; exit 1; }
R="$1"; SEED="$2"; LADO="$3"; PRIM="$4"; MODS="${5:-gen/mods_teste2}"
if [ "$LADO" = "n" ]; then export TF_IA_OFF=1; LADO=-1; else unset TF_IA_OFF; fi
[ -n "$PRIM" ] && export TF_PRIMEIRO="$PRIM"
export INPUTS=gen/arena/inputs_fd_shift.txt TF_RNG_SEED="$SEED" TF_CPUCPU=1 TF_ARENA_SAIR=1 TF_IA_LADO="$LADO" MODDIR="$MODS" TAG="_arena_${R}_$SEED" ENDF="${ENDF:-70000}" SNAPS=100
sh tools/replay.sh "$S" 1000 "${TIMEOUT:-2400}" > /dev/null 2>&1
LOG="gen/replay/${S}_arena_${R}_$SEED/log.txt"
grep -a "FIM quadro" "$LOG" | head -1 | sed 's/.*FIM/FIM/' > "gen/arena/res_${R}_$SEED.txt"
[ -s "gen/arena/res_${R}_$SEED.txt" ] || echo "sem fim" > "gen/arena/res_${R}_$SEED.txt"
cat "gen/arena/res_${R}_$SEED.txt"
