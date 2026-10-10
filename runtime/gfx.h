/* Opcoes graficas da imagem final (menu Graficos): filtro de escala, proporcao e escala inteira. Gravadas em
 * tagforce_recomp.ini ([graficos]) ao lado do .exe. */
#pragma once
#include <windows.h>
#include <stdint.h>
enum { GFX_FILTER_NEAREST = 0, GFX_FILTER_SHARP = 1, GFX_FILTER_BILINEAR = 2 };
enum { GFX_ASPECT_ORIGINAL = 0, GFX_ASPECT_STRETCH = 1, GFX_ASPECT_4_3 = 2 };
enum { GFX_RENDER_SOFTWARE = 0, GFX_RENDER_D3D11 = 1, GFX_RENDER_VULKAN = 2 };   /* renderizadores: software (CPU) ou GPU (Direct3D 11 / Vulkan, gpu_*.cpp) */
struct GfxSettings { int filter; int aspect; bool integer; int res; int renderer; int msaa; int texscale; int fps60; int hdfont; int sync; };   /* sync: apresentacao na GPU - 0 = padrao (Vulkan MAILBOX, sem rasgos; D3D11 sem espera, com tearing se houver), 1 = vsync ligado (FIFO / Present(1)), 2 = menor latencia (Vulkan IMMEDIATE, pode rasgar) */   /* hdfont: 1 = fonte em alta resolucao (hdfont.h). fps60: 1 = 60 FPS por interpolacao (interp.h), 0 = 30 originais. res: resolucao interna 1..8 (1 = nativa; software so ate 4); msaa: amostras 1/2/4/8 (so GPU); texscale: ampliacao embutida das texturas sem substituicao 1/2/4 (so GPU) */
extern GfxSettings g_gfx;
void gfx_load(void);
void gfx_save(void);
/* pinta a imagem sw x sh (0x00RRGGBB) na area cw x ch do dc, com as opcoes atuais */
void gfx_paint(HDC dc, const uint32_t *pix, int sw, int sh, int cw, int ch);   /* pix = imagem sw x sh */
void gfx_dest_rect(int cw, int ch, int *ox, int *oy, int *ow, int *oh);   /* onde a imagem aparece dentro da area do cliente */
