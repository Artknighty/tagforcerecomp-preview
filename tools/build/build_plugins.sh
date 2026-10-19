#!/bin/sh
# Compila os mods de comportamento (plugins) de plugins/src/<Mod>/ para gen/mods/<Mod>/plugin.dll e copia os outros arquivos do mod
# (mod.txt, leia-me, imagens...). Um arquivo 'disabled' que o jogador tenha criado em gen/mods/<Mod>/ e preservado.
# API dos plugins: plugins/sdk/tfmod.h. Uso: sh tools/build/build_plugins.sh [Mod ...]   (MODS_OUT=pasta muda o destino, padrao gen/mods)
set -e
cd "$(dirname "$0")/../.."
export PATH=/c/msys64/ucrt64/bin:$PATH
OUTROOT="${MODS_OUT:-gen/mods}"
LIST="$*"; [ -z "$LIST" ] && LIST=$(cd plugins/src && ls -d */ 2>/dev/null | tr -d /)
for m in $LIST; do
  src="plugins/src/$m"; out="$OUTROOT/$m"; [ -d "$src" ] || { echo "sem $src"; exit 1; }
  mkdir -p "$out"
  for f in "$src"/*; do case "$f" in *.cpp|*.c|*.h) ;; *) cp -r "$f" "$out/";; esac; done
  srcs=$(ls "$src"/*.cpp "$src"/*.c 2>/dev/null || true)
  if [ -n "$srcs" ]; then
    # -static: o plugin nao depende de DLLs do MSYS2 na maquina do jogador
    # -Wno-misleading-indentation: 'if (a) x; if (b) y;' na mesma linha e o estilo do projeto (igual ao build.sh)
    g++ -std=c++17 -O2 -Wall -Wno-misleading-indentation -shared -static -s -Iplugins/sdk -o "$out/plugin.dll" $srcs -lwinhttp -lws2_32
    echo "plugin: $out/plugin.dll"
  fi
done
