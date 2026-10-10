#include "pacing.h"
#include <windows.h>
#include <dwmapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <algorithm>

namespace {
LARGE_INTEGER g_freq; bool g_init;
int64_t g_grid;                 /* proximo vblank nominal */
int64_t g_lastx;                /* alvo da saida anterior (a cadeia usa o alvo, nao a hora real: o atraso de cada espera nao se acumula) */
int64_t g_exit_qpc; bool g_have_exit;
double g_slept_ms;
int g_skip_mid; double g_lag_ms; static uint32_t g_susp_n;   /* g_lag_ms: mesmo atraso acumulado do medidor de "atrasados" do perf (display.cpp) */
double g_phi_ms = 6.0, g_work[16]; int g_work_i;
double g_w2_ms, g_w2[16]; int g_w2_i;   /* saida da chamada do quadro novo -> entrada da chamada seguinte (o jogo trabalha entre os dois vblanks) */
int64_t g_real_due, g_real_start; int g_real_fb = -1;
int64_t g_last_present;         /* hora da ultima apresentacao na GPU (pace_presented) */
const bool g_dbg = getenv("TF_LAT_DEBUG") != nullptr;

/* SINCRONIA COM O MONITOR. O jogo tinha um relogio proprio de 59,94 Hz; num monitor de 120 Hz (2 atualizacoes por quadro de 60 FPS) as saidas
 * escorregavam devagar em relacao as atualizacoes e, quando caiam perto da borda de uma, cada imagem ficava 1 atualizacao e a seguinte 3 (8,3 ms /
 * 25 ms) em vez de 2 e 2: tremido medido on a test monitor in ~30% do tempo (tools/tela_gravar.cpp). Agora, se a taxa do monitor (do DWM) e
 * multipla de ~60 (60, 119,88, 120, 240 Hz...), o passo vira exatamente k atualizacoes (120 Hz: 16,667 ms em vez de 16,683; 0,1% mais rapido, o
 * controle de reserva do audio absorve) e uma malha de fase desliza as cadeias de saida e da grade no maximo 0,3 ms por apresentacao ate as
 * apresentacoes cairem na fase g_fase de uma atualizacao (padrao 0,5 = no meio, longe das duas bordas). Nenhuma espera nova: o caminho controle ->
 * logica -> quadro real continua do mesmo tamanho; so muda QUANDO, em relacao ao monitor, ele acontece. Sem sincronia (75, 144 Hz...: 60 nao
 * divide) fica como antes. TF_RITMO_SEM_FASE=1 desliga; TF_RITMO_FASE=0..1 muda a fase alvo. */
int64_t g_vb_period, g_vb_ref; int g_vb_k; bool g_vb_lock, g_lock_allow; int g_mon_hz; uint32_t g_vb_poll;
double g_fase = 0.5;
double g_ph_sum, g_ph_abs; int g_ph_n;   /* estatistica do erro de fase (TF_LAT_DEBUG) */

void init(void) {
    if (!g_init) {
        QueryPerformanceFrequency(&g_freq); LARGE_INTEGER n; QueryPerformanceCounter(&n); g_grid = n.QuadPart; g_init = true;
        if (const char *e = getenv("TF_RITMO_FASE")) g_fase = std::max(0.0, std::min(0.99, atof(e)));
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
        if (lock && told_lock != g_vb_period * k) { told_lock = g_vb_period * k; fprintf(stderr, "[ritmo] monitor %.2f Hz: quadros a %.3f/s (%d atualizacao(oes) cada), apresentacoes no meio das atualizacoes (fase %.2f)%c", hz, (double)g_freq.QuadPart / (double)(g_vb_period * k), k, g_fase, 10); }
        if (!lock && !told_free && g_lock_allow && !off && hz > 0) { told_free = true; fprintf(stderr, "[ritmo] monitor %.2f Hz (janela em %d Hz): sem sincronia (60 nao divide), ritmo proprio de 59,94%c", hz, g_mon_hz, 10); }
        g_vb_lock = lock; g_vb_k = k;
    }
}
}

void pace_monitor(int hz, bool allow) { g_mon_hz = hz; g_lock_allow = allow; if (!allow && g_vb_lock) { g_vb_poll = 0; vsync_poll(); } }
void pace_presented(void) {
    init(); g_last_present = pace_now();
    vsync_poll();
    if (!g_vb_lock || !g_vb_period) return;
    /* erro de fase em [-P/2, P/2): quanto a apresentacao esta depois da fase alvo dentro da atualizacao */
    const int64_t P = g_vb_period, alvo = g_vb_ref + (int64_t)(g_fase * (double)P);
    int64_t e = (g_last_present - alvo) % P; if (e < 0) e += P; if (e >= P / 2) e -= P;
    const int64_t lim = g_freq.QuadPart * 3 / 10000;   /* 0,3 ms por apresentacao */
    const int64_t corr = std::max(-lim, std::min(lim, -e / 4));
    if (g_lastx) g_lastx += corr;
    g_grid += corr;
    if (g_dbg) { const double ms = pace_ms(e); g_ph_sum += ms; g_ph_abs += fabs(ms); if (++g_ph_n >= 600) { fprintf(stderr, "[lat] fase das apresentacoes: erro medio %.2f ms, medio absoluto %.2f ms (periodo do monitor %.2f ms)%c", g_ph_sum / g_ph_n, g_ph_abs / g_ph_n, pace_ms(P), 10); g_ph_sum = g_ph_abs = 0; g_ph_n = 0; } }
}

int64_t pace_now(void) { init(); LARGE_INTEGER n; QueryPerformanceCounter(&n); return n.QuadPart; }
double pace_ms(int64_t ticks) { init(); return (double)ticks * 1000.0 / (double)g_freq.QuadPart; }

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

void pace_exit(bool timed) {
    init();
    int64_t n = pace_now();
    if (!timed) g_lastx = 0;
    else {
        if (g_lastx && n < g_lastx + step()) { g_lastx += step(); pace_wait_until(g_lastx); n = pace_now(); }   /* espacamento de 1/60 s entre saidas */
        else g_lastx = n;
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
    g_lag_ms = std::min(250.0, std::max(0.0, g_lag_ms + std::min(work_ms, 2000.0 / 60.0) - 1000.0 / 60.0));
    if (g_lag_ms > 1000.0 / 60.0 && g_skip_mid == 0) {
        g_skip_mid = 150; g_lag_ms = 0; g_susp_n++;
        if (g_dbg) fprintf(stderr, "[perf] 60 FPS: GPU/CPU no limite, quadros intermediarios suspensos por ~2,5 s%c", 10);
    }
}

uint32_t pace_susp_n(void) { return g_susp_n; }
bool pace_mid_suspended(void) { return g_skip_mid > 0; }
bool pace_mid_allowed(void) { if (g_skip_mid > 0) g_skip_mid--; return g_skip_mid == 0; }

int64_t pace_mid_target(int64_t call_entry) {
    init();
    g_work[g_work_i++ & 15] = pace_ms(call_entry - g_exit_qpc);   /* quanto o jogo levou para terminar o quadro */
    double s[16]; std::copy(g_work, g_work + 16, s); std::nth_element(s, s + 12, s + 16);   /* percentil ~75: um pico isolado nao atrasa todos os quadros */
    g_phi_ms = std::min(10.0, std::max(1.0, s[12] + 0.5));
    /* o real sai 1 passo depois do intermediario, no inicio da chamada seguinte; essa chamada so chega depois do trabalho que o jogo faz entre os
     * dois vblanks (w2, ~5 ms em telas do jogo). Com o intermediario saindo antes de w2, o real atrasava: M->R ~21 ms e R->M ~12 ms (caixa-preta do
     * F8), e no monitor de 120 Hz as imagens ficavam 1 e 3 atualizacoes. Soltar o intermediario so depois de w2 deixa os dois a 1/60 s; o quadro real
     * (o que traz a resposta ao controle) sai na mesma hora de antes. */
    static const bool sem_w2 = getenv("TF_RITMO_SEM_W2") != nullptr;   /* A/B: comportamento antigo (so phi) */
    const double off = std::min(14.0, std::max(g_phi_ms, g_w2_ms > 0 && !sem_w2 ? g_w2_ms + 0.5 : 0.0));
    return g_exit_qpc + (int64_t)(off * (double)g_freq.QuadPart / 1000.0);
}

void pace_real_arm(int fb, int64_t work_start) { g_real_due = (g_last_present ? g_last_present : pace_now()) + step(); g_real_start = work_start; g_real_fb = fb; }
bool pace_real_take(int *fb, int64_t *due, int64_t *work_start) {
    if (!g_real_due) return false;
    g_w2[g_w2_i++ & 15] = pace_ms(pace_now() - g_exit_qpc);   /* a chamada seguinte ao quadro novo chegou agora: trabalho do jogo entre os vblanks */
    if (g_dbg) {   /* o real saiu atrasado (a chamada chegou depois da hora dele)? */
        static int n, late; static double soma, w2s; const double atraso = pace_ms(pace_now() - g_real_due);
        n++; w2s += g_w2[(g_w2_i - 1) & 15]; if (atraso > 1.0) { late++; soma += atraso; }
        if (n >= 300) { fprintf(stderr, "[lat] quadro real depois da hora: %d de %d (medio %.1f ms); w2 medio %.1f ms, w2 p75 %.1f ms, phi %.1f ms%c", late, n, late ? soma / late : 0.0, w2s / n, g_w2_ms, g_phi_ms, 10); n = late = 0; soma = w2s = 0; }
    }
    if (g_w2_i >= 4) { double s[16]; const int n = std::min(g_w2_i, 16); std::copy(g_w2, g_w2 + n, s); std::nth_element(s, s + n * 3 / 4, s + n); g_w2_ms = std::min(12.0, s[n * 3 / 4]); }
    *fb = g_real_fb; *due = g_real_due; *work_start = g_real_start; g_real_due = 0;
    return true;
}

double pace_take_slept_ms(void) { const double s = g_slept_ms; g_slept_ms = 0; return s; }

void pace_stat(PaceStat id, double v) {
    if (!g_dbg) return;
    static const char *names[PACE_STAT_N] = {"saidas consecutivas (ideal 16,68)", "quadro real: ms depois do inicio do trabalho do jogo (era ~33,4)", "interp_present + apresentacao (ms)"};
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
