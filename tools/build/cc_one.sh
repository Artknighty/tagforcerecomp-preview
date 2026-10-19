#!/bin/sh
# Compila UM arquivo C++ do runtime/decomp para um .o (chamado em paralelo pelo build.sh). Uso: sh tools/build/cc_one.sh <fonte.cpp> <saida.o>
# Variaveis (exportadas pelo build.sh): CXXOPT (otimizacao), GAMEID (id do jogo embutido).
set -e
export PATH=/c/msys64/ucrt64/bin:$PATH
echo "g++ $1"
g++ -std=c++17 ${CXXOPT:--O2} -g -DPSP_TRACE "-DTF_GAME_ID=\"$GAMEID\"" -Wall -Wno-misleading-indentation -Iruntime -MMD -MF "${2%.o}.d" -c "$1" -o "$2"
