#!/bin/sh
# Joga de verdade (sua sessao normal, gen/) e grava o MONITOR o tempo todo (120 Hz, sem custo para o jogo) para caçar piscadas/flicker.
# Aperte L3+R3 (ou F7/F8) quando vir o defeito: a marca fica na gravacao e os ULTIMOS 6 s ANTES dela (+1 s depois) sao guardados em 960x540
# (metade da tela, media 2x2 na GPU; o jogo nao sente), convertidos em PNG: <pasta>/marca_N/ e marca_N_folha_k.png (numero da saida em amarelo). Ao fechar o jogo, a analise roda sozinha.
# Uso: sh tools/jogar_gravando.sh [minutos_no_anel=6]      Resultado: gen/gravacoes/<data>/ (seg_*.bin/.tsv) e piscadas.txt
cd "$(dirname "$0")/.."
MIN="${1:-6}"
[ -f gen/tela_continua.exe ] && [ gen/tela_continua.exe -nt tools/tela_continua.cpp ] || PATH=/c/msys64/ucrt64/bin:$PATH g++ -O2 -o gen/tela_continua.exe tools/tela_continua.cpp -ld3d11 -ldxgi || exit 1
D="gen/gravacoes/$(date +%Y%m%d_%H%M%S)"; mkdir -p "$D"
./gen/tela_continua.exe "$D" --manter "$MIN" 2> "$D/gravador.log" &
REC=$!
(cd gen && ./run.exe)
sleep 2; kill $REC 2>/dev/null; taskkill //F //IM tela_continua.exe >/dev/null 2>&1; wait $REC 2>/dev/null
tail -3 "$D/gravador.log"
python tools/marca_ver.py "$D" --passo 2
python tools/piscadas_analisar.py --continuo "$D" --png "$D/png" | tee "$D/piscadas.txt"
