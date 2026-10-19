/* 60/120 FPS por interpolacao da apresentacao (menu Graficos -> Taxa de quadros). O jogo continua a 30 quadros por segundo de logica (rodar o
 * motor a 60 dobraria a velocidade de tudo); como o display_present roda a cada vblank (60 Hz) e o jogo so produz um quadro novo a cada
 * 2 vblanks, o vblank "do meio" mostra um quadro INTERMEDIARIO: as listas de desenho da GPU do quadro anterior e do novo sao pareadas e as
 * posicoes/cores dos vertices sao interpoladas (t = 0,5). Custo: um quadro de atraso de apresentacao (~17 ms). No 120 FPS sao tres
 * intermediarios por quadro do jogo (t = 0,25 / 0,5 / 0,75; o ritmo esta em display.cpp e pacing.cpp). Qualquer duvida (quadro
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
/* o mesmo em duas etapas, para o intermediario chegar ao monitor na hora certa: interp_prepare desenha o intermediario na fracao t (0,5 no 60 FPS,
 * 0,25 no 120) e ja o envia a GPU; true = pronto, e interp_show o apresenta (na hora marcada pelo chamador). Antes ele so era desenhado na hora de
 * aparecer e chegava ~4,5 ms atrasado em 8x (ritmo 12/21 ms em vez de 16,7/16,7 no monitor, medido com tools/analysis/tela_gravar.cpp). */
bool interp_prepare(int gfb, uint64_t vblank, bool allow, int res, float t);
void interp_show(void);
bool interp_mid_static(void);   /* o ultimo intermediario preparado e um par parado: interp_show apresenta a imagem real (nada foi enviado a GPU) */
bool interp_hold_ok(void);      /* o quadro real pode esperar o intermediario na fila da GPU (nenhuma copia congelada de alvo de textura neste quadro) */
/* 120 FPS: depois de um interp_prepare com t != 0,5, os outros intermediarios do mesmo par (t = 0,5 e 0,75) saem de interp_extra + interp_show;
 * interp_extra_done libera o quadro guardado para eles (chamar depois do ultimo) */
bool interp_extra(float t, int res);
void interp_extra_done(void);
bool interp_enabled(void);
uint32_t interp_fb_version(int fb);                            /* muda a cada desenho/copia no quadro sombra (o display.cpp nao reapresenta uma imagem igual) */
bool interp_has_new(int gfb);                                    /* ha desenho novo no quadro exibido desde a ultima apresentacao? */
bool interp_mid_ready(int gfb, uint64_t vblank, bool allow);     /* o quadro novo esta pronto e vai ganhar o intermediario: o chamador pode apresentar sem esperar a grade de vblanks */
bool interp_defer_release(uint32_t tex);   /* gpu_tex_release: true = a textura sera liberada depois do proximo quadro intermediario */
void gpu_tex_release_now(uint32_t h);
