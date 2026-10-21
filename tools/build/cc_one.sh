#!/bin/sh
# Compiles ONE runtime/decomp C++ file to a .o (run in parallel by build.sh). Usage: sh tools/build/cc_one.sh <source.cpp> <output.o>
# Variables (exported by build.sh): CXXOPT (optimization), GAMEID (built-in game id).
set -e
export PATH=/c/msys64/ucrt64/bin:$PATH
echo "g++ $1"
g++ -std=c++17 ${CXXOPT:--O2} -g -DPSP_TRACE "-DTF_GAME_ID=\"$GAMEID\"" -Wall -Wno-misleading-indentation -Iruntime -Iruntime/core -Iruntime/hle -Iruntime/gpu -Iruntime/ui -Iruntime/audio -Iruntime/mods -MMD -MF "${2%.o}.d" -c "$1" -o "$2"
