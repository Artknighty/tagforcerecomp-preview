#!/bin/sh
# Apaga os artefatos de TESTE em gen/ (reproducoes, despejos, capturas, mods temporarios). Nunca toca em gen/logs (player sessions),
# gen/ms0 (save), gen/mods (pacote de texturas e mods) nem nas referencias (gen/regressao_ref, gen/bench_ref*).
# Uso: sh tools/build/limpar.sh        (mostra o tamanho antes e depois)
cd "$(dirname "$0")/../.."
du -sh . 2>/dev/null | tail -1
# junções de mods temporarios (ex.: gen/mods_t/*/PackSub/textures/*): remover o LINK, nunca o conteudo apontado
for d in gen/mods_t/*/*/textures/*; do [ -d "$d" ] && cmd //c rmdir "$(cygpath -w "$d")" >/dev/null 2>&1; done
rm -rf gen/mods_t gen/replay/* gen/regressao_cur gen/bench_cur gen/bench_cur_r2 gen/dump_* gen/snap_*.bmp gen/vram_*.bmp gen/tex_*.bmp gen/sheet.png gen/log_play.txt gen/logs_teste gen/plugins_teste gen/interp_*.bmp gen/selftest_*.bmp gen/wt_novo gen/wt_*.png gen/wincap.png
rm -rf gen/play_w   # play.sh test folder (the ./ms0 in the root is the live save: never removed here)
du -sh . 2>/dev/null | tail -1
