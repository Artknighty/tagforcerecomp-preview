/* Despachante dos backends de GPU (ver gpu.h). */
#include "gpu.h"
#include "gfx.h"
#include "interp.h"
#include "pacing.h"
#include <stdio.h>
#include <stdlib.h>
#include <mutex>

double g_gpu_us[8], g_gpu_pipe_ms;
double gpu_now_us(void) { LARGE_INTEGER c, f; QueryPerformanceCounter(&c); QueryPerformanceFrequency(&f); return (double)c.QuadPart * 1e6 / (double)f.QuadPart; }

static const GpuBackend *g_b;
/* 120 FPS com a thread de apresentacao (display.cpp): ela apresenta enquanto a thread do jogo desenha; toda chamada ao backend passa por esta
 * trava (recursiva: as funcoes daqui chamam umas as outras). Os desenhos do jogo pegam a trava por chamada (~20 ns cada). */
static std::mutex g_gpu_mu; static thread_local int t_gpu_depth;   /* trava + profundidade por thread (as funcoes daqui chamam umas as outras) */
struct GpuLock {   /* TF_GPU_LOCK_LOG: registra esperas longas pela trava (diagnostico da thread de apresentacao) */
    GpuLock() {
        if (t_gpu_depth++) return;
        if (g_gpu_mu.try_lock()) return;
        static const bool lg = getenv("TF_GPU_LOCK_LOG") != nullptr; const double t0 = lg ? gpu_now_us() : 0;
        g_gpu_mu.lock();
        if (lg) { const double w = (gpu_now_us() - t0) / 1000.0; if (w > 3.0) fprintf(stderr, "[trava] thread %lu esperou %.1f ms pela GPU%c", GetCurrentThreadId(), w, 10); }
    }
    ~GpuLock() { if (--t_gpu_depth == 0) g_gpu_mu.unlock(); }
};
/* o backend solta a trava durante esperas que nao mexem no estado de gravacao (pegar a imagem da swapchain, vkQueuePresentKHR: 4-9 ms no 120 FPS) */
void gpu_lock_yield_begin(int *saved) { *saved = t_gpu_depth; if (t_gpu_depth) { t_gpu_depth = 0; g_gpu_mu.unlock(); } }
void gpu_lock_yield_end(int saved) { if (saved) { g_gpu_mu.lock(); t_gpu_depth = saved; } }
#define GL GpuLock gl_
bool gpu_select(int renderer) { GL;
    if (renderer == GFX_RENDER_D3D11) g_b = &gpu_backend_d3d11;
    else if (renderer == GFX_RENDER_VULKAN) g_b = &gpu_backend_vulkan;
    else return false;
    return true;
}
const char *gpu_backend_name(void) { return g_b ? g_b->name : "nenhum"; }
bool gpu_init(HWND wnd) { GL; return g_b && g_b->init(wnd); }
bool gpu_ready(void) { GL; return g_b && g_b->ready(); }
void gpu_release_swapchain(void) { GL; if (g_b) g_b->release_swapchain(); }
int gpu_fb_create(int nw, int nh, int res) { GL; return g_b ? g_b->fb_create(nw, nh, res) : -1; }
void gpu_fb_destroy(int fb) { GL; if (g_b) g_b->fb_destroy(fb); }
void gpu_fb_upload(int fb, const uint32_t *col, uint32_t cs, const uint16_t *z, uint32_t zs) { GL; if (g_b) g_b->fb_upload(fb, col, cs, z, zs); }
int gpu_check_start(int fb) { GL; return g_b ? g_b->check_start(fb) : -1; }
bool gpu_check_get(int t, uint32_t *out) { GL; return g_b && g_b->check_get(t, out); }
bool gpu_fb_readback(int fb, std::vector<uint32_t> &out, int *w, int *h) { GL; return g_b && g_b->fb_readback(fb, out, w, h); }
void gpu_draw(const GpuState &s, const GpuVtx *v, int n) { GL; if (g_b) g_b->draw(s, v, n); }
void gpu_flush(void) { GL; if (g_b) g_b->flush(); }
void gpu_kick(void) { GL; if (g_b && g_b->kick) g_b->kick(); }
void gpu_hold(void) { GL; if (g_b && g_b->hold) g_b->hold(); else gpu_kick(); }
void gpu_mid_scope(bool on) { GL; if (g_b && g_b->mid_scope) g_b->mid_scope(on); }
double g_gpu_wait_ms;   /* soma das esperas de gpu_wait_kicked (o display.cpp mostra nos quadros pesados) */
double gpu_wait_kicked_for(double max_ms) { GL;
    if (!g_b) return 0;
    if (!g_b->wait_kicked_for) return gpu_wait_kicked();
    const double w = g_b->wait_kicked_for(max_ms); if (w > 0) g_gpu_wait_ms += w; return w;
}
double gpu_wait_kicked(void) { GL; const double w = g_b && g_b->wait_kicked ? g_b->wait_kicked() : 0.0; g_gpu_wait_ms += w; return w; }
uint32_t gpu_tex_create(int w, int h, const uint32_t *px, bool mips, const uint32_t *chain) { GL; if (!g_b) return 0; if (mips && chain && g_b->tex_create_chain) return g_b->tex_create_chain(w, h, px, chain); return g_b->tex_create(w, h, px, mips); }
uint32_t gpu_tex_create_bc7(int w, int h, int levels, const uint8_t *data) { GL; return g_b && g_b->tex_create_bc7 ? g_b->tex_create_bc7(w, h, levels, data) : 0; }
bool gpu_tex_update(uint32_t h, int x, int y, int w, int hh, const uint32_t *px, uint32_t stride) { GL; return g_b && g_b->tex_update && g_b->tex_update(h, x, y, w, hh, px, stride); }
void gpu_tex_release_now(uint32_t h) { GL; if (g_b) g_b->tex_release(h); }
void gpu_tex_release(uint32_t h) { GL; if (!interp_defer_release(h)) gpu_tex_release_now(h); }
void display_seq_present(int fb, int tag);   /* display.cpp (capturas.inc): gravador de sequencia dos quadros apresentados */
static int g_seq_tag;
uint64_t g_gpu_presents;   /* apresentacoes feitas (contador F3: imagens por segundo) */
void gpu_seq_tag(int tag) { g_seq_tag = tag; }
void gpu_present_fb_tag(int fb, int tag) { GL; g_seq_tag = tag; static const bool lg = getenv("TF_GPU_LOCK_LOG") != nullptr; const double t0 = gpu_now_us(); gpu_present_fb(fb);
    if (lg && gpu_now_us() - t0 > 3000.0) fprintf(stderr, "[trava] apresentacao levou %.1f ms%c", (gpu_now_us() - t0) / 1000.0, 10); }   /* thread de apresentacao: marca e apresenta sem outra apresentacao no meio */
/* pace_presented DEPOIS da apresentacao, com a hora do inicio: a chamada pode esperar o driver (o 60 FPS mede a fase no fim dela, o 120 no inicio; ver pacing.cpp) */
void gpu_present_fb(int fb) { GL;
    if (!g_b) return;
    static const bool plog = getenv("TF_PRESENT_LOG") != nullptr;   /* depuracao do ritmo: hora (QPC em ms, a mesma do tools/tela_gravar.cpp) de cada apresentacao */
    const double t0 = plog ? pace_ms(pace_now()) : 0; const int tag = g_seq_tag;
    display_seq_present(fb, g_seq_tag); g_seq_tag = 0; const int64_t ts = pace_now(); g_b->present_fb(fb); pace_presented(ts); g_gpu_presents++;
    if (plog) { extern uint64_t display_frames(void); fprintf(stderr, "[present] %.3f %.3f %c %llu\n", t0, pace_ms(pace_now()), tag ? 'M' : 'R', (unsigned long long)display_frames()); }   /* + quadro do jogo (achar o trecho de um tranco) */
}
void gpu_present_pixels(const uint32_t *p, int w, int h) { GL; if (g_b) { const int64_t ts = pace_now(); g_b->present_pixels(p, w, h); pace_presented(ts); g_gpu_presents++; } }
void gpu_present_last(void) { GL; if (g_b) g_b->present_last(); }
void gpu_set_overlay(const uint32_t *rgba) { GL; if (g_b) g_b->set_overlay(rgba); }
bool gpu_has_stencil(void) { GL; return g_b && g_b->has_stencil && g_b->has_stencil(); }
bool gpu_ring_push(int fb, int slot, int nslots) { GL; return g_b && g_b->ring_push && g_b->ring_push(fb, slot, nslots); }
bool gpu_ring_read(int slot, std::vector<uint32_t> &out, int *w, int *h) { GL; return g_b && g_b->ring_read && g_b->ring_read(slot, out, w, h); }
