#!/bin/sh
# Reproduz uma sessao gravada pelo diag (gen/logs/<sessao>.inputs + <sessao>_ms0) numa pasta isolada, sem tocar em
# nenhum save. Uso: sh tools/test/replay.sh <sessao> [passo_capturas=1000] [segundos=900] ["texto1|texto2" do OSK]
# Resultado em gen/replay/<sessao><TAG>/: gen/snap_*.bmp, sheet.png (todas as capturas) e log.txt (log da execucao).
# Opcoes (variaveis de ambiente):
#   ENDF=n       quadro final (padrao: ultima tecla + 600)      SNAPS=a,b,c   capturas exatas (no lugar do passo)
#   TAG=_x       sufixo da pasta (varias reproducoes lado a lado) RUN_EXE=gen/base.exe   outro executavel
#   MODDIR=pasta usa essa pasta de mods (ex.: gen/mods, com o pacote de texturas of the player; so leitura, sem copiar)
#   PACE=1       com o limitador de 60 fps (tempo real; mede atrasos de carregamento) - o padrao e sem limitador
#   INPUTS=arq   usa outro arquivo de entradas (ex.: a sessao cortada + teclas novas) no lugar de gen/logs/<sessao>.inputs
#   PRIMEIRO_PLANO=1  mostra a janela; COM_SOM=1 liga o som (padrao: escondida e muda, p/ nao atrapalhar)    
#   RECENT=arq   copia uma lista texturas_recentes.txt para a pasta da reproducao (testa o pre-carregamento das texturas substitutas)
#   TEXCACHE=pasta  copia um texcache/ de outra reproducao (pixels ja decodificados: testa a 2a visita as texturas)
#   INI=arq      parte de uma copia desse .ini (INI=gen/tagforce_recomp.ini = config exata of the player, inclusive filtro/proporcao, que nao tem variavel)
cd "$(dirname "$0")/../.."
ROOT=$(pwd); S="$1"; STEP="${2:-1000}"
ISO="${TF_ISO:-$(grep -i "^iso=" gen/tagforce_recomp.ini 2>/dev/null | head -1 | cut -d= -f2- | tr -d "[:cntrl:]")}"; [ -f "$ISO" ] || ISO=$(cygpath -u "$ISO" 2>/dev/null); [ -f "$ISO" ] || { echo "set TF_ISO=<path to your ISO> (or iso= in gen/tagforce_recomp.ini)"; exit 1; }
IN="${INPUTS:-$ROOT/gen/logs/$S.inputs}"; [ -f "$IN" ] || { echo "sem $IN"; exit 1; }
EXE="$ROOT/${RUN_EXE:-gen/pkg/TagForceRecomp/TagForceRecomp_bg.exe}"; case "$RUN_EXE" in /*) EXE="$RUN_EXE";; esac
W="$ROOT/gen/replay/$S$TAG"; rm -rf "$W"; mkdir -p "$W/gen"
[ -d "$ROOT/gen/logs/${S}_ms0" ] && cp -r "$ROOT/gen/logs/${S}_ms0" "$W/ms0"
[ -n "$MODDIR" ] && export TF_MODS_DIR="$(cygpath -m "$(cd "$MODDIR" && pwd)")"   # usada no lugar (so leitura), sem copiar
# sem MODDIR: os mods de COMPORTAMENTO ligados (plugin.dll, ex. caixa de boosters) entram assim mesmo - eles mudam o jogo, e a
# reproducao precisa repetir what was played (antes eram embutidos no run.exe); os de dados ficam de fora (determinismo)
# o modelo de IA do IAAprimorada (llm/, ~500 MB) so e copiado se for usado (TF_IA_LLM=1 ou llm=1 no config.ini do mod; o padrao e desligado)
copiar_mod() { src="${1%/}"; dst="$2/$(basename "$src")"; mkdir -p "$dst"
  for f in "$src"/*; do if [ "$(basename "$f")" = llm ] && [ "${TF_IA_LLM:-0}" = 0 ] && ! grep -qs '^llm=1' "$src/config.ini"; then continue; fi; cp -r "$f" "$dst/"; done; }
if [ -z "$MODDIR" ]; then mkdir -p "$W/mods"; for d in "$ROOT"/gen/pkg/TagForceRecomp/mods/*/; do [ -f "$d/plugin.dll" ] && [ ! -f "$d/disabled" ] && copiar_mod "$d" "$W/mods"; done; fi
[ -n "$RECENT" ] && cp "$RECENT" "$W/texturas_recentes.txt"
[ -n "$TEXCACHE" ] && cp -r "$TEXCACHE" "$W/texcache"   # cache em disco das texturas decodificadas de uma execucao anterior (teste "a quente")
LAST=$(tr ',' '\n' < "$IN" | grep : | tail -1 | cut -d: -f1); END=${ENDF:-$((LAST + 600))}
F=$(python -c "print(','.join(str(i) for i in list(range($STEP, $END, $STEP)) + [$END]))")
[ -n "$SNAPS" ] && F="$SNAPS"
echo "reproduzindo $S ate o quadro $END (capturas: $F)"
# sessoes gravadas antes dos filmes (sem linha "[mpeg]" no log) reproduzem com os filmes desligados
grep -q '^\[mpeg\]' "$ROOT/gen/logs/$S.log" 2>/dev/null || export TF_NO_MOVIES=1
NP="NO_PACE=1"; [ -n "$PACE" ] && NP="TF_PACE=1"
export TF_LOG_DIR="$(cygpath -m "$W/logs" 2>/dev/null || echo "$W/logs")"   # o log desta execucao fica na pasta da reproducao (gen/logs e so do usuario)
export TF_INI="$TF_LOG_DIR/teste.ini"   # .ini proprio: as configuracoes of the player nao influem nem sao gravadas (use TF_GPU/TF_RES/TF_MSAA...)
# INI=arq: parte de uma COPIA desse .ini (ex.: INI=gen/tagforce_recomp.ini = a config exata of the player: filtro, proporcao, 60 FPS...; o original nunca e gravado)
[ -n "$INI" ] && mkdir -p "$TF_LOG_DIR" && cp "$INI" "$TF_INI"
[ -z "$PRIMEIRO_PLANO" ] && export TF_SEGUNDO_PLANO=1; [ -z "$COM_SOM" ] && export NO_AUDIO=1
# janela escondida: caixa Sim/Nao (ex.: sair do jogo) respondida com Nao, senao o teste fica parado esperando um clique
[ -z "$PRIMEIRO_PLANO" ] && [ -z "$TF_CONFIRM_AUTO" ] && export TF_CONFIRM_AUTO=0
# MODO LEVE (padrao): sem cache da ISO na RAM (800 MB por processo) e renderizador por software: ~140 MB por duelo em vez de ~1 GB, mesmo resultado (testado: duelo identico) e mesma velocidade.
# PESADO=1 desliga (testes de textura/cenario/capturas usam a GPU); PACE=1 (performance real) e TF_CAPTURAS tambem mantem a config completa.
[ -z "$PESADO" ] && [ -z "$PACE" ] && [ -z "$TF_CAPTURAS" ] && export TF_NO_ISO_CACHE=1 TF_GPU=0
cd "$W" && env $NP TF_KEEP_CWD=1 EXIT_FRAME="$((END + 1))" AUTO_PRESS_FILE="$IN" AUTO_TEXT="${4:-}" SNAP_FRAME="$F" timeout "${3:-900}" "$EXE" --iso "$ISO" > /dev/null 2>&1
echo "exit=$?"
cat logs/*.log > log.txt 2>/dev/null; grep -v '^\[snap\]' log.txt | tail -15 | cut -c1-220
python - "$F" <<'PY'
import sys, os
from PIL import Image
ns = [int(n) for n in sys.argv[1].split(',') if os.path.exists('gen/snap_%s.bmp' % n)]
if ns:
    cols = 4; rows = (len(ns) + cols - 1) // cols
    s = Image.new('RGB', (240 * cols, 136 * rows))
    for i, n in enumerate(ns): s.paste(Image.open('gen/snap_%d.bmp' % n).convert('RGB').resize((240, 136)), ((i % cols) * 240, (i // cols) * 136))
    s.save('sheet.png'); print('sheet:', ns)
PY
