/* Mouse nos menus: acha as barras de menu desenhadas no quadro exibido (sprites 2D em 3 pedacos: ponta, meio, ponta), descobre qual esta
 * selecionada pelo desenho (a barra cujo conjunto de sprites difere das outras do mesmo tamanho) e, quando o mouse mexe/clica numa barra,
 * aperta o direcional ate a selecao chegar nela (e confirma com X no clique). O jogo so conhece direcional e botoes: nada e escrito na
 * memoria dele. See docs/ARCHITECTURE.md. */
#pragma once
#include <stdint.h>

/* chamado pelo GE a cada PRIM de sprite 2D texturizado (dois vertices, modo through) */
void mui_prim(uint32_t fb, int x0, int y0, int x1, int y1, float u0, float v0, float u1, float v1, uint32_t tex, uint32_t col);
/* varios sprites num PRIM so (linha de texto): retangulo que os envolve */
void mui_run(uint32_t fb, int x0, int y0, int x1, int y1);
/* sceDisplaySetFrameBuf: o buffer vira o quadro exibido; seus sprites viram a tela atual */
void mui_set_frame(uint32_t fb);
/* uma vez por quadro (leitura do teclado/mouse). (mx,my) = mouse em coordenadas 480x272 (inside = sobre a imagem), l_edge = clique esquerdo novo,
 * l_down = botao esquerdo apertado, user_dpad = direcional que o jogador esta apertando de verdade. Devolve botoes do PSP a acrescentar;
 * *eat_cross = true: o clique esquerdo NAO deve virar X por conta propria (o modulo confirma quando a selecao chegar na barra). */
uint32_t mui_update(uint64_t frame, bool inside, int mx, int my, bool l_edge, bool l_down, uint32_t user_dpad, bool *eat_cross);
