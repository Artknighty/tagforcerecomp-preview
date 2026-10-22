#include "pacing.h"
#include "util.h"
#include <windows.h>
#include <dwmapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <algorithm>
#include <vector>

namespace {
LARGE_INTEGER g_freq; bool g_init;
int64_t g_grid;                 /* proximo vblank nominal */
int64_t g_lastx;                /* alvo da saida anterior (a cadeia usa o alvo, nao a hora real: o atraso de cada espera nao se acumula) */
int64_t g_exit_qpc; bool g_have_exit;
double g_slept_ms;
int g_skip_mid; double g_lag_ms; static uint32_t g_susp_n;   /* g_lag_ms: mesmo atraso acumulado do medidor de "atrasados" do perf (display.cpp) */
/* Suspension back-off (2026-10-20): in a scene that stays too heavy for the 60 FPS (friend's RTX 2060 log, duel at 4x) the intermediate frames
 * came back every 2.5 s, missed again at once and were suspended again: a 60/30 switch (one hitch each) every 2.5 s for half a minute. A
 * suspension that starts less than ~5 s after the previous one ended now lasts twice as long (up to 12x); a calm stretch resets it. */
int g_since_resume = 1 << 30, g_susp_mult = 1;
int susp_len(int base) { g_susp_mult = g_since_resume < 300 ? std::min(12, g_susp_mult * 2) : 1; return base * g_susp_mult; }
double g_phi_ms = 6.0, g_work[16]; int g_work_i;
double g_w2_ms, g_w2[16]; int g_w2_i;   /* saida da chamada do quadro novo -> entrada da chamada seguinte (o jogo trabalha entre os dois vblanks) */
int64_t g_real_due, g_real_start; int g_real_fb = -1; bool g_real_w2 = true;
int64_t g_last_present;         /* hora da ultima apresentacao na GPU (pace_presented) */
const bool g_dbg = tf_lat_debug();
/* GPU: o intermediario e desenhado e enviado logo que o quadro novo chega (interp_prepare); o display.cpp espera a GPU terminar
 * (gpu_wait_kicked, ele ficaria parado ate a fase de qualquer jeito) e pace_gpu_done anota em que fase isso aconteceu: g_off_gpu e a fase que
 * cobre quase todos os quadros recentes, e o intermediario so sai depois dela. Antes ele aparecia ~4,5 ms depois da hora (a GPU so comecava ao
 * apresentar) e o real na hora: ritmo 12/21 ms no monitor em vez de 16,7/16,7 (tools/analysis/tela_gravar.cpp, sessao 205658). */
double g_off_gpu, g_off_ms, g_off_typ;   /* g_off_typ: fase tipica (mediana) em que a GPU termina o intermediario */
int g_sub = 1;                  /* apresentacoes por passo: 1 no 60 FPS, 2 no 120 (cada uma corrige a fase pela metade) */
int g_quarter_off;              /* 120 FPS: quadros novos que ainda vao no ritmo de 60 (a fase nao cabe em meio passo) */
int g_late_run;                 /* quadros novos seguidos em que a GPU terminou o intermediario depois da fase usada */
uint32_t g_quarter_falls;

/* SINCRONIA COM O MONITOR. O jogo tinha um relogio proprio de 59,94 Hz; num monitor de 120 Hz (2 atualizacoes por quadro de 60 FPS) as saidas
 * escorregavam devagar em relacao as atualizacoes e, quando caiam perto da borda de uma, cada imagem ficava 1 atualizacao e a seguinte 3 (8,3 ms /
 * 25 ms) em vez de 2 e 2: tremido medido on a test monitor in ~30% do tempo (tools/analysis/tela_gravar.cpp). Agora, se a taxa do monitor (do DWM) e
 * multipla de ~60 (60, 119,88, 120, 240 Hz...), o passo vira exatamente k atualizacoes (120 Hz: 16,667 ms em vez de 16,683; 0,1% mais rapido, o
 * controle de reserva do audio absorve) e uma malha de fase desliza as cadeias de saida e da grade no maximo 0,3 ms por apresentacao ate as
 * apresentacoes cairem na fase g_fase de uma atualizacao. Nenhuma espera nova: o caminho controle -> logica -> quadro real continua do mesmo
 * tamanho; so muda QUANDO, em relacao ao monitor, ele acontece. Sem sincronia (75, 144 Hz...: 60 nao divide) fica como antes.
 * Fase 0,8 (2026-10-14; era 0,5): com o intermediario pronto antes da hora (interp_prepare), a apresentacao so faz a escala final (~0,8 ms ate a
 * tela). Medido no monitor de 120 Hz do jogador (tela_gravar + TF_PRESENT_LOG): no meio da atualizacao a imagem ora entrava na hora (0,8 ms), ora
 * esperava a atualizacao seguinte (5 ms), e cada troca dava um par 12/21 ms; em 0,7-0,9 ela entra sempre na proxima atualizacao (1,5-2,4 ms) e
 * os intervalos ficaram em 16,7 (pares 15/18 de vez em quando). TF_RITMO_SEM_FASE=1 desliga; TF_RITMO_FASE=0..1 muda a fase alvo. */
int64_t g_vb_period, g_vb_ref; int g_vb_k; bool g_vb_lock, g_lock_allow; int g_mon_hz; uint32_t g_vb_poll;
double g_fase = 0.8, g_fase120 = 0.3;   /* g_fase120: ver pace_presented */
double g_ph_sum, g_ph_abs; int g_ph_n;   /* estatistica do erro de fase (TF_LAT_DEBUG) */

void init(void) {
    if (!g_init) {
        QueryPerformanceFrequency(&g_freq); LARGE_INTEGER n; QueryPerformanceCounter(&n); g_grid = n.QuadPart; g_init = true;
        if (const char *e = getenv("TF_RITMO_FASE")) g_fase = std::max(0.0, std::min(0.99, atof(e)));
        if (const char *e = getenv("TF_RITMO_FASE120")) g_fase120 = std::max(0.0, std::min(0.99, atof(e)));
    }
}
int64_t nominal(void) { return g_freq.QuadPart * 1001 / 60000; }   /* 1/59,94 s */
int64_t step(void) { return g_vb_lock ? g_vb_period * g_vb_k : nominal(); }
void vsync_poll(void) {   /* relogio do monitor (DWM) a cada ~0,5 s; decide a sincronia */
    if (g_vb_poll++ % 30) return;
    static const bool off = getenv("TF_RITMO_SEM_FASE") != nullptr;
    DWM_TIMING_INFO ti = {}; ti.cbSize = sizeof ti;
    bool lock = false; int k = 0; double hz = 0;
    if (!off && g_lock_allow && SUCCEEDED(DwmGetCompositionTimingInfo(nullptr, &ti)) && ti.qpcRefreshPeriod > 0 && ti.rateRefresh.uiDenominator) {
        hz = (double)ti.rateRefresh.uiNumerator / ti.rateRefresh.uiDenominator;
        g_vb_period = (int64_t)ti.qpcRefreshPeriod; g_vb_ref = (int64_t)ti.qpcVBlank;
        k = (int)llround((double)nominal() / (double)g_vb_period);
        lock = k >= 1 && k <= 8 && fabs((double)(g_vb_period * k) - (double)nominal()) < 0.004 * (double)nominal() && (g_mon_hz <= 0 || fabs(hz - g_mon_hz) < 1.5);
    }
    if (lock != g_vb_lock || k != g_vb_k) {   /* no log so a 1a vez de cada situacao (as telas de carregamento desligam o limitador e religam) */
        static int64_t told_lock; static bool told_free;
        if (lock && told_lock != g_vb_period * k) { told_lock = g_vb_period * k; fprintf(stderr, "[ritmo] monitor %.2f Hz: quadros a %.3f/s (%d atualizacao(oes) cada), apresentacoes na fase %.2f das atualizacoes%c", hz, (double)g_freq.QuadPart / (double)(g_vb_period * k), k, g_fase, 10); }
        if (!lock && !told_free && g_lock_allow && !off && hz > 0) { told_free = true; fprintf(stderr, "[ritmo] monitor %.2f Hz (janela em %d Hz): sem sincronia (60 nao divide), ritmo proprio de 59,94%c", hz, g_mon_hz, 10); }
        g_vb_lock = lock; g_vb_k = k;
    }
}
}

int pace_monitor_hz(void) { return g_mon_hz; }
static double g_as_work[16], g_as_off = 0, g_as_gpu = 4.0; static int g_as_wi;
int64_t pace_async_target(int64_t call_entry) {
    init();
    g_as_work[g_as_wi++ & 15] = pace_ms(call_entry - g_exit_qpc);
    double s[16]; const int n = std::min(g_as_wi, 16); std::copy(g_as_work, g_as_work + n, s); std::nth_element(s, s + (n * 9) / 10, s + n);
    /* o jogo chega (p90), desenha os 3 intermediarios e a copia do real (~1 ms de CPU) e a GPU termina (g_as_gpu, aprendido com os atrasos) */
    const double alvo = std::max(1.5, std::min(pace_ms(step()) * 1.8, s[(n * 9) / 10] + 1.0 + g_as_gpu));
    if (alvo > g_as_off) g_as_off = alvo; else g_as_off = std::max(alvo, g_as_off - 0.1);   /* sobe na hora, desce devagar (a malha de fase acompanha) */
    return g_exit_qpc + (int64_t)(g_as_off * (double)g_freq.QuadPart / 1000.0);
}
void pace_async_late(bool late) { g_as_gpu = late ? std::min(15.0, g_as_gpu + 0.5) : std::max(1.5, g_as_gpu - 0.01); }
void pace_monitor(int hz, bool allow) { g_mon_hz = hz; g_lock_allow = allow; if (!allow && g_vb_lock) { g_vb_poll = 0; vsync_poll(); } }
/* 120 FPS (2026-10-15): com uma apresentacao por atualizacao do monitor o vkQueuePresentKHR (MAILBOX, driver NVIDIA; 3 ou 4 imagens dao o mesmo)
 * prende a thread ~3 ms. Medida no FIM da chamada, a malha punha o inicio na fase ~0,57 com espalhamento 0,31-0,83 e um terco das imagens so
 * aparecia uma atualizacao depois (+8,3 ms). O limite nao e fixo: depende da fila do driver (na fase 0,5-0,6 ora entra direto, ora fica
 * retida, e retida continua). Medido com tela_gravar: comecando ate a fase ~0,5 entra direto (~5 ms ate a tela). No 120 a fase e medida no
 * INICIO da apresentacao, com alvo g_fase120 = 0,3 (TF_RITMO_FASE120): 97,6% dos intervalos a 8,3 +-1,5 ms e ~10% retidas (antes 32%).
 * O 60 continua medindo no fim da chamada, na fase 0,8 (la ela dura 0,2 ms). */
void pace_presented(int64_t start) {
    init(); g_last_present = pace_now();
    vsync_poll();
    if (!g_vb_lock || !g_vb_period) return;
    /* erro de fase em [-P/2, P/2): quanto a apresentacao esta depois da fase alvo dentro da atualizacao */
    const bool q = g_sub >= 2 && start;
    const int64_t P = g_vb_period, alvo = g_vb_ref + (int64_t)((q ? g_fase120 : g_fase) * (double)P);
    int64_t e = ((q ? start : g_last_present) - alvo) % P; if (e < 0) e += P; if (e >= P / 2) e -= P;
    const int64_t lim = g_freq.QuadPart * 3 / 10000 / g_sub;   /* 0,3 ms por apresentacao (no 120 FPS sao o dobro de apresentacoes: metade cada) */
    const int64_t corr = std::max(-lim, std::min(lim, -e / (4 * g_sub)));
    if (g_lastx) g_lastx += corr;
    g_grid += corr;
    if (g_dbg) { const double ms = pace_ms(e); g_ph_sum += ms; g_ph_abs += fabs(ms); if (++g_ph_n >= 600) { fprintf(stderr, "[lat] fase das apresentacoes: erro medio %.2f ms, medio absoluto %.2f ms (periodo do monitor %.2f ms)%c", g_ph_sum / g_ph_n, g_ph_abs / g_ph_n, pace_ms(P), 10); g_ph_sum = g_ph_abs = 0; g_ph_n = 0; } }
}

int64_t pace_now(void) { init(); LARGE_INTEGER n; QueryPerformanceCounter(&n); return n.QuadPart; }
double pace_ms(int64_t ticks) { init(); return (double)ticks * 1000.0 / (double)g_freq.QuadPart; }
int64_t pace_ticks(double ms) { init(); return (int64_t)(ms * (double)g_freq.QuadPart / 1000.0); }
/* hora de uma apresentacao que deveria sair em `ideal`, sem passar da saida desta chamada (o jogo e o audio esperam por ela): no maximo 0,5 ms
 * antes da saida marcada, e nunca antes de `earliest` (a atualizacao do monitor que mostra a imagem anterior) */
int64_t pace_fit_call(int64_t ideal, int64_t earliest) {
    const int64_t xt = pace_exit_target();
    if (!xt) return ideal;
    return std::max(earliest, std::min(ideal, xt - pace_ticks(0.5)));
}

/* Sleep(ms) no Windows dorme em passos de ~15,6 ms (e trunca): o ritmo ficava irregular e os quadros chegavam mais tarde que o necessario.
 * Aqui: temporizador de alta resolucao ate ~1,5 ms antes e o resto em espera ativa. */
void pace_wait_until(int64_t target) {
    init();
    static HANDLE tm = nullptr; static bool tried;
    if (!tried) {
        tried = true;
        tm = CreateWaitableTimerExW(nullptr, nullptr, 0x2 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */, TIMER_ALL_ACCESS);
        if (!tm) timeBeginPeriod(1);   /* Windows antigo: Sleep de 1 ms */
    }
    const int64_t t0 = pace_now();
    for (;;) {
        const double ms = pace_ms(target - pace_now());
        if (ms <= 0) break;
        if (ms > 2.0) {
            if (tm) { LARGE_INTEGER due; due.QuadPart = -(LONGLONG)((ms - 1.5) * 10000.0); SetWaitableTimer(tm, &due, 0, nullptr, nullptr, FALSE); WaitForSingleObject(tm, 50); }
            else Sleep((DWORD)(ms - 1.5));
        } else YieldProcessor();
    }
    g_slept_ms += pace_ms(pace_now() - t0);
}

void pace_grid(bool sleep) {
    init();
    g_grid += step();
    const int64_t now = pace_now();
    if (now < g_grid) { if (sleep) pace_wait_until(g_grid); }
    else if (now - g_grid > g_freq.QuadPart / 10) g_grid = now;   /* atrasado mais de 100 ms: nao tenta recuperar (senao a grade ficava para tras para sempre) */
}

/* Exit schedule catch-up (2026-10-20). The exit chain used to restart from "now" whenever a call came out late, so every late present (a missed
 * monitor refresh: ~27 ms frames on a laptop with a GTX 1650) was lost game time for good. With the 60/120 FPS interpolation the game then ran
 * 1-3% slower than real time and the audio had to be stretched (pitch drop, "slow audio"); 30 FPS was fine because pace_grid keeps an absolute
 * schedule. Now a late exit keeps its slot in the schedule: the following exits wait less until the game is back on time, so the game clock (and
 * the audio it produces) averages exactly one step per call. More than 100 ms behind (load, window drag) restarts the chain as before.
 * TF_RITMO_RECUPERA=0 restores the old behaviour (A/B). TF_SIM_ATRASO="p,ms" (tests) makes a fraction p of the calls exit ms later, to mimic a
 * slow machine on a fast one. */
static const bool g_catchup = !(getenv("TF_RITMO_RECUPERA") && atoi(getenv("TF_RITMO_RECUPERA")) == 0);
static uint32_t g_catch_n, g_catch_reset_n;
static void sim_delay(void) {
    static double p = -1, ms; static uint32_t rng = 12345;
    if (p < 0) { p = 0; if (const char *e = getenv("TF_SIM_ATRASO")) { p = atof(e); if (const char *c = strchr(e, ',')) ms = atof(c + 1); } }
    if (p <= 0 || ms <= 0) return;
    rng = rng * 1664525u + 1013904223u;
    if ((rng >> 8) * (1.0 / 16777216.0) < p) pace_wait_until(pace_now() + pace_ticks(ms));
}
void pace_exit(bool timed) {
    init();
    if (timed) sim_delay();
    int64_t n = pace_now();
    if (!timed) g_lastx = 0;
    else {
        if (g_lastx && n < g_lastx + step()) { g_lastx += step(); pace_wait_until(g_lastx); n = pace_now(); }   /* espacamento de 1/60 s entre saidas */
        else if (g_lastx && g_catchup && n - (g_lastx + step()) < g_freq.QuadPart / 10) { g_lastx += step(); g_catch_n++; }   /* late: keep the slot, no wait */
        else { if (g_lastx) g_catch_reset_n++; g_lastx = n; }
        if (g_dbg) { static uint32_t calls; if (++calls % 600 == 0) { fprintf(stderr, "[ritmo] saidas atrasadas recuperadas: %u, cadeia reiniciada (>100 ms): %u (ultimas 600 chamadas)%c", g_catch_n, g_catch_reset_n, 10); g_catch_n = g_catch_reset_n = 0; } }
    }
    if (g_have_exit) pace_stat(PACE_STAT_EXIT, pace_ms(n - g_exit_qpc));
    g_exit_qpc = n; g_have_exit = true;
}
bool pace_have_exit(void) { return g_have_exit; }
int64_t pace_last_exit(void) { return g_exit_qpc; }

/* Frame skipping: o trabalho de cada quadro entra num atraso acumulado (como o perf: cada quadro soma o trabalho e tira um vblank). Quadros
 * pesado/leve alternados (30 fps no fundo, comum na CPU x CPU) nao acumulam; so um atraso sustentado (passou de 1 vblank e continua) suspende. Antes
 * a regra era a media movel de "saida tarde" e suspendia com quadros que o perf mostrava sem atraso nenhum (sessao 2026-10-08, 7 vezes). */
void pace_work(double work_ms) {
    /* um quadro isolado (carregamento, travada) conta no maximo 2 vblanks: so atraso sustentado suspende */
    /* The per-call time includes the blocked present / vblank wait (not counted as limiter sleep): at 120 FPS a healthy game frame is one call of
     * ~33 ms (4 presents, 8.3 ms each) followed by a near-zero call. With the budget at exactly 16.7 ms the lag was a reflected random walk and
     * crossed the threshold on its own (title screen idle: 5-7 suspensions in 2 min, each ~2.5-10 s at 30 FPS; Leo's session 20261021_202449).
     * The budget now has 8 ms of slack and the threshold is 1.5 vblanks: only calls sustained above ~25 ms (3 heavy calls in a row) suspend. */
    g_lag_ms = std::min(250.0, std::max(0.0, g_lag_ms + std::min(work_ms, 2000.0 / 60.0) - (1000.0 / 60.0 + 8.0)));
    static const bool nosusp = getenv("TF_INTERP_NOSUSP") != nullptr;   /* testes: replays escondidos sem limitador (apresentar numa janela oculta bloqueia ~8 ms e disparava a suspensao) */
    if (g_lag_ms > 1500.0 / 60.0 && g_skip_mid == 0 && !nosusp) {
        g_skip_mid = susp_len(150); g_lag_ms = 0; g_susp_n++;
        if (g_dbg) fprintf(stderr, "[perf] 60 FPS: GPU/CPU no limite, quadros intermediarios suspensos por ~2,5 s%c", 10);
    }
}

/* intermediario que saiu atrasado (> 4 ms) nos 4 ultimos quadros novos: o jogo e/ou a GPU nao tem folga para o 60 com ritmo (cut-in de
 * ataque com rastro de movimento em 8x: o jogo leva ~12 ms e a GPU mais ~9; sessao 205658). Cada imagem ficava ~24/9 ms, o 'nao parece 60'.
 * Nesses trechos o jogo mostra so os quadros reais, a 30 constantes, por ~1 s (display.cpp apresenta o real no inicio da chamada seguinte). So atraso
 * sustentado: 1 ou 2 atrasos (textura nova, quadro pesado isolado) nao suspendem */
static uint32_t g_late_susp_n; static unsigned g_late_hist;
/* MODO ADIADO (2026-10-15). No cut-in pesado (sessao 205658, quadros ~10140-10200) o jogo chega ~12,6 ms depois da saida e a GPU termina o
 * intermediario ~3 ms depois: ele nao cabe antes da saida da chamada (16,7). Isso e agenda, nao falta de maquina. Em vez de cair para 30 (o
 * fallback abaixo), o intermediario do quadro F sai na chamada SEGUINTE (a do 2o vblank, que chega logo depois da saida), na mesma fase de sempre
 * em relacao as saidas, e o real F no inicio da chamada do quadro F+1 (a GPU ja terminou os dois ha muito). O compasso continua 16,7/16,7; custa
 * +16,7 ms de latencia so enquanto dura o trecho pesado. Entrar custa uma imagem de 33 ms (o pipeline anda uma posicao); sair, um quadro sem
 * intermediario. Entra com 2 intermediarios seguidos atrasados > 4 ms; sai quando a GPU termina o intermediario ate 12,5 ms depois da saida em todos
 * os ultimos 45 quadros novos (~1,5 s; minimo 2 s no modo). TF_RITMO_ADIADO=0 volta ao fallback de 30; =2 (teste) deixa o modo sempre ligado. */
static bool g_def; static int g_def_frames; static double g_def_need[45], g_def_work[16], g_def_off; static int g_def_ni, g_def_wi; static uint32_t g_def_n; static int64_t g_def_t0;
static const bool g_def_off_env = getenv("TF_RITMO_ADIADO") && atoi(getenv("TF_RITMO_ADIADO")) == 0;
static const bool g_def_force = getenv("TF_RITMO_ADIADO") && atoi(getenv("TF_RITMO_ADIADO")) == 2;   /* teste: modo adiado sempre ligado */
static void defer_enter(void);
bool pace_defer_active(void) { if (g_def_force && !g_def && g_have_exit) defer_enter(); return g_def; }
void pace_defer_reset(const char *why) {
    if (!g_def) return;
    g_def = false; g_late_hist = 0;
    if (g_dbg || g_def_n <= 50) fprintf(stderr, "[ritmo] modo adiado: fim (%s) depois de %.1f s, %d quadros novos%c", why, pace_ms(pace_now() - g_def_t0), g_def_frames, 10);
}
int64_t pace_defer_target(int64_t call_entry) {
    init();
    const double w = pace_ms(call_entry - g_exit_qpc);
    g_work[g_work_i++ & 15] = w; g_def_work[g_def_wi++ & 15] = w;
    double s[16]; const int n = std::min(g_def_wi, 16); std::copy(g_def_work, g_def_work + n, s); std::nth_element(s, s + (n * 9) / 10, s + n);
    /* o real sai 1 passo depois do intermediario, no inicio da chamada do proximo quadro novo: a fase precisa cobrir o trabalho do jogo (p90 + 1 ms) */
    const double alvo = std::min(pace_ms(step()) - 0.7, std::max(2.0, s[(n * 9) / 10] + 1.0));
    if (alvo > g_def_off) g_def_off = alvo; else g_def_off = std::max(alvo, g_def_off - 0.1);   /* sobe na hora (o real nunca atrasa), desce devagar (a malha de fase acompanha) */
    return g_exit_qpc + step() + (int64_t)(g_def_off * (double)g_freq.QuadPart / 1000.0);
}
void pace_defer_need(double need_ms) {
    g_def_need[g_def_ni++ % 45] = need_ms; g_def_frames++;
    if (g_def_force || g_def_frames < 60 || g_def_ni < 45) return;
    double mx = 0; for (double v : g_def_need) mx = std::max(mx, v);
    if (mx <= 12.5) pace_defer_reset("a GPU voltou a terminar o intermediario a tempo");
}
static void defer_enter(void) {
    g_def = true; g_def_frames = 0; g_def_ni = 0; g_def_wi = 0; g_def_n++; g_def_t0 = pace_now();
    g_def_off = std::min(pace_ms(step()) - 0.7, std::max(g_off_ms, g_phi_ms));   /* comeca na fase de agora: as apresentacoes ficam no mesmo lugar em relacao ao monitor */
    if (g_dbg || g_def_n <= 50) fprintf(stderr, "[ritmo] modo adiado: intermediarios atrasados em sequencia (jogo/GPU sem folga na chamada); intermediario na chamada seguinte, 60 constantes com +16,7 ms de latencia (%u vez(es))%c", g_def_n, 10);
}
void pace_mid_result(double late_ms) {
    static const bool nosusp = getenv("TF_INTERP_NOSUSP") != nullptr;
    g_late_hist = ((g_late_hist << 1) | (late_ms > 4.0 ? 1u : 0u)) & 15u;
    if (!g_def_off_env && (g_late_hist & 3u) == 3u && !g_def && g_have_exit) { g_late_hist = 0; defer_enter(); return; }
    if (g_late_hist == 15u && g_skip_mid == 0 && !nosusp) {
        g_skip_mid = susp_len(60); g_late_hist = 0; g_late_susp_n++;
        if (g_dbg || g_late_susp_n <= 50) fprintf(stderr, "[ritmo] intermediarios atrasados em sequencia (jogo/GPU sem folga): ~1 s a 30 quadros reais constantes (%u vez(es))%c", g_late_susp_n, 10);
    }
}
uint32_t pace_susp_n(void) { return g_susp_n; }
int pace_susp_mult(void) { return g_susp_mult; }
bool pace_mid_suspended(void) { return g_skip_mid > 0; }
bool pace_mid_allowed(void) { if (g_skip_mid > 0) { if (--g_skip_mid == 0) g_since_resume = 0; } else if (g_since_resume < (1 << 30)) g_since_resume++; return g_skip_mid == 0; }

static bool quarter_ok(double typ);
int64_t pace_mid_target(int64_t call_entry, bool want_quarter, bool *quarter) {
    init();
    g_work[g_work_i++ & 15] = pace_ms(call_entry - g_exit_qpc);   /* quanto o jogo levou para terminar o quadro */
    /* percentil ~75: um pico isolado nao atrasa todos os quadros (only the samples taken so far: the empty slots used to pull it to 0 at start) */
    double s[16]; const int n = std::min(g_work_i, 16); std::copy(g_work, g_work + n, s); std::nth_element(s, s + n * 3 / 4, s + n);
    g_phi_ms = std::min(10.0, std::max(1.0, s[n * 3 / 4] + 0.5));
    /* o real sai 1 passo depois do intermediario, no inicio da chamada seguinte; essa chamada so chega depois do trabalho que o jogo faz entre os
     * dois vblanks (w2, ~5 ms em telas do jogo). Com o intermediario saindo antes de w2, o real atrasava: M->R ~21 ms e R->M ~12 ms (caixa-preta do
     * F8), e no monitor de 120 Hz as imagens ficavam 1 e 3 atualizacoes. Soltar o intermediario so depois de w2 deixa os dois a 1/60 s; o quadro real
     * (o que traz a resposta ao controle) sai na mesma hora de antes. */
    static const bool sem_w2 = getenv("TF_RITMO_SEM_W2") != nullptr;   /* A/B: comportamento antigo (so phi) */
    const double base = std::max(g_phi_ms, g_w2_ms > 0 && !sem_w2 ? g_w2_ms + 0.5 : 0.0);
    /* a fase anda no maximo 0,1 ms por quadro novo: um salto (um pico entrando ou saindo da janela da GPU) mudava a hora do intermediario em relacao
     * ao monitor de uma vez, mais rapido do que a malha de fase corrige, e com a apresentacao a 1,7 ms da atualizacao ele perdia a vez (17 ms
     * numa imagem, 120 FPS medido com tela_gravar). Devagar, a malha de fase leva junto as saidas e as apresentacoes ficam no lugar. */
    const bool q = want_quarter && quarter_ok(std::max(base, g_off_typ));
    /* 120 FPS: os quatro passos so cabem nas duas chamadas com a fase ate ~meio passo; vale a fase tipica + 1 ms (o 2o maior da GPU, em 8x, passa
     * disso): um quadro mais lento sai atrasado, sem mudar o ritmo dos outros */
    const double alvo = std::min(14.0, std::max(base, q ? std::min(g_off_gpu, g_off_typ + 1.0) : g_off_gpu));
    /* ... exceto quando a GPU passou da hora em 2 quadros novos seguidos (a cena ficou mais pesada: intro com a parede de cartas, 8x): ai a fase sobe
     * de uma vez. Subindo 0,1 ms por quadro, uma cena 6 ms mais pesada dava ~2 s de intermediarios atrasados em sequencia (sessao 205658); de uma vez,
     * a malha de fase realinha as saidas e sobra um so intervalo fora do ritmo. Para baixo continua devagar. */
    static double suave = -1;
    if (suave < 0 || (g_late_run >= 2 && alvo > suave)) { suave = alvo; g_late_run = 0; }
    else suave += std::max(-0.1, std::min(0.1, alvo - suave));
    double off = suave;
    if (q) off = std::min(off, pace_ms(step()) / 2.0 + 0.5);
    if (quarter) *quarter = q;
    g_off_ms = off;
    return g_exit_qpc + (int64_t)(off * (double)g_freq.QuadPart / 1000.0);
}
void pace_gpu_done(void) {
    static const bool off = getenv("TF_RITMO_SEM_GPU") != nullptr;   /* A/B: sem a fase da GPU */
    if (off || !g_have_exit) return;
    /* fase em que a GPU terminou o intermediario em cada um dos ultimos 32 quadros novos (+0,3 ms); vale o 2o maior: uma espera isolada (cena nova,
     * texturas subindo) so atrasa aquele quadro, e nao a latencia de todos os seguintes. A mediana (g_off_typ) decide o 120 FPS */
    const double done = pace_ms(pace_now() - g_exit_qpc);
    static double need[32]; static int ni;
    need[ni++ & 31] = std::min(14.0, done + 0.3);
    g_late_run = done > g_off_ms ? g_late_run + 1 : 0;
    double s[32]; std::copy(need, need + 32, s); std::nth_element(s, s + 30, s + 32);
    g_off_gpu = s[30];
    std::nth_element(s, s + 16, s + 32); g_off_typ = s[16];
    if (g_dbg) {
        static int n, late; static double soma, mx; static std::vector<double> all; n++; soma += done; mx = std::max(mx, done); if (done > g_off_ms) late++; all.push_back(done);
        if (n >= 300) {
            std::sort(all.begin(), all.end());
            fprintf(stderr, "[lat] GPU terminou o intermediario %.2f ms depois da saida (medio; p50 %.2f p90 %.2f p97 %.2f max %.2f), depois da fase em %d de %d; fase da GPU %.2f ms, tipica %.2f, usada %.2f ms%c", soma / n,
                    all[n / 2], all[n * 9 / 10], all[n * 97 / 100], mx, late, n, g_off_gpu, g_off_typ, g_off_ms, 10);
            n = late = 0; soma = mx = 0; all.clear();
        }
    }
}
int64_t pace_step_ticks(void) { init(); return step(); }
int64_t pace_exit_target(void) { return g_lastx ? g_lastx + step() : 0; }
void pace_set_sub(int n) { g_sub = n >= 2 ? 2 : 1; }
/* 120 FPS neste quadro novo? Monitor de 100 Hz ou mais (num de 60 o segundo intermediario de cada atualizacao nunca apareceria) e os quatro
 * passos de meio vblank cabem nas duas chamadas do quadro: intermediario 1 e 2 antes da saida do 1o vblank e o real antes da saida do 2o
 * (fase ate meio passo + 0,5 ms; um pouco alem, pace_fit_call adianta o 2o e o real para logo antes de cada saida e no monitor de 120 Hz eles
 * caem na mesma atualizacao). Decide pela fase TIPICA (mediana): com o 2o maior, dois quadros pesados num segundo derrubavam o 120 a toda hora
 * (fase 10-14 ms em 8x). Se nem a tipica cabe (jogo ou GPU lentos), uns 3 s no ritmo do 60 FPS antes de tentar de novo. Em 8x a GPU termina
 * o 1o intermediario ~4,9 ms depois da saida (4x: ~3,4 ms). */
static bool quarter_ok(double typ) {
    if (g_mon_hz > 0 && g_mon_hz < 100) return false;
    const double half = pace_ms(step()) / 2.0;
    if (typ > half + 0.5) {
        if (g_quarter_off == 0) { g_quarter_falls++; if (g_dbg || g_quarter_falls <= 20) fprintf(stderr, "[ritmo] 120 FPS: fase tipica %.1f ms nao cabe em meio passo (%.1f ms): ~3 s no ritmo de 60 (%u vez(es))%c", typ, half, g_quarter_falls, 10); }
        g_quarter_off = 90; return false;
    }
    if (g_quarter_off > 0) { g_quarter_off--; return false; }
    return true;
}

void pace_real_arm(int fb, int64_t work_start, int64_t due, bool w2) { g_real_due = due ? due : (g_last_present ? g_last_present : pace_now()) + step(); g_real_start = work_start; g_real_fb = fb; g_real_w2 = w2; }
static void w2_update(void) { if (g_w2_i >= 4) { double s[16]; const int n = std::min(g_w2_i, 16); std::copy(g_w2, g_w2 + n, s); std::nth_element(s, s + n * 3 / 4, s + n); g_w2_ms = std::min(12.0, s[n * 3 / 4]); } }
void pace_w2_sample(void) { g_w2[g_w2_i++ & 15] = pace_ms(pace_now() - g_exit_qpc); w2_update(); }
bool pace_real_take(int *fb, int64_t *due, int64_t *work_start) {
    if (!g_real_due) return false;
    if (!g_real_w2) { *fb = g_real_fb; *due = g_real_due; *work_start = g_real_start; g_real_due = 0; return true; }   /* modo adiado: esta chamada e a do quadro novo seguinte */
    g_w2[g_w2_i++ & 15] = pace_ms(pace_now() - g_exit_qpc);   /* a chamada seguinte ao quadro novo chegou agora: trabalho do jogo entre os vblanks */
    if (g_dbg) {   /* o real saiu atrasado (a chamada chegou depois da hora dele)? */
        static int n, late; static double soma, w2s; const double atraso = pace_ms(pace_now() - g_real_due);
        n++; w2s += g_w2[(g_w2_i - 1) & 15]; if (atraso > 1.0) { late++; soma += atraso; }
        if (n >= 300) { fprintf(stderr, "[lat] quadro real depois da hora: %d de %d (medio %.1f ms); w2 medio %.1f ms, w2 p75 %.1f ms, phi %.1f ms%c", late, n, late ? soma / late : 0.0, w2s / n, g_w2_ms, g_phi_ms, 10); n = late = 0; soma = w2s = 0; }
    }
    w2_update();
    *fb = g_real_fb; *due = g_real_due; *work_start = g_real_start; g_real_due = 0;
    return true;
}

double pace_take_slept_ms(void) { const double s = g_slept_ms; g_slept_ms = 0; return s; }
double pace_slept_peek_ms(void) { return g_slept_ms; }   /* leitura sem zerar (divisao do quadro, display.cpp) */

void pace_stat(PaceStat id, double v) {
    if (!g_dbg) return;
    static const char *names[PACE_STAT_N] = {"saidas consecutivas (ideal 16,68)", "quadro real: ms depois do inicio do trabalho do jogo (era ~33,4)", "montar o intermediario (ms; sem a fase: + apresentacao)",
        "lista do GE do quadro novo: comeco (ms depois da saida)", "lista do GE do quadro novo: fim = quadro real enviado a GPU (ms depois da saida)"};
    static double sum[PACE_STAT_N], sq[PACE_STAT_N], mx[PACE_STAT_N]; static int n[PACE_STAT_N];
    sum[id] += v; sq[id] += v * v; mx[id] = std::max(mx[id], v);
    const int N = id == PACE_STAT_EXIT ? 600 : 300;
    if (++n[id] >= N) {
        const double m = sum[id] / n[id];
        fprintf(stderr, "[lat] %s: medio %.2f, desvio %.2f, max %.1f%s", names[id], m, sqrt(std::max(0.0, sq[id] / n[id] - m * m)), mx[id], id == PACE_STAT_REAL ? "" : "\n");
        if (id == PACE_STAT_REAL) fprintf(stderr, " (fase phi %.1f ms)\n", g_phi_ms);
        sum[id] = sq[id] = mx[id] = 0; n[id] = 0;
    }
}
