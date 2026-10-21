/* Audio output (rewritten 2026-10-20).
 *
 * The game's channels (sceAudioOutput*) mix their blocks, untouched, into one ring on the GAME timeline (44.1 kHz stereo): each channel writes from
 * its own cursor, which only moves forward. A device thread PULLS from the ring with a continuous resampler (4-point cubic, fractional read position
 * kept across calls) and writes to WASAPI (shared mode, event driven, the device's own rate and format). The read speed follows the game's real
 * speed with a smooth rate control on the ring's fill level: normally within +-1% (not audible), changed a little at every device period, never a
 * jump. Before this the game blocks themselves were stretched one by one (each ~6 ms block resampled on its own, restarting the interpolation at
 * every block boundary), which made small audible seams whenever the reserve control was active, and waveOut added its own buffering.
 *
 * If a channel falls behind the read position (a hitch longer than the reserve), it is moved a reserve ahead (a short gap) and the reserve target
 * grows (adaptive, 75 -> 170 ms); if it is too far ahead (no limiter) the samples are dropped. WASAPI failing to start falls back to waveOut with the
 * same pull path; TF_AUDIO_WAVEOUT=1 forces it. Disabled with NO_AUDIO=1 or NO_PACE=1. */
#include <windows.h>
#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <mutex>
#include <algorithm>
#include <cmath>
#include <vector>
#include <string>
#include "util.h"

uint64_t k_now_us(void);
extern volatile uint64_t g_diag_frames;
static const uint32_t RATE = 44100;              /* game timeline */
static const uint32_t RING = 1u << 16;          /* game samples in the ring (~1.5 s) */
static int32_t g_ring[RING * 2];
static std::mutex g_mx;
static double g_rpos;                            /* read position on the game timeline (fractional) */
static uint64_t g_rd;                            /* floor(g_rpos): samples consumed */
static uint64_t g_cleared;                       /* ring entries below this were zeroed after reading (kept 4 behind g_rd for the interpolation) */
static bool g_on;
static double now_ms(void) { LARGE_INTEGER q, f; QueryPerformanceCounter(&q); QueryPerformanceFrequency(&f); return q.QuadPart * 1000.0 / f.QuadPart; }

/* Loading fast-forward: audio produced out of time is dropped; the volume follows a continuous ramp (down ~70 ms, which fits in the reserve
 * already in the ring, up ~0.6 s with a smoothstep curve when the new track starts). g_pos: 0 = mute, 1 = full volume. */
static volatile bool g_skip;
static double g_pos = 1.0, g_skip_end_ms;
void audio_out_skip(bool on) { if (g_skip && !on) g_skip_end_ms = now_ms(); g_skip = on; }

static uint32_t g_realign;   /* channels that fell behind the read position (gap = click) since the last read */
uint32_t audio_out_realigns(void) { std::lock_guard<std::mutex> lk(g_mx); uint32_t n = g_realign; g_realign = 0; return n; }

/* black box for the audio mark: what went to the device in the last 8 s, at the device rate */
static std::vector<int16_t> g_hist; static uint64_t g_hist_n; static uint32_t g_hist_rate = RATE;

/* per-channel state (key = the channel's cursor in hle_audio.cpp) */
struct Chan { uint64_t *key; double last_ms; };
static Chan g_ch[8]; static int g_nch;
static Chan *chan_for(uint64_t *cursor) {
    for (int i = 0; i < g_nch; i++) if (g_ch[i].key == cursor) return &g_ch[i];
    return g_nch < 8 ? &(g_ch[g_nch++] = {cursor, 0.0}) : nullptr;
}
/* reserve target ahead of the read position, adaptive (see the top comment); after 60 s without an underrun it comes back down 25 ms */
static const double RESERVE_MIN = 3300, RESERVE_MAX = 7500, RESERVE_STEP = 1100;
static double g_target = RESERVE_MIN, g_under_ms;

/* rate control state (device thread, under g_mx) */
static double g_fill_ema = -1, g_adj, g_low_since;
static double g_adj_max;                         /* largest |rate change| since the last debug line */
static uint32_t g_dev_wakes, g_dev_empty; static double g_dev_gap_max;

/* Render n output frames (stereo float, -1..1) at 'dev_rate'. Called by the device thread only. */
static void render(float *out, uint32_t n, uint32_t dev_rate) {
    std::lock_guard<std::mutex> lk(g_mx);
    const double t = now_ms();
    /* fill level: the most advanced channel that wrote in the last 250 ms (an idle effect channel does not count) */
    double head = -1;
    for (int i = 0; i < g_nch; i++) if (g_ch[i].last_ms && t - g_ch[i].last_ms < 250.0) head = std::max(head, (double)*g_ch[i].key);
    if (g_target > RESERVE_MIN && t - g_under_ms > 60000.0) { g_target = std::max(RESERVE_MIN, g_target - RESERVE_STEP); g_under_ms = t; }
    const double base = (double)RATE / (double)dev_rate, dt = (double)n / dev_rate;
    if (head >= 0) {
        const double fill = head - g_rpos;
        /* the game delivers in bursts (3 blocks per 33 ms frame): smooth over ~0.4 s */
        if (g_fill_ema < 0) g_fill_ema = fill; else g_fill_ema += std::min(1.0, dt / 0.4) * (fill - g_fill_ema);
        const double err = (g_fill_ema - g_target) / g_target;   /* > 0: too much buffered -> read a little faster */
        if (err < -0.6) { if (!g_low_since) g_low_since = t; } else g_low_since = 0;
        /* Proportional only, like PPSSPP's / Dolphin's resampler: the reserve settles a little off target instead of being chased. With gain 0.005 the
         * loop's time constant is ~15 s, far slower than the fill filter (0.4 s), so it cannot oscillate; a first version with an integral term swung
         * the rate +-1% every few seconds (the reserve went 2700 <-> 4000): a slow pitch wobble. A 0.1% speed mismatch moves the reserve by 20%.
         * Near empty the correction gets stronger (avoid a gap); if the reserve stays very low for 3 s the game really runs slower than real time
         * (weak PC) and the read may slow down by up to 5% (a slight pitch drop) instead of a gap every second. */
        const double lo = g_low_since && t - g_low_since > 3000.0 ? -0.05 : -0.012, hi = err > 1.0 ? 0.03 : 0.006;
        const double want = std::max(lo, std::min(hi, 0.005 * err - 0.03 * std::max(0.0, -err - 0.5) + 0.01 * std::max(0.0, err - 1.0)));
        g_adj += std::max(-0.0002, std::min(0.0002, want - g_adj));   /* slew limit per device period: no zipper noise */
    }
    g_adj_max = std::max(g_adj_max, fabs(g_adj));
    const double step = base * (1.0 + g_adj);
    static const bool mute = getenv("TF_AUDIO_MUDO") != nullptr;   /* tests: everything runs, the device plays silence */
    const double fade_out = 1.0 / (dev_rate * 0.07), fade_in = 1.0 / (dev_rate * 0.6);
    if (g_hist.empty()) { g_hist_rate = dev_rate; g_hist.assign((size_t)dev_rate * 8 * 2, 0); }
    const size_t hn = g_hist.size() / 2;
    for (uint32_t f = 0; f < n; f++) {
        const uint64_t i = (uint64_t)g_rpos; const double x = g_rpos - (double)i;
        if (g_skip) g_pos = std::max(0.0, g_pos - fade_out); else g_pos = std::min(1.0, g_pos + fade_in);
        const double gain = mute ? 0.0 : g_pos * g_pos * (3.0 - 2.0 * g_pos) / 32768.0;
        for (int c = 0; c < 2; c++) {
            const double y0 = g_ring[((i - 1) & (RING - 1)) * 2 + c], y1 = g_ring[(i & (RING - 1)) * 2 + c],
                         y2 = g_ring[((i + 1) & (RING - 1)) * 2 + c], y3 = g_ring[((i + 2) & (RING - 1)) * 2 + c];
            /* Catmull-Rom (4-point cubic Hermite) */
            const double v = y1 + 0.5 * x * (y2 - y0 + x * (2.0 * y0 - 5.0 * y1 + 4.0 * y2 - y3 + x * (3.0 * (y1 - y2) + y3 - y0)));
            const float s = (float)std::max(-1.0, std::min(1.0, v * gain));
            out[f * 2 + c] = s;
            g_hist[(size_t)((g_hist_n + f) % hn) * 2 + c] = (int16_t)lrintf(s * 32767.0f);
        }
        g_rpos += step;
    }
    g_hist_n += n;
    g_rd = (uint64_t)g_rpos;
    while (g_cleared + 4 < g_rd) { const uint32_t k = (uint32_t)(g_cleared & (RING - 1)) * 2; g_ring[k] = g_ring[k + 1] = 0; g_cleared++; }
}

/* ---------------- WASAPI backend ---------------- */
static HANDLE g_ready;   /* set when a backend is running (or failed) */
static volatile bool g_quit; static HANDLE g_done;   /* audio_out_shutdown: the WASAPI thread stops the stream, releases it and signals g_done */
static bool g_wasapi_ok;
template <class T> static void rel(T *&p) { if (p) { p->Release(); p = nullptr; } }
static std::wstring default_id(IMMDeviceEnumerator *en) {
    IMMDevice *d = nullptr; std::wstring s;
    if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &d))) { LPWSTR id = nullptr; if (SUCCEEDED(d->GetId(&id)) && id) { s = id; CoTaskMemFree(id); } rel(d); }
    return s;
}
static void pro_audio_priority(void) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    typedef HANDLE(WINAPI * AvSet)(LPCWSTR, LPDWORD);
    if (HMODULE m = LoadLibraryW(L"avrt.dll")) if (AvSet f = (AvSet)(void *)GetProcAddress(m, "AvSetMmThreadCharacteristicsW")) { DWORD idx = 0; f(L"Pro Audio", &idx); }
}
/* Runs one WASAPI session until the device goes away or the default device changes; false = could not start. */
static bool wasapi_session(IMMDeviceEnumerator *en, bool first) {
    IMMDevice *dev = nullptr; IAudioClient *ac = nullptr; IAudioRenderClient *rc = nullptr; WAVEFORMATEX *wf = nullptr; HANDLE ev = nullptr; bool started = false;
    const std::wstring id0 = default_id(en);
    do {
        if (FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev))) break;
        if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void **)&ac))) break;
        if (FAILED(ac->GetMixFormat(&wf)) || !wf) break;
        bool is_float = wf->wFormatTag == 3 /* WAVE_FORMAT_IEEE_FLOAT */, is_pcm16 = wf->wFormatTag == WAVE_FORMAT_PCM && wf->wBitsPerSample == 16;
        if (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
            const WAVEFORMATEXTENSIBLE *x = (const WAVEFORMATEXTENSIBLE *)wf;
            is_float = x->SubFormat.Data1 == 3 && wf->wBitsPerSample == 32; is_pcm16 = x->SubFormat.Data1 == 1 && wf->wBitsPerSample == 16;
        }
        if (!is_float && !is_pcm16) { fprintf(stderr, "[audio] WASAPI: unsupported mix format (tag %u, %u bits)\n", wf->wFormatTag, wf->wBitsPerSample); break; }
        if (FAILED(ac->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 400000 /* 40 ms */, 0, wf, nullptr))) break;
        ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (FAILED(ac->SetEventHandle(ev))) break;
        if (FAILED(ac->GetService(__uuidof(IAudioRenderClient), (void **)&rc))) break;
        UINT32 bufn = 0; ac->GetBufferSize(&bufn);
        const uint32_t rate = wf->nSamplesPerSec, ch = wf->nChannels;
        REFERENCE_TIME per = 0; ac->GetDevicePeriod(&per, nullptr);
        if (first) fprintf(stderr, "[audio] WASAPI shared: %u Hz, %u channel(s), %s, buffer %u frames (%.1f ms), period %.1f ms; game audio 44100 Hz resampled (cubic)\n",
                           rate, ch, is_float ? "float32" : "s16", bufn, bufn * 1000.0 / rate, per / 10000.0);
        else fprintf(stderr, "[audio] WASAPI reopened on the default device (%u Hz)\n", rate);
        std::vector<float> tmp((size_t)bufn * 2);
        { BYTE *p = nullptr; if (SUCCEEDED(rc->GetBuffer(bufn, &p))) { memset(p, 0, (size_t)bufn * wf->nBlockAlign); rc->ReleaseBuffer(bufn, 0); } }   /* start from a full buffer of silence */
        if (FAILED(ac->Start())) break;
        started = true; g_wasapi_ok = true; SetEvent(g_ready);
        double last = 0; uint32_t polls = 0;
        for (;;) {
            if (WaitForSingleObject(ev, 200) != WAIT_OBJECT_0) { /* no event for 200 ms: device stalled or gone; checked below */ }
            if (g_quit) break;
            UINT32 pad = 0;
            if (FAILED(ac->GetCurrentPadding(&pad))) break;   /* AUDCLNT_E_DEVICE_INVALIDATED: unplugged, format changed... */
            const UINT32 nf = bufn - pad;
            const double t = now_ms();
            { std::lock_guard<std::mutex> lk(g_mx); g_dev_wakes++; if (pad == 0) g_dev_empty++; if (last) g_dev_gap_max = std::max(g_dev_gap_max, t - last); }
            last = t;
            if (nf) {
                render(tmp.data(), nf, rate);
                BYTE *p = nullptr;
                if (FAILED(rc->GetBuffer(nf, &p))) break;
                if (is_float) { float *o = (float *)p; for (UINT32 i = 0; i < nf; i++) { for (uint32_t c = 0; c < ch; c++) o[i * ch + c] = c < 2 ? tmp[i * 2 + c] : 0.0f; if (ch == 1) o[i] = 0.5f * (tmp[i * 2] + tmp[i * 2 + 1]); } }
                else { int16_t *o = (int16_t *)p; for (UINT32 i = 0; i < nf; i++) { for (uint32_t c = 0; c < ch; c++) o[i * ch + c] = c < 2 ? (int16_t)lrintf(tmp[i * 2 + c] * 32767.0f) : 0; if (ch == 1) o[i] = (int16_t)lrintf((tmp[i * 2] + tmp[i * 2 + 1]) * 16383.0f); } }
                if (FAILED(rc->ReleaseBuffer(nf, 0))) break;
            }
            if (++polls % 200 == 0 && default_id(en) != id0) { fprintf(stderr, "[audio] default output device changed\n"); break; }   /* ~2 s */
        }
        ac->Stop();
    } while (false);
    if (wf) CoTaskMemFree(wf);
    rel(rc); rel(ac); rel(dev); if (ev) CloseHandle(ev);
    return started;
}
static DWORD WINAPI wasapi_thread(void *) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    pro_audio_priority();
    IMMDeviceEnumerator *en = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void **)&en))) { SetEvent(g_ready); return 0; }
    bool first = true;
    for (;;) {
        const bool ok = wasapi_session(en, first);
        if (g_quit) { rel(en); CoUninitialize(); SetEvent(g_done); return 0; }
        if (first && !ok) { fprintf(stderr, "[audio] WASAPI could not start\n"); SetEvent(g_ready); rel(en); return 0; }
        first = false;
        Sleep(ok ? 100 : 1000);   /* device gone / changed: reopen on the (new) default device */
    }
}

/* ---------------- waveOut fallback ---------------- */
static const uint32_t WO_BUFF = 512; static const int WO_NBUF = 6;   /* 6 x 11.6 ms */
static HWAVEOUT g_wo; static WAVEHDR g_hdr[WO_NBUF]; static int16_t g_wbuf[WO_NBUF][WO_BUFF * 2]; static HANDLE g_wev;
static void wo_fill(int i) {
    float tmp[WO_BUFF * 2]; render(tmp, WO_BUFF, RATE);
    for (uint32_t k = 0; k < WO_BUFF * 2; k++) g_wbuf[i][k] = (int16_t)lrintf(tmp[k] * 32767.0f);
    waveOutWrite(g_wo, &g_hdr[i], sizeof(WAVEHDR));
}
static DWORD WINAPI waveout_thread(void *) {
    pro_audio_priority();
    for (int i = 0; i < WO_NBUF; i++) wo_fill(i);
    double last = 0;
    for (;;) {
        WaitForSingleObject(g_wev, INFINITE);
        int done = 0;
        for (int i = 0; i < WO_NBUF; i++) if (g_hdr[i].dwFlags & WHDR_DONE) { g_hdr[i].dwFlags &= ~WHDR_DONE; done++; wo_fill(i); }
        if (done) { const double t = now_ms(); std::lock_guard<std::mutex> lk(g_mx); g_dev_wakes++; if (done >= WO_NBUF) g_dev_empty++; if (last) g_dev_gap_max = std::max(g_dev_gap_max, t - last); last = t; }
    }
}
static bool waveout_start(void) {
    WAVEFORMATEX wf = {}; wf.wFormatTag = WAVE_FORMAT_PCM; wf.nChannels = 2; wf.nSamplesPerSec = RATE; wf.wBitsPerSample = 16; wf.nBlockAlign = 4; wf.nAvgBytesPerSec = RATE * 4;
    g_wev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (waveOutOpen(&g_wo, WAVE_MAPPER, &wf, (DWORD_PTR)g_wev, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) return false;
    for (int i = 0; i < WO_NBUF; i++) { g_hdr[i] = WAVEHDR{}; g_hdr[i].lpData = (LPSTR)g_wbuf[i]; g_hdr[i].dwBufferLength = WO_BUFF * 4; waveOutPrepareHeader(g_wo, &g_hdr[i], sizeof(WAVEHDR)); }
    fprintf(stderr, "[audio] waveOut: 44100 Hz stereo 16-bit, %d buffers of %u frames (%.1f ms each)\n", WO_NBUF, WO_BUFF, WO_BUFF * 1000.0 / RATE);
    CreateThread(nullptr, 0, waveout_thread, nullptr, 0, nullptr);
    return true;
}

/* Called before ExitProcess: with the WASAPI stream still running inside the audio stack, ExitProcess could hang (the test process never ended). */
void audio_out_shutdown(void) {
    if (!g_wasapi_ok || !g_done) return;
    g_quit = true; WaitForSingleObject(g_done, 500);
}
void audio_out_init(void) {
    if (g_on || getenv("NO_AUDIO") || getenv("NO_PACE")) return;
    g_on = true;
    if (!getenv("TF_AUDIO_WAVEOUT")) {
        g_ready = CreateEventW(nullptr, TRUE, FALSE, nullptr); g_done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        CreateThread(nullptr, 0, wasapi_thread, nullptr, 0, nullptr);
        WaitForSingleObject(g_ready, 3000);
        if (g_wasapi_ok) return;
    }
    if (!waveout_start()) { g_on = false; fprintf(stderr, "[audio] no audio output device\n"); }
}

/* Writes the last seconds sent to the device as a WAV (stereo 16-bit, device rate); returns the length in s (0 = nothing). */
double audio_out_dump(const wchar_t *path) {
    std::lock_guard<std::mutex> lk(g_mx);
    if (!g_on || g_hist.empty()) return 0.0;
    const uint32_t hn = (uint32_t)(g_hist.size() / 2), n = (uint32_t)std::min<uint64_t>(g_hist_n, hn);
    if (!n) return 0.0;
    FILE *f = _wfopen(path, L"wb"); if (!f) return 0.0;
    const uint32_t bytes = n * 4, riff = 36 + bytes, fmt = 16, rate = g_hist_rate, brate = rate * 4; const uint16_t pcm = 1, ch = 2, al = 4, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmt, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f);
    fwrite(&rate, 4, 1, f); fwrite(&brate, 4, 1, f); fwrite(&al, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    for (uint32_t i = 0; i < n; i++) { const size_t k = (size_t)((g_hist_n - n + i) % hn) * 2; fwrite(&g_hist[k], 2, 2, f); }
    fclose(f); return (double)n / rate;
}

/* Mixes 'frames' game samples (stereo or mono, 16-bit) into the ring from *cursor (per channel) with volumes 0..0x8000. No stretching here: the
 * device thread adapts the read speed. */
void audio_out_mix(uint64_t *cursor, const int16_t *src, uint32_t frames, bool stereo, int lvol, int rvol) {
    if (!g_on || !src || g_skip || !frames) return;
    std::lock_guard<std::mutex> lk(g_mx);
    Chan *ch = chan_for(cursor);
    const double t = now_ms();
    if (*cursor < g_rd + 64) {   /* the device caught up with this channel: real lack of samples */
        if (*cursor) {
            const bool active = ch && ch->last_ms && t - ch->last_ms < 300.0 && t - g_skip_end_ms > 500.0;   /* idle channel restarting / after loading: not a hitch */
            if (active) {
                g_realign++; g_under_ms = t;
                if (tf_lat_debug()) fprintf(stderr, "[audio] realinhou: canal %d cursor-leitura = %lld, bloco %u, %.1f ms desde a ultima mistura do canal, reserva media %.0f%c", ch ? (int)(ch - g_ch) : -1, (long long)*cursor - (long long)g_rd, frames, ch && ch->last_ms ? t - ch->last_ms : -1.0, g_fill_ema, 10);
                if (g_target < RESERVE_MAX) { g_target = std::min(RESERVE_MAX, g_target + RESERVE_STEP); fprintf(stderr, "[audio] underrun: reserve raised to %.0f ms%c", g_target * 1000.0 / RATE, 10); }
            }
        }
        *cursor = g_rd + (uint64_t)g_target;
    }
    {   /* debug (TF_LAT_DEBUG / diag.txt): samples each channel delivered per real second; format kept for diag_matrix.ps1 / audio_ritmo_resumo.py */
        static const bool dbg = tf_lat_debug();
        if (dbg) { static double t0; static uint64_t v0, fr0, nn[8]; static int calls[8];
            const int ci = ch ? (int)(ch - g_ch) : 7; nn[ci] += frames; calls[ci]++;
            if (t - t0 >= 1000.0) { if (t0) { fprintf(stderr, "[audio] por s real (virtual %llu us, %llu quadros, real %.1f ms):", (unsigned long long)(k_now_us() - v0), (unsigned long long)(g_diag_frames - fr0), t - t0);
                    for (int i = 0; i < g_nch; i++) fprintf(stderr, " canal%d=%llu amostras em %d blocos;", i, (unsigned long long)nn[i], calls[i]);
                    fprintf(stderr, " reserva %.0f (alvo %.0f) | device: %u wakeups, longest gap %.1f ms, %u all-empty; stretch max %.1f%%%c", g_fill_ema, g_target, g_dev_wakes, g_dev_gap_max, g_dev_empty, g_adj_max * 100.0, 10);
                    g_dev_wakes = g_dev_empty = 0; g_dev_gap_max = g_adj_max = 0; }
                t0 = t; v0 = k_now_us(); fr0 = g_diag_frames; memset(nn, 0, sizeof nn); memset(calls, 0, sizeof calls); } }
    }
    if (*cursor + frames > g_cleared + RING - 4096) return;   /* far ahead of the reader: drop */
    for (uint32_t f = 0; f < frames; f++) {
        const uint32_t k = (uint32_t)((*cursor + f) & (RING - 1)) * 2;
        const int l = stereo ? src[f * 2] : src[f], r = stereo ? src[f * 2 + 1] : src[f];
        g_ring[k] += (l * lvol) >> 15; g_ring[k + 1] += (r * rvol) >> 15;
    }
    *cursor += frames;
    if (ch) ch->last_ms = t;
}
