/* Ritmo dos quadros (limitador de 59,94 Hz) e apresentacao antecipada do 60 FPS (ver display.cpp, display_present).
 * Tudo em contagens do QueryPerformanceCounter (QPC). Nada aqui conhece o jogo nem a GPU.
 *
 *  - Grade de vblanks: o instante nominal em que cada chamada de display_present apresentaria o quadro (pace_grid).
 *  - Saida: a hora em que display_present devolve o controle ao jogo (e ao audio, que produz amostras ali). Precisa ser regular:
 *    pace_exit impoe 1/60 s entre saidas.
 *  - Apresentacao antecipada: com quadro novo + intermediario o intermediario sai logo que o quadro esta pronto (pace_mid_target)
 *    e o real 1/60 s depois (pace_real_arm / pace_real_take), em vez de esperar a grade.
 *  - Frame skipping: se as chamadas ficam atrasadas de forma sustentada, os intermediarios sao suspensos por um tempo
 *    (pace_mid_allowed), porque a interpolacao desenha cada quadro duas vezes na GPU. */
#pragma once
#include <stdint.h>

int64_t pace_now(void);
double pace_ms(int64_t ticks);                     /* QPC -> milissegundos */
void pace_wait_until(int64_t target);              /* espera precisa (temporizador de alta resolucao + espera ativa no fim) */

void pace_grid(bool sleep);                        /* avanca a grade de um vblank; com sleep, espera ate ela (o limitador de sempre) */

void pace_exit(bool timed);                        /* fim de display_present: espacamento de 1/60 s entre saidas (timed = houve limitador) */
bool pace_have_exit(void);                         /* ja houve uma saida? */
int64_t pace_last_exit(void);                      /* hora da ultima saida = o jogo voltou a rodar */

void pace_work(double work_ms);                    /* trabalho do quadro (ms, sem o tempo dormido): atraso acumulado; suspende os intermediarios se passar de 1 vblank */
uint32_t pace_susp_n(void);                      /* quantas vezes os intermediarios foram suspensos (display.cpp registra no log com o quadro) */
bool pace_mid_suspended(void);                     /* suspensos agora? (nao consome nada) */
bool pace_mid_allowed(void);                       /* chamar UMA vez por display_present: false enquanto os intermediarios estao suspensos */
int64_t pace_mid_target(int64_t call_entry);       /* hora em que o intermediario deve sair (ultima saida + fase estimada do trabalho do jogo) */

void pace_real_arm(int fb, int64_t work_start);    /* o quadro real `fb` sai 1/60 s depois de agora (na proxima chamada) */
bool pace_real_take(int *fb, int64_t *due, int64_t *work_start);

double pace_take_slept_ms(void);                   /* tempo esperado desde a ultima leitura (estatistica de trabalho do display.cpp) */

/* Sincronia com o monitor (ver pacing.cpp): display.cpp informa a taxa do monitor da janela e se a sincronia vale (GPU, limitador ligado, nao e o
 * modo de menor latencia); gpu.cpp chama pace_presented() a cada apresentacao (a malha de fase mede ali). */
void pace_monitor(int hz, bool allow);
void pace_presented(void);

enum PaceStat { PACE_STAT_EXIT, PACE_STAT_REAL, PACE_STAT_PRESENT, PACE_STAT_N };
void pace_stat(PaceStat id, double v);             /* TF_LAT_DEBUG=1: media/desvio/maximo a cada N amostras no log */
