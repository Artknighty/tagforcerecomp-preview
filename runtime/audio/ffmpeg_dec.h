/* Decodificadores do FFmpeg carregado dinamicamente (ver ffmpeg_dec.cpp): ATRAC3/ATRAC3plus (sceAtrac e audio dos
 * filmes) e H.264 (video dos filmes PSMF). Sem as DLLs, os *_open devolvem nullptr. */
#pragma once
#include <stdint.h>
void ffmpeg_preload(void);   /* carrega as DLLs numa thread (inicio do jogo) */
struct AtracDec;
/* nullptr se o FFmpeg nao estiver disponivel ou o formato nao abrir */
AtracDec *atracdec_open(bool plus, int chans, int block_align, const uint8_t *extra, int extra_len);
void atracdec_close(AtracDec *d);
void atracdec_flush(AtracDec *d);
/* decodifica um bloco de block_align bytes; ate max_frames quadros estereo s16 em out; devolve os quadros gerados */
int atracdec_decode(AtracDec *d, const uint8_t *block, int16_t *out, int max_frames);

struct AvcDec;
AvcDec *avcdec_open(void);
void avcdec_close(AvcDec *d);
void avcdec_flush(AvcDec *d);
/* Envia um access unit H.264 (Annex B; data = nullptr: fim do fluxo, esvazia os quadros atrasados) e, se sair um quadro,
 * converte para o formato de pixel do PSP (0 = 5650, 1 = 5551, 2 = 4444, 3 = 8888, 4 = 0x00RRGGBB) em dst com largura de linha
 * stride (pixels), no maximo max_w x max_h. Devolve 1 se escreveu um quadro, 0 se nao, <0 em erro. */
int avcdec_decode(AvcDec *d, const uint8_t *data, int size, uint8_t *dst, int stride, int psm, int max_w, int max_h);
