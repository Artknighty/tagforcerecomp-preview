/* Ritmo dos quadros (limitador de 59,94 Hz) e apresentacao antecipada do 60 FPS (ver display.cpp, display_present).
 * Tudo em contagens do QueryPerformanceCounter (QPC). Nada aqui conhece o jogo nem a GPU.
 *
 *  - Grade de vblanks: o instante nominal em que cada chamada de display_present apresentaria o quadro (pace_grid).
 *  - Saida: a hora em que display_present devolve o controle ao jogo (e ao audio, que produz amostras ali). Precisa ser regular:
 *    pace_exit impoe 1/60 s entre saidas.
 *  - Apresentacao antecipada: com quadro novo + intermediario o intermediario sai logo que o quadro esta pronto (pace_mid_target)
 *    e o real 1/60 s depois (pace_real_arm / pace_real_take), em vez de esperar a grade. No 120 FPS sao tres intermediarios a cada meio
 *    passo (dois na chamada do quadro novo, o terceiro e o real na seguinte; ver display_present).
 *  - Frame skipping: se as chamadas ficam atrasadas de forma sustentada, os intermediarios sao suspensos por um tempo
 *    (pace_mid_allowed), porque a interpolacao desenha cada quadro duas vezes na GPU. */
#pragma once
#include <stdint.h>

int64_t pace_now(void);
double pace_ms(int64_t ticks);                     /* QPC -> milissegundos */
int64_t pace_ticks(double ms);                     /* milissegundos -> QPC */
int64_t pace_fit_call(int64_t ideal, int64_t earliest);   /* hora de apresentar: ideal, mas antes da saida desta chamada e nao antes de earliest */
void pace_wait_until(int64_t target);              /* espera precisa (temporizador de alta resolucao + espera ativa no fim) */

void pace_grid(bool sleep);                        /* avanca a grade de um vblank; com sleep, espera ate ela (o limitador de sempre) */

void pace_exit(bool timed);                        /* fim de display_present: espacamento de 1/60 s entre saidas (timed = houve limitador) */
bool pace_have_exit(void);                         /* ja houve uma saida? */
int64_t pace_last_exit(void);                      /* hora da ultima saida = o jogo voltou a rodar */

void pace_work(double work_ms);                    /* trabalho do quadro (ms, sem o tempo dormido): atraso acumulado; suspende os intermediarios se passar de 1 vblank */
uint32_t pace_susp_n(void);                      /* quantas vezes os intermediarios foram suspensos (display.cpp registra no log com o quadro) */
void pace_mid_result(double late_ms);            /* quanto o intermediario saiu depois da hora (ms): 4 atrasos > 4 ms seguidos suspendem os intermediarios ~1 s */
bool pace_mid_suspended(void);                     /* suspensos agora? (nao consome nada) */
bool pace_mid_allowed(void);                       /* chamar UMA vez por display_present: false enquanto os intermediarios estao suspensos */
/* hora em que o intermediario deve sair (ultima saida + fase estimada do trabalho do jogo e da GPU); want_quarter: o jogador escolheu 120 FPS,
 * *quarter = vai em 120 neste quadro (monitor >= 100 Hz e a fase tipica cabe em meio passo) */
int64_t pace_mid_target(int64_t call_entry, bool want_quarter = false, bool *quarter = nullptr);

void pace_real_arm(int fb, int64_t work_start, int64_t due = 0, bool w2 = true);   /* o quadro real `fb` sai na hora due (0 = 1/60 s depois da ultima apresentacao), na proxima chamada;
                                                                                    * w2 = a chamada que o apresentar e a seguinte ao quadro novo (mede o trabalho do jogo entre os dois vblanks) */
/* MODO ADIADO (cut-in pesado): quando o intermediario nao cabe na chamada do quadro novo (o jogo chega tarde e a GPU ainda desenha), ele sai na
 * chamada seguinte e o real no inicio da outra: 60 constantes com +1/60 s de latencia so nesses trechos (antes: ~1 s a 30). Ver pacing.cpp. */
bool pace_defer_active(void);
int64_t pace_defer_target(int64_t call_entry);      /* chamada do quadro novo em modo adiado: hora (na chamada seguinte) em que o intermediario sai */
void pace_defer_need(double need_ms);               /* fase (ms depois da saida anterior) em que a GPU terminou o intermediario adiado; 99 = nao terminou antes da saida */
void pace_defer_reset(const char *why);             /* sai do modo adiado (carregamento, quadros fora do compasso...) */
void pace_w2_sample(void);                          /* modo adiado: a chamada seguinte ao quadro novo chegou (mesma medida do pace_real_take) */
bool pace_real_take(int *fb, int64_t *due, int64_t *work_start);
void pace_gpu_done(void);                          /* a GPU acabou de terminar o intermediario (gpu_wait_kicked): a fase do intermediario cobre esse tempo */
int64_t pace_step_ticks(void);                     /* um passo (1/60 s, ou k atualizacoes do monitor com sincronia) em QPC */
int64_t pace_exit_target(void);                    /* hora marcada para a saida desta chamada (0 = sem limitador) */
void pace_set_sub(int n);                          /* apresentacoes por passo: 1 (60 FPS) ou 2 (120 FPS); divide a correcao de fase */

double pace_take_slept_ms(void);                   /* tempo esperado desde a ultima leitura (estatistica de trabalho do display.cpp) */

/* Sincronia com o monitor (ver pacing.cpp): display.cpp informa a taxa do monitor da janela e se a sincronia vale (GPU, limitador ligado, nao e o
 * modo de menor latencia); gpu.cpp chama pace_presented() a cada apresentacao (a malha de fase mede ali). */
void pace_monitor(int hz, bool allow);
int pace_monitor_hz(void);                         /* taxa do monitor da janela (0 = desconhecida) */
/* 120 FPS com a thread de apresentacao (display.cpp): Q1 sai em pace_async_target (ultima saida + trabalho do jogo + GPU), Q2/Q3/real a cada
 * meio passo, sem depender da thread do jogo. pace_async_late: a thread de apresentacao achou a GPU ainda desenhando na hora do Q1 */
int64_t pace_async_target(int64_t call_entry);
void pace_async_late(bool late);
void pace_presented(int64_t start = 0);           /* start: hora em que a apresentacao comecou (o 120 FPS mede a fase ali) */

enum PaceStat { PACE_STAT_EXIT, PACE_STAT_REAL, PACE_STAT_PRESENT, PACE_STAT_LIST0, PACE_STAT_LIST1, PACE_STAT_N };
void pace_stat(PaceStat id, double v);             /* TF_LAT_DEBUG=1: media/desvio/maximo a cada N amostras no log */
