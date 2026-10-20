/* Saida de audio real: waveOut (44.1 kHz, estereo, 16 bits) alimentado por um anel de mixagem.
 * Cada canal do jogo (sceAudioOutput*) escreve no anel a partir do seu proprio cursor, que so anda para a frente; o
 * dispositivo consome o anel no ritmo real numa thread propria. Como o jogo roda a ~60 quadros/s reais, producao e
 * consumo andam juntos; se um canal fica para tras (underrun) ele e realinhado alguns ms a frente do ponto de leitura,
 * se fica muito a frente (ex.: NO_PACE) as amostras sao descartadas. Desligado com NO_AUDIO=1 ou NO_PACE=1. */
#include <windows.h>
#include <mmsystem.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <mutex>
#include <algorithm>
#include <cmath>
#include "util.h"

uint64_t k_now_us(void);
extern volatile uint64_t g_diag_frames;
static const uint32_t RING = 1u << 16;          /* quadros de audio (~1.5 s) */
static const uint32_t BUFF = 1024;              /* quadros por buffer do waveOut (~23 ms) */
static const int NBUF = 4;
static int32_t g_ring[RING * 2];
static std::mutex g_mx;
static uint64_t g_rd;                            /* quadros ja entregues ao dispositivo */
static bool g_on;
/* Transicao entre areas (avanco rapido do carregamento): o volume segue uma rampa continua (por amostra, na thread do dispositivo) em vez de
 * cortar seco e subir linear: desce em ~70 ms (cabe na reserva que ja esta no anel, entao a musica antiga some sem estalo) e sobe em ~0,6 s
 * com curva suave (smoothstep) quando a nova faixa comeca. g_pos: 0 = mudo, 1 = volume cheio. */
static volatile bool g_skip;   /* avanco rapido (carregamento): o audio produzido fora de tempo e descartado, senao atrasa depois */
static double g_pos = 1.0;
static const double FADE_OUT_LEN = 44100 * 0.07, FADE_IN_LEN = 44100 * 0.6;
static uint32_t g_realign;   /* quantas vezes um canal ficou para tras (falta de audio = estalo/engasgo) desde a ultima leitura */
uint32_t audio_out_realigns(void) { std::lock_guard<std::mutex> lk(g_mx); uint32_t n = g_realign; g_realign = 0; return n; }
static const uint32_t HIST = 44100 * 8;          /* caixa-preta do audio: o que foi entregue ao dispositivo nos ultimos 8 s (marca de audio) */
static int16_t g_hist[HIST * 2];
static uint64_t g_hist_n;
static HWAVEOUT g_wo;
static WAVEHDR g_hdr[NBUF];
static int16_t g_buf[NBUF][BUFF * 2];
static HANDLE g_ev;
static uint32_t g_dev_wakes, g_dev_empty;   /* device thread: wakeups, and wakeups that found every buffer already played (device ran dry or we woke late) */
static double g_dev_gap_max, g_st_max;      /* longest gap between wakeups (ms) and largest time-stretch applied (fraction), since the last debug line */

static void fill(int i) {
    {
        std::lock_guard<std::mutex> lk(g_mx);
        for (uint32_t f = 0; f < BUFF; f++) {
            uint32_t k = (uint32_t)((g_rd + f) & (RING - 1)) * 2;
            if (g_skip) g_pos = std::max(0.0, g_pos - 1.0 / FADE_OUT_LEN); else g_pos = std::min(1.0, g_pos + 1.0 / FADE_IN_LEN);
            const double gain = g_pos * g_pos * (3.0 - 2.0 * g_pos);
            for (int c = 0; c < 2; c++) {
                int32_t v = g_ring[k + c]; g_ring[k + c] = 0;
                if (gain < 1.0) v = (int32_t)std::lround(v * gain);
                g_buf[i][f * 2 + c] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
                g_hist[(g_hist_n + f) % HIST * 2 + c] = g_buf[i][f * 2 + c];
            }
        }
        g_hist_n += BUFF; g_rd += BUFF;
    }
    waveOutWrite(g_wo, &g_hdr[i], sizeof(WAVEHDR));
}
static DWORD WINAPI audio_thread(void *) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    for (;;) {
        WaitForSingleObject(g_ev, INFINITE);
        LARGE_INTEGER q, fq; QueryPerformanceCounter(&q); QueryPerformanceFrequency(&fq);
        static double last_ms; const double now_ms = q.QuadPart * 1000.0 / fq.QuadPart; int done = 0;
        for (int i = 0; i < NBUF; i++) if (g_hdr[i].dwFlags & WHDR_DONE) { g_hdr[i].dwFlags &= ~WHDR_DONE; done++; fill(i); }
        if (done) { std::lock_guard<std::mutex> lk(g_mx); g_dev_wakes++; if (done >= NBUF) g_dev_empty++; if (last_ms) g_dev_gap_max = std::max(g_dev_gap_max, now_ms - last_ms); last_ms = now_ms; }
    }
}
void audio_out_init(void) {
    if (g_on || getenv("NO_AUDIO") || getenv("NO_PACE")) return;
    WAVEFORMATEX wf = {}; wf.wFormatTag = WAVE_FORMAT_PCM; wf.nChannels = 2; wf.nSamplesPerSec = 44100;
    wf.wBitsPerSample = 16; wf.nBlockAlign = 4; wf.nAvgBytesPerSec = 44100 * 4;
    g_ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (waveOutOpen(&g_wo, WAVE_MAPPER, &wf, (DWORD_PTR)g_ev, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) return;
    for (int i = 0; i < NBUF; i++) {
        g_hdr[i] = WAVEHDR{}; g_hdr[i].lpData = (LPSTR)g_buf[i]; g_hdr[i].dwBufferLength = BUFF * 4;
        waveOutPrepareHeader(g_wo, &g_hdr[i], sizeof(WAVEHDR));
    }
    g_on = true;
    fprintf(stderr, "[audio] waveOut open: 44100 Hz stereo 16-bit, %d buffers of %u frames (%.0f ms each)%c", NBUF, BUFF, BUFF * 1000.0 / 44100, 10);
    for (int i = 0; i < NBUF; i++) fill(i);
    CreateThread(nullptr, 0, audio_thread, nullptr, 0, nullptr);
}
/* Grava os ultimos segundos entregues ao dispositivo como WAV (44,1 kHz estereo 16 bits); devolve a duracao em s (0 = nada). */
double audio_out_dump(const wchar_t *path) {
    std::lock_guard<std::mutex> lk(g_mx);
    const uint32_t n = (uint32_t)std::min<uint64_t>(g_hist_n, HIST);
    if (!g_on || !n) return 0.0;
    FILE *f = _wfopen(path, L"wb"); if (!f) return 0.0;
    const uint32_t bytes = n * 4, riff = 36 + bytes, fmt = 16, rate = 44100, brate = 44100 * 4; const uint16_t pcm = 1, ch = 2, al = 4, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmt, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f);
    fwrite(&rate, 4, 1, f); fwrite(&brate, 4, 1, f); fwrite(&al, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    for (uint32_t i = 0; i < n; i++) { const uint32_t k = (uint32_t)((g_hist_n - n + i) % HIST) * 2; fwrite(&g_hist[k], 2, 2, f); }
    fclose(f); return n / 44100.0;
}
/* Mistura 'frames' quadros (estereo ou mono, 16 bits) no anel a partir de *cursor (por canal) com volumes 0..0x8000. */
void audio_out_skip(bool on) { g_skip = on; }
namespace {
/* Controle de reserva por canal: o ritmo medio do jogo (tempo virtual -> real) nunca bate exatamente com o relogio do dispositivo (alguns decimos de %
 * por causa das esperas); sem controle a reserva encolhe ate zerar (engasgo) ou cresce ate descartar. */
struct ReserveCtl { const uint64_t *key; double ema; double last; double frac; double low_since; double integ; };   /* low_since: when the reserve first fell below target (ms, 0 = not low) */
ReserveCtl g_ctl[8]; int g_nctl;
ReserveCtl *ctl_for(const uint64_t *cursor) {
    for (int q = 0; q < g_nctl; q++) if (g_ctl[q].key == cursor) return &g_ctl[q];
    return g_nctl < 8 ? &(g_ctl[g_nctl++] = {cursor, 0.0, 0.0, 0.0}) : nullptr;
}
const uint64_t RESERVE_TARGET = 3300;   /* reserva alvo a frente do ponto de leitura (~75 ms; a producao chega em rajadas de 3 blocos por quadro e quadros de 25 ms sao comuns) */
}
void audio_out_mix(uint64_t *cursor, const int16_t *src, uint32_t frames, bool stereo, int lvol, int rvol) {
    if (!g_on || !src || g_skip || !frames) return;
    std::lock_guard<std::mutex> lk(g_mx);
    ReserveCtl *ctl = ctl_for(cursor);
    if (*cursor < g_rd + 64) {   /* o dispositivo ja alcancou o cursor (falta real de amostras); antes o limite era 512 quadros e o salto abria ~40 ms de silencio com a reserva ainda positiva */
        if (*cursor) {
            g_realign++;
            static const bool dbg = tf_lat_debug();
            if (dbg) { LARGE_INTEGER q, f; QueryPerformanceCounter(&q); QueryPerformanceFrequency(&f); const double now = q.QuadPart * 1000.0 / f.QuadPart;
                fprintf(stderr, "[audio] realinhou: canal %d cursor-leitura = %lld, bloco %u, %.1f ms desde a ultima mistura do canal, reserva media %.0f, t=%.0f ms%c", ctl ? (int)(ctl - g_ctl) : -1, (long long)*cursor - (long long)g_rd, frames, ctl && ctl->last ? now - ctl->last : -1.0, ctl ? ctl->ema : 0.0, now, 10); }
        }
        *cursor = g_rd + RESERVE_TARGET;
        if (ctl) { ctl->ema = (double)RESERVE_TARGET; ctl->low_since = 0; }
    }
    {   /* depuracao (TF_LAT_DEBUG): quantas amostras cada canal entregou por segundo real */
        static const bool dbg2 = tf_lat_debug();
        if (dbg2) { static double t0; static uint64_t v0, fr0, n[8]; static int calls[8]; LARGE_INTEGER q, f; QueryPerformanceCounter(&q); QueryPerformanceFrequency(&f); const double now = q.QuadPart * 1000.0 / f.QuadPart;
            const int ci = ctl ? (int)(ctl - g_ctl) : 7; n[ci] += frames; calls[ci]++;
            if (now - t0 >= 1000.0) { if (t0) { fprintf(stderr, "[audio] por s real (virtual %llu us, %llu quadros):", (unsigned long long)(k_now_us() - v0), (unsigned long long)(g_diag_frames - fr0)); for (int i = 0; i < g_nctl; i++) fprintf(stderr, " canal%d=%llu amostras em %d blocos (reserva %.0f);", i, (unsigned long long)n[i], calls[i], g_ctl[i].ema); fprintf(stderr, " | device: %u wakeups, longest gap %.1f ms, %u all-empty; stretch max %.1f%%%c", g_dev_wakes, g_dev_gap_max, g_dev_empty, g_st_max * 100.0, 10); g_dev_wakes = g_dev_empty = 0; g_dev_gap_max = g_st_max = 0; } t0 = now; v0 = k_now_us(); fr0 = g_diag_frames; memset(n, 0, sizeof n); memset(calls, 0, sizeof calls); } }
    }
    uint32_t out = frames;
    if (ctl) {
        /* correcao proporcional: pequena (<= ~0,4%) enquanto a reserva so oscila; correcoes pequenas que ligam e desligam por bloco soam como tremor de altura.
         * Se a reserva esta muito abaixo do alvo, o jogo esta rodando mais devagar que o tempo real (ex.: 120 FPS em cena pesada: ~52 quadros/s = ~87% do
         * audio por segundo) e a alternativa era um buraco de ~60 ms de silencio uma vez por segundo; entao o bloco e esticado ate 25% (altura cai junto com
         * a velocidade do jogo, em vez de picotar). A fracao de amostra que sobra fica no 'frac' do canal. */
        if (ctl->ema == 0.0) ctl->ema = (double)RESERVE_TARGET;
        ctl->ema += 0.05 * ((double)((long long)*cursor - (long long)g_rd) - ctl->ema);
        const double dev = (double)RESERVE_TARGET - ctl->ema;
        double st;
        /* A short hitch (texture creation, scene load: 50-100 ms) drains the reserve, and stretching up to 25% to refill it made the music drop by
         * a few semitones for about a second (logs from a laptop: stretch 5-12% right after each hitch). Stretching is capped at 4% (about a
         * quarter tone) unless the reserve stays low for 4 s, which means the game really runs slower than real time. */
        LARGE_INTEGER q, fq; QueryPerformanceCounter(&q); QueryPerformanceFrequency(&fq);
        const double now_ms = q.QuadPart * 1000.0 / fq.QuadPart;
        if (dev > 400.0) { if (!ctl->low_since) ctl->low_since = now_ms; } else ctl->low_since = 0;
        const double cap = ctl->low_since && now_ms - ctl->low_since > 4000.0 ? 0.25 : 0.04;
        /* PI controller instead of switching between ~0.4% and the cap: when the game runs 1-3% slower than real time (slow CPU, frames missing the
         * monitor's refresh) the old law flipped between 4% and 0.4% every second, so the pitch wobbled up and down. The integral term learns the
         * steady speed deficit and applies it continuously (a constant 1.5% is about a quarter of a semitone, hardly audible); the proportional
         * term only reacts to the reserve being off target. The reserve is allowed to sag while the integral converges (a few seconds). */
        const double dev_n = dev / (double)RESERVE_TARGET, dt = (double)frames / 44100.0;
        ctl->integ = std::max(-0.02, std::min(cap, ctl->integ + 0.03 * dev_n * dt));
        st = std::max(-0.02, std::min(cap, 0.01 * dev_n + ctl->integ));
        g_st_max = std::max(g_st_max, st);
        const double want = (double)frames * (1.0 + st) + ctl->frac;
        out = (uint32_t)std::max(1.0, std::floor(want)); ctl->frac = want - (double)out;
    }
    if (*cursor + out > g_rd + RING - 2 * BUFF) return;            /* muito a frente: descarta */
    for (uint32_t f = 0; f < out; f++) {
        uint32_t k = (uint32_t)((*cursor + f) & (RING - 1)) * 2;
        int l, r;
        if (out == frames) { l = stereo ? src[f * 2] : src[f]; r = stereo ? src[f * 2 + 1] : src[f]; }
        else {   /* bloco esticado/comprimido: interpolacao linear */
            const double pos = (double)f * (double)frames / (double)out; const uint32_t i0 = (uint32_t)pos, i1 = std::min(i0 + 1, frames - 1); const double fr = pos - (double)i0;
            const double l0 = stereo ? src[i0 * 2] : src[i0], l1 = stereo ? src[i1 * 2] : src[i1], r0 = stereo ? src[i0 * 2 + 1] : src[i0], r1 = stereo ? src[i1 * 2 + 1] : src[i1];
            l = (int)(l0 + (l1 - l0) * fr); r = (int)(r0 + (r1 - r0) * fr);
        }
        g_ring[k] += (l * lvol) >> 15; g_ring[k + 1] += (r * rvol) >> 15;
    }
    *cursor += out;
    if (ctl) { LARGE_INTEGER q, f; QueryPerformanceCounter(&q); QueryPerformanceFrequency(&f); ctl->last = q.QuadPart * 1000.0 / f.QuadPart; }
}
