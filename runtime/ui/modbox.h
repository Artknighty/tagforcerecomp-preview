#pragma once
#include <stdint.h>
#include <string>
#include <vector>
void modbox_show(uint64_t frame, const wchar_t *title, const std::vector<std::wstring> &lines, const wchar_t *foot);   /* chamar a cada quadro em que a caixa deve ficar visivel */
void modbox_panel(uint64_t frame, int corner, const wchar_t *title, const std::vector<std::wstring> &lines);         /* painel translucido num canto (0..3); chamar a cada quadro */
const uint32_t *modbox_pixels(uint64_t frame);   /* overlay GPU_OVW x GPU_OVH pronto (caixa e/ou paineis), ou nullptr se nenhum mod pediu neste quadro */
uint64_t modbox_version(void);   /* muda quando a imagem e refeita (o display so reenvia o overlay entao) */
void modbox_sprite(uint64_t frame, int id, const uint32_t *rgba, int w, int h, float x, float y, float dw, float dh, int alpha);   /* API 5: imagem RGBA por cima do jogo (pixels do PSP 480x272); chamar a cada quadro */
void modbox_quad(uint32_t fb, float x0, float y0, float x1, float y1);   /* hle_ge: quad 3D texturizado desenhado em fb */
void modbox_quad_frame(uint32_t fb);                                     /* sceDisplaySetFrameBuf: fb vira o quadro exibido */
int modbox_quad_near(int x, int y, int tol, int wmin, int wmax);          /* ha um quad de largura wmin..wmax centrado a ate tol pixels de (x,y) no quadro exibido? */
