#!/bin/sh
# One-time setup of the dev package from your own ISO (what the "Install and play" button does):
#   1. installs the game files into gen/pkg/TagForceRecomp/jogo/
#   2. the first replay/run then translates the game code (embedded Python) and compiles it with TinyCC (a few seconds)
# Usage: sh tools/install_dev.sh [path-to-iso]      (default: TF_ISO, or iso= in gen/tagforce_recomp.ini)
cd "$(dirname "$0")/.."
PKG=gen/pkg/TagForceRecomp
[ -f $PKG/TagForceRecomp.exe ] || { echo "build first: sh tools/build.sh"; exit 1; }
ISO="${1:-${TF_ISO:-$(grep -i "^iso=" gen/tagforce_recomp.ini 2>/dev/null | head -1 | cut -d= -f2- | tr -d "[:cntrl:]")}}"; [ -f "$ISO" ] || ISO=$(cygpath -u "$ISO" 2>/dev/null); [ -f "$ISO" ] || { echo "set TF_ISO=<path to your ISO>"; exit 1; }
if [ -d $PKG/jogo ]; then echo "already installed: $PKG/jogo"; exit 0; fi
(cd $PKG && ./TagForceRecomp.exe --install "$(cygpath -w "$ISO")") 2>&1 | tail -3
ls $PKG/jogo >/dev/null 2>&1 && echo "installed: $PKG/jogo" || { echo "install failed"; exit 1; }
