/* Fonte em alta resolucao. O sceFont HLE (hle_font.cpp) desenha cada glifo com a fonte do Windows em ~21 px num rascunho 4 bits e o JOGO copia o
 * bitmap para um atlas 4 bits (512x512) com codigo proprio; a 8x a GPU so podia ampliar esse bitmap (borrado). Aqui:
 *  1. a cada glifo pedido pelo jogo (hdfont_record) guardamos tambem o mesmo glifo desenhado em S x o tamanho (HDS), indexado pelo hash do
 *     recorte justo (sem as bordas vazias) do bitmap 4 bits que o jogo recebeu;
 *  2. ao desenhar sprites que amostram um atlas 4 bits (hdfont_texture, chamado pelo GE), cada retangulo UV e comparado com os glifos
 *     guardados (mesmo hash do recorte justo); os que casam entram num atlas sombra S x maior (branco + alfa; a cor vem do vertice, como no
 *     atlas original), o resto e ampliado por bilinear. O atlas sombra e usado como textura substituta (UV normalizado funciona). */
#pragma once
#include <stdint.h>
#include <vector>

enum { HDS = 4 };   /* fator de ampliacao dos glifos */
struct HdRect { int x0, y0, x1, y1; };   /* retangulo UV em texels do atlas nativo, [x0,x1) x [y0,y1) */
bool hdfont_enabled(void);
/* glifo que o jogo acabou de receber: nib = w*h niveis 0..15 do bitmap nativo, com o canto superior esquerdo em (xp, yp) do rascunho 32x32 */
void hdfont_record(uint32_t code, int xp, int yp, const uint8_t *nib, int w, int h);
/* atlas 4 bits (tp = memoria da textura; swz/rowb/swzrow/tlimit como no GE). Devolve o handle GPU do atlas sombra HDS x maior ou 0 (usar o nativo). */
uint32_t hdfont_texture(uint32_t addr, int W, int H, const uint8_t *tp, bool swz, uint32_t rowb, uint32_t swzrow, uint32_t tlimit, const HdRect *rects, int n);
void hdfont_reset(void);   /* troca de renderizador/resolucao: os handles da GPU deixam de valer */
/* implementado em hle_font.cpp: o glifo code desenhado em HDS x o tamanho (a8 = cobertura 0..255, w*h) */
bool font_hd_glyph(uint32_t code, int *w, int *h, std::vector<uint8_t> *a8);
