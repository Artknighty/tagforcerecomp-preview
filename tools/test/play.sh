#!/bin/sh
# Roda um roteiro automatico e monta gen/sheet.png com as capturas. Uso:
#   sh tools/test/play.sh "<AUTO_PRESS>" "<quadros separados por virgula>" [segundos]
# Ex.: sh tools/test/play.sh "300:4000,306:0,900:8,906:0" "800,1000" 120
export TF_KEEP_CWD=1
# filmes (abertura de ~90 s) mudam a linha do tempo dos roteiros: desligados, salvo TF_MOVIES=1
[ -z "$TF_MOVIES" ] && export TF_NO_MOVIES=1
cd "$(dirname "$0")/../.."
ISO="${TF_ISO:-$(grep -i "^iso=" gen/tagforce_recomp.ini 2>/dev/null | head -1 | cut -d= -f2- | tr -d "[:cntrl:]")}"; [ -f "$ISO" ] || ISO=$(cygpath -u "$ISO" 2>/dev/null); GA1=--iso; GA2="$ISO"   # no ISO on disk: fall back to the game data installed in the dev package (tools/install_dev.sh)
if [ ! -f "$ISO" ]; then
  if [ -d "$(pwd)/gen/pkg/TagForceRecomp/jogo" ]; then GA1=--dados; GA2="$(pwd)/gen/pkg/TagForceRecomp/jogo"; else echo "set TF_ISO=<path to your ISO> (or iso= in gen/tagforce_recomp.ini), or run tools/install_dev.sh"; exit 1; fi
fi
rm -f gen/snap_*.bmp
# Save de teste = COPY of the real save (gen/ms0), refeita a cada execucao: o teste parte do ponto mais avancado dele e
# nunca escreve em gen/ms0. TF_FRESH_SAVE=1 => ./ms0 vazio (so para testar New Game).
rm -rf ms0
if [ -z "$TF_FRESH_SAVE" ] && [ -d gen/ms0 ]; then cp -r gen/ms0 ms0; fi
# outra versao do jogo (ex. ULUS10136): usa a copia do save europeu sob o ID dela (TF_SAVE_ID=ULUS101360001); so a copia de teste muda
[ -n "$TF_SAVE_ID" ] && [ -d ms0/PSP/SAVEDATA/ULES006000001 ] && cp -r ms0/PSP/SAVEDATA/ULES006000001 ms0/PSP/SAVEDATA/$TF_SAVE_ID
# sai sozinho logo depois da ultima captura (a menos que EXIT_FRAME ja venha definido)
LASTSNAP=$(echo "$2" | tr ',' '\n' | sort -n | tail -1)
export EXIT_FRAME="${EXIT_FRAME:-$((LASTSNAP + 1))}"
# log desta execucao em gen/logs_teste/ (gen/logs e so das sessoes do usuario); copia em gen/log_play.txt
rm -rf gen/logs_teste; export TF_LOG_DIR="$(cygpath -m "$PWD/gen/logs_teste" 2>/dev/null || echo "$PWD/gen/logs_teste")"
# .ini proprio do teste: o tagforce_recomp.ini of the player never e lido nem gravado (configuracoes so por TF_GPU/TF_RES/...)
export TF_INI="$TF_LOG_DIR/teste.ini"
# mods de comportamento ligados (plugin.dll) entram como in the real game; os de dados ficam de fora (TF_MODS_DIR=pasta escolhe outra)
# (o modelo de IA do IAAprimorada, llm/ ~500 MB, so e copiado se for usado: TF_IA_LLM=1 ou llm=1 no config.ini do mod; o padrao e desligado)
copiar_mod() { src="${1%/}"; dst="$2/$(basename "$src")"; mkdir -p "$dst"
  for f in "$src"/*; do if [ "$(basename "$f")" = llm ] && [ "${TF_IA_LLM:-0}" = 0 ] && ! grep -qs '^llm=1' "$src/config.ini"; then continue; fi; cp -r "$f" "$dst/"; done; }
if [ -z "$TF_MODS_DIR" ]; then rm -rf gen/plugins_teste; mkdir -p gen/plugins_teste; for d in gen/pkg/TagForceRecomp/mods/*/; do [ -f "$d/plugin.dll" ] && [ ! -f "$d/disabled" ] && copiar_mod "$d" gen/plugins_teste; done; export TF_MODS_DIR="$(cygpath -m "$PWD/gen/plugins_teste" 2>/dev/null || echo "$PWD/gen/plugins_teste")"; fi
# janela escondida e sem som, como no replay.sh (the player uses the PC enquanto os testes rodam; as capturas vem do framebuffer). PRIMEIRO_PLANO=1 mostra.
[ -z "$PRIMEIRO_PLANO" ] && export TF_SEGUNDO_PLANO=1
# janela escondida: caixa Sim/Nao (ex.: sair do jogo) respondida com Nao, senao o teste fica parado esperando um clique
[ -z "$PRIMEIRO_PLANO" ] && [ -z "$TF_CONFIRM_AUTO" ] && export TF_CONFIRM_AUTO=0
AUTO_TEXT="${AUTO_TEXT:-Player}" NO_PACE=1 AUTO_PRESS="$1" SNAP_FRAME="$2" timeout "${3:-300}" "${RUN_EXE:-./gen/pkg/TagForceRecomp/TagForceRecomp_bg.exe}" $GA1 "$GA2" > /dev/null 2>&1
echo "exit=$?"
cat gen/logs_teste/*.log > gen/log_play.txt 2>/dev/null
grep -v "^\[snap\]" gen/log_play.txt | tail -"${TAIL:-8}" | cut -c1-300
python - "$2" <<'PY'
import sys, os
from PIL import Image
ns = [int(n) for n in sys.argv[1].split(',') if os.path.exists('gen/snap_%s.bmp' % n)]
if ns:
    cols = min(3, len(ns)); rows = (len(ns) + cols - 1) // cols
    s = Image.new('RGB', (480 * cols, 272 * rows))
    for i, n in enumerate(ns): s.paste(Image.open('gen/snap_%d.bmp' % n).convert('RGB'), ((i % cols) * 480, (i // cols) * 272))
    s.save('gen/sheet.png'); print('sheet:', ns)
PY
