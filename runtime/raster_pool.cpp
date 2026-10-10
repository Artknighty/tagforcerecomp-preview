#include "raster_pool.h"
#include <windows.h>
#include <atomic>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <emmintrin.h>
#include <x86intrin.h>

/* Sincronizacao: g_gen conta os trabalhos publicados (as threads dormem em WaitOnAddress ate mudar). g_ticket = gen(32) |
 * nbandas(16) | proxima banda(16): a banda so e reservada por CAS se o gen bate com o do trabalhador, entao uma thread
 * atrasada de um trabalho antigo nunca rouba banda do novo. Os campos do trabalho so sao lidos depois de reservar uma banda
 * (o principal so publica o proximo trabalho quando todas as bandas do atual terminaram), logo nao ha leitura rasgada. */
static std::atomic<uint32_t> g_gen{0};
static std::atomic<uint64_t> g_ticket{0};
static std::atomic<int> g_done{0};
static raster_fn g_fn; static void *g_ctx; static int g_y0, g_y1, g_bh;
static int g_nthreads = 1; static bool g_init;
static std::atomic<unsigned long long> g_busy{0}, g_wall{0}, g_calls{0}, g_bands{0};
void raster_stats_print() { if (!getenv("RASTER_STATS")) return; fprintf(stderr, "[rstat] chamadas %llu bandas %llu ocupado %.1f Mcy parede %.1f Mcy (paralelismo %.2f)\n", g_calls.load(), g_bands.load(), g_busy.load() / 1e6, g_wall.load() / 1e6, (double)g_busy.load() / (double)(g_wall.load() ? g_wall.load() : 1)); }

static inline uint64_t pack(uint32_t gen, uint32_t nb, uint32_t idx) { return ((uint64_t)gen << 32) | ((uint64_t)nb << 16) | idx; }
static void run_bands(uint32_t gen) {
    for (;;) {
        uint64_t t = g_ticket.load(std::memory_order_acquire);
        if ((uint32_t)(t >> 32) != gen) return;
        uint32_t nb = (t >> 16) & 0xffff, idx = t & 0xffff;
        if (idx >= nb) return;
        if (!g_ticket.compare_exchange_weak(t, t + 1, std::memory_order_acq_rel)) continue;
        int by0 = g_y0 + (int)idx * g_bh, by1 = by0 + g_bh; if (by1 > g_y1) by1 = g_y1;
        unsigned long long tb = __rdtsc(); g_fn(g_ctx, by0, by1); g_busy.fetch_add(__rdtsc() - tb, std::memory_order_relaxed); g_bands.fetch_add(1, std::memory_order_relaxed);
        g_done.fetch_add(1, std::memory_order_release);
    }
}
static DWORD WINAPI worker(LPVOID) {
    uint32_t seen = 0;
    for (;;) {
        uint32_t cur;
        /* gira ate ~0,3 ms (a proxima primitiva costuma vir em microssegundos; acordar de WaitOnAddress custa dezenas de us por
         * primitiva) e depois dorme */
        unsigned long long t0 = __rdtsc(); const unsigned long long spin_cycles = 1000000ull;
        for (int spin = 0; ; spin++) {
            cur = g_gen.load(std::memory_order_acquire);
            if (cur != seen) break;
            if ((spin & 63) != 63 || __rdtsc() - t0 < spin_cycles) _mm_pause(); else WaitOnAddress(&g_gen, &seen, sizeof seen, INFINITE);
        }
        seen = cur;
        run_bands(cur);
    }
}
static void init() {
    g_init = true;
    SYSTEM_INFO si; GetSystemInfo(&si);
    int n = (int)si.dwNumberOfProcessors; if (n > 8) n = 8;
    if (const char *e = getenv("TF_RASTER_THREADS")) n = atoi(e);
    if (n < 2) { g_nthreads = 1; return; }
    g_nthreads = n;
    for (int i = 1; i < n; i++) if (HANDLE h = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr)) CloseHandle(h); else g_nthreads = i;
}
int raster_threads(void) { if (!g_init) init(); return g_nthreads; }
void raster_parallel(int y0, int y1, raster_fn fn, void *ctx) {
    static uint32_t gen = 0;
    int rows = y1 - y0;
    if (rows <= 0) return;
    int nt = raster_threads();
    if (nt < 2 || rows < 8) { fn(ctx, y0, y1); return; }
    int nb = nt * 3; if (nb > rows) nb = rows;                 /* mais bandas que threads: equilibra a carga */
    int bh = (rows + nb - 1) / nb; nb = (rows + bh - 1) / bh;
    g_fn = fn; g_ctx = ctx; g_y0 = y0; g_y1 = y1; g_bh = bh; g_done.store(0, std::memory_order_relaxed);
    gen++; g_calls.fetch_add(1, std::memory_order_relaxed); unsigned long long tw0 = __rdtsc();
    g_ticket.store(pack(gen, (uint32_t)nb, 0), std::memory_order_release);
    g_gen.store(gen, std::memory_order_release);
    WakeByAddressAll(&g_gen);
    run_bands(gen);                                            /* a thread do jogo tambem rasteriza */
    while (g_done.load(std::memory_order_acquire) < nb) _mm_pause();
    g_wall.fetch_add(__rdtsc() - tw0, std::memory_order_relaxed);
}
