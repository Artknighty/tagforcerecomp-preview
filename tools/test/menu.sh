#!/bin/sh
# Abre a opcao N do MAIN MENU (0 = Story Mode, 2 = Deck Menu, 3 = Database, 5 = Options, 6 = Save Data, 7 = Language)
# a partir do save de TESTE (./ms0), aperta X e tira capturas. Uso: sh tools/test/menu.sh <N> [capturas] [extra AUTO_PRESS]
cd "$(dirname "$0")/../.."
N=$1
DOWN=$(python -c "print(''.join(',%d:40,%d:0' % (2100 + i * 20, 2106 + i * 20) for i in range($N)))")
C="300:4000,306:0,900:8,906:0,1100:4000,1106:0,1600:4000,1606:0$DOWN,2400:4000,2406:0${3:+,$3}"
sh tools/test/play.sh "$C" "${2:-2300,2700,3200,3800}" 300
grep -E "nao implementado|CRASH|watchdog|instrucao|sem funcao" gen/log_play.txt | head -5
