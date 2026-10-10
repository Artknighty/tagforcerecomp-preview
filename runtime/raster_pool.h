/* Pool de threads do rasterizador: divide uma faixa de linhas da tela em bandas e roda fn(ctx, y0, y1) em paralelo
 * (bloqueia ate todas terminarem). As bandas sao disjuntas, entao o resultado e identico ao de uma thread so.
 * TF_RASTER_THREADS=n: total de threads (contando a do jogo); 0 ou 1 desliga; padrao = nucleos logicos, no maximo 8. */
#pragma once
typedef void (*raster_fn)(void *ctx, int y0, int y1);
int raster_threads(void);                                       /* total de threads que rasterizam (1 = sem paralelismo) */
void raster_parallel(int y0, int y1, raster_fn fn, void *ctx);  /* roda fn nas bandas de [y0, y1) */
