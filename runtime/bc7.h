/* Compressao BC7 (formato de textura comprimida da GPU, 1 byte por texel = 1/4 do RGBA) das texturas substitutas, feita nas threads
 * de leitura do texrepl.cpp: o cache em disco e em RAM guarda a textura ja comprimida, com todos os mipmaps, entao a primeira vez que ela
 * aparece numa sessao custa so ler ~1/4 dos bytes e copiar para a GPU (sem decodificar PNG nem gerar mipmaps).
 * Codificador proprio (modos 1, 3, 5, 6 e 7; ver bc7.cpp). */
#pragma once
#include <stdint.h>
#include <stddef.h>

size_t bc7_level_bytes(int w, int h);                        /* um nivel w x h (blocos 4x4 de 16 bytes; bordas arredondam para cima) */
size_t bc7_chain_bytes(int w, int h, int *levels);           /* niveis 0..n ate 1x1 (os mesmos tamanhos de texrepl_make_mips) */
void bc7_encode(const uint32_t *rgba, int w, int h, uint8_t *out);   /* um nivel (RGBA 0xAABBGGRR) */
