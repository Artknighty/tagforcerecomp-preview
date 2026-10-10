/* 60 FPS por interpolacao da apresentacao (menu Graficos -> Taxa de quadros). O jogo continua a 30 quadros por segundo de logica (rodar o
 * motor a 60 dobraria a velocidade de tudo); como o display_present roda a cada vblank (60 Hz) e o jogo so produz um quadro novo a cada
 * 2 vblanks, o vblank "do meio" mostra um quadro INTERMEDIARIO: as listas de desenho da GPU do quadro anterior e do novo sao pareadas e as
 * posicoes/cores dos vertices sao interpoladas (t = 0,5). Custo: um quadro de atraso de apresentacao (~17 ms). Qualquer duvida (quadro
 * parcialmente desenhado pelo raster nativo, listas diferentes demais, cena em 60 fps) = mostra o quadro real, nunca um quadro errado. */
#pragma once
#include <stdint.h>
#include "gpu.h"

/* chamado a cada desenho enviado a GPU num buffer EXIBIDO ou, com rtt, num alvo de textura (a lista do alvo serve para refaze-lo interpolado);
 * cull: 0, ou o modo do GE (1/2): a lista traz tambem os triangulos descartados e o quadro do meio refaz o cull */
void interp_record(const GpuState &s, const GpuVtx *v, int n, bool rtt = false, int cull = 0);
void interp_fb_drawn(int fb);                                    /* houve desenho no quadro sombra `fb` (versao das copias congeladas de render-to-texture) */
void interp_taint(int fb);                                       /* o quadro sombra `fb` nao e mais so o resultado das listas gravadas (raster nativo, refeito do nativo) */
void interp_reset(void);                                         /* apaga tudo (troca de renderizador/resolucao/MSAA): os ids de quadro da GPU deixam de valer */
/* chamado no vblank, depois de ge_gpu_frame: true = ja apresentou um quadro intermediario (o chamador NAO apresenta o real) */
bool interp_present(int gfb, uint64_t vblank, bool allow, int res);
bool interp_enabled(void);
bool interp_has_new(int gfb);                                    /* ha desenho novo no quadro exibido desde a ultima apresentacao? */
bool interp_mid_ready(int gfb, uint64_t vblank, bool allow);     /* o quadro novo esta pronto e vai ganhar o intermediario: o chamador pode apresentar sem esperar a grade de vblanks */
bool interp_defer_release(uint32_t tex);   /* gpu_tex_release: true = a textura sera liberada depois do proximo quadro intermediario */
void gpu_tex_release_now(uint32_t h);
