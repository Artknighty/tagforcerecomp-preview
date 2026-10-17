#!/bin/sh
# Regressao visual (para refatoracoes): reproduz recorded sessions em software, D3D11 e Vulkan e compara as capturas com
# uma referencia. Como a emulacao e deterministica, uma refatoracao pura deve dar "IDENTICAS" em todos os casos.
# Uso: sh tools/regressao.sh [ref|cmp] [rapido]
#   ref    grava as capturas atuais como referencia (gen/regressao_ref); use RUN_EXE=gen/base.exe para gravar com outro executavel
#   cmp    (padrao) compara com a referencia
#   rapido so os casos curtos (mapa e New Game); sem ele roda tambem as sessoes longas (menus/duelo e historia/campo, ~30 min)
# Sem pacote de texturas e sem ampliacao (as substitutas chegam em tempos variaveis); o .ini do usuario e sobreposto pelas variaveis.
cd "$(dirname "$0")/.."
MODE="${1:-cmp}"; FAST="$2"
export TF_RES_BUDGET_MS=0 TF_TEXSCALE=1 TF_MSAA=1 TF_TEX_PREFETCH=0 TF_HDFONT=0   # fonte HD desligada: a referencia e anterior a ela
# caixas Sim/Nao respondidas com Nao (escondida, ninguem clica: o teste ficava parado para sempre; ex.: 'Exit to Desktop' do titulo)
export TF_CONFIRM_AUTO=0
R=gen/regressao_cur; rm -rf "$R"; mkdir -p "$R"
caso() {   # caso <nome> <sessao> <quadro_final> <passo> <renderizador 0/1/2> <resolucao>
  # PESADO: sem ele o modo leve do replay.sh forca TF_GPU=0 (os casos de GPU rodavam em software desde 2026-10-04).
  # MODDIR vazio: jogo puro. Sem isso o replay.sh carrega os mods de COMPORTAMENTO ligados (IA, decks, DP...), que mudam os duelos e
  # a referencia deixa de valer (os casos longos divergiam ate em software).
  mkdir -p gen/regressao_sem_mods
  TAG=_rg_$1 ENDF=$3 TF_GPU=$5 TF_RES=$6 PESADO=1 MODDIR=gen/regressao_sem_mods sh tools/replay.sh "$2" "$4" 2400 > /dev/null 2>&1
  W=gen/replay/$2_rg_$1; mkdir -p "$R/$1"; cp "$W"/gen/snap_*.bmp "$R/$1/" 2>/dev/null
  P=$(grep -E "nao implementado|CRASH|watchdog|excecao" "$W/log.txt" | head -2 | cut -c1-120)
  S=$(grep "^\[perf\] sessao" "$W/log.txt" | cut -c1-90)
  echo "$1: $(ls "$R/$1" | wc -l) capturas; $S ${P:+PROBLEMA: $P}"
}
caso mapa_vk sessao_20260930_203930 2200 300 2 2
caso mapa_dx sessao_20260930_203930 2200 300 1 2
caso mapa_sw sessao_20260930_203930 2200 300 0 1
# New Game do zero (save vazio): titulo -> New Game -> nome -> primeiras cenas. Titulo em estilo PC (2026-10-15): Continue / New Game / Exit to
# Desktop, cursor comeca em Continue: desce (40) para New Game (com o 'sobe' antigo o roteiro caia em Exit e a caixa de sair prendia o teste). Jogo puro tambem (pasta de mods vazia): o play.sh carregaria
# os plugins ligados (conferido em 2026-10-08: com e sem eles a referencia bate; sem eles ela continua valendo se um plugin mudar)
mkdir -p gen/regressao_sem_mods
TF_MODS_DIR="$(cygpath -m "$PWD/gen/regressao_sem_mods" 2>/dev/null || echo "$PWD/gen/regressao_sem_mods")" TF_GPU=2 TF_RES=2 TF_FRESH_SAVE=1 AUTO_TEXT="Player" sh tools/play.sh "300:4000,306:0,900:8,906:0,1000:40,1006:0,1100:4000,1106:0,1300:4000,1306:0,1500:8,1506:0,1700:80,1706:0,1800:4000,1806:0" "1200,1600,2000,2600,3200" 400 > /dev/null 2>&1
mkdir -p "$R/newgame"; cp gen/snap_*.bmp "$R/newgame/" 2>/dev/null; echo "newgame: $(ls "$R/newgame" | wc -l) capturas"
if [ -z "$FAST" ]; then
  caso menus_sw sessao_20260930_104250 36500 3000 0 1
  caso menus_vk sessao_20260930_104250 36500 3000 2 2
  # campo: a sessao 205208 (2026-09-29) passava pela tela de idioma no inicio e o fluxo do titulo mudou; o replay saia do trilho (outro lugar
  # do mapa, sem o duelo). Trocada em 2026-10-15 por uma sessao recente com dormitorio + mapa da ilha (os modulos carregam na ordem do original)
  caso campo_sw sessao_20261012_185321 3800 300 0 1
  caso campo_dx sessao_20261012_185321 3800 300 1 1
fi
if [ "$MODE" = "ref" ]; then rm -rf gen/regressao_ref; cp -r "$R" gen/regressao_ref; echo "referencia gravada em gen/regressao_ref"; exit 0; fi
python - <<'PY'
import os
import numpy as np
from PIL import Image
r, c = 'gen/regressao_ref', 'gen/regressao_cur'
if not os.path.isdir(r): print('sem referencia (rode: sh tools/regressao.sh ref)'); raise SystemExit
bad = 0
for caso in sorted(os.listdir(c)):
    if not os.path.isdir(os.path.join(r, caso)): print('%-10s sem referencia' % caso); continue
    worst, msgs = 0, []
    fr, fc = set(os.listdir(os.path.join(r, caso))), set(os.listdir(os.path.join(c, caso)))
    if fr != fc: msgs.append('capturas diferentes: faltam %s, sobram %s' % (sorted(fr - fc)[:4], sorted(fc - fr)[:4]))
    for f in sorted(fr & fc):
        a = np.asarray(Image.open(os.path.join(r, caso, f)).convert('RGB'), dtype=int); b = np.asarray(Image.open(os.path.join(c, caso, f)).convert('RGB'), dtype=int)
        if a.shape != b.shape: msgs.append('%s: tamanho %s x %s' % (f, a.shape, b.shape)); worst = 999; continue
        d = np.abs(a - b); n = int((d.sum(2) > 0).sum()); m = int(d.max())
        if n: msgs.append('%s: %d pixels diferentes (max %d)' % (f, n, m))
        worst = max(worst, m)
    print('%-10s %s' % (caso, 'IDENTICAS' if worst == 0 and not msgs else 'DIFERENTES'))
    for m in msgs[:6]: print('    ' + m)
    bad += bool(msgs)
print('resultado: tudo IDENTICO' if not bad else 'resultado: %d caso(s) com diferenca' % bad)
PY
