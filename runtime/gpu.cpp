/* Despachante dos backends de GPU (ver gpu.h). */
#include "gpu.h"
#include "gfx.h"
#include "interp.h"
#include "pacing.h"

double g_gpu_us[8];
double gpu_now_us(void) { LARGE_INTEGER c, f; QueryPerformanceCounter(&c); QueryPerformanceFrequency(&f); return (double)c.QuadPart * 1e6 / (double)f.QuadPart; }

static const GpuBackend *g_b;
bool gpu_select(int renderer) {
    if (renderer == GFX_RENDER_D3D11) g_b = &gpu_backend_d3d11;
    else if (renderer == GFX_RENDER_VULKAN) g_b = &gpu_backend_vulkan;
    else return false;
    return true;
}
const char *gpu_backend_name(void) { return g_b ? g_b->name : "nenhum"; }
bool gpu_init(HWND wnd) { return g_b && g_b->init(wnd); }
bool gpu_ready(void) { return g_b && g_b->ready(); }
void gpu_release_swapchain(void) { if (g_b) g_b->release_swapchain(); }
const char *gpu_adapter_name(void) { return g_b ? g_b->adapter_name() : "?"; }
int gpu_fb_create(int nw, int nh, int res) { return g_b ? g_b->fb_create(nw, nh, res) : -1; }
void gpu_fb_destroy(int fb) { if (g_b) g_b->fb_destroy(fb); }
void gpu_fb_upload(int fb, const uint32_t *col, uint32_t cs, const uint16_t *z, uint32_t zs) { if (g_b) g_b->fb_upload(fb, col, cs, z, zs); }
int gpu_check_start(int fb) { return g_b ? g_b->check_start(fb) : -1; }
bool gpu_check_get(int t, uint32_t *out) { return g_b && g_b->check_get(t, out); }
bool gpu_fb_readback(int fb, std::vector<uint32_t> &out, int *w, int *h) { return g_b && g_b->fb_readback(fb, out, w, h); }
void gpu_draw(const GpuState &s, const GpuVtx *v, int n) { if (g_b) g_b->draw(s, v, n); }
void gpu_flush(void) { if (g_b) g_b->flush(); }
uint32_t gpu_tex_create(int w, int h, const uint32_t *px, bool mips) { return g_b ? g_b->tex_create(w, h, px, mips) : 0; }
void gpu_tex_release_now(uint32_t h) { if (g_b) g_b->tex_release(h); }
void gpu_tex_release(uint32_t h) { if (!interp_defer_release(h)) gpu_tex_release_now(h); }
void display_seq_present(int fb, int tag);   /* display.cpp (capturas.inc): gravador de sequencia dos quadros apresentados */
static int g_seq_tag;
void gpu_seq_tag(int tag) { g_seq_tag = tag; }
/* pace_presented DEPOIS da apresentacao: a chamada pode esperar uma imagem livre da swapchain, e o que conta para o ritmo e quando a imagem foi entregue */
void gpu_present_fb(int fb) { if (g_b) { display_seq_present(fb, g_seq_tag); g_seq_tag = 0; g_b->present_fb(fb); pace_presented(); } }
void gpu_present_pixels(const uint32_t *p, int w, int h) { if (g_b) { g_b->present_pixels(p, w, h); pace_presented(); } }
void gpu_present_last(void) { if (g_b) g_b->present_last(); }
void gpu_set_overlay(const uint32_t *rgba) { if (g_b) g_b->set_overlay(rgba); }
bool gpu_has_stencil(void) { return g_b && g_b->has_stencil && g_b->has_stencil(); }
bool gpu_ring_push(int fb, int slot, int nslots) { return g_b && g_b->ring_push && g_b->ring_push(fb, slot, nslots); }
bool gpu_ring_read(int slot, std::vector<uint32_t> &out, int *w, int *h) { return g_b && g_b->ring_read && g_b->ring_read(slot, out, w, h); }
