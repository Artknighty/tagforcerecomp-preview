/* Opcoes graficas da imagem final (menu Graficos): filtro de escala, proporcao e escala inteira. Gravadas em
 * tagforce_recomp.ini ([graficos]) ao lado do .exe. */
#pragma once
#include <windows.h>
#include <stdint.h>
enum { GFX_FILTER_NEAREST = 0, GFX_FILTER_SHARP = 1, GFX_FILTER_BILINEAR = 2 };
enum { GFX_ASPECT_ORIGINAL = 0, GFX_ASPECT_STRETCH = 1, GFX_ASPECT_4_3 = 2 };
enum { GFX_RENDER_SOFTWARE = 0, GFX_RENDER_D3D11 = 1, GFX_RENDER_VULKAN = 2 };   /* renderizadores: software (CPU) ou GPU (Direct3D 11 / Vulkan, gpu_*.cpp) */
enum { GFX_FPS_30 = 0, GFX_FPS_60 = 1, GFX_FPS_120 = 2 };   /* taxa de quadros (fps): 30 originais; 60 e 120 por interpolacao (interp.h). 120 so com monitor de 100 Hz ou mais (senao fica 60) */
struct GfxSettings { int filter; int aspect; bool integer; int res; int renderer; int msaa; int texscale; int fps; int sync; bool res_auto; };   /* res_auto: resolucao "Match monitor" (no .ini resolucao=0): res = gfx_res_monitor() */   /* sync: apresentacao na GPU - 0 = padrao (Vulkan MAILBOX, sem rasgos; D3D11 sem espera, com tearing se houver), 1 = vsync ligado (FIFO / Present(1)), 2 = menor latencia (Vulkan IMMEDIATE, pode rasgar) */   /* fps: GFX_FPS_30/60/120 (no .ini, chave fps60 = 0/1/2). res: resolucao interna 1..8 (1 = nativa; software so ate 4); msaa: amostras 1/2/4/8 (so GPU); texscale: ampliacao embutida das texturas sem substituicao 1/2/4 (so GPU) */
extern GfxSettings g_gfx;
void gfx_load(void);
void gfx_save(void);
/* pinta a imagem sw x sh (0x00RRGGBB) na area cw x ch do dc, com as opcoes atuais */
void gfx_paint(HDC dc, const uint32_t *pix, int sw, int sh, int cw, int ch);   /* pix = imagem sw x sh */
void gfx_dest_rect(int cw, int ch, int *ox, int *oy, int *ow, int *oh);   /* onde a imagem aparece dentro da area do cliente */
bool gfx_raw_psp();   /* true = no texture package on (Original): plain PSP picture */
int gfx_texscale();   /* texture upscaling in effect (1 when gfx_raw_psp) */
/* Resolucao como num jogo de PC (2026-10-15): o jogador escolhe 1280x720, 1920x1080, 2560x1440, 3840x2160 ou a do monitor; por dentro vale o menor
 * multiplo inteiro de 480x272 que cobre essa altura (3x = 1440x816, 4x = 1920x1088, 6x = 2880x1632, 8x = 3840x2176) e a imagem e reduzida para a janela.
 * Fator inteiro: os sprites 2D do PSP (menus, cartas, texto) nao ganham costuras nem tremem como numa escala fracionaria. */
int gfx_res_monitor(void);   /* fator para a altura do monitor principal */
