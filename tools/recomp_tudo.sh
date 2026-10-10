#!/bin/sh
# Recompila TUDO a partir da ISO do usuario, sem nenhuma ferramenta de decompilacao externa (Ghidra):
#   1. instala os dados do jogo em gen/jogo (se ainda nao estiverem; uma unica vez);
#   2. gera a tabela de NIDs a partir dos nomes que o proprio runtime implementa;
#   3. traduz os modulos do jogo (Allegrex -> C) descobrindo as funcoes pela estrutura do codigo (tools/recomp.py: discover_starts);
#   4. gera o icone (a partir do ICON0.PNG do jogo) e compila/liga gen/run.exe.
# Uso:  sh tools/recomp_tudo.sh [<caminho da ISO>]        (a ISO so e necessaria na 1a vez)
#       GHIDRA=1 sh tools/recomp_tudo.sh                  (usa a exportacao do Ghidra de gen/ferramentas_decomp, se existir)
# Requisitos: Windows + MSYS2 UCRT64 (gcc, g++, windres, glslc, ffmpeg) + Python 3 com Pillow. Nada disso e distribuido com o projeto:
# o codigo C gerado e o executavel ficam em gen/ (fora do git) e valem so para a copia do jogo de quem rodou o script.
set -e
cd "$(dirname "$0")/.."
export PATH=/c/msys64/ucrt64/bin:$PATH
for c in python gcc g++ windres; do command -v $c > /dev/null || { echo "faltou '$c' no PATH (instale o MSYS2 UCRT64: https://www.msys2.org/)"; exit 1; }; done
# a versao do jogo vem da ISO (UMD_DATA.BIN): a europeia ULES-00600 usa gen/, gen/jogo e gen/run.exe; qualquer outra (ex. a americana ULUS-10136 e as
# traducoes feitas sobre ela) usa gen/<ID>/, gen/jogo_<ID> e gen/run_<ID>.exe (ver tools/modules_<ID>.tsv e docs/VERSIONS.md)
ISO="$1"
[ -z "$ISO" ] && ISO=$(grep -i '^iso=' gen/tagforce_recomp.ini 2>/dev/null | head -1 | cut -d= -f2- | tr -d '\r')
if [ -n "$ISO" ] && [ -f "$ISO" ]; then TF_VER=$(python tools/id_jogo.py "$ISO"); elif [ -n "$TF_VER" ]; then :; else TF_VER=; fi
export TF_VER
JOGO=gen/jogo${TF_VER:+_$TF_VER}; GV=gen${TF_VER:+/$TF_VER}
[ -n "$TF_VER" ] && [ ! -f tools/modules_$TF_VER.tsv ] && { echo "versao $TF_VER ainda nao suportada: falta tools/modules_$TF_VER.tsv"; exit 1; }
echo "versao do jogo: ${TF_VER:-ULES00600 (europeia)}"
if [ ! -f $JOGO/_disco.tsv ]; then
  [ -f "$ISO" ] || { echo "informe a ISO do jogo: sh tools/recomp_tudo.sh <ISO>"; exit 1; }
  echo "== 1/4 instalando os dados do jogo em $JOGO (uma unica vez)"
  python tools/extrair_jogo.py "$ISO" $JOGO | tail -1
else
  echo "== 1/4 dados ja instalados em $JOGO"
fi
echo "== 2/4 tabela de NIDs"
python tools/gerar_nids.py
echo "== 3/4 traduzindo os modulos (Allegrex -> C)"
[ -n "$GHIDRA" ] && export TF_USAR_GHIDRA=1
python tools/build_modules.py > $GV/recomp_modulos.log 2>&1 || { tail -20 $GV/recomp_modulos.log; exit 1; }
tail -1 $GV/recomp_modulos.log
echo "== 4/4 icone e compilacao"
[ -f gen/app.ico ] || python tools/make_icon.py "$ISO" > /dev/null 2>&1 || echo "(sem icone: ICON0.PNG nao encontrado)"
sh tools/build_run.sh
echo "pronto: gen/run${TF_VER:+_$TF_VER}.exe (abra e clique em Jogar; a ISO nao e mais necessaria)"
