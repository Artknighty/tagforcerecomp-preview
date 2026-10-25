/* HLE: sceGe_user + sceDisplay + o GE do PSP. Interpreta as display lists e rasteriza por software o quadro nativo (o jogo
 * le a VRAM de volta); com o renderizador por GPU (gpu.h) cada primitiva tambem vai para o "quadro sombra" na GPU em
 * resolucao interna maior (secao "renderizador por GPU" mais abaixo).
 * Cobre: through (2D) e 3D (world/view/proj, recorte no plano proximo, cull), iluminacao por vertice, skinning, texturas
 * (5650/5551/4444/8888, CLUT4-32, DXT1/3/5, swizzle, bilinear, render-to-texture), blend, alpha/depth/stencil test, mascara de
 * escrita, scissor, clear mode, pontos/linhas e transferencias de blocos. Sem fog e sem curvas/patches. */
#include "ctl.h"
#include "util.h"
#include "hle.h"
#include <windows.h>
#include "raster_pool.h"
#include "gpu.h"
#include "pacing.h"
extern "C" { extern uint32_t g_trace[1024]; extern uint32_t g_trace_i; }
#include "gfx.h"
#include "imgio.h"
#include "texrepl.h"
#include "hdfont.h"
#include "gameid.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <memory>

struct GeCb { uint32_t signal_fn, signal_arg, finish_fn, finish_arg; };
static std::map<uint32_t, GeCb> g_gecb;
struct GeList { uint32_t start, pc, stall, cbid, base, offset; bool done; uint32_t stack[32]; int sp; };
static std::map<uint32_t, GeList> g_lists;
static uint32_t g_next_list = 1, g_next_cb = 0;

/* ------------------------------------------------------------------ estado grafico */
struct Ge {
    uint32_t cmd[256];
    float world[12], view[12], proj[16], tgen[12], bone[8 * 12];
    int wn, vn, pn, tn, bn;
    uint8_t clut[8192];
    uint32_t clut_bytes;
    uint32_t vaddr, iaddr;   /* VADDR/IADDR resolvidos; avancam a cada PRIM */
    uint32_t tri_warn;
} G;
static inline float fdata(uint32_t d) { uint32_t u = d << 8; float f; memcpy(&f, &u, 4); return f; }
#define CMD(n) (G.cmd[n])
/* enderecos do GE: 28 bits; fora da RAM/VRAM/scratchpad devolve um buffer descartavel */
static uint8_t g_dummy[0x10000];
static inline uint8_t *ge_ptr(uint32_t a) {
    a &= 0x0fffffffu;
    if ((a >= 0x04000000u && a < 0x04200000u - 64) || (a >= 0x08000000u && a < 0x0a000000u - 64) || (a >= 0x00010000u && a < 0x00014000u - 64)) return mem_ptr(a);
    memset(g_dummy, 0, 256); return g_dummy;
}

/* rw = 1/w do clip (1 em through); cx..cw = coordenadas de clip (so em 3D, clip = true) para recortar no plano proximo */
struct Vtx { float x, y, z, u, v; uint32_t col; bool has_col; float rw; float cx, cy, cz, cw; bool clip; };

static uint32_t fb_base() { return 0x04000000u | (CMD(0x9c) & 0x1ffff0u); }   /* o framebuffer e sempre VRAM */
static uint32_t fb_stride() { return CMD(0x9d) & 0xffff; }
static uint32_t z_base() { return 0x04000000u | (CMD(0x9e) & 0x1ffff0u); }

static inline uint32_t expand565(uint32_t p) { uint32_t r = p & 31, g = (p >> 5) & 63, b = (p >> 11) & 31; return 0xff000000u | ((b * 255 / 31) << 16) | ((g * 255 / 63) << 8) | (r * 255 / 31); }
static inline uint32_t expand5551(uint32_t p) { uint32_t r = p & 31, g = (p >> 5) & 31, b = (p >> 10) & 31, a = (p >> 15) & 1; return (a ? 0xff000000u : 0) | ((b * 255 / 31) << 16) | ((g * 255 / 31) << 8) | (r * 255 / 31); }
static inline uint32_t expand4444(uint32_t p) { return (((p >> 12) & 15) * 17u << 24) | (((p >> 8) & 15) * 17u << 16) | (((p >> 4) & 15) * 17u << 8) | ((p & 15) * 17u); }
static inline uint32_t pack565(uint32_t c) { return ((c & 255) >> 3) | ((((c >> 8) & 255) >> 2) << 5) | ((((c >> 16) & 255) >> 3) << 11); }
static inline uint32_t pack5551(uint32_t c) { return ((c & 255) >> 3) | ((((c >> 8) & 255) >> 3) << 5) | ((((c >> 16) & 255) >> 3) << 10) | ((c >> 31) << 15); }
static inline uint32_t pack4444(uint32_t c) { return ((c & 255) >> 4) | ((((c >> 8) & 255) >> 4) << 4) | ((((c >> 16) & 255) >> 4) << 8) | ((c >> 28) << 12); }

/* ------------------------------------------------------------------ texturas */
static uint32_t clut_entry(uint32_t idx) {
    uint32_t cm = CMD(0xc5), fmt = cm & 3, shift = (cm >> 2) & 31, mask = (cm >> 8) & 255, start = ((cm >> 16) & 31) << 4;
    idx = (((idx >> shift) & mask) + start) & 0x3ff;
    if (fmt == 3) return idx * 4 + 4 <= sizeof G.clut ? *(uint32_t *)&G.clut[idx * 4] : 0;
    uint32_t p = *(uint16_t *)&G.clut[idx * 2];
    return fmt == 0 ? expand565(p) : fmt == 1 ? expand5551(p) : expand4444(p);
}
static uint32_t tex_addr() { return (CMD(0xa0) & 0xffffff) | ((CMD(0xa8) & 0xf0000) << 8); }
/* DXT no layout do PSP: bloco de cor = 4 bytes de indices (2 bits/pixel, uma linha por byte) + cor0 + cor1 (565, vermelho
 * nos bits baixos); DXT3 = bloco de cor + 4 x u16 de alfa (4 bits/pixel); DXT5 = bloco de cor + u32/u16 de indices de alfa
 * (3 bits/pixel) + alfa0 + alfa1. Blocos 4x4 em ordem de linha; tbw em pixels. */
static uint32_t dxt_texel(uint32_t tpsm, uint32_t base, uint32_t stride, int x, int y) {
    uint32_t bsz = tpsm == 8 ? 8 : 16, bpr = std::max(1u, stride / 4);
    const uint8_t *b = ge_ptr(base + (((uint32_t)y / 4) * bpr + (uint32_t)x / 4) * bsz);
    int px = x & 3, py = y & 3;
    uint16_t c0 = *(const uint16_t *)(b + 4), c1 = *(const uint16_t *)(b + 6);
    /* no DXT as cores 565 tem o VERMELHO nos bits altos (ao contrario das texturas 5650 do GE): o verso das cartas sai
     * marrom como no PPSSPP (com a ordem do GE saia azul) */
    auto swap_rb = [](uint32_t c) { return (c & 0xff00ff00u) | ((c & 0xff) << 16) | ((c >> 16) & 0xff); };
    uint32_t k0 = swap_rb(expand565(c0)), k1 = swap_rb(expand565(c1)), idx = (b[py] >> (px * 2)) & 3, col;
    auto mix = [](uint32_t a, uint32_t bb, int wa, int wb, int d) {
        uint32_t o = 0xff000000u;
        for (int s = 0; s < 24; s += 8) o |= ((((a >> s) & 255) * wa + ((bb >> s) & 255) * wb) / d) << s;
        return o;
    };
    if (c0 > c1) col = idx == 0 ? k0 : idx == 1 ? k1 : idx == 2 ? mix(k0, k1, 2, 1, 3) : mix(k0, k1, 1, 2, 3);
    else col = idx == 0 ? k0 : idx == 1 ? k1 : idx == 2 ? mix(k0, k1, 1, 1, 2) : (tpsm == 8 ? 0u : 0xff000000u);
    if (tpsm == 9) {
        uint32_t a = (*(const uint16_t *)(b + 8 + py * 2) >> (px * 4)) & 15;
        col = (col & 0x00ffffffu) | ((a * 17) << 24);
    } else if (tpsm == 10) {
        uint64_t bits = *(const uint16_t *)(b + 12) | ((uint64_t)*(const uint32_t *)(b + 8) << 16);
        uint32_t a0 = b[14], a1 = b[15], ai = (uint32_t)(bits >> (3 * (py * 4 + px))) & 7, a;
        if (ai == 0) a = a0; else if (ai == 1) a = a1;
        else if (a0 > a1) a = (a0 * (8 - ai) + a1 * (ai - 1)) / 7;
        else a = ai == 6 ? 0 : ai == 7 ? 255 : (a0 * (6 - ai) + a1 * (ai - 1)) / 5;
        col = (col & 0x00ffffffu) | (a << 24);
    }
    return col;
}
#define CH(c, s) (((c) >> (s)) & 255)
/* floor inline (o floorf da libm e uma chamada por pixel sem SSE4.1); valido para |x| < 2^31 */
static inline int ifloor(float x) { int i = (int)x; return i - (x < (float)i); }
/* ------------------------------------------------------------------ fragmento */
static bool test_func(uint32_t f, uint32_t a, uint32_t b) {
    switch (f) { case 0: return false; case 1: return true; case 2: return a == b; case 3: return a != b; case 4: return a < b; case 5: return a <= b; case 6: return a > b; default: return a >= b; }
}
static inline uint32_t blend_factor(uint32_t f, uint32_t src, uint32_t dst, bool is_src, int c) {
    uint32_t sa = src >> 24, da = dst >> 24; int s = c * 8;
    if (is_src) switch (f) { case 0: return CH(dst, s); case 1: return 255 - CH(dst, s); case 2: return sa; case 3: return 255 - sa; case 4: return da; case 5: return 255 - da;
        case 6: return std::min(255u, sa * 2); case 7: return std::min(255u, (255 - sa) * 2); case 8: return std::min(255u, da * 2); case 9: return std::min(255u, (255 - da) * 2);
        default: return CH(CMD(0xe0), s); }
    switch (f) { case 0: return CH(src, s); case 1: return 255 - CH(src, s); case 2: return sa; case 3: return 255 - sa; case 4: return da; case 5: return 255 - da;
        case 6: return std::min(255u, sa * 2); case 7: return std::min(255u, (255 - sa) * 2); case 8: return std::min(255u, da * 2); case 9: return std::min(255u, (255 - da) * 2);
        default: return CH(CMD(0xe1), s); }
}

/* Estado de rasterizacao pre-calculado uma vez por PRIM (setup_state): o laco por pixel so le deste struct. */
static uint32_t region_left(uint32_t a) {   /* bytes validos a partir de a dentro de VRAM/RAM/scratchpad (0 = invalido) */
    a &= 0x0fffffffu;
    if (a >= 0x04000000u && a < 0x04200000u) return 0x04200000u - a;
    if (a >= 0x08000000u && a < 0x0a000000u) return 0x0a000000u - a;
    if (a >= 0x00010000u && a < 0x00014000u) return 0x00014000u - a;
    return 0;
}
/* Cache de texturas decodificadas (RGBA8888), preenchido sob demanda em blocos 4x4: amostrar CLUT/swizzle/DXT texel a texel era
 * ~25% do tempo de um quadro. Vale dentro de uma chamada de ge_run: e limpo a cada ge_run (a CPU pode reescrever a textura
 * entre uma e outra), a cada transferencia e a cada troca do framebuffer (render-to-texture). A chave inclui o hash da
 * paleta (mesma textura CLUT, cores diferentes). */
struct TexEnt { bool valid, full; double acc; uint32_t addr, psm, w, h, bw, swz, pal; std::vector<uint32_t> px; std::vector<uint8_t> done; uint32_t bwd; };
static TexEnt g_tc[64]; static unsigned g_tc_next;
static uint64_t g_ep_ge = 1, g_ep_tr = 1;   /* epocas do renderizador GPU: g_ep_ge muda a cada texcache_flush; g_ep_tr a cada transferencia */
static void texcache_flush() { for (auto &e : g_tc) e.valid = false; g_ep_ge++; }
struct RState {
    TexEnt *tc;
    uint8_t *fb; uint32_t fstride, psm, bpp_fb, flimit, pmask;
    uint16_t *zb; uint32_t zstride, zlimit; bool ztest, zwrite; uint32_t zfunc;
    uint32_t clear;
    bool atest; uint32_t afunc, aref, amask;
    bool st; uint32_t sfunc, sref, smask, sfail, szfail, szpass;
    bool blend, bfast; uint32_t bsf, bdf, beq;   /* bfast: SRCALPHA / INVSRCALPHA / soma (o blend da UI) */
    bool tex, tdbl, talpha; uint32_t tfn, tenv;
    uint32_t tpsm, tw, th, tbw, rowb, bppt, swzrow, tlimit, taddr; bool swz, linear, clampu, clampv; const uint8_t *tp;
    uint32_t pal[256];
};
static RState R;

static void setup_state(bool textured) {
    R.psm = CMD(0xd2) & 3; R.bpp_fb = R.psm == 3 ? 4 : 2;
    R.fb = mem_ptr(fb_base()); R.fstride = fb_stride(); R.flimit = region_left(fb_base());
    R.pmask = R.psm == 3 ? ((CMD(0xe8) & 0xffffff) | ((CMD(0xe9) & 0xff) << 24)) : 0;
    R.zb = (uint16_t *)mem_ptr(z_base()); R.zstride = CMD(0x9f) & 0xffff; R.zlimit = region_left(z_base());
    R.ztest = CMD(0x23) & 1; R.zwrite = !(CMD(0xe7) & 1); R.zfunc = CMD(0xde) & 7;
    R.clear = (CMD(0xd3) & 1) ? (CMD(0xd3) & 0x700) | 1 : 0;
    uint32_t at = CMD(0xdb); R.atest = CMD(0x22) & 1; R.afunc = at & 7; R.amask = (at >> 16) & 255; R.aref = (at >> 8) & R.amask;
    uint32_t stst = CMD(0xdc), sop = CMD(0xdd);
    R.st = CMD(0x24) & 1; R.sfunc = stst & 7; R.sref = (stst >> 8) & 255; R.smask = (stst >> 16) & 255;
    R.sfail = sop & 7; R.szfail = (sop >> 8) & 7; R.szpass = (sop >> 16) & 7;
    uint32_t bl = CMD(0xdf); R.blend = CMD(0x21) & 1; R.bsf = bl & 15; R.bdf = (bl >> 4) & 15; R.beq = (bl >> 8) & 7;
    R.bfast = R.blend && R.bsf == 2 && R.bdf == 3 && R.beq == 0;
    R.tex = textured && !R.clear;
    R.tc = nullptr;
    if (!R.tex) return;
    uint32_t tf = CMD(0xc9); R.tfn = tf & 7; R.talpha = (tf >> 8) & 1; R.tdbl = (tf >> 16) & 1; R.tenv = CMD(0xca);
    uint32_t ts = CMD(0xb8); R.tw = 1u << (ts & 15); R.th = 1u << ((ts >> 8) & 15);
    R.tpsm = CMD(0xc3) & 15; R.tbw = CMD(0xa8) & 0xffff; R.taddr = tex_addr(); R.swz = CMD(0xc2) & 1;
    R.bppt = (R.tpsm == 4) ? 4 : (R.tpsm == 5) ? 8 : (R.tpsm == 3 || R.tpsm == 7) ? 32 : 16;
    R.rowb = R.tbw * R.bppt / 8; R.swzrow = std::max(1u, R.rowb / 16) * 128;
    R.tlimit = region_left(R.taddr); R.tp = R.tlimit ? mem_ptr(R.taddr) : nullptr;
    uint32_t flt = CMD(0xc6); R.linear = (flt & 1) || ((flt >> 8) & 1);
    uint32_t wrap = CMD(0xc7); R.clampu = wrap & 1; R.clampv = (wrap >> 8) & 1;
    if (R.tpsm == 4) for (uint32_t i = 0; i < 16; i++) R.pal[i] = clut_entry(i);
    else if (R.tpsm == 5) for (uint32_t i = 0; i < 256; i++) R.pal[i] = clut_entry(i);
    if (R.tpsm == 6 || R.tpsm == 7 || R.tpsm > 10 || R.tw * R.th > 1024 * 1024) return;   /* sem cache */
    uint32_t ph = 2166136261u;
    if (R.tpsm == 4 || R.tpsm == 5) for (uint32_t i = 0, n = R.tpsm == 4 ? 16 : 256; i < n; i++) ph = (ph ^ R.pal[i]) * 16777619u;
    TexEnt *e = nullptr;
    for (auto &c : g_tc) if (c.valid && c.addr == R.taddr && c.psm == R.tpsm && c.w == R.tw && c.h == R.th && c.bw == R.tbw && c.swz == (uint32_t)R.swz && c.pal == ph) { e = &c; break; }
    if (!e) {
        for (auto &c : g_tc) if (!c.valid) { e = &c; break; }
        if (!e) e = &g_tc[g_tc_next++ % 64];
        e->valid = true; e->full = false; e->acc = 0; e->addr = R.taddr; e->psm = R.tpsm; e->w = R.tw; e->h = R.th; e->bw = R.tbw; e->swz = R.swz; e->pal = ph;
        e->bwd = (R.tw + 3) >> 2;
        e->px.resize((size_t)R.tw * R.th); e->done.assign((size_t)e->bwd * ((R.th + 3) >> 2), 0);
    }
    R.tc = e;
}
static inline uint32_t tfetch_raw(int x, int y) {   /* x, y ja dentro da textura */
    if (R.tpsm >= 8) return R.tpsm <= 10 ? dxt_texel(R.tpsm, R.taddr, R.tbw, x, y) : 0xffff00ffu;
    uint32_t xb = (uint32_t)x * R.bppt / 8, off;
    if (R.swz) off = ((uint32_t)y >> 3) * R.swzrow + (xb >> 4) * 128 + ((uint32_t)y & 7) * 16 + (xb & 15);
    else off = (uint32_t)y * R.rowb + xb;
    if (off + 4 > R.tlimit) return 0;
    const uint8_t *p = R.tp + off;
    switch (R.tpsm) {
    case 0: return expand565(*(const uint16_t *)p);
    case 1: return expand5551(*(const uint16_t *)p);
    case 2: return expand4444(*(const uint16_t *)p);
    case 3: return *(const uint32_t *)p;
    case 4: return R.pal[(x & 1) ? (*p >> 4) : (*p & 15)];
    case 5: return R.pal[*p];
    case 6: return clut_entry(*(const uint16_t *)p);
    default: return clut_entry(*(const uint32_t *)p);
    }
}
/* Varias threads do rasterizador podem decodificar o mesmo bloco ao mesmo tempo: a decodificacao e idempotente (mesmos
 * valores) e o flag "done" e publicado com release/acquire, entao quem ve done=1 ve os texels. */
static void texcache_decode(TexEnt *e, int bx, int by) {   /* decodifica o bloco 4x4 que comeca em (bx, by) */
    for (int y = by; y < by + 4 && y < (int)R.th; y++) for (int x = bx; x < bx + 4 && x < (int)R.tw; x++) e->px[(size_t)y * R.tw + x] = tfetch_raw(x, y);
    __atomic_store_n(&e->done[(size_t)(by >> 2) * e->bwd + (bx >> 2)], (uint8_t)1, __ATOMIC_RELEASE);
}
/* decodifica a textura inteira (primitivas grandes usam a maior parte dela; evita a checagem por bloco a cada texel) */
static void texcache_fill(TexEnt *e) {
    if (e->full) return;
    uint32_t tw = R.tw, th = R.th;
    /* formatos comuns (indexados 4/8 bits, 16 e 32 bits) decodificados em laco direto; DXT/outros pelo caminho por texel */
    uint32_t total = R.swz ? (((th + 7) >> 3) * R.swzrow) : (th * R.rowb);
    if (R.tpsm <= 5 && R.tlimit >= total + 16 && tw >= 16) {
        uint32_t *px = e->px.data(); const uint8_t *tp = R.tp;
        for (uint32_t y = 0; y < th; y++) {
            uint32_t *o = px + (size_t)y * tw;
            const uint8_t *row = R.swz ? tp + (y >> 3) * R.swzrow + (y & 7) * 16 : tp + (size_t)y * R.rowb;
            for (uint32_t x = 0; x < tw; x++) {
                uint32_t xb = x * R.bppt / 8;
                const uint8_t *p = R.swz ? row + (xb >> 4) * 128 + (xb & 15) : row + xb;
                switch (R.tpsm) {
                case 0: o[x] = expand565(*(const uint16_t *)p); break;
                case 1: o[x] = expand5551(*(const uint16_t *)p); break;
                case 2: o[x] = expand4444(*(const uint16_t *)p); break;
                case 3: o[x] = *(const uint32_t *)p; break;
                case 4: o[x] = R.pal[(x & 1) ? (*p >> 4) : (*p & 15)]; break;
                default: o[x] = R.pal[*p]; break;
                }
            }
        }
    } else {
        for (int by = 0; by < (int)th; by += 4) for (int bx = 0; bx < (int)tw; bx += 4)
            if (!e->done[(size_t)(by >> 2) * e->bwd + (bx >> 2)]) texcache_decode(e, bx, by);
    }
    std::fill(e->done.begin(), e->done.end(), (uint8_t)1);
    e->full = true;
}
static inline int wrap_x(int x) { return R.clampu ? (x < 0 ? 0 : (x >= (int)R.tw ? (int)R.tw - 1 : x)) : (x & ((int)R.tw - 1)); }
static inline int wrap_y(int y) { return R.clampv ? (y < 0 ? 0 : (y >= (int)R.th ? (int)R.th - 1 : y)) : (y & ((int)R.th - 1)); }
static inline uint32_t tfetch_w(int x, int y) {   /* coordenadas ja dentro da textura (apos wrap_x/wrap_y) */
    TexEnt *e = R.tc;
    if (!e) return tfetch_raw(x, y);
    if (e->full) return e->px[(size_t)y * R.tw + x];   /* textura inteira ja decodificada: sem checagem de bloco */
    if (!__atomic_load_n(&e->done[(size_t)(y >> 2) * e->bwd + (x >> 2)], __ATOMIC_ACQUIRE)) texcache_decode(e, x & ~3, y & ~3);
    return e->px[(size_t)y * R.tw + x];
}
static inline uint32_t tfetch(int x, int y) { return tfetch_w(wrap_x(x), wrap_y(y)); }
/* interpolacao bilinear com pesos 0..256 (mesma conta de antes: (top*(256-ay)+bot*ay)>>16), dois canais por vez em 64 bits */
static inline uint64_t pk_rb(uint32_t c) { return (uint64_t)(c & 0xff) | ((uint64_t)((c >> 16) & 0xff) << 32); }
static inline uint64_t pk_ga(uint32_t c) { return (uint64_t)((c >> 8) & 0xff) | ((uint64_t)(c >> 24) << 32); }
static inline uint32_t bilerp(uint32_t t00, uint32_t t10, uint32_t t01, uint32_t t11, int ax, int ay) {
    uint64_t wa = (uint64_t)(256 - ax), wb = (uint64_t)ax, wc = (uint64_t)(256 - ay), wd = (uint64_t)ay;
    uint64_t rb = ((pk_rb(t00) * wa + pk_rb(t10) * wb) * wc + (pk_rb(t01) * wa + pk_rb(t11) * wb) * wd) >> 16;
    uint64_t ga = ((pk_ga(t00) * wa + pk_ga(t10) * wb) * wc + (pk_ga(t01) * wa + pk_ga(t11) * wb) * wd) >> 16;
    rb &= 0x000000ff000000ffull; ga &= 0x000000ff000000ffull;
    return (uint32_t)rb | ((uint32_t)(rb >> 32) << 16) | ((uint32_t)ga << 8) | ((uint32_t)(ga >> 32) << 24);
}
static inline uint32_t tsample(float u, float v) {
    if (!R.linear) return tfetch(ifloor(u), ifloor(v));
    float fu = u - 0.5f, fv = v - 0.5f; int x0 = ifloor(fu), y0 = ifloor(fv);
    int ax = (int)((fu - x0) * 256.f), ay = (int)((fv - y0) * 256.f);
    int xa = wrap_x(x0), xb = wrap_x(x0 + 1), ya = wrap_y(y0), yb = wrap_y(y0 + 1);   /* 4 wraps em vez de 8 */
    uint32_t t00 = tfetch_w(xa, ya), t10 = tfetch_w(xb, ya), t01 = tfetch_w(xa, yb), t11 = tfetch_w(xb, yb);
    if (t00 == t10 && t00 == t01 && t00 == t11) return t00;
    return bilerp(t00, t10, t01, t11, ax, ay);
}
static inline uint32_t px_read(const uint8_t *p) {
    switch (R.psm) { case 0: return expand565(*(const uint16_t *)p); case 1: return expand5551(*(const uint16_t *)p);
                     case 2: return expand4444(*(const uint16_t *)p); default: return *(const uint32_t *)p; }
}
uint64_t g_fb_writes;
static inline void px_write(uint8_t *p, uint32_t c) {
    if (R.psm == 3) { uint32_t *q = (uint32_t *)p; *q = R.pmask ? ((c & ~R.pmask) | (*q & R.pmask)) : c; }
    else *(uint16_t *)p = (uint16_t)(R.psm == 0 ? pack565(c) : R.psm == 1 ? pack5551(c) : pack4444(c));
}
static inline uint32_t stencil_op(uint32_t op, uint32_t sval) {
    switch (op) { case 0: return sval; case 1: return 0; case 2: return R.sref; case 3: return (~sval) & 255;
                  case 4: return sval < 255 ? sval + 1 : 255; case 5: return sval > 0 ? sval - 1 : 0; default: return sval; }
}
static inline void frag(int x, int y, float z, uint32_t col, float u, float v) {
    uint32_t off = ((uint32_t)y * R.fstride + (uint32_t)x) * R.bpp_fb;
    if (off + R.bpp_fb > R.flimit) return;
    uint8_t *pp = R.fb + off;
    uint32_t zoff = ((uint32_t)y * R.zstride + (uint32_t)x) * 2;
    uint16_t *zp = zoff + 2 <= R.zlimit ? (uint16_t *)((uint8_t *)R.zb + zoff) : nullptr;
    if (R.clear) {   /* modo clear: grava cor/alfa/z do vertice conforme os bits 8-10, sem testes */
        if (R.clear & 0x300) {
            uint32_t nc = px_read(pp);
            if (R.clear & 0x100) nc = (nc & 0xff000000u) | (col & 0x00ffffffu);
            if (R.clear & 0x200) nc = (nc & 0x00ffffffu) | (col & 0xff000000u);
            px_write(pp, nc);
        }
        if ((R.clear & 0x400) && zp) *zp = (uint16_t)std::max(0.f, std::min(65535.f, z));
        return;
    }
    /* early-Z: sem alpha test nem stencil o resultado do teste de profundidade nao depende da cor, entao testa (e grava o z)
     * antes de amostrar a textura; pixels escondidos (overdraw) saem sem custo de textura/blend */
    bool zdone = false;
    if (R.ztest && zp && !R.atest && !R.st) {
        uint32_t zi = (uint32_t)std::max(0.f, std::min(65535.f, z));
        if (!test_func(R.zfunc, zi, *zp)) return;
        if (R.zwrite) *zp = (uint16_t)zi;
        zdone = true;
    }
    if (R.tex) {
        uint32_t t = tsample(u, v), o[3];
        if (R.tfn == 0 && !R.tdbl && col == 0xffffffffu) {   /* modulate com cor branca: tc*255/255 == tc */
            col = R.talpha ? t : (t | 0xff000000u);
            goto textured_done;
        }
        for (int c = 0; c < 3; c++) {
            uint32_t tc = CH(t, c * 8), vc = CH(col, c * 8), r;
            switch (R.tfn) {
            case 0: r = tc * vc / 255; if (R.tdbl) r = std::min(255u, r * 2); break;
            case 1: r = R.talpha ? (tc * (t >> 24) + vc * (255 - (t >> 24))) / 255 : tc; break;
            case 2: r = (vc * (255 - tc) + CH(R.tenv, c * 8) * tc) / 255; break;
            case 3: r = tc; break;
            default: r = std::min(255u, vc + tc); break;
            }
            o[c] = r;
        }
        uint32_t ta = t >> 24, va = col >> 24, a;
        switch (R.tfn) { case 0: case 2: case 4: a = R.talpha ? ta * va / 255 : va; break; case 3: a = R.talpha ? ta : va; break; default: a = va; }
        col = (a << 24) | (o[2] << 16) | (o[1] << 8) | o[0];
    }
textured_done:
    if (R.atest && !test_func(R.afunc, (col >> 24) & R.amask, R.aref)) return;
    /* stencil: vive no canal alfa do framebuffer. Teste = (ref & mask) FUNC (stencil & mask).
     * Com o teste desligado o alfa gravado e o do fragmento. */
    uint32_t sval = 0;
    if (R.st) {
        uint32_t d = px_read(pp); sval = d >> 24;
        if (!test_func(R.sfunc, R.sref & R.smask, sval & R.smask)) { px_write(pp, (d & 0x00ffffffu) | (stencil_op(R.sfail, sval) << 24)); return; }
    }
    if (R.ztest && zp && !zdone) {
        uint32_t zi = (uint32_t)std::max(0.f, std::min(65535.f, z));
        if (!test_func(R.zfunc, zi, *zp)) {
            if (R.st) { uint32_t d = px_read(pp); px_write(pp, (d & 0x00ffffffu) | (stencil_op(R.szfail, sval) << 24)); }
            return;
        }
        if (R.zwrite) *zp = (uint16_t)zi;
    }
    if (R.st) sval = stencil_op(R.szpass, sval);
    if (R.bfast) {   /* (src*sa + dst*(255-sa)) / 255 por canal; sa == 255 deixa a cor como esta */
        uint32_t sa = col >> 24;
        if (sa == 0 && !R.st) return;   /* totalmente transparente: o destino nao muda */
        if (sa != 255) {
            uint32_t dst = px_read(pp), ia = 255 - sa, out = 0;
            for (int s = 0; s < 24; s += 8) out |= ((CH(col, s) * sa + CH(dst, s) * ia) / 255) << s;
            col = out | (col & 0xff000000u);
        }
    } else if (R.blend) {
        uint32_t dst = px_read(pp), out = 0;
        for (int c = 0; c < 3; c++) {
            int sh = c * 8; int sv = CH(col, sh), dv = CH(dst, sh);
            int sfac = blend_factor(R.bsf, col, dst, true, c), dfac = blend_factor(R.bdf, col, dst, false, c), r;
            switch (R.beq) { case 0: r = (sv * sfac + dv * dfac) / 255; break; case 1: r = (sv * sfac - dv * dfac) / 255; break; case 2: r = (dv * dfac - sv * sfac) / 255; break;
                             case 3: r = std::min(sv, dv); break; case 4: r = std::max(sv, dv); break; default: r = abs(sv * sfac - dv * dfac) / 255; }
            out |= (uint32_t)std::max(0, std::min(255, r)) << sh;
        }
        col = out | (col & 0xff000000u);
    }
    if (R.st) col = (col & 0x00ffffffu) | (sval << 24);
    px_write(pp, col);
}

/* ------------------------------------------------------------------ vertices */
struct VType { int tc, col, nrm, pos, wt, idx, nw, through; };
static VType decode_vtype(uint32_t vt) { return {(int)(vt & 3), (int)((vt >> 2) & 7), (int)((vt >> 5) & 3), (int)((vt >> 7) & 3), (int)((vt >> 9) & 3), (int)((vt >> 11) & 3), (int)(((vt >> 14) & 7) + 1), (int)((vt >> 23) & 1)}; }
static int esz(int f) { return f == 1 ? 1 : f == 2 ? 2 : 4; }
static int align_up(int v, int a) { return (v + a - 1) & ~(a - 1); }
static int vertex_size(const VType &t, int *o_wt, int *o_tc, int *o_col, int *o_nrm, int *o_pos) {
    int size = 0, big = 1;
    if (t.wt) { int s = esz(t.wt); size = align_up(size, s); *o_wt = size; size += s * t.nw; big = std::max(big, s); }
    if (t.tc) { int s = esz(t.tc); size = align_up(size, s); *o_tc = size; size += s * 2; big = std::max(big, s); }
    if (t.col) { int s = t.col == 7 ? 4 : 2; size = align_up(size, s); *o_col = size; size += s; big = std::max(big, s); }
    if (t.nrm) { int s = esz(t.nrm); size = align_up(size, s); *o_nrm = size; size += s * 3; big = std::max(big, s); }
    if (t.pos) { int s = esz(t.pos); size = align_up(size, s); *o_pos = size; size += s * 3; big = std::max(big, s); }
    return align_up(size, big);
}
static void mul34(const float *m, const float in[3], float w, float out[3]) {
    for (int i = 0; i < 3; i++) out[i] = m[i] * in[0] + m[3 + i] * in[1] + m[6 + i] * in[2] + m[9 + i] * w;
}
/* ------------------------------------------------------------------ iluminacao (LTE 0x17), por vertice, no espaco do mundo
 * Materiais: emissiva 0x54, ambiente 0x55 (+alfa 0x58), difusa 0x56, especular 0x57, SPOW 0x5b; CMAT 0x53 (bits 0-2) troca
 * ambiente/difusa/especular pela cor do vertice. Luz ambiente global 0x5c (+alfa 0x5d). Luzes 0-3: liga 0x18+i, tipo 0x5f+i
 * (bits 0-1: 0 difusa, 1 difusa+especular, 2 difusa elevada a SPOW; bits 8-9: 0 direcional, 1 ponto, 2 spot), posicao
 * 0x63+3i, direcao 0x6f+3i, atenuacao 0x7b+3i (const, linear, quad), expoente spot 0x87+i, corte 0x8b+i, cores
 * ambiente/difusa/especular 0x8f+3i. Alfa final = alfa(ambiente global) * alfa(material ambiente). */
static int g_off_nrm, g_off_wt;   /* offsets da normal e dos pesos no vertice atual (definidos em draw_prim) */
static inline void unpack_rgb(uint32_t c, float o[3]) { o[0] = (c & 255) / 255.f; o[1] = ((c >> 8) & 255) / 255.f; o[2] = ((c >> 16) & 255) / 255.f; }
static uint32_t light_vertex(const uint8_t *p, const VType &t, const float pos[3], uint32_t vcol, bool has_vcol) {
    float n[3] = {0, 0, 1};
    if (t.nrm) {
        const uint8_t *q = p + g_off_nrm;
        for (int i = 0; i < 3; i++)
            n[i] = t.nrm == 3 ? ((const float *)q)[i] : t.nrm == 2 ? ((const int16_t *)q)[i] / 32768.f : ((const int8_t *)q)[i] / 128.f;
        if (t.wt) {   /* a normal tambem passa pelos ossos (so a parte 3x3) */
            const uint8_t *wq = p + g_off_wt; float acc[3] = {0, 0, 0};
            for (int b = 0; b < t.nw && b < 8; b++) {
                float w = t.wt == 3 ? ((const float *)wq)[b] : t.wt == 2 ? ((const uint16_t *)wq)[b] / 32768.f : wq[b] / 128.f;
                if (w == 0.f) continue;
                float o[3]; mul34(G.bone + b * 12, n, 0.f, o);
                for (int k = 0; k < 3; k++) acc[k] += w * o[k];
            }
            n[0] = acc[0]; n[1] = acc[1]; n[2] = acc[2];
        }
    }
    if (CMD(0x51) & 1) { n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2]; }   /* RNORM */
    float wp[3], wn[3]; mul34(G.world, pos, 1.f, wp); mul34(G.world, n, 0.f, wn);
    float nl = sqrtf(wn[0] * wn[0] + wn[1] * wn[1] + wn[2] * wn[2]); if (nl > 1e-12f) for (int k = 0; k < 3; k++) wn[k] /= nl;
    uint32_t cm = CMD(0x53) & 7;
    uint32_t amb_c = (cm & 1) && has_vcol ? vcol : ((CMD(0x58) & 255) << 24 | (CMD(0x55) & 0xffffff));
    uint32_t dif_c = (cm & 2) && has_vcol ? vcol : CMD(0x56), spc_c = (cm & 4) && has_vcol ? vcol : CMD(0x57);
    float ma[3], md[3], ms[3], me[3], ga[3];
    unpack_rgb(amb_c, ma); unpack_rgb(dif_c, md); unpack_rgb(spc_c, ms); unpack_rgb(CMD(0x54), me); unpack_rgb(CMD(0x5c), ga);
    float c[3]; for (int k = 0; k < 3; k++) c[k] = me[k] + ga[k] * ma[k];
    float spow = fdata(CMD(0x5b));
    for (int i = 0; i < 4; i++) {
        if (!(CMD(0x18 + i) & 1)) continue;
        uint32_t lt = CMD(0x5f + i), comp = lt & 3, type = (lt >> 8) & 3;
        float lp[3] = {fdata(CMD(0x63 + 3 * i)), fdata(CMD(0x64 + 3 * i)), fdata(CMD(0x65 + 3 * i))}, L[3], att = 1.f;
        if (type == 0) { for (int k = 0; k < 3; k++) L[k] = lp[k]; }
        else {
            for (int k = 0; k < 3; k++) L[k] = lp[k] - wp[k];
            float d = sqrtf(L[0] * L[0] + L[1] * L[1] + L[2] * L[2]);
            float k0 = fdata(CMD(0x7b + 3 * i)), k1 = fdata(CMD(0x7c + 3 * i)), k2 = fdata(CMD(0x7d + 3 * i)), den = k0 + k1 * d + k2 * d * d;
            att = den > 1e-6f ? std::min(1.f, 1.f / den) : 1.f;
        }
        float ll = sqrtf(L[0] * L[0] + L[1] * L[1] + L[2] * L[2]); if (ll > 1e-12f) for (int k = 0; k < 3; k++) L[k] /= ll;
        if (type == 2) {
            float sd[3] = {fdata(CMD(0x6f + 3 * i)), fdata(CMD(0x70 + 3 * i)), fdata(CMD(0x71 + 3 * i))};
            float sl = sqrtf(sd[0] * sd[0] + sd[1] * sd[1] + sd[2] * sd[2]); if (sl > 1e-12f) for (int k = 0; k < 3; k++) sd[k] /= sl;
            float cosang = -(L[0] * sd[0] + L[1] * sd[1] + L[2] * sd[2]);
            att *= cosang >= fdata(CMD(0x8b + i)) ? powf(std::max(cosang, 0.f), fdata(CMD(0x87 + i))) : 0.f;
        }
        float la[3], ld[3], ls[3]; unpack_rgb(CMD(0x8f + 3 * i), la); unpack_rgb(CMD(0x90 + 3 * i), ld); unpack_rgb(CMD(0x91 + 3 * i), ls);
        float ndl = wn[0] * L[0] + wn[1] * L[1] + wn[2] * L[2], dif = std::max(ndl, 0.f);
        if (comp == 2) dif = dif > 0.f ? powf(dif, spow) : 0.f;
        float spec = 0.f;
        if (comp == 1 && ndl >= 0.f) {
            float h[3] = {L[0], L[1], L[2] + 1.f}, hl = sqrtf(h[0] * h[0] + h[1] * h[1] + h[2] * h[2]);
            float ndh = hl > 1e-12f ? (wn[0] * h[0] + wn[1] * h[1] + wn[2] * h[2]) / hl : 0.f;
            spec = ndh > 0.f ? powf(ndh, spow) : 0.f;
        }
        for (int k = 0; k < 3; k++) c[k] += att * (la[k] * ma[k] + ld[k] * md[k] * dif + ls[k] * ms[k] * spec);
    }
    uint32_t a = ((amb_c >> 24) & 255) * (CMD(0x5d) & 255) / 255, out = a << 24;
    for (int k = 0; k < 3; k++) out |= (uint32_t)(std::max(0.f, std::min(1.f, c[k])) * 255.f + 0.5f) << (k * 8);
    return out;
}
/* clip -> tela (viewport 0x42-0x47, offset 0x4c/0x4d) */
static int g_ss = 1;   /* fator de escala da passada de rasterizacao atual (1 = quadro nativo; N = quadro sombra em resolucao interna) */
static void project(Vtx &v) {
    float cw = v.cw > 1e-6f ? v.cw : 1e-6f;
    v.rw = 1.f / cw;
    float xs = fdata(CMD(0x42)), ys = fdata(CMD(0x43)), zs = fdata(CMD(0x44)), xc = fdata(CMD(0x45)), yc = fdata(CMD(0x46)), zc = fdata(CMD(0x47));
    float ox = (CMD(0x4c) & 0xffff) / 16.f, oy = (CMD(0x4d) & 0xffff) / 16.f;
    v.x = xc + xs * v.cx / cw - ox; v.y = yc + ys * v.cy / cw - oy; v.z = zc + zs * v.cz / cw;
    if (g_ss > 1) { v.x *= g_ss; v.y *= g_ss; }   /* passada em resolucao interna maior */
}
static Vtx fetch(const uint8_t *p, const VType &t, int oc, int ot, int op, int ow) {
    Vtx v = {}; v.rw = 1.f; float pos[3] = {0, 0, 0};
    if (t.pos) {
        const uint8_t *q = p + op;
        for (int i = 0; i < 3; i++) {
            if (t.pos == 3) pos[i] = ((const float *)q)[i];
            else if (t.pos == 2) pos[i] = (t.through && i == 2) ? (float)((const uint16_t *)q)[i] : ((const int16_t *)q)[i] * (t.through ? 1.f : 1.f / 32768.f);
            else pos[i] = ((const int8_t *)q)[i] * (t.through ? 1.f : 1.f / 128.f);
        }
    }
    if (t.tc) {
        const uint8_t *q = p + ot;
        for (int i = 0; i < 2; i++) {
            float f;
            if (t.tc == 3) f = ((const float *)q)[i]; else if (t.tc == 2) f = ((const uint16_t *)q)[i] * (t.through ? 1.f : 1.f / 32768.f); else f = q[i] * (t.through ? 1.f : 1.f / 128.f);
            (i ? v.v : v.u) = f;
        }
    }
    if (t.col) {
        const uint8_t *q = p + oc;
        v.has_col = true;
        switch (t.col) { case 4: v.col = expand565(*(const uint16_t *)q); break; case 5: v.col = expand5551(*(const uint16_t *)q); break;
                         case 6: v.col = expand4444(*(const uint16_t *)q); break; default: v.col = *(const uint32_t *)q; }
    }
    if (t.wt && !t.through) {   /* skinning: pos = soma(peso_i * osso_i * pos), depois world/view/proj */
        const uint8_t *q = p + ow; float acc[3] = {0, 0, 0};
        for (int i = 0; i < t.nw && i < 8; i++) {
            float w = t.wt == 3 ? ((const float *)q)[i] : t.wt == 2 ? ((const uint16_t *)q)[i] / 32768.f : q[i] / 128.f;
            if (w == 0.f) continue;
            float o[3]; mul34(G.bone + i * 12, pos, 1.f, o);
            for (int k = 0; k < 3; k++) acc[k] += w * o[k];
        }
        pos[0] = acc[0]; pos[1] = acc[1]; pos[2] = acc[2];
    }
    if (t.through) { v.x = pos[0]; v.y = pos[1]; v.z = pos[2]; }
    else {
        float w3[3], v3[3];
        mul34(G.world, pos, 1.f, w3); mul34(G.view, w3, 1.f, v3);
        float cx = G.proj[0] * v3[0] + G.proj[4] * v3[1] + G.proj[8] * v3[2] + G.proj[12];
        float cy = G.proj[1] * v3[0] + G.proj[5] * v3[1] + G.proj[9] * v3[2] + G.proj[13];
        float cz = G.proj[2] * v3[0] + G.proj[6] * v3[1] + G.proj[10] * v3[2] + G.proj[14];
        float cw = G.proj[3] * v3[0] + G.proj[7] * v3[1] + G.proj[11] * v3[2] + G.proj[15];
        v.cx = cx; v.cy = cy; v.cz = cz; v.cw = cw; v.clip = true;
        project(v);
        if (t.tc) {   /* TEXMAP modo UV: escala/offset */
            v.u = v.u * fdata(CMD(0x48)) + fdata(CMD(0x4a)); v.v = v.v * fdata(CMD(0x49)) + fdata(CMD(0x4b));
        }
    }
    if (!t.through && (CMD(0x17) & 1)) { v.col = light_vertex(p, t, pos, v.has_col ? v.col : 0, v.has_col); v.has_col = true; }
    if (!v.has_col) v.col = (CMD(0x58) & 255) << 24 | (CMD(0x55) & 0xffffff);   /* cor ambiente do material (AMC/AMA) */
    return v;
}

/* ------------------------------------------------------------------ rasterizacao */
/* ------------------------------------------------------------------ caminho rapido do fragmento
 * O frag() generico consulta dezenas de campos de R a cada pixel (o compilador nao consegue mante-los em registradores:
 * as escritas no framebuffer podem "aliasar" R). Para os estados mais comuns (sem clear, sem stencil, framebuffer 8888 sem
 * mascara, blend inexistente ou SRCALPHA/INVSRCALPHA, textura inteira ja decodificada com funcao modulate/replace) o
 * FastShade faz a mesma conta com copias locais e o estado (textura, filtro, blend, z, alpha test) como parametros de
 * template. O resultado e IDENTICO ao do frag() (o bench compara as imagens). */
struct FastBase {
    uint8_t *fb; uint32_t fstride, flimit; uint16_t *zb; uint32_t zstride, zlimit; bool zwrite; uint32_t zfunc, afunc, amask, aref;
    const uint32_t *tpx; uint32_t tw, th; bool clampu, clampv, talpha, tdbl; uint32_t tfn;
    uint32_t bsf, bdf, beq, fixs, fixd;   /* blend geral: fatores e cores fixas (CMD 0xe0/0xe1) */
};
static inline uint32_t bfac(uint32_t f, uint32_t src, uint32_t dst, bool is_src, int c, uint32_t fix) {   /* como blend_factor, com a cor fixa ja lida */
    uint32_t sa = src >> 24, da = dst >> 24; int s = c * 8;
    switch (f) { case 0: return CH(is_src ? dst : src, s); case 1: return 255 - CH(is_src ? dst : src, s); case 2: return sa; case 3: return 255 - sa; case 4: return da; case 5: return 255 - da;
        case 6: return std::min(255u, sa * 2); case 7: return std::min(255u, (255 - sa) * 2); case 8: return std::min(255u, da * 2); case 9: return std::min(255u, (255 - da) * 2);
        default: return CH(fix, s); }
}
template <bool TEX, bool LIN, int BL, bool ZT, bool AT> struct FastShade {
    FastBase b;
    mutable int cx = INT_MIN, cy = INT_MIN; mutable uint32_t c00 = 0, c10 = 0, c01 = 0, c11 = 0;   /* cache dos 4 texels do bilinear */
    inline int wx(int x) const { return b.clampu ? (x < 0 ? 0 : (x >= (int)b.tw ? (int)b.tw - 1 : x)) : (x & ((int)b.tw - 1)); }
    inline int wy(int y) const { return b.clampv ? (y < 0 ? 0 : (y >= (int)b.th ? (int)b.th - 1 : y)) : (y & ((int)b.th - 1)); }
    inline uint32_t sample(float u, float v) const {
        if (!LIN) return b.tpx[(size_t)wy(ifloor(v)) * b.tw + wx(ifloor(u))];
        float fu = u - 0.5f, fv = v - 0.5f; int x0 = ifloor(fu), y0 = ifloor(fv);
        int ax = (int)((fu - x0) * 256.f), ay = (int)((fv - y0) * 256.f);
        int xa = wx(x0), xb = wx(x0 + 1), ya = wy(y0), yb = wy(y0 + 1);
        if (x0 != cx || y0 != cy) {   /* mesmos 4 texels do pixel anterior (textura ampliada): reaproveita */
            const uint32_t *r0 = b.tpx + (size_t)ya * b.tw, *r1 = b.tpx + (size_t)yb * b.tw;
            c00 = r0[xa]; c10 = r0[xb]; c01 = r1[xa]; c11 = r1[xb]; cx = x0; cy = y0;
        }
        if (c00 == c10 && c00 == c01 && c00 == c11) return c00;
        return bilerp(c00, c10, c01, c11, ax, ay);
    }
    inline void operator()(int x, int y, float z, uint32_t col, float u, float v) const {
        uint32_t off = ((uint32_t)y * b.fstride + (uint32_t)x) * 4;
        if (off + 4 > b.flimit) return;
        uint32_t *pp = (uint32_t *)(b.fb + off);
        uint16_t *zp = nullptr;
        if (ZT) { uint32_t zoff = ((uint32_t)y * b.zstride + (uint32_t)x) * 2; if (zoff + 2 <= b.zlimit) zp = (uint16_t *)((uint8_t *)b.zb + zoff); }
        if (ZT && !AT && zp) {   /* early-Z */
            uint32_t zi = (uint32_t)std::max(0.f, std::min(65535.f, z));
            if (!test_func(b.zfunc, zi, *zp)) return;
            if (b.zwrite) *zp = (uint16_t)zi;
        }
        if (TEX) {
            uint32_t t = sample(u, v);
            if (b.tfn == 0 && !b.tdbl && col == 0xffffffffu) col = b.talpha ? t : (t | 0xff000000u);
            else {
                uint32_t o[3];
                for (int c = 0; c < 3; c++) {
                    uint32_t tc = CH(t, c * 8), vc = CH(col, c * 8);
                    if (b.tfn == 0) { uint32_t r = tc * vc / 255; if (b.tdbl) r = std::min(255u, r * 2); o[c] = r; }
                    else o[c] = tc;
                }
                uint32_t ta = t >> 24, va = col >> 24, a;
                if (b.tfn == 0) a = b.talpha ? ta * va / 255 : va; else a = b.talpha ? ta : va;
                col = (a << 24) | (o[2] << 16) | (o[1] << 8) | o[0];
            }
        }
        if (AT && !test_func(b.afunc, (col >> 24) & b.amask, b.aref)) return;
        if (ZT && AT && zp) {
            uint32_t zi = (uint32_t)std::max(0.f, std::min(65535.f, z));
            if (!test_func(b.zfunc, zi, *zp)) return;
            if (b.zwrite) *zp = (uint16_t)zi;
        }
        if (BL == 2) {
            uint32_t dst = *pp, out = 0;
            for (int c = 0; c < 3; c++) {
                int sh = c * 8; int sv = CH(col, sh), dv = CH(dst, sh);
                int sfac = bfac(b.bsf, col, dst, true, c, b.fixs), dfac = bfac(b.bdf, col, dst, false, c, b.fixd), r;
                switch (b.beq) { case 0: r = (sv * sfac + dv * dfac) / 255; break; case 1: r = (sv * sfac - dv * dfac) / 255; break; case 2: r = (dv * dfac - sv * sfac) / 255; break;
                                 case 3: r = std::min(sv, dv); break; case 4: r = std::max(sv, dv); break; default: r = abs(sv * sfac - dv * dfac) / 255; }
                out |= (uint32_t)std::max(0, std::min(255, r)) << sh;
            }
            col = out | (col & 0xff000000u);
        }
        if (BL == 1) {
            uint32_t sa = col >> 24;
            if (sa == 0) return;
            if (sa != 255) {
                uint32_t dst = *pp, ia = 255 - sa, out = 0;
                for (int s = 0; s < 24; s += 8) out |= ((CH(col, s) * sa + CH(dst, s) * ia) / 255) << s;
                col = out | (col & 0xff000000u);
            }
        }
        *pp = col;
    }
};
struct GenericShade { inline void operator()(int x, int y, float z, uint32_t col, float u, float v) const { frag(x, y, z, col, u, v); } };
static bool fast_ok() {
    return !R.clear && !R.st && R.psm == 3 && R.pmask == 0 &&
           (!R.tex || (R.tc && R.tc->full && (R.tfn == 0 || R.tfn == 3)));
}
/* escolhe o sombreador (rapido, se o estado permite, ou o generico) e chama f(sombreador) */
template <class F> static inline void with_shade(F &&f) {
    if (!fast_ok()) { GenericShade g; f(g); return; }
    FastBase b = {R.fb, R.fstride, R.flimit, R.zb, R.zstride, R.zlimit, R.zwrite, R.zfunc, R.afunc, R.amask, R.aref,
                  R.tc ? R.tc->px.data() : nullptr, R.tw, R.th, R.clampu, R.clampv, R.talpha, R.tdbl, R.tfn,
                  R.bsf, R.bdf, R.beq, CMD(0xe0), CMD(0xe1)};
    const bool tex = R.tex, lin = R.tex && R.linear, zt = R.ztest, at = R.atest;
    const int bl = !R.blend ? 0 : R.bfast ? 1 : 2;
#define FS(T, L, B, Z, A) if (tex == T && lin == L && bl == B && zt == Z && at == A) { FastShade<T, L, B, Z, A> sh{b}; f(sh); return; }
#define FS_ZA(T, L, B) FS(T, L, B, false, false) FS(T, L, B, false, true) FS(T, L, B, true, false) FS(T, L, B, true, true)
#define FS_B(T, L) FS_ZA(T, L, 0) FS_ZA(T, L, 1) FS_ZA(T, L, 2)
    FS_B(false, false) FS_B(true, false) FS_B(true, true)
#undef FS
#undef FS_ZA
#undef FS_B
    GenericShade g; f(g);
}
struct Scissor { int x1, y1, x2, y2; };
/* banda de linhas da thread atual (rasterizacao paralela: cada thread desenha so as linhas [t_band_y0, t_band_y1)) */
static thread_local int t_band_y0 = INT_MIN, t_band_y1 = INT_MAX;
static Scissor scissor() {
    Scissor s = {(int)(CMD(0xd4) & 0x3ff), (int)((CMD(0xd4) >> 10) & 0x3ff), (int)(CMD(0xd5) & 0x3ff), (int)((CMD(0xd5) >> 10) & 0x3ff)};
    if (g_ss > 1) { s.x1 *= g_ss; s.y1 *= g_ss; s.x2 = (s.x2 + 1) * g_ss - 1; s.y2 = (s.y2 + 1) * g_ss - 1; }
    if (s.y1 < t_band_y0) s.y1 = t_band_y0;
    if (s.y2 > t_band_y1 - 1) s.y2 = t_band_y1 - 1;
    return s;
}
/* Sprite (retangulo de 2 vertices). Normalmente U varia com X e V com Y. Em through, se o segundo vertice esta a esquerda e
 * abaixo do primeiro (ou a direita e acima), o GE gira a textura 90 graus: U passa a variar com Y e V com X. O jogo usa isso
 * na moldura da carta do painel de detalhes (as laterais sao faixas horizontais da textura giradas). */
template <class Sh> static void draw_sprite_t(const Vtx &a, const Vtx &b, bool through, const Sh &sh) {
    Scissor sc = scissor();
    float x1 = std::min(a.x, b.x), x2 = std::max(a.x, b.x), y1 = std::min(a.y, b.y), y2 = std::max(a.y, b.y);
    float dx = b.x - a.x, dy = b.y - a.y;
    bool rot = through && dx * dy < 0;
    float dux = 0, duy = 0, dvx = 0, dvy = 0;   /* derivadas de U e V por pixel em X e em Y */
    if (rot) { duy = dy != 0 ? (b.u - a.u) / dy : 0; dvx = dx != 0 ? (b.v - a.v) / dx : 0; }
    else { dux = dx != 0 ? (b.u - a.u) / dx : 0; dvy = dy != 0 ? (b.v - a.v) / dy : 0; }
    int xs = std::max((int)floorf(x1), sc.x1), xe = std::min((int)ceilf(x2), sc.x2 + 1);
    int ys = std::max((int)floorf(y1), sc.y1), ye = std::min((int)ceilf(y2), sc.y2 + 1);
    for (int y = ys; y < ye; y++) {
        float fx = xs + 0.5f - a.x, fy = y + 0.5f - a.y;
        float u = a.u + fx * dux + fy * duy, v = a.v + fx * dvx + fy * dvy;
        for (int x = xs; x < xe; x++, u += dux, v += dvx) sh(x, y, b.z, b.col, u, v);
    }
}
static void draw_sprite(const Vtx &a, const Vtx &b, bool through) { with_shade([&](const auto &sh) { draw_sprite_t(a, b, through, sh); }); }
/* linha de 1 pixel (DDA), interpolando cor/UV/Z; o ultimo pixel nao e desenhado (como no GE) salvo em pontos */
static void draw_line(const Vtx &a, const Vtx &b, bool flat) {
    Scissor sc = scissor();
    float dx = b.x - a.x, dy = b.y - a.y; int n = (int)std::max(fabsf(dx), fabsf(dy));
    if (n == 0) n = 1;
    bool point = (a.x == b.x && a.y == b.y);
    for (int i = 0; i < (point ? 1 : n); i++) {
        float t = (float)i / n; int x = (int)floorf(a.x + dx * t), y = (int)floorf(a.y + dy * t);
        if (x < sc.x1 || x > sc.x2 || y < sc.y1 || y > sc.y2) continue;
        uint32_t col = b.col;
        if (!flat) { col = 0; for (int k = 0; k < 32; k += 8) col |= (uint32_t)(CH(a.col, k) + (CH(b.col, k) - (float)CH(a.col, k)) * t) << k; }
        frag(x, y, a.z + (b.z - a.z) * t, col, a.u + (b.u - a.u) * t, a.v + (b.v - a.v) * t);
    }
}
static void draw_tri(const Vtx &a, const Vtx &b, const Vtx &c, bool flat);
/* renderizador GPU: com t_tri_sink definido, os triangulos ja recortados vao para ele em vez de serem rasterizados */
static thread_local void (*t_tri_sink)(const Vtx &, const Vtx &, const Vtx &, bool) = nullptr;
static inline void emit_tri(const Vtx &a, const Vtx &b, const Vtx &c, bool flat) { if (t_tri_sink) t_tri_sink(a, b, c, flat); else draw_tri(a, b, c, flat); }
/* Recorte (Sutherland-Hodgman) no espaco de clip contra o plano w >= EPS (na frente da camera): sem isso, triangulos
 * com vertices atras da camera viram coordenadas absurdas e somem (ex.: fundo da arena do duelo). */
static void draw_tri_clip(const Vtx &a, const Vtx &b, const Vtx &c, bool flat) {
    if (!a.clip) { emit_tri(a, b, c, flat); return; }
    if (a.z > 65535.f && b.z > 65535.f && c.z > 65535.f) return;   /* wholly beyond the far plane: the GE clips it away (the duel win screen has a huge quad at z=1.8e7 that would otherwise paint the whole screen orange) */
    const float EPS = 1e-4f;
    auto dist = [&](const Vtx &v) { return v.cw - EPS; };
    float da = dist(a), db = dist(b), dc = dist(c);
    if (da >= 0 && db >= 0 && dc >= 0) { emit_tri(a, b, c, flat); return; }
    if (da < 0 && db < 0 && dc < 0) return;
    auto lerp = [&](const Vtx &p, const Vtx &q, float t) {
        Vtx r = p;
        r.cx = p.cx + (q.cx - p.cx) * t; r.cy = p.cy + (q.cy - p.cy) * t; r.cz = p.cz + (q.cz - p.cz) * t; r.cw = p.cw + (q.cw - p.cw) * t;
        r.u = p.u + (q.u - p.u) * t; r.v = p.v + (q.v - p.v) * t;
        uint32_t col = 0;
        for (int k = 0; k < 32; k += 8) col |= (uint32_t)(CH(p.col, k) + (CH(q.col, k) - (float)CH(p.col, k)) * t + 0.5f) << k;
        r.col = col; project(r); return r;
    };
    const Vtx *in[3] = {&a, &b, &c}; Vtx out[4]; int n = 0;
    for (int i = 0; i < 3; i++) {
        const Vtx &p = *in[i], &q = *in[(i + 1) % 3]; float dp = dist(p), dq = dist(q);
        if (dp >= 0) out[n++] = p;
        if ((dp >= 0) != (dq >= 0) && n < 4) out[n++] = lerp(p, q, dp / (dp - dq));
    }
    for (int i = 1; i + 1 < n; i++) {
        Vtx t2 = out[i + 1]; if (flat) t2.col = c.col;   /* sombreamento flat usa a cor do ultimo vertice original */
        emit_tri(out[0], out[i], t2, flat);
    }
}
static int g_cull = 0;   /* 0 = sem cull; 1 = descarta area > 0 (horario na tela); 2 = descarta area < 0. Definido por PRIM. */
template <class Sh> static void draw_tri_t(const Vtx &a, const Vtx &b, const Vtx &c, bool flat, const Sh &sh) {
    Scissor sc = scissor();
    float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (fabsf(area) < 1e-6f) return;
    if (g_cull && (area > 0) == (g_cull == 1)) return;   /* face de tras (a carta virada mostra so o verso) */
    int x0 = std::max((int)floorf(std::min({a.x, b.x, c.x})), sc.x1), x1 = std::min((int)ceilf(std::max({a.x, b.x, c.x})), sc.x2);
    int y0 = std::max((int)floorf(std::min({a.y, b.y, c.y})), sc.y1), y1 = std::min((int)ceilf(std::max({a.y, b.y, c.y})), sc.y2);
    if (x0 > x1 || y0 > y1) return;
    /* baricentricas como funcoes lineares de (px, py): w = K + A*px + B*py (ja divididas pela area) */
    float inv = 1.f / area;
    float K0 = (b.x * c.y - b.y * c.x) * inv, A0 = (b.y - c.y) * inv, B0 = (c.x - b.x) * inv;
    float K1 = (c.x * a.y - c.y * a.x) * inv, A1 = (c.y - a.y) * inv, B1 = (a.x - c.x) * inv;
    bool persp = R.tex && !(a.rw == b.rw && b.rw == c.rw);   /* rw igual nos 3 vertices: interpolacao afim ja e a correta */
    float ca[4], cb[4], cc[4];
    for (int k = 0; k < 4; k++) { ca[k] = (float)CH(a.col, k * 8); cb[k] = (float)CH(b.col, k * 8); cc[k] = (float)CH(c.col, k * 8); }
    const bool samecol = a.col == b.col && b.col == c.col;   /* cor constante: sem interpolar (e sem o erro de truncamento 255 -> 254) */
    for (int y = y0; y <= y1; y++) {
        float py = y + 0.5f, px = x0 + 0.5f;
        float w0 = K0 + A0 * px + B0 * py, w1 = K1 + A1 * px + B1 * py;
        for (int x = x0; x <= x1; x++, w0 += A0, w1 += A1) {
            float w2 = 1.f - w0 - w1;
            if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f) continue;
            uint32_t col;
            if (flat || samecol) col = c.col;
            else { col = 0; for (int k = 0; k < 4; k++) col |= (uint32_t)std::max(0.f, std::min(255.f, ca[k] * w0 + cb[k] * w1 + cc[k] * w2)) << (k * 8); }
            float u, v;
            if (persp) {   /* correcao de perspectiva: interpola u/w, v/w e 1/w */
                float q0 = w0 * a.rw, q1 = w1 * b.rw, q2 = w2 * c.rw, iq = 1.f / (q0 + q1 + q2);
                u = (a.u * q0 + b.u * q1 + c.u * q2) * iq; v = (a.v * q0 + b.v * q1 + c.v * q2) * iq;
            } else { u = a.u * w0 + b.u * w1 + c.u * w2; v = a.v * w0 + b.v * w1 + c.v * w2; }
            sh(x, y, a.z * w0 + b.z * w1 + c.z * w2, col, u, v);
        }
    }
}
static void draw_tri(const Vtx &a, const Vtx &b, const Vtx &c, bool flat) { with_shade([&](const auto &sh) { draw_tri_t(a, b, c, flat, sh); }); }
uint64_t g_prims;
struct PrimJob { uint32_t type, count; const Vtx *vv; bool flat, through; };
static void raster_prim_all(const PrimJob &j) {   /* rasteriza a primitiva inteira (dentro da banda da thread, se houver) */
    const Vtx *vv = j.vv; uint32_t count = j.count; bool flat = j.flat;
    switch (j.type) {
    case 6: for (uint32_t i = 0; i + 1 < count; i += 2) draw_sprite(vv[i], vv[i + 1], j.through); break;
    case 3: for (uint32_t i = 0; i + 2 < count; i += 3) draw_tri_clip(vv[i], vv[i + 1], vv[i + 2], flat); break;
    case 4: for (uint32_t i = 0; i + 2 < count; i++) { if (i & 1) draw_tri_clip(vv[i + 1], vv[i], vv[i + 2], flat); else draw_tri_clip(vv[i], vv[i + 1], vv[i + 2], flat); } break;
    case 5: for (uint32_t i = 1; i + 1 < count; i++) draw_tri_clip(vv[0], vv[i], vv[i + 1], flat); break;
    case 0: for (uint32_t i = 0; i < count; i++) draw_line(vv[i], vv[i], true); break;                    /* pontos */
    case 1: for (uint32_t i = 0; i + 1 < count; i += 2) draw_line(vv[i], vv[i + 1], flat); break;         /* linhas */
    case 2: for (uint32_t i = 0; i + 1 < count; i++) draw_line(vv[i], vv[i + 1], flat); break;             /* faixa de linhas */
    default: break;
    }
}
static void raster_band(void *ctx, int y0, int y1) {
    t_band_y0 = y0; t_band_y1 = y1;
    raster_prim_all(*(const PrimJob *)ctx);
    t_band_y0 = INT_MIN; t_band_y1 = INT_MAX;
}
/* Primitivas grandes (area estimada pela soma das caixas dos triangulos) sao divididas em bandas de linhas entre as threads;
 * as pequenas rodam direto (a sincronizacao custaria mais que o desenho). O resultado e identico ao de uma thread so. */
static void raster_prim(const PrimJob &j) {
    if (j.type >= 3 && j.type <= 6) {
        Scissor sc = scissor();
        double area = 0; int ymin = INT_MAX, ymax = INT_MIN;
        auto add = [&](const Vtx &a, const Vtx &b, const Vtx *c) {
            float x0 = std::min(a.x, b.x), x1 = std::max(a.x, b.x), y0 = std::min(a.y, b.y), y1 = std::max(a.y, b.y);
            if (c) { x0 = std::min(x0, c->x); x1 = std::max(x1, c->x); y0 = std::min(y0, c->y); y1 = std::max(y1, c->y); }
            x0 = std::max(x0, (float)sc.x1); x1 = std::min(x1, (float)sc.x2 + 1); y0 = std::max(y0, (float)sc.y1); y1 = std::min(y1, (float)sc.y2 + 1);
            if (x1 <= x0 || y1 <= y0) return;
            area += (double)(x1 - x0) * (y1 - y0); ymin = std::min(ymin, (int)floorf(y0)); ymax = std::max(ymax, (int)ceilf(y1));
        };
        const Vtx *vv = j.vv; uint32_t n = j.count;
        switch (j.type) {
        case 6: for (uint32_t i = 0; i + 1 < n; i += 2) add(vv[i], vv[i + 1], nullptr); break;
        case 3: for (uint32_t i = 0; i + 2 < n; i += 3) add(vv[i], vv[i + 1], &vv[i + 2]); break;
        case 4: for (uint32_t i = 0; i + 2 < n; i++) add(vv[i], vv[i + 1], &vv[i + 2]); break;
        default: for (uint32_t i = 1; i + 1 < n; i++) add(vv[0], vv[i], &vv[i + 1]); break;
        }
        if (R.tc && !R.tc->full) { R.tc->acc += area; if (R.tc->acc >= (double)R.tw * R.th) texcache_fill(R.tc); }
        if (raster_threads() > 1 && area >= 6000 && ymax - ymin >= 8) { raster_parallel(ymin, ymax, raster_band, (void *)&j); return; }
    }
    raster_prim_all(j);
}
/* ------------------------------------------------------------------ resolucao interna (quadro sombra)
 * O jogo le e copia o quadro nativo (480x272 na VRAM), entao ele continua sendo desenhado normalmente. Para os buffers que
 * o jogo exibe (sceDisplaySetFrameBuf), cada primitiva e rasterizada uma segunda vez, com as coordenadas multiplicadas por
 * g_ge_res, num quadro "sombra" 8888 maior (cor + profundidade). Na hora de apresentar, ge_shadow_get compara a sombra com
 * o quadro nativo: se elas nao combinam (algo escreveu no quadro sem passar pelo GE: filme, copia da CPU/DMA...), a sombra
 * e refeita a partir do nativo e este quadro sai em resolucao nativa. */
int g_ge_res = 1;
static bool g_gpu_on;   /* renderizador por GPU ligado (ver mais abaixo) */
static void gpu_reset_fbs(void);
void ge_set_msaa(void) { gpu_reset_fbs(); }
static void gpu_tex_clear(void);
void ge_set_texscale(void) { texrepl_upscale_reset(); gpu_tex_clear(); }   /* a ampliacao das texturas mudou: o cache de texturas da GPU e refeito */   /* MSAA mudou: os quadros sombra da GPU sao recriados (materializa o nativo antes) */
struct Shadow { uint32_t addr = 0, stride = 0, zaddr = 0, zstride = 0; int res = 0; bool stale = false; std::vector<uint32_t> col; std::vector<uint16_t> z; };
static Shadow g_sh[4]; static int g_sh_next;
static std::vector<uint32_t> g_disp_addrs;
void ge_shadow_add_display(uint32_t addr) { addr = 0x04000000u | (addr & 0x1ffff0u); for (uint32_t a : g_disp_addrs) if (a == addr) return; g_disp_addrs.push_back(addr); }
static int sw_res(void) { return std::min(g_ge_res, 4); }   /* o raster por software so vai ate 4x (a GPU ate 8x) */
void ge_set_res(int n) { n = std::max(1, std::min(8, n)); if (n == g_ge_res) return; g_ge_res = n; for (auto &s : g_sh) { s = Shadow(); } gpu_reset_fbs(); if (n > 1) fprintf(stderr, "[res] resolucao interna %dx\n", n); }
static void shadow_resync(Shadow &s) {   /* refaz a sombra (cor e profundidade) a partir do quadro nativo, por replicacao */
    int N = s.res; uint32_t cs = s.stride * N, zs = s.zstride * N;
    s.col.assign((size_t)cs * 272 * N, 0); s.z.assign((size_t)zs * 272 * N, 0);
    const uint32_t *fb = (const uint32_t *)mem_ptr(s.addr); const uint16_t *zb = (const uint16_t *)mem_ptr(s.zaddr);
    for (int y = 0; y < 272 * N; y++) {
        const uint32_t *r = fb + (size_t)(y / N) * s.stride; const uint16_t *zr = zb + (size_t)(y / N) * s.zstride;
        uint32_t *o = &s.col[(size_t)y * cs]; uint16_t *zo = &s.z[(size_t)y * zs];
        for (int x = 0; x < 480 * N; x++) { o[x] = r[x / N]; zo[x] = zr[x / N]; }
    }
}
static Shadow *shadow_for(uint32_t addr, uint32_t stride, uint32_t zaddr, uint32_t zstride) {
    bool known = false; for (uint32_t a : g_disp_addrs) if (a == addr) known = true;
    if (!known) return nullptr;
    for (auto &s : g_sh) if (s.res == sw_res() && s.addr == addr && s.stride == stride && s.zstride == zstride) { s.zaddr = zaddr; return &s; }
    Shadow &s = g_sh[g_sh_next++ & 3];
    s = Shadow(); s.addr = addr; s.stride = stride; s.zaddr = zaddr; s.zstride = zstride; s.res = sw_res();
    shadow_resync(s);
    return &s;
}
/* Resolucao dinamica: se a segunda passada (a sombra) ja gastou mais que o orcamento neste quadro, as primitivas restantes nao
 * entram na sombra; ela fica marcada como incompleta e o quadro sai em resolucao nativa (a sombra e refeita a partir dele).
 * Assim efeitos pesados (invocacoes, cut-ins) nunca passam do custo nativo + orcamento. TF_RES_BUDGET_MS (padrao 15; 0 = sem limite). */
static double g_sh_us;   /* tempo gasto na sombra desde a ultima apresentacao */
static double qpc_us() { LARGE_INTEGER c, f; QueryPerformanceCounter(&c); QueryPerformanceFrequency(&f); return (double)c.QuadPart * 1e6 / (double)f.QuadPart; }
static const double g_budget_max_us = (getenv("TF_RES_BUDGET_MS") ? atof(getenv("TF_RES_BUDGET_MS")) : 15.0) * 1000.0;
static double g_budget_us = g_budget_max_us;   /* orcamento atual: o de TF_RES_BUDGET_MS e o TETO; ge_res_feedback o reduz se o quadro estoura */
/* Orcamento adaptativo: display.cpp informa o tempo de trabalho de cada quadro (sem o Sleep do limitador). O custo nativo, a
 * apresentacao e o resto nao entram no orcamento fixo, entao ele e ajustado para o quadro total caber em ~15,5 ms (folga p/ 16,68). */
void ge_res_feedback(double work_ms) {
    if (g_gpu_on) return;   /* o orcamento e da sombra por software */
    static double avg = 8;   /* media movel do trabalho (~10 quadros): um quadro pesado isolado nao derruba o orcamento */
    if (g_budget_max_us <= 0 || sw_res() <= 1 || work_ms > 60) return;   /* sem limite / sem sombra / travada isolada (filme, carga) */
    avg += 0.1 * (work_ms - avg);
    double d = 0.25 * (15.5 - avg);
    g_budget_us += std::max(-1000.0, std::min(300.0, d * 1000.0));
    g_budget_us = std::max(2000.0, std::min(g_budget_max_us, g_budget_us));
}
static void shadow_pass(const PrimJob &j) {
    Shadow *s = shadow_for(fb_base(), R.fstride, z_base(), R.zstride); if (!s) return;
    const double budget_us = g_budget_max_us > 0 ? g_budget_us : 0;
    if (budget_us > 0 && g_sh_us > budget_us) { s->stale = true; return; }
    double t0 = budget_us > 0 ? qpc_us() : 0;
    int N = s->res;
    std::vector<Vtx> sv(j.vv, j.vv + j.count);
    for (auto &v : sv) { v.x *= N; v.y *= N; }
    uint8_t *ofb = R.fb; uint32_t ofs = R.fstride, ofl = R.flimit, ozs = R.zstride, ozl = R.zlimit; uint16_t *ozb = R.zb;
    R.fb = (uint8_t *)s->col.data(); R.fstride = s->stride * N; R.flimit = (uint32_t)(s->col.size() * 4);
    R.zb = s->z.data(); R.zstride = s->zstride * N; R.zlimit = (uint32_t)(s->z.size() * 2);
    g_ss = N;
    PrimJob j2 = j; j2.vv = sv.data();
    raster_prim(j2);
    g_ss = 1;
    R.fb = ofb; R.fstride = ofs; R.flimit = ofl; R.zb = ozb; R.zstride = ozs; R.zlimit = ozl;
    if (budget_us > 0) g_sh_us += qpc_us() - t0;
}
/* devolve a sombra do buffer exibido (cor 8888 ABGR, com o passo de linha em pixels) se ela combina com o quadro nativo */
const uint32_t *ge_shadow_get(uint32_t addr, uint32_t fmt, int *w, int *h, uint32_t *stride) {
    g_sh_us = 0;   /* novo quadro: zera o orcamento da sombra */
    if (sw_res() <= 1 || fmt != 3) return nullptr;
    addr = 0x04000000u | (addr & 0x1ffff0u);
    Shadow *s = nullptr; for (auto &c : g_sh) if (c.res == sw_res() && c.addr == addr && !c.col.empty()) s = &c;
    if (!s) return nullptr;
    if (s->stale) { s->stale = false; shadow_resync(*s); static const bool dbg2 = getenv("TF_RES_DEBUG") != nullptr; if (dbg2) fprintf(stderr, "[res] orcamento estourado: quadro nativo\n"); return nullptr; }
    int N = s->res; const uint32_t *fb = (const uint32_t *)mem_ptr(addr); int bad = 0, tot = 0;
    for (int y = 0; y < 272; y += 2) for (int x = 0; x < 480; x += 2) {
        int sum[3] = {0, 0, 0};
        for (int j = 0; j < N; j++) for (int i = 0; i < N; i++) { uint32_t c = s->col[(size_t)(y * N + j) * s->stride * N + x * N + i]; sum[0] += c & 255; sum[1] += (c >> 8) & 255; sum[2] += (c >> 16) & 255; }
        uint32_t n = fb[(size_t)y * s->stride + x]; int d = abs(sum[0] / (N * N) - (int)(n & 255)) + abs(sum[1] / (N * N) - (int)((n >> 8) & 255)) + abs(sum[2] / (N * N) - (int)((n >> 16) & 255));
        tot++; if (d > 96) bad++;
    }
    if (bad * 100 > tot * 6) { static const bool dbg = getenv("TF_RES_DEBUG") != nullptr; if (dbg) fprintf(stderr, "[res] sombra fora de sincronia (%d de %d): quadro nativo\n", bad, tot); shadow_resync(*s); return nullptr; }
    *w = 480 * N; *h = 272 * N; *stride = s->stride * N;
    return s->col.data();
}

/* ------------------------------------------------------------------ renderizador por GPU (Direct3D 11, gpu_d3d11.cpp)
 * Mesma ideia do quadro sombra por software, mas as primitivas dos buffers exibidos sao refeitas na GPU em g_ge_res x.
 * O rasterizador por software continua desenhando o quadro nativo (o jogo o le de volta); a GPU so precisa do estado, dos
 * vertices ja projetados/recortados e das texturas decodificadas (cache na GPU por endereco + formato + hash do conteudo).
 * Estados que a GPU nao reproduz (stencil, mascara de escrita, pontos/linhas, blends raros) marcam o quadro sombra como
 * "stale": o resto do quadro nao entra nele e na apresentacao ele e refeito a partir do nativo. */
/* Modo "sem raster nativo" (TF_GPU_SKIPNATIVE=0 desliga): o rasterizador por software custava ~75% do trabalho de CPU por quadro e so serve
 * para o jogo ler a VRAM de volta, o que quase nunca acontece nos buffers exibidos. Depois de alguns quadros sem nenhum acesso (e sem
 * divergencia), as paginas da VRAM do buffer viram PAGE_NOACCESS e as primitivas dele so vao para a GPU ("prot"). Qualquer acesso
 * (leitura ou escrita: jogo, DMA, transferencia do GE, textura renderizada...) cai no tratador de excecao, que reconstroi o quadro
 * nativo a partir da GPU (media N x N), libera as paginas e volta ao modo hibrido (nativo + GPU) por alguns quadros; se foi uma
 * escrita, a GPU e refeita a partir do nativo depois dela (needResync). */
struct GFbRec { bool used; uint32_t addr, stride, zaddr, zstride; int res, id; bool stale; int ticket; bool ckp; bool prot, needResync, nativeFresh; uint64_t lastFault, lastBad; int nw, nh; bool isRtt; uint64_t lastUse; bool stOk; int stw, sth; uint32_t nat[240 * 136]; };   /* stOk: o buffer de stencil da GPU e igual ao alfa do PSP na regiao [0,stw)x[0,sth) (ver gpu_shadow_pass_impl) */   /* nat: nativo amostrado (1 de cada 2x2) quando a comparacao comecou */
static GFbRec g_gf[4]; static unsigned g_gf_next;
static GFbRec g_rt[12];   /* alvos de render-to-texture na GPU (o nativo dos alvos e SEMPRE mantido: a imagem da GPU e so uma copia melhor, refeita do nativo se houver duvida) */
static const bool g_skip_ok = !(getenv("TF_GPU_SKIPNATIVE") && atoi(getenv("TF_GPU_SKIPNATIVE")) == 0);
static uint64_t g_gpu_faults, g_gpu_skipframes, g_gpu_frames;
static uint32_t g_small[480 * 272]; static bool g_small_valid;   /* ultima reducao 480x272 (ABGR) recebida da GPU: deteccao da tela de carregamento */
static size_t prot_bytes(const GFbRec &f) { return ((size_t)f.stride * 272 * 4 + 4095) & ~(size_t)4095; }
static void gpu_materialize(GFbRec &f, bool write = true);
static const bool g_gpu_dbg = getenv("TF_GPU_DEBUG") != nullptr;
static const char *const g_gpu_why[] = {"", "pontos/linhas", "stencil", "mascara de escrita", "blend abs", "fator 2*dst", "fator 2*src com outro alfa", "duas cores fixas", "textura", "textura grande"};
static uint64_t g_gpu_stat[16], g_gpu_prims, g_gpu_verts;
void interp_reset(void); void interp_taint(int); void interp_record(const GpuState &, const GpuVtx *, int, bool, int); bool interp_enabled(void); void interp_fb_drawn(int); bool interp_hold_ok(void);
static void gpu_reset_fbs(void) { interp_reset(); hdfont_reset(); for (auto &f : g_gf) { if (f.used) { gpu_materialize(f); gpu_fb_destroy(f.id); } f = GFbRec(); } for (auto &f : g_rt) { if (f.used) gpu_fb_destroy(f.id); f = GFbRec(); } }
static void gpu_fb_resync(GFbRec &f) { interp_taint(f.id); gpu_fb_upload(f.id, (const uint32_t *)mem_ptr(f.addr), f.stride, (const uint16_t *)mem_ptr(f.zaddr), f.zstride); f.stale = false; f.stOk = false; }   /* refazer zera o stencil da GPU */
static GFbRec *gpu_fb_for(uint32_t addr, uint32_t stride, uint32_t zaddr, uint32_t zstride) {
    bool known = false; for (uint32_t a : g_disp_addrs) if (a == addr) known = true;
    if (!known) return nullptr;
    for (auto &f : g_gf) if (f.used && f.res == g_ge_res && f.addr == addr && f.stride == stride && f.zstride == zstride) { f.zaddr = zaddr; return &f; }
    GFbRec &f = g_gf[g_gf_next++ & 3];
    if (f.used) { gpu_materialize(f); gpu_fb_destroy(f.id); }
    f = GFbRec(); f.addr = addr; f.stride = stride; f.zaddr = zaddr; f.zstride = zstride; f.res = g_ge_res; f.nw = 480; f.nh = 272; f.id = gpu_fb_create(480, 272, g_ge_res); f.used = f.id >= 0;
    if (!f.used) return nullptr;
    gpu_fb_resync(f);
    return &f;
}
/* Render-to-texture na GPU: alvos de desenho que nao sao buffers exibidos (ex.: o painel de informacao da carta no duelo, montado numa
 * textura interna e depois desenhado na tela). As primitivas deles tambem vao para a GPU (com as texturas substitutas, em N x) e, quando
 * uma primitiva usa esse alvo como textura, a imagem da GPU e amostrada direto. O nativo continua sendo desenhado (a GPU nao o substitui). */
static int rtt_res(int nw, int nh) { int r = g_ge_res; while (r > 1 && (long long)nw * nh * r * r > 16000000ll) r--; return r; }
static GFbRec *gpu_rtt_for(uint32_t addr, uint32_t stride, uint32_t zaddr, uint32_t zstride) {
    extern uint64_t display_frames(void);
    if (addr < 0x04000000u || addr >= 0x04200000u || stride < 16 || stride > 512 || zaddr < 0x04000000u || zaddr >= 0x04200000u || zstride < stride || zstride > 512) return nullptr;
    uint32_t rows = std::min<uint32_t>(512, (0x04200000u - addr) / (stride * 4));
    rows = std::min<uint32_t>(rows, (0x04200000u - zaddr) / (zstride * 2));
    if (rows < 16) return nullptr;
    int nw = (int)stride, nh = (int)rows, res = rtt_res(nw, nh); uint64_t fr = display_frames();
    for (auto &f : g_rt) if (f.used && f.addr == addr && f.stride == stride && f.res == res && f.zstride == zstride) { f.zaddr = zaddr; f.lastUse = fr; return &f; }
    GFbRec *slot = nullptr; for (auto &f : g_rt) if (!f.used) { slot = &f; break; }
    if (!slot) { slot = &g_rt[0]; for (auto &f : g_rt) if (f.lastUse < slot->lastUse) slot = &f; gpu_fb_destroy(slot->id); *slot = GFbRec(); }
    GFbRec &f = *slot; f = GFbRec(); f.addr = addr; f.stride = stride; f.zaddr = zaddr; f.zstride = zstride; f.res = res; f.nw = nw; f.nh = nh; f.isRtt = true; f.lastUse = fr;
    f.id = gpu_fb_create(nw, nh, res); f.used = f.id >= 0;
    if (!f.used) return nullptr;
    gpu_fb_resync(f);
    return &f;
}
/* a textura do estado atual (R) e a imagem de algum quadro sombra (alvo de textura ou buffer exibido)? devolve o handle GPU_FBTEX|id */
static uint32_t gpu_fbtex_lookup(GFbRec *dst, uint32_t *tw, uint32_t *th) {
    extern uint64_t display_frames(void);
    if (R.tpsm != 3 || R.swz || R.taddr < 0x04000000u || R.taddr >= 0x04200000u) return 0;
    auto pick = [&](GFbRec &s) -> uint32_t {
        /* altura: a textura declarada pode passar do fim da VRAM (carta grande da loja: 128x256 em 041e8000, so 192 linhas
         * existem); o alvo da GPU vai ate o fim da VRAM, entao cobre tudo o que existe (antes caia na copia nativa 1x, sem HD) */
        bool tall = (uint32_t)s.nh >= R.th || s.addr + (uint32_t)s.nh * s.stride * 4 >= 0x04200000u;
        if (!s.used || &s == dst || s.addr != R.taddr || s.stride != R.tbw || !tall) return 0;
        if ((s.stale || s.needResync) && !s.prot) { gpu_fb_resync(s); s.needResync = false; }   /* o nativo e valido: a GPU parte dele */
        s.lastUse = display_frames(); *tw = (uint32_t)s.nw; *th = (uint32_t)s.nh;
        return (uint32_t)GPU_FBTEX | (uint32_t)s.id;
    };
    for (auto &s : g_rt) if (uint32_t h = pick(s)) return h;
    for (auto &s : g_gf) if (uint32_t h = pick(s)) return h;
    return 0;
}
/* algo escreveu na VRAM fora das primitivas (transferencia do GE, DMA): os quadros sombra que cobrem a regiao sao refeitos do nativo */
void ge_vram_written(uint32_t addr, uint32_t size) {
    if (!g_gpu_on || !size) return;
    addr &= 0x3fffffffu; if (addr >= 0x04200000u || addr + size <= 0x04000000u) return;
    auto hit = [&](GFbRec &f) { if (f.used && addr < f.addr + f.stride * (uint32_t)f.nh * 4 && addr + size > f.addr) f.needResync = true; };
    for (auto &f : g_rt) hit(f);
    for (auto &f : g_gf) if (f.nh == 0) f.nh = 272;
    for (auto &f : g_gf) hit(f);
}
/* reconstroi o quadro nativo (cor) a partir da GPU e libera as paginas da VRAM */
static void gpu_materialize(GFbRec &f, bool write) {
    extern uint64_t display_frames(void);
    if (!f.prot) return;
    double t0 = gpu_now_us();
    DWORD old; VirtualProtect(mem_ptr(f.addr), prot_bytes(f), PAGE_READWRITE, &old);   /* antes de escrever, senao a escrita cairia aqui de novo */
    f.prot = false; f.ckp = false;
    /* so escrita/estado sem suporte mantem o buffer no modo hibrido por alguns quadros; uma leitura (ex.: o jogo copia o quadro com uma
     * transferencia do GE) deixa o nativo valido ("fresh") so ate a proxima primitiva, que reprotege as paginas */
    if (write) f.lastFault = display_frames(); else f.nativeFresh = true;
    static std::vector<uint32_t> small(480 * 272);   /* reducao N x N feita na GPU: so 480x272 sobe para a CPU */
    int t = gpu_check_start(f.id);
    if (t >= 0 && gpu_check_get(t, small.data())) {
        uint32_t *dst = (uint32_t *)mem_ptr(f.addr);
        for (int y = 0; y < 272; y++) memcpy(dst + (size_t)y * f.stride, &small[(size_t)y * 480], 480 * 4);
    }
    g_gpu_us[5] += gpu_now_us() - t0;
}
static LONG CALLBACK vram_fault(EXCEPTION_POINTERS *ep) {
    static bool busy;
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || !g_gpu_on || busy) return EXCEPTION_CONTINUE_SEARCH;
    const uint8_t *fa = (const uint8_t *)ep->ExceptionRecord->ExceptionInformation[1];
    for (auto &f : g_gf) {
        if (!f.used || !f.prot) continue;
        const uint8_t *b = mem_ptr(f.addr);
        if (fa < b || fa >= b + prot_bytes(f)) continue;
        busy = true;
        bool write = ep->ExceptionRecord->ExceptionInformation[0] == 1;
        if (g_gpu_dbg && g_gpu_faults < 400) {   /* quem acessou: RIP nativo (HLE) ou a ultima funcao recompilada do jogo */
            extern uint64_t display_frames(void);
            fprintf(stderr, "[gpu] quadro %llu: acesso %s a VRAM protegida (+0x%llx) rip=exe+0x%llx, ultimas funcoes do jogo %08x %08x: quadro nativo reconstruido da GPU\n", (unsigned long long)display_frames(),
                    write ? "de escrita" : "de leitura", (unsigned long long)(fa - b), (unsigned long long)(ep->ContextRecord->Rip - (DWORD64)GetModuleHandleA(nullptr)), g_trace[(g_trace_i - 1) & 1023], g_trace[(g_trace_i - 2) & 1023]);
            { HMODULE hm = nullptr; char mn[MAX_PATH] = "?"; if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)ep->ContextRecord->Rip, &hm)) GetModuleFileNameA(hm, mn, sizeof mn);
              fprintf(stderr, "[gpu]   modulo do rip: %s\n", mn); }
            fprintf(stderr, "[gpu]   GE: fb=%08x tex=%08x tpsm=%u tsize=%06x cmd(prim)=%06x zbuf=%08x\n", fb_base(), tex_addr(), CMD(0xc3) & 15, CMD(0xb8), CMD(0x04), z_base());
        }
        gpu_materialize(f, write); if (write) f.needResync = true; g_gpu_faults++;
        busy = false;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
/* texturas: cache na GPU */
struct GTKey { uint32_t addr, psm, w, h, bw, swz, pal; bool operator<(const GTKey &o) const { return memcmp(this, &o, sizeof *this) < 0; } };
struct GTEnt { uint32_t id; uint64_t hash, stamp, used; uint32_t dh = 0; bool awaiting = false; int akind = 0; uint64_t bytes = 0, retry = 0; uint32_t tcol = 0xffffffffu; uint64_t asked = 0; };   /* asked: quadro em que a substituta foi pedida */   /* tcol: cor media da original (escolha da variante por paleta) */   /* dh: hash dos dados (texrepl); awaiting: substituta/ampliacao em curso (akind 0 = PNG, 1 = Scale2x) */
static std::map<GTKey, GTEnt> g_gtex;
static uint64_t hash_bytes(const uint8_t *p, size_t n) {   /* so comparado em memoria (carimbo do conteudo): 4 faixas independentes escondem a latencia da multiplicacao */
    uint64_t h = 0x9E3779B97F4A7C15ull; size_t i = 0;
    if (n >= 32) {
        uint64_t a = h, b = h + 1, c = h + 2, d = h + 3;
        for (; i + 32 <= n; i += 32) {
            uint64_t w0, w1, w2, w3; memcpy(&w0, p + i, 8); memcpy(&w1, p + i + 8, 8); memcpy(&w2, p + i + 16, 8); memcpy(&w3, p + i + 24, 8);
            a = (a ^ w0) * 0xff51afd7ed558ccdull; a ^= a >> 32; b = (b ^ w1) * 0xff51afd7ed558ccdull; b ^= b >> 32;
            c = (c ^ w2) * 0xff51afd7ed558ccdull; c ^= c >> 32; d = (d ^ w3) * 0xff51afd7ed558ccdull; d ^= d >> 32;
        }
        h = (a ^ (b << 1) ^ (c << 2) ^ (d << 3)) * 0xc4ceb9fe1a85ec53ull;
    }
    for (; i + 8 <= n; i += 8) { uint64_t w; memcpy(&w, p + i, 8); h = (h ^ w) * 0xff51afd7ed558ccdull; h ^= h >> 32; }
    for (; i < n; i++) h = (h ^ p[i]) * 0x100000001b3ull;
    return h;
}
static uint64_t g_tex_bytes, g_tex_fail;   /* memoria estimada das texturas na GPU (cache) e falhas de criacao */
/* GPU texture budget: 1536 MB, but at most 40% of the video memory (the internal-resolution framebuffers need the rest), never below 384 MB */
static uint64_t tex_budget_default(void) {
    extern int gfx_vram_mb(bool *); const int mb = gfx_vram_mb(nullptr);
    return mb > 0 ? (uint64_t)std::max(384, std::min(1536, mb * 4 / 10)) : 1536;
}
static const uint64_t g_tex_budget = (uint64_t)(getenv("TF_TEX_BUDGET_MB") ? atoll(getenv("TF_TEX_BUDGET_MB")) : (long long)tex_budget_default()) << 20;
/* TEXTURAS HD COMPARTILHADAS (2026-10-15): a substituta de um hash (+ variante de cor, + ampliada ou nao) vira UMA textura na GPU, usada por
 * todas as entradas do cache que a pedem (o jogo recarrega a mesma arte em outro endereco ao voltar a uma tela). Antes cada entrada criava a sua
 * e a entrada parada 5 s saia da GPU: ao voltar a uma tela as HD subiam de novo (~11 ms de GPU por 16 MB, o intermediario do 60 FPS saia
 * atrasado: tranco a cada visita, sessao 205658). Agora a HD sem uso fica na GPU ate o orcamento (TF_TEX_BUDGET_MB) encher; sai a menos recente. */
struct HdTex { uint32_t id; uint64_t bytes, last; int refs; };
static std::unordered_map<uint64_t, HdTex> g_hd; static std::unordered_map<uint32_t, uint64_t> g_hd_of;   /* chave -> textura; id da GPU -> chave */
static uint64_t hd_key(uint32_t dh, uint32_t tcol, int akind) { return ((uint64_t)tcol << 32 | dh) ^ (akind ? 0x8000000000000000ull : 0); }
static void tex_drop(uint32_t id, uint64_t bytes) {   /* uma entrada do cache deixa de usar a textura id */
    extern uint64_t display_frames(void);
    auto it = g_hd_of.find(id);
    if (it == g_hd_of.end()) { gpu_tex_release(id); g_tex_bytes -= std::min(g_tex_bytes, bytes); return; }
    HdTex &t = g_hd[it->second]; if (t.refs > 0) t.refs--; t.last = display_frames();
}
static void hd_free_unused(uint64_t target) {   /* HD sem nenhuma entrada usando, das menos recentes, ate g_tex_bytes <= target */
    if (g_tex_bytes <= target) return;
    std::vector<std::pair<uint64_t, uint64_t>> v; for (auto &kv : g_hd) if (kv.second.refs <= 0) v.push_back({kv.second.last, kv.first});
    std::sort(v.begin(), v.end());
    for (auto &x : v) {
        if (g_tex_bytes <= target) break;
        HdTex &t = g_hd[x.second]; gpu_tex_release(t.id); g_tex_bytes -= std::min(g_tex_bytes, t.bytes); g_hd_of.erase(t.id); g_hd.erase(x.second);
    }
}
static void gpu_tex_clear(void) {
    for (auto &kv : g_gtex) if (!g_hd_of.count(kv.second.id)) gpu_tex_release(kv.second.id);
    for (auto &kv : g_hd) gpu_tex_release(kv.second.id);
    g_gtex.clear(); g_hd.clear(); g_hd_of.clear(); g_tex_bytes = 0;
}
/* libera as texturas menos usadas (menos recentes) ate g_tex_bytes <= target; nao mexe nas usadas neste quadro */
static void gpu_tex_evict_to(uint64_t target) {
    extern uint64_t display_frames(void);
    hd_free_unused(target);
    if (g_tex_bytes <= target) return;
    uint64_t fr = display_frames();
    std::vector<std::map<GTKey, GTEnt>::iterator> v;
    for (auto it = g_gtex.begin(); it != g_gtex.end(); ++it) if (it->second.used < fr) v.push_back(it);
    std::sort(v.begin(), v.end(), [](const std::map<GTKey, GTEnt>::iterator &a, const std::map<GTKey, GTEnt>::iterator &b) { return a->second.used < b->second.used; });
    for (auto it : v) {
        if (g_tex_bytes <= target) break;
        tex_drop(it->second.id, it->second.bytes); g_gtex.erase(it);
        hd_free_unused(target);
    }
}
/* F8 ("tem algo errado na tela"): texturas usadas nos 3 ultimos quadros (hash do dado = nome do PNG no pacote de texturas; tamanho; formato; estado da substituicao) */
void ge_marca_texturas(int n) {
    extern uint64_t display_frames(void); const uint64_t fr = display_frames(); int tot = 0;
    for (auto &kv : g_gtex) if (kv.second.used + 2 >= fr) tot++;
    int q = 0, rdy = 0, fail = 0; uint64_t ld = 0, fl = 0, dp = 0; texrepl_stats(&q, &rdy, &ld, &fl, &dp);
    fprintf(stderr, "[marca-graf] #%d texturas usadas nos 3 ultimos quadros: %d (cache da GPU: %zu texturas, %llu MB; falhas de criacao %llu; PNGs na fila %d, lidos %llu, falhos %llu)%c", n, tot, g_gtex.size(), (unsigned long long)(g_tex_bytes >> 20), (unsigned long long)g_tex_fail, q, (unsigned long long)ld, (unsigned long long)fl, 10); (void)rdy; (void)fail;
    int c = 0;
    for (auto &kv : g_gtex) {
        if (kv.second.used + 2 < fr || c >= 70) continue; const GTKey &k = kv.first; const GTEnt &e = kv.second; c++;
        fprintf(stderr, "[marca-graf] #%d   tex hash=%08x %ux%u psm=%u endereco=%08x %s%c", n, e.dh, k.w, k.h, k.psm, k.addr, e.awaiting ? "SUBSTITUTA CARREGANDO (mostrando a original)" : e.akind == 1 ? "ampliada (Scale2x)" : e.dh ? "com hash (pacote de texturas pode substituir)" : "sem hash (textura de GPU/render-to-texture ou sem pacote)", 10);
    }
}
/* custo do quadro (o display.cpp registra os quadros pesados): texturas criadas na GPU (quantas, MB, ms) e espera pelos PNGs substitutos */
static int g_fc_tex_n; static double g_fc_tex_mb, g_fc_tex_ms, g_fc_wait_ms;
void ge_frame_cost(int *n, double *mb, double *ms, double *wait_ms, double *alloc_ms, double *copy_ms) {
    static double a0, c0; *alloc_ms = std::max(0.0, g_gpu_us[7] - a0) / 1000.0; *copy_ms = std::max(0.0, g_gpu_us[6] - c0) / 1000.0; a0 = g_gpu_us[7]; c0 = g_gpu_us[6];
    *n = g_fc_tex_n; *mb = g_fc_tex_mb; *ms = g_fc_tex_ms; *wait_ms = g_fc_wait_ms; g_fc_tex_n = 0; g_fc_tex_mb = g_fc_tex_ms = g_fc_wait_ms = 0; }
/* cria a textura na GPU; se faltar memoria/descritores libera as menos usadas e tenta de novo (antes: falhava em silencio e ficava a original) */
static uint32_t gpu_tex_create_ev(int w, int h, const uint32_t *px, bool mips, uint64_t *bytes, const uint32_t *chain) {
    struct Cost { double t0; int w, h; ~Cost() { g_fc_tex_n++; g_fc_tex_mb += (double)w * h * 4 / 1048576.0; g_fc_tex_ms += (gpu_now_us() - t0) / 1000.0; } } cost{gpu_now_us(), w, h};
    uint64_t b = (uint64_t)w * h * 4 * (mips ? 4 : 3) / 3; *bytes = b;
    if (g_tex_bytes + b > g_tex_budget) gpu_tex_evict_to(b > g_tex_budget ? 0 : g_tex_budget - b);
    uint32_t id = gpu_tex_create(w, h, px, mips, chain);
    if (!id) { gpu_tex_evict_to(g_tex_bytes / 2); id = gpu_tex_create(w, h, px, mips, chain); }
    if (!id) { g_tex_fail++; if (g_tex_fail <= 10) fprintf(stderr, "[gpu] nao consegui criar a textura %dx%d (cache: %llu MB em %zu texturas)\n", w, h, (unsigned long long)(g_tex_bytes >> 20), g_gtex.size()); return 0; }
    g_tex_bytes += b; return id;
}
/* textura substituta na GPU: BC7 pronta do cache (bc7.h) ou RGBA com os mipmaps; *bytes = memoria de video estimada */
static uint32_t hd_create(const TexReplImg &ri, uint64_t *bytes) {
    if (ri.bc.empty()) return gpu_tex_create_ev(ri.w, ri.h, ri.px.data(), true, bytes, ri.mips.empty() ? nullptr : ri.mips.data());
    struct Cost { double t0; size_t n; ~Cost() { g_fc_tex_n++; g_fc_tex_mb += (double)n / 1048576.0; g_fc_tex_ms += (gpu_now_us() - t0) / 1000.0; } } cost{gpu_now_us(), ri.bc.size()};
    const uint64_t b = ri.bc.size(); *bytes = b;
    if (g_tex_bytes + b > g_tex_budget) gpu_tex_evict_to(b > g_tex_budget ? 0 : g_tex_budget - b);
    uint32_t id = gpu_tex_create_bc7(ri.w, ri.h, ri.levels, ri.bc.data());
    if (!id) { gpu_tex_evict_to(g_tex_bytes / 2); id = gpu_tex_create_bc7(ri.w, ri.h, ri.levels, ri.bc.data()); }
    if (!id) { texrepl_no_bc(); g_tex_fail++; return 0; }   /* sem suporte a BC7: as proximas leituras voltam ao PNG */
    g_tex_bytes += b; return id;
}
/* substituta carregando em segundo plano: quando chega, troca a textura da GPU (o original serve enquanto isso) */
static uint64_t g_rw_n, g_rw_sum, g_rw_max, g_rw_late;   /* espera (quadros) entre o pedido e a chegada das substitutas: quantas, soma, maior, > 10 quadros */
static void repl_wait_note(const GTEnt &e, uint64_t fr) { if (e.akind != 0 || !e.asked) return; uint64_t w = fr - e.asked; g_rw_n++; g_rw_sum += w; g_rw_max = std::max(g_rw_max, w); if (w > 10) g_rw_late++; }
/* desenho num alvo de textura (render-to-texture): a imagem composta fica la e o jogo a reaproveita por varios quadros sem
 * redesenhar (carta revelada na abertura de pacotes, carta grande da lista da loja), entao a substituta que chegasse depois
 * nunca apareceria. Nesse caso espera a leitura do PNG (ate 200 ms por quadro no total; normalmente poucos ms). */
static int repl_get(uint32_t dh, int ow, int oh, std::shared_ptr<TexReplImg> &ri, uint32_t tcol, bool wait, bool first = false) {
    int st = texrepl_get(dh, CMD(0xb8) & 0xf0f, ow, oh, ri, tcol);
    /* primeira vez que a textura aparece (nao esta no cache em RAM): segura o quadro ate o PNG ficar pronto, em vez de mostrar a original
     * de baixa resolucao e trocar depois (pop-in). Orcamento por quadro: TF_TEX_WAIT_MS (padrao 80; 0 = nunca segura). */
    static const double first_us = (getenv("TF_TEX_WAIT_MS") ? atof(getenv("TF_TEX_WAIT_MS")) : 80.0) * 1000.0;
    /* the wait also stops the game thread's audio: never hold the frame longer than the queued audio can cover (minus a margin), or it clicks */
    extern double audio_out_slack_ms(void);
    const double aud_us = std::max(0.0, audio_out_slack_ms() - 40.0) * 1000.0;
    const double limit = wait ? 200000.0 : first ? std::min(first_us, aud_us) : 0.0;
    if (st != 1 || limit <= 0.0) return st;
    extern uint64_t display_frames(void);
    static uint64_t frame = ~0ull; static double spent; if (display_frames() != frame) { frame = display_frames(); spent = 0; }
    const double t0 = gpu_now_us();
    while (st == 1 && spent + (gpu_now_us() - t0) < limit) { Sleep(1); st = texrepl_get(dh, CMD(0xb8) & 0xf0f, ow, oh, ri, tcol); }
    spent += gpu_now_us() - t0; g_fc_wait_ms += (gpu_now_us() - t0) / 1000.0;
    { const double w = (gpu_now_us() - t0) / 1000.0; static int told; if (w > 15.0 && told < 200) { told++; fprintf(stderr, "[texturas] espera lenta %.0f ms pela substituta %08x (%s, limite do quadro %.0f ms)%c", w, dh, st == 1 ? "ainda carregando: fila de leitura" : "chegou", limit / 1000.0, 10); } }
    return st;
}
static uint32_t gpu_tex_create_ev(int w, int h, const uint32_t *px, bool mips, uint64_t *bytes, const uint32_t *chain = nullptr);
static uint32_t hd_create(const TexReplImg &ri, uint64_t *bytes);
static uint32_t hd_get(uint32_t dh, uint32_t tcol, int akind, const TexReplImg &ri) {   /* textura HD compartilhada (cria se preciso); 0 = sem memoria */
    extern uint64_t display_frames(void);
    const uint64_t k = hd_key(dh, tcol, akind);
    auto it = g_hd.find(k);
    if (it != g_hd.end()) { it->second.refs++; it->second.last = display_frames(); return it->second.id; }
    uint64_t b = 0; const uint32_t id = hd_create(ri, &b);
    if (!id) return 0;
    g_hd[k] = {id, b, display_frames(), 1}; g_hd_of[id] = k; return id;
}
static void repl_poll(GTEnt &e, int ow, int oh, bool wait) {
    extern uint64_t display_frames(void); uint64_t fr = display_frames();
    if (fr < e.retry) return;
    std::shared_ptr<TexReplImg> ri; int st = e.akind == 1 ? texrepl_upscale_take(e.dh, ri) : repl_get(e.dh, ow, oh, ri, e.tcol, wait);
    if (st == 2) {
        const uint32_t nid = hd_get(e.dh, e.tcol, e.akind, *ri);
        if (nid) { tex_drop(e.id, e.bytes); e.id = nid; e.bytes = 0; e.awaiting = false; repl_wait_note(e, fr); }
        else e.retry = fr + 120;   /* sem memoria agora: a original segue e tenta de novo em 2 s (a imagem sera relida) */
    }
    else if (st == 0) e.awaiting = false;
}
static uint32_t gpu_tex_bind(bool rtt) {   /* handle da textura do estado atual (R) na GPU; 0 = nao deu; rtt = o destino e um alvo de textura */
    extern uint64_t display_frames(void);
    if (R.tpsm > 10 || R.tw > 1024 || R.th > 1024) return 0;
    static const bool font_dbg = getenv("TF_FONT_DBG") != nullptr;   /* (antes um getenv a cada uso do atlas da fonte) */
    if (R.tpsm == 4 && R.tw == 512 && font_dbg) { static int nd; if (nd++ < 12) { fprintf(stderr, "[fontdbg] atlas %08x tbw=%u swz=%d pal:", R.taddr, R.tbw, (int)R.swz); for (int i = 0; i < 16; i += 3) fprintf(stderr, " %08x", R.pal[i]); fprintf(stderr, " [15]=%08x flt=%d fn=%u\n", R.pal[15], (int)R.linear, R.tfn); } }
    GTKey k = {R.taddr, R.tpsm, R.tw, R.th, R.tbw, (uint32_t)R.swz, 0};
    if (R.tpsm == 4 || R.tpsm == 5) { uint32_t ph = 2166136261u; for (uint32_t i = 0, n = R.tpsm == 4 ? 16 : 256; i < n; i++) ph = (ph ^ R.pal[i]) * 16777619u; k.pal = ph; }
    else if (R.tpsm == 6 || R.tpsm == 7) k.pal = (uint32_t)hash_bytes(G.clut, G.clut_bytes) ^ CMD(0xc5);
    size_t n;
    if (R.tpsm >= 8) { uint32_t bsz = R.tpsm == 8 ? 8 : 16, bpr = std::max(1u, R.tbw / 4); n = (size_t)((R.th + 3) / 4) * bpr * bsz; }
    else if (R.swz) n = (size_t)((R.th + 7) >> 3) * R.swzrow; else n = (size_t)R.th * R.rowb;
    n = std::min<size_t>(n, R.tlimit);
    /* verificacao do conteudo: texturas na VRAM (render-to-texture) e pequenas a cada epoca de lista; as grandes da RAM 1 vez por quadro */
    bool often = (R.taddr >= 0x04000000u && R.taddr < 0x04200000u) || n <= 65536;
    uint64_t stamp = often ? g_ep_ge : display_frames() * 1000003ull + g_ep_tr;
    GTEnt *e = nullptr; auto it = g_gtex.find(k); if (it != g_gtex.end()) e = &it->second;
    if (e && e->stamp == stamp && e->id) { e->used = display_frames(); if (e->awaiting) repl_poll(*e, (int)R.tw, (int)R.th, rtt); return e->id; }
    uint64_t h = n && R.tp ? hash_bytes(R.tp, n) : 0;
    if (e && e->id && e->hash == h) { e->stamp = stamp; e->used = display_frames(); if (e->awaiting) repl_poll(*e, (int)R.tw, (int)R.th, rtt); return e->id; }
    std::vector<uint32_t> tmp; const uint32_t *px;
    if (R.tc) { texcache_fill(R.tc); px = R.tc->px.data(); }
    else { tmp.resize((size_t)R.tw * R.th); for (uint32_t y = 0; y < R.th; y++) for (uint32_t x = 0; x < R.tw; x++) tmp[(size_t)y * R.tw + x] = tfetch_raw((int)x, (int)y); px = tmp.data(); }
    { static const char *dd = getenv("TF_TEX_DUMP_RAW");   /* pesquisa do hash das texturas: bytes de origem + metadados (TF_TEX_DUMP_RAW=pasta) */
      if (dd && !(R.taddr >= 0x04000000u && R.taddr < 0x04200000u) && n) {
        char base[512]; snprintf(base, sizeof base, "%s/raw_%08x_%u_%ux%u_%u_%u_%08x", dd, R.taddr, R.tpsm, R.tw, R.th, R.tbw, (uint32_t)R.swz, texrepl_hash(R.tp, n));   /* + hash: cada versao do conteudo num arquivo */
        char fn[560]; snprintf(fn, sizeof fn, "%s.bin", base); if (FILE *f = fopen(fn, "wb")) { fwrite(R.tp, 1, n, f); fclose(f); }
        snprintf(fn, sizeof fn, "%s.txt", base); if (FILE *f = fopen(fn, "w")) { fprintf(f, "addr=%08x psm=%u w=%u h=%u bw=%u swz=%u bytes=%zu clutmode=%06x clutbytes=%u\n", R.taddr, R.tpsm, R.tw, R.th, R.tbw, (uint32_t)R.swz, n, CMD(0xc5), G.clut_bytes); fclose(f); }
        snprintf(fn, sizeof fn, "%s.clut", base); if (FILE *f = fopen(fn, "wb")) { fwrite(G.clut, 1, G.clut_bytes, f); fclose(f); }
        snprintf(fn, sizeof fn, "%s.png", base); img_save_png(fn, (int)R.tw, (int)R.th, px);
      } }
    /* texturas substitutas / despejo: o hash dos dados identifica a textura (nao vale para render-to-texture na VRAM) */
    uint32_t dh = 0; bool repl = false, awaiting = false; std::shared_ptr<TexReplImg> ri;
    if (n && R.tp && !(R.taddr >= 0x04000000u && R.taddr < 0x04200000u) && (texrepl_active() || texrepl_dumping() || gfx_texscale() > 1)) {
        dh = texrepl_hash(R.tp, n); repl = true;
    }
    uint32_t id = 0, tcol = 0xffffffffu; uint64_t nbytes = 0, retry = 0;
    if (repl && !texrepl_active()) texrepl_dump(dh, (int)R.tw, (int)R.th, R.tpsm, R.taddr, px, false);
    if (repl && texrepl_active()) {
        if (texrepl_ambiguous(dh)) tcol = texrepl_color(px, (size_t)R.tw * R.th);   /* varias cores por paleta: escolhe pela cor da textura original */
        int st = repl_get(dh, (int)R.tw, (int)R.th, ri, tcol, rtt, true);
        texrepl_dump(dh, (int)R.tw, (int)R.th, R.tpsm, R.taddr, px, st != 0);
        if (st == 2) { id = hd_get(dh, tcol, 0, *ri); if (!id) { awaiting = true; retry = display_frames() + 120; } } else awaiting = st == 1;
    }
    int akind = 0;
    if (!id && !awaiting && repl && gfx_texscale() > 1 && R.tw * R.th <= 65536) { texrepl_upscale_request(dh, (int)R.tw, (int)R.th, px, gfx_texscale()); awaiting = true; akind = 1; }   /* sem substituta: amplia em segundo plano */
    if (!id) id = gpu_tex_create_ev((int)R.tw, (int)R.th, px, false, &nbytes);
    if (!id) return 0;
    if (e) tex_drop(e->id, e->bytes);
    GTEnt ne = {id, h, stamp, display_frames()}; ne.dh = dh; ne.awaiting = awaiting; ne.akind = akind; ne.bytes = nbytes; ne.retry = retry; ne.tcol = tcol; ne.asked = awaiting ? display_frames() : 0; if (ri && !awaiting) g_rw_n++; g_gtex[k] = ne;   /* pronta na hora: espera 0 */
    return id;
}
static std::vector<GpuVtx> g_gv;
/* O PSP arredonda as coordenadas de tela para 1/16 de pixel. Na GPU, a 8x, um limite entre duas pecas desenhadas separadamente (ex.: personagem do campo = quad de cima + quad de baixo, y=129,3125) cai no centro de uma linha de pixels e, com erros de float diferentes nas duas pecas, nenhuma cobre a linha: fresta de 1 pixel. Arredondar as duas para o mesmo valor exato fecha a fresta. */
static inline float q16(float v) { static const bool off = getenv("TF_NO_Q16") != nullptr; return off ? v : roundf(v * 16.f) * 0.0625f; }
static inline void gpu_push(const Vtx &v, uint32_t col) { g_gv.push_back({q16(v.x), q16(v.y), v.z, v.u, v.v, v.rw, col}); }
/* 60 FPS: com a interpolacao ligada, os triangulos de costas (cull) e degenerados tambem vao para a lista GRAVADA (g_hidden = os que nao
 * sao desenhados agora): assim a lista de um modelo 3D que gira tem sempre o mesmo tamanho e pareia com a do quadro anterior; o quadro do
 * meio refaz o cull com as posicoes interpoladas (sem isso o desenho ficava sem par: olhos do professor aparecendo por baixo dos oculos) */
static bool g_rec_all; static std::vector<uint32_t> g_hidden; static std::vector<GpuVtx> g_gall;
static void gpu_tri_sink(const Vtx &a, const Vtx &b, const Vtx &c, bool flat) {
    float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    const bool vis = fabsf(area) >= 1e-6f && !(g_cull && (area > 0) == (g_cull == 1));
    if (!vis && !g_rec_all) return;
    if (!vis) g_hidden.push_back((uint32_t)(g_gv.size() / 3));
    bool same = flat || (a.col == b.col && b.col == c.col);   /* como draw_tri_t: cor constante = a do ultimo vertice */
    gpu_push(a, same ? c.col : a.col); gpu_push(b, same ? c.col : b.col); gpu_push(c, c.col);
}
/* Retangulo de UV (texels) de cada sprite 2D enviado a GPU: o sombreador amostra so dentro dele (GpuState::uvclamp), sem misturar a borda da peca
 * com o vizinho do atlas quando o filtro linear roda numa resolucao maior. Um por sprite que gerou triangulos, na ordem. */
struct SpriteUvRect { float u0, v0, u1, v1; };
static std::vector<SpriteUvRect> g_sprite_rects; static bool g_sprite_collect;
static void gpu_sprite(const Vtx &a, const Vtx &b, bool through) {   /* como draw_sprite_t: cobre os pixels nativos tocados */
    float x1 = std::min(a.x, b.x), x2 = std::max(a.x, b.x), y1 = std::min(a.y, b.y), y2 = std::max(a.y, b.y);
    float dx = b.x - a.x, dy = b.y - a.y; bool rot = through && dx * dy < 0;
    float dux = 0, duy = 0, dvx = 0, dvy = 0;
    if (rot) { duy = dy != 0 ? (b.u - a.u) / dy : 0; dvx = dx != 0 ? (b.v - a.v) / dx : 0; }
    else { dux = dx != 0 ? (b.u - a.u) / dx : 0; dvy = dy != 0 ? (b.v - a.v) / dy : 0; }
    float xs = floorf(x1), xe = ceilf(x2), ys = floorf(y1), ye = ceilf(y2);
    if (xe <= xs || ye <= ys) return;
    auto at = [&](float x, float y) { float fx = x - a.x, fy = y - a.y; Vtx v = b; v.x = x; v.y = y; v.z = b.z; v.rw = 1.f; v.u = a.u + fx * dux + fy * duy; v.v = a.v + fx * dvx + fy * dvy; return v; };
    Vtx p00 = at(xs, ys), p10 = at(xe, ys), p01 = at(xs, ye), p11 = at(xe, ye);
    gpu_push(p00, b.col); gpu_push(p10, b.col); gpu_push(p01, b.col);
    gpu_push(p10, b.col); gpu_push(p11, b.col); gpu_push(p01, b.col);
    if (g_sprite_collect) g_sprite_rects.push_back({std::min(a.u, b.u), std::min(a.v, b.v), std::max(a.u, b.u), std::max(a.v, b.v)});
}
/* linha/ponto: os mesmos pixels nativos da DDA do draw_line, cada um como um quadrado de 1 pixel nativo (cor/UV/Z do pixel) */
static void gpu_line(const Vtx &a, const Vtx &b, bool flat) {
    float dx = b.x - a.x, dy = b.y - a.y; int n = (int)std::max(fabsf(dx), fabsf(dy));
    if (n == 0) n = 1;
    bool point = (a.x == b.x && a.y == b.y);
    for (int i = 0; i < (point ? 1 : n); i++) {
        float t = (float)i / n; float x = floorf(a.x + dx * t), y = floorf(a.y + dy * t);
        uint32_t col = b.col;
        if (!flat) { col = 0; for (int k = 0; k < 32; k += 8) col |= (uint32_t)(CH(a.col, k) + (CH(b.col, k) - (float)CH(a.col, k)) * t) << k; }
        Vtx v = b; v.z = a.z + (b.z - a.z) * t; v.u = a.u + (b.u - a.u) * t; v.v = a.v + (b.v - a.v) * t; v.rw = 1.f;
        Vtx p00 = v, p10 = v, p01 = v, p11 = v;
        p00.x = x; p00.y = y; p10.x = x + 1; p10.y = y; p01.x = x; p01.y = y + 1; p11.x = x + 1; p11.y = y + 1;
        gpu_push(p00, col); gpu_push(p10, col); gpu_push(p01, col);
        gpu_push(p10, col); gpu_push(p11, col); gpu_push(p01, col);
    }
}
/* os sprites do clear cobrem o quadro inteiro (dentro do scissor)? uniao exata dos retangulos (como gpu_sprite: pixels tocados) */
static bool clear_covers_fb(const PrimJob &j, const Scissor &sc, int nw, int nh) {
    if (j.type != 6 || sc.x1 > 0 || sc.y1 > 0 || sc.x2 < nw - 1 || sc.y2 < nh - 1 || j.count < 2 || j.count > 128) return false;
    struct Rc { int x1, y1, x2, y2; }; std::vector<Rc> rs; std::vector<int> xs = {0, nw}, ys = {0, nh};
    for (uint32_t i = 0; i + 1 < j.count; i += 2) {
        const Vtx &a = j.vv[i], &b = j.vv[i + 1];
        Rc r = {std::max(0, (int)floorf(std::min(a.x, b.x))), std::max(0, (int)floorf(std::min(a.y, b.y))), std::min(nw, (int)ceilf(std::max(a.x, b.x))), std::min(nh, (int)ceilf(std::max(a.y, b.y)))};
        if (r.x2 <= r.x1 || r.y2 <= r.y1) continue;
        rs.push_back(r); xs.push_back(r.x1); xs.push_back(r.x2); ys.push_back(r.y1); ys.push_back(r.y2);
    }
    std::sort(xs.begin(), xs.end()); xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
    std::sort(ys.begin(), ys.end()); ys.erase(std::unique(ys.begin(), ys.end()), ys.end());
    for (size_t yi = 0; yi + 1 < ys.size(); yi++) for (size_t xi = 0; xi + 1 < xs.size(); xi++) {
        int cx = xs[xi], cy = ys[yi]; bool in = false;
        for (auto &r : rs) if (cx >= r.x1 && cx < r.x2 && cy >= r.y1 && cy < r.y2) { in = true; break; }
        if (!in) return false;
    }
    return true;
}
/* sprites do texto (atlas 4 bits, paleta branco + rampa de alfa): handle do atlas sombra em alta resolucao, ou 0 (usar a textura normal) */
static uint32_t hd_font_tex(const PrimJob &j) {
    if (!hdfont_enabled()) return 0;
    for (int i = 0; i < 16; i++) if (R.pal[i] != (((uint32_t)(i * 17) << 24) | 0x00ffffffu)) {
        static const bool dbg = getenv("TF_FONT_DBG") != nullptr; static uint32_t seen[8]; static int ns;
        if (dbg) { bool k = false; for (int q = 0; q < ns; q++) if (seen[q] == R.taddr) k = true; if (!k && ns < 8) { seen[ns++] = R.taddr; fprintf(stderr, "[hdfont] atlas %08x %ux%u ignorado: paleta [%d]=%08x\n", R.taddr, R.tw, R.th, i, R.pal[i]); } }
        return 0;
    }
    static std::vector<HdRect> rs; rs.clear();
    for (uint32_t i = 0; i + 1 < j.count; i += 2) {
        const Vtx &a = j.vv[i], &b = j.vv[i + 1];
        rs.push_back({(int)floorf(std::min(a.u, b.u) + 0.01f), (int)floorf(std::min(a.v, b.v) + 0.01f), (int)ceilf(std::max(a.u, b.u) - 0.01f), (int)ceilf(std::max(a.v, b.v) - 0.01f)});
    }
    return hdfont_texture(R.taddr, (int)R.tw, (int)R.th, R.tp, R.swz, R.rowb, R.swzrow, R.tlimit, rs.data(), (int)rs.size());
}
static bool gpu_shadow_pass_impl(const PrimJob &j, GFbRec *f);
static bool gpu_shadow_pass(const PrimJob &j, GFbRec *f) { double t = gpu_now_us(); bool r = gpu_shadow_pass_impl(j, f); g_gpu_us[3] += gpu_now_us() - t; return r; }
/* desenha a primitiva na GPU; false = nao deu (quadro sombra ja estava/ficou stale): quem chamou precisa do raster nativo */
static bool gpu_shadow_pass_impl(const PrimJob &j, GFbRec *f) {
    if (!f || f->stale) return false;
    int why = 0;
    /* mascara de escrita (so existe no 8888): canal inteiro preservado/gravado vira a mascara de cor da GPU; o byte do alfa (= stencil no
     * PSP) vai bit a bit para a mascara de escrita do stencil */
    uint32_t cmask = 15, swmask = 0xff;
    if (R.pmask) {
        for (int c = 0; c < 3; c++) { uint32_t b = (R.pmask >> (8 * c)) & 255; if (b == 255) cmask &= ~(1u << c); else if (b && !why) why = 3; }
        uint32_t ab = R.pmask >> 24; swmask = ~ab & 255; if (ab) cmask &= ~8u;
    }
    /* Stencil. No PSP ele E o alfa do framebuffer: todo desenho grava o alfa. Na GPU ele vive num buffer de stencil separado, que so os
     * desenhos com stencil e o modo clear com alfa atualizam; f->stOk diz se o buffer de stencil ainda e igual ao alfa do PSP (fica
     * verdadeiro depois de um clear do alfa na tela toda e falso quando um desenho comum grava o alfa). Teste que le o stencil com
     * stOk falso = sem suporte (raster nativo). O padrao do mapa/campo: clear do alfa -> mascara (ALWAYS/REPLACE 255) -> desenho
     * com EQUAL 255 -> clear do alfa. */
    bool hasSt = gpu_has_stencil() && R.psm == 3, st = false; uint32_t aconst = 0;
    if (!why && R.st && !R.clear) {
        bool reads = R.sfunc > 1 && (R.smask & 255);   /* NEVER/ALWAYS (ou mascara 0) nao dependem do valor atual */
        Scissor s0 = scissor();   /* o stencil so precisa valer onde o desenho pode escrever (alvo de textura do reflexo da loja: clear 256x256 num alvo registrado com 512 linhas) */
        bool inside = std::min(s0.x2, f->nw - 1) < f->stw && std::min(s0.y2, f->nh - 1) < f->sth;
        if (!hasSt || (reads && !(f->stOk && inside))) why = 2;
        else {
            st = true;
            /* alfa gravado na cor = novo valor do stencil quando previsivel (sem blend: o alfa da saida tambem e o fator do blend);
             * senao o alfa da cor fica como estava (o buffer de stencil e o que vale para os testes). Com blend o alfa tambem e gravado (os
             * backends usam o canal alfa do fator de blend, sem mexer no RGB): no PSP o alfa do alvo E o stencil, e um alvo de textura usado depois
             * como textura (as cartas da Polymerization: cartas desenhadas no alvo com stencil e fatiadas em malha) precisa dele */
            if (swmask == 255 && (R.szpass == 1 || R.szpass == 2)) aconst = 0x100 | (R.szpass == 2 ? R.sref & 255 : 0);
            else cmask &= ~8u;
        }
    }
    if (why == 3 && g_gpu_dbg) { static int pm; if (pm++ < 8) fprintf(stderr, "[pmask] %08x blend=%d tex=%d st=%d z=%d/%d prim=%d n=%d" "%c", R.pmask, R.blend, R.tex, R.st, R.ztest, R.zwrite, j.type, (int)j.count, 10); }
    if (why == 2 && g_gpu_dbg) { static int pn; if (pn++ < 12) fprintf(stderr, "[stencil] sem suporte: func=%u ref=%u mask=%u op=%u/%u/%u psm=%d stOk=%d gpu=%d blend=%d tex=%d z=%d/%d prim=%d\n", R.sfunc, R.sref, R.smask, R.sfail, R.szfail, R.szpass, R.psm, (int)f->stOk, (int)gpu_has_stencil(), R.blend, R.tex, R.ztest, R.zwrite, j.type); }
    uint32_t fix = 0, sfixc = 0; bool twofix = false;
    if (!why && R.blend && !R.clear) {
        bool sfix = R.bsf >= 10, dfix = R.bdf >= 10, sdbl = R.bsf == 6 || R.bsf == 7, ddbl = R.bdf == 6 || R.bdf == 7;
        if (R.beq == 5) why = 4;
        else if (R.bsf == 8 || R.bsf == 9 || R.bdf == 8 || R.bdf == 9) why = 5;
        else if (ddbl || (sdbl && (R.bdf == 2 || R.bdf == 3))) why = 6;
        fix = sfix ? CMD(0xe0) : CMD(0xe1);
        if (sfix && dfix && CMD(0xe0) != CMD(0xe1)) { fix = CMD(0xe1); sfixc = CMD(0xe0); twofix = true; }
    }
    uint32_t tex = 0;
    uint32_t fbt_w = 0, fbt_h = 0;
    if (!why && R.tex) {
        tex = gpu_fbtex_lookup(f, &fbt_w, &fbt_h);   /* render-to-texture: imagem da GPU */
        if (!tex && R.tpsm == 4 && j.type == 6 && j.through) tex = hd_font_tex(j);   /* texto: atlas da fonte em alta resolucao (hdfont.cpp) */
        if (!tex) { tex = gpu_tex_bind(f->isRtt); if (!tex) why = (R.tw > 1024 || R.th > 1024) ? 9 : 8; }
    }
    if (why) { f->stale = true; interp_taint(f->id); g_gpu_stat[why]++; return false; }
    GpuState gs; memset(&gs, 0, sizeof gs);
    gs.fb = f->id; gs.ztest = R.ztest; gs.zwrite = R.zwrite; gs.zfunc = R.zfunc; gs.clear = R.clear ? (R.clear & 0x700) : 0;
    gs.blend = R.blend; gs.bsf = twofix ? 11 : R.bsf >= 10 ? 10 : R.bsf; gs.sfix = sfixc; gs.bdf = R.bdf >= 10 ? 10 : R.bdf; gs.beq = R.beq; gs.fix = fix;
    gs.atest = R.atest; gs.afunc = R.afunc; gs.aref = R.aref; gs.amask = R.amask;
    if (tex) { gs.tex = tex; gs.tw = (tex & GPU_FBTEX) ? fbt_w : R.tw; gs.th = (tex & GPU_FBTEX) ? fbt_h : R.th; gs.tfn = R.tfn; gs.talpha = R.talpha; gs.tdbl = R.tdbl; gs.tenv = R.tenv; gs.linear = R.linear; gs.clampu = R.clampu; gs.clampv = R.clampv; { static const char *nl = getenv("TF_DBG_NOLINEAR"); if (nl) gs.linear = 0; } }
    Scissor sc = scissor(); gs.sx1 = sc.x1; gs.sy1 = sc.y1; gs.sx2 = sc.x2; gs.sy2 = sc.y2;
    gs.cmask = cmask; gs.aconst = aconst;
    if (st) { gs.st = 1; gs.sfunc = R.sfunc; gs.sref = R.sref & 255; gs.smask = R.smask & 255; gs.swmask = swmask; gs.sfail = R.sfail; gs.szfail = R.szfail; gs.szpass = R.szpass; }
    g_gv.clear(); g_hidden.clear(); g_rec_all = interp_enabled(); t_tri_sink = gpu_tri_sink;
    g_sprite_rects.clear(); { static const bool off = getenv("TF_NO_UVCLAMP") != nullptr; g_sprite_collect = !off && tex && j.type == 6 && j.through; }   /* TF_NO_UVCLAMP=1: sem o recorte de UV dos sprites (comparacao A/B) */
    const Vtx *vv = j.vv; uint32_t n = j.count;
    /* Axis-aligned textured quads that use their texture once (menu pieces such as the caps and the centre of the MAIN MENU pill) are
     * clamped to their own UV rectangle like the 2D sprites: with a bigger internal resolution and an HD texture the linear filter would
     * otherwise blend the edge column with the opposite edge (wrap) and leave a faint line at the seam between neighbouring pieces. */
    if (tex && j.type == 4 && n == 4 && !j.through && vv[0].x == vv[2].x && vv[1].x == vv[3].x && vv[0].y == vv[1].y && vv[2].y == vv[3].y) {
        static const bool off = getenv("TF_NO_UVCLAMP") != nullptr;
        float u0 = vv[0].u, u1 = vv[0].u, v0 = vv[0].v, v1 = vv[0].v;
        for (int i = 1; i < 4; i++) { u0 = std::min(u0, vv[i].u); u1 = std::max(u1, vv[i].u); v0 = std::min(v0, vv[i].v); v1 = std::max(v1, vv[i].v); }
        if (!off && !(tex & GPU_FBTEX) && u0 >= 0 && v0 >= 0 && u1 <= (float)R.tw && v1 <= (float)R.th && u1 > u0 && v1 > v0) { gs.uvclamp = 1; gs.cu0 = u0; gs.cv0 = v0; gs.cu1 = u1; gs.cv1 = v1; }
    }
    switch (j.type) {
    case 6: for (uint32_t i = 0; i + 1 < n; i += 2) gpu_sprite(vv[i], vv[i + 1], j.through); break;
    case 3: for (uint32_t i = 0; i + 2 < n; i += 3) draw_tri_clip(vv[i], vv[i + 1], vv[i + 2], j.flat); break;
    case 4: for (uint32_t i = 0; i + 2 < n; i++) { if (i & 1) draw_tri_clip(vv[i + 1], vv[i], vv[i + 2], j.flat); else draw_tri_clip(vv[i], vv[i + 1], vv[i + 2], j.flat); } break;
    case 5: for (uint32_t i = 1; i + 1 < n; i++) draw_tri_clip(vv[0], vv[i], vv[i + 1], j.flat); break;
    case 0: case 1: case 2: if (g_gpu_dbg) { extern uint64_t display_frames(void); static int nl; if (nl++ < 6) fprintf(stderr, "[gpu] quadro %llu: %s (%u vertices) na GPU\n", (unsigned long long)display_frames(), j.type ? "linhas" : "pontos", n); }
        if (j.type == 1) { for (uint32_t i = 0; i + 1 < n; i += 2) gpu_line(vv[i], vv[i + 1], j.flat); break; }
        if (j.type == 2) { for (uint32_t i = 0; i + 1 < n; i++) gpu_line(vv[i], vv[i + 1], j.flat); break; }
        for (uint32_t i = 0; i < n; i++) gpu_line(vv[i], vv[i], true); break;                           /* pontos */
    default: break;
    }
    t_tri_sink = nullptr;
    const GpuVtx *recv = g_gv.data(); size_t recn = g_gv.size();
    if (!g_hidden.empty()) {   /* desenha so os visiveis; grava todos (g_gall) */
        g_gall.swap(g_gv); g_gv.clear(); size_t h = 0;
        for (size_t t = 0; t * 3 + 2 < g_gall.size(); t++) { if (h < g_hidden.size() && g_hidden[h] == t) { h++; continue; } g_gv.insert(g_gv.end(), g_gall.begin() + t * 3, g_gall.begin() + t * 3 + 3); }
        recv = g_gall.data(); recn = g_gall.size();
    }
    if (R.clear && (R.clear & 0x200) && swmask) {   /* clear do alfa: o stencil recebe o alfa do vertice (precisa ser o mesmo em todos) */
        bool same = !g_gv.empty(); for (auto &v : g_gv) if ((v.col >> 24) != (g_gv[0].col >> 24)) { same = false; break; }
        if (hasSt && same) {
            gs.st = 1; gs.sfunc = 1; gs.sref = g_gv[0].col >> 24; gs.smask = 255; gs.swmask = swmask; gs.sfail = gs.szfail = gs.szpass = 2;
            if (swmask == 255 && sc.x1 <= 0 && sc.y1 <= 0) {   /* clear que cobre todo o recorte (ou o alvo inteiro): stencil valido nessa regiao */
                const int cw = std::min(f->nw, sc.x2 + 1), ch = std::min(f->nh, sc.y2 + 1);
                if (cw > 0 && ch > 0 && clear_covers_fb(j, sc, cw, ch)) { f->stOk = true; f->stw = cw; f->sth = ch; }
            }
        } else if (!g_gv.empty()) f->stOk = false;
    } else if (!R.clear && !R.st && swmask && !g_gv.empty()) f->stOk = false;   /* desenho comum grava o alfa (o stencil do PSP) */
    g_gpu_prims++; g_gpu_verts += g_gv.size();
    /* sprites 2D com textura: um desenho por grupo de sprites com o mesmo retangulo de UV (cada grupo leva o seu retangulo no estado) */
    const bool perSprite = g_sprite_collect && g_hidden.empty() && g_sprite_rects.size() * 6 == g_gv.size() && recn == g_gv.size();
    g_sprite_collect = false;
    if (perSprite) {
        for (size_t i = 0; i < g_sprite_rects.size();) {
            size_t k = i + 1;
            while (k < g_sprite_rects.size() && !memcmp(&g_sprite_rects[k], &g_sprite_rects[i], sizeof(SpriteUvRect))) k++;
            GpuState gi = gs; gi.uvclamp = 1; gi.cu0 = g_sprite_rects[i].u0; gi.cv0 = g_sprite_rects[i].v0; gi.cu1 = g_sprite_rects[i].u1; gi.cv1 = g_sprite_rects[i].v1;
            gpu_draw(gi, g_gv.data() + i * 6, (int)((k - i) * 6));
            interp_record(gi, recv + i * 6, (int)((k - i) * 6), f->isRtt, 0);
            i = k;
        }
        interp_fb_drawn(f->id);
        return true;
    }
    if (!g_gv.empty()) { gpu_draw(gs, g_gv.data(), (int)g_gv.size()); interp_fb_drawn(f->id); }
    if (recn) interp_record(gs, recv, (int)recn, f->isRtt, (j.type >= 3 && j.type <= 5) ? g_cull : 0);
    return true;
}
/* apresentacao: id do quadro sombra do buffer exibido (se sincronizado com o nativo) ou -1 (usar o quadro nativo) */
static bool skip_eligible(const GFbRec &r) {   /* paginas inteiras e exclusivas do buffer (endereco e tamanho multiplos de 4 KB) */
    return (r.addr & 0xfff) == 0 && r.stride >= 480 && r.addr + prot_bytes(r) <= 0x04200000u;
}
bool ge_gpu_skip_active(uint32_t addr) {   /* o nativo deste buffer esta invalido (so a GPU tem a imagem)? */
    if (!g_gpu_on) return false;
    addr = 0x04000000u | (addr & 0x1ffff0u);
    for (auto &c : g_gf) if (c.used && c.res == g_ge_res && c.addr == addr) return c.prot;
    return false;
}
const uint32_t *ge_gpu_small(void) { return g_small_valid ? g_small : nullptr; }
static int g_ck_pend = -1;   /* quadro (indice em g_gf) cuja reducao para a comparacao com o nativo ficou para ge_gpu_frame_post */
/* SUBIDA ANTECIPADA (2026-10-15): as texturas usadas nas ultimas sessoes ja estao na RAM (pre-carregamento do texrepl), mas so iam para a GPU no
 * quadro em que a cena as pedia: 20-60 MB de uma vez e o intermediario do 60 FPS esperava a copia (o tranco ao entrar numa tela, sessao 205658).
 * Agora sobem aos poucos (ate ~4 MB por quadro, no fim do display_present, depois do intermediario) como texturas HD compartilhadas sem dono (g_hd);
 * quando a cena pede, ja estao la. Respeita 90% do orcamento (TF_TEX_BUDGET_MB). TF_TEX_SUBIDA=0 desliga. */
static void hd_preupload(void) {
    static const bool off = getenv("TF_TEX_SUBIDA") && atoi(getenv("TF_TEX_SUBIDA")) == 0;
    if (off || !g_gpu_on || !texrepl_active()) return;
    extern uint64_t display_frames(void);
    uint64_t sent = 0; uint32_t h; std::shared_ptr<TexReplImg> img;
    while (sent < (4u << 20) && texrepl_take_preupload(&h, img)) {
        const uint64_t k = hd_key(h, 0xffffffffu, 0);
        if (g_hd.count(k)) continue;
        const uint64_t b = img->bc.empty() ? (uint64_t)img->w * img->h * 4 * 4 / 3 : (uint64_t)img->bc.size();
        if (g_tex_bytes + b > g_tex_budget / 10 * 9) return;
        uint64_t nb = 0; const uint32_t id = hd_create(*img, &nb);
        if (!id) return;
        g_hd[k] = {id, nb, display_frames(), 0}; g_hd_of[id] = k; sent += b;
    }
}
void ge_gpu_frame_post(void) {
    hd_preupload();
    const int i = g_ck_pend; g_ck_pend = -1;
    if (i < 0 || !g_gf[i].used) return;
    const int t = gpu_check_start(g_gf[i].id); if (t >= 0) { g_gf[i].ticket = t; g_gf[i].ckp = true; }
}
int ge_gpu_frame(uint32_t addr, uint32_t fmt) {
    extern uint64_t display_frames(void);
    if (!g_gpu_on) return -1;
    uint64_t fr = display_frames();
    g_gpu_frames++;
    for (auto &c : g_gf) if (c.used && c.prot) { g_gpu_skipframes++; break; }
    if (!g_gpu_dbg && texrepl_active() && fr % 600 == 0) {   /* resumo das texturas substitutas no log da sessao (sempre): ajuda a achar por que elas pararam de aparecer */
        int q = 0, rd = 0; uint64_t ld = 0, fl = 0, dr = 0; texrepl_stats(&q, &rd, &ld, &fl, &dr);
        texrepl_save_recent();
        uint64_t rm = 0, rh = 0, dms = 0; int nt = 0; texrepl_stats2(&rm, &rh, &dms, &nt); uint64_t pfc = texrepl_prefetched();
        fprintf(stderr, "[texturas] quadro %llu: GPU %llu MB em %zu texturas (%llu falhas de criacao); PNG: fila %d, prontas %d, lidas %llu, falhas %llu, descartadas %llu; cache RAM %llu MB (%llu acertos, %llu pre-carregadas); decodificacao %llu ms em %d threads; espera das substitutas: %llu, media %.1f quadros, max %llu, >10 quadros: %llu\n", (unsigned long long)fr,
                (unsigned long long)(g_tex_bytes >> 20), g_gtex.size(), (unsigned long long)g_tex_fail, q, rd, (unsigned long long)ld, (unsigned long long)fl, (unsigned long long)dr,
                (unsigned long long)rm, (unsigned long long)rh, (unsigned long long)pfc, (unsigned long long)dms, nt, (unsigned long long)g_rw_n, g_rw_n ? (double)g_rw_sum / g_rw_n : 0.0, (unsigned long long)g_rw_max, (unsigned long long)g_rw_late);
        g_rw_n = g_rw_sum = g_rw_max = g_rw_late = 0;
        { uint64_t bd = 0, bms = 0, dmb = 0, bs = 0; size_t bp = 0; texrepl_bc_stats(&bd, &bms, &bp, &dmb, &bs);
          if (bd || bp || bs) fprintf(stderr, "[texturas] BC7: %llu lidas direto do cache, %llu comprimidas nesta sessao (%llu ms em segundo plano), %zu na fila, cache em disco %llu MB\n", (unsigned long long)bs, (unsigned long long)bd, (unsigned long long)bms, bp, (unsigned long long)dmb); }
    }
    if (g_gpu_dbg && fr % 600 == 0) {
        fprintf(stderr, "[gpu] quadro %llu: %llu prims na GPU (%llu vertices), texturas %zu; sem raster nativo em %llu de %llu quadros, %llu acessos a VRAM protegida (%.2f ms); stale:", (unsigned long long)fr, (unsigned long long)g_gpu_prims, (unsigned long long)g_gpu_verts, g_gtex.size(),
                (unsigned long long)g_gpu_skipframes, (unsigned long long)g_gpu_frames, (unsigned long long)g_gpu_faults, g_gpu_us[5] / 1e3);
        fprintf(stderr, " [ms/quadro: passe %.2f flush %.2f downsample %.2f present %.2f nativo %.2f]", g_gpu_us[3] / 600e3, g_gpu_us[0] / 600e3, g_gpu_us[1] / 600e3, g_gpu_us[2] / 600e3, g_gpu_us[4] / 600e3); memset(g_gpu_us, 0, sizeof g_gpu_us);
        for (int i = 1; i < 10; i++) if (g_gpu_stat[i]) fprintf(stderr, " %s=%llu", g_gpu_why[i], (unsigned long long)g_gpu_stat[i]);
        { int q = 0, rd = 0; uint64_t ld = 0, fl = 0, dr = 0; texrepl_stats(&q, &rd, &ld, &fl, &dr);
          fprintf(stderr, " [texturas GPU: %llu MB, %llu falhas; PNG: fila %d, prontas %d, lidas %llu, falhas %llu, descartadas %llu]", (unsigned long long)(g_tex_bytes >> 20), (unsigned long long)g_tex_fail, q, rd, (unsigned long long)ld, (unsigned long long)fl, (unsigned long long)dr); }
        fputc(10, stderr);
    }
    if (fr % 120 == 0) for (auto &f : g_rt) if (f.used && f.lastUse + 300 < fr) { gpu_fb_destroy(f.id); f = GFbRec(); }   /* alvos de textura sem uso */
    if (fr % 120 == 0) for (auto it = g_gtex.begin(); it != g_gtex.end();) { if (it->second.used + 300 < fr) { tex_drop(it->second.id, it->second.bytes); it = g_gtex.erase(it); } else ++it; }   /* uma HD continua na GPU (tex_drop): so sai quando o orcamento enche */
    if (fmt != 3) return -1;
    addr = 0x04000000u | (addr & 0x1ffff0u);
    GFbRec *f = nullptr; for (auto &c : g_gf) if (c.used && c.res == g_ge_res && c.addr == addr) f = &c;
    if (!f) return -1;
    for (auto &r : g_gf) if (r.used && r.needResync && !r.prot) { gpu_fb_resync(r); r.needResync = false; }   /* escrita da CPU num quadro sem raster nativo */
    gpu_flush();
    if (f->stale) { gpu_fb_resync(*f); f->lastBad = fr; f->ckp = false; if (g_gpu_dbg) fprintf(stderr, "[gpu] quadro sombra refeito do nativo (estado sem suporte)\n"); return f->id; }
    /* Comparacao com o nativo, assincrona: o resultado do quadro anterior DESTE buffer (chegou 1-2 quadros atras, sem esperar a GPU) e
     * conferido agora. Se a sombra estava fora de sincronia (algo escreveu no quadro sem passar pelo GE: filme, copia da CPU), ela e
     * refeita a partir do nativo e este quadro sai em resolucao nativa (retorno -1). No modo sem raster nativo nao ha o que comparar
     * (a escrita cai no tratador de excecao); a reducao so serve para a deteccao da tela de carregamento (ge_gpu_small).
     * TF_GPU_SYNC=1: compara no proprio quadro. */
    static const bool sync = getenv("TF_GPU_SYNC") != nullptr;
    static std::vector<uint32_t> ds(480 * 272);
    bool native = false;
    auto check = [&](GFbRec &r) {
        if (!r.ckp) return;
        r.ckp = false;
        if (!gpu_check_get(r.ticket, ds.data())) return;
        if (r.prot) { memcpy(g_small, ds.data(), sizeof g_small); g_small_valid = true; return; }
        int bad = 0, tot = 0;
        for (int y = 0; y < 272; y += 2) for (int x = 0; x < 480; x += 2) {
            uint32_t sv = ds[(size_t)y * 480 + x], n = r.nat[(y >> 1) * 240 + (x >> 1)];
            int d = abs((int)(sv & 255) - (int)(n & 255)) + abs((int)((sv >> 8) & 255) - (int)((n >> 8) & 255)) + abs((int)((sv >> 16) & 255) - (int)((n >> 16) & 255));
            tot++; if (d > 96) bad++;
        }
        if (bad * 100 > tot * 6) {
            if (g_gpu_dbg) fprintf(stderr, "[gpu] quadro %llu: sombra fora de sincronia (%d de %d): refeita do nativo\n", (unsigned long long)fr, bad, tot);
            gpu_fb_resync(r); r.lastBad = fr; native = true;
        }
    };
    check(*f);   /* resultado da comparacao anterior deste buffer */
    if (!f->prot) {
        const uint32_t *fbm = (const uint32_t *)mem_ptr(addr);
        for (int y = 0; y < 272; y += 2) for (int x = 0; x < 480; x += 2) f->nat[(y >> 1) * 240 + (x >> 1)] = fbm[(size_t)y * f->stride + x];
    }
    /* a reducao le o quadro real: fica para ge_gpu_frame_post, depois do intermediario (com ela aqui o quadro real retido na fila saia antes dele) */
    if (sync) { int t = gpu_check_start(f->id); if (t >= 0) { f->ticket = t; f->ckp = true; check(*f); } }
    else g_ck_pend = (int)(f - g_gf);
    /* entra no modo sem raster nativo depois de 3 quadros sem acesso nem divergencia */
    if (g_skip_ok) for (auto &r : g_gf) {
        if (!r.used || r.prot || r.stale || r.needResync || !skip_eligible(r) || fr < std::max(r.lastFault, r.lastBad) + 3) continue;
        if (&r == f && native) continue;
        DWORD old; if (!VirtualProtect(mem_ptr(r.addr), prot_bytes(r), PAGE_NOACCESS, &old)) continue;
        r.prot = true; r.ckp = false;
    }
    return native ? -1 : f->id;
}
void ge_set_gpu(bool on) {
    if (on == g_gpu_on) return;
    if (!on) { gpu_reset_fbs(); gpu_tex_clear(); }
    g_gpu_on = on;
    if (on) { static bool h; if (!h) { h = true; AddVectoredExceptionHandler(1, vram_fault); } }
    g_small_valid = false;
    for (auto &s : g_sh) s = Shadow();
    fprintf(stderr, "[gpu] renderizador por GPU %s\n", on ? "ligado" : "desligado");
}

/* Textura lida de um buffer exibido protegido (modo sem raster nativo): o raster nativo a leria texel a texel nas threads do rasterizador e o acesso
 * cairia no vram_fault fora da thread do jogo (duas threads ao mesmo tempo = a segunda nao era tratada e o processo caia). Reconstroi antes, aqui,
 * exatamente como o tratador faria numa leitura. */
static void gpu_unprotect_texture(void) {
    const uint32_t a = R.taddr & 0x0fffffffu; if (a < 0x04000000u || a >= 0x04200000u) return;
    uint32_t n = R.swz ? ((R.th + 7) >> 3) * R.swzrow : R.th * R.rowb;
    if (R.tpsm >= 8) n = ((R.th + 3) / 4) * std::max(1u, R.tbw / 4) * (R.tpsm == 8 ? 8u : 16u);
    for (auto &f : g_gf) if (f.used && f.prot && a < f.addr + prot_bytes(f) && a + std::max(n, 1u) > f.addr) gpu_materialize(f, false);
}
static void draw_prim(GeList &l, uint32_t data) {
    g_prims++;
    uint32_t type = (data >> 16) & 7, count = data & 0xffff;
    VType t = decode_vtype(CMD(0x12));
    int oc = 0, ot = 0, op = 0, ow = 0, on = 0, vs = vertex_size(t, &ow, &ot, &oc, &on, &op);
    (void)l;
    g_off_nrm = on; g_off_wt = ow;
    static const bool nodraw_env = getenv("GE_NODRAW") != nullptr;   /* medicao: pula a rasterizacao */
    static const char *gps = getenv("GE_PRIM_STOP");   /* GE_PRIM_STOP=quadro:n: nesse quadro so rasteriza os n primeiros PRIMs (a captura mostra o estado parcial) */
    bool stop_here = false;
    if (gps) { extern uint64_t display_frames(void); static uint64_t sf = ~0ull; static int sn = 0; unsigned long long fq = 0; int lim = 0;
        if (sscanf(gps, "%llu:%d", &fq, &lim) == 2 && display_frames() + 4 >= fq && display_frames() <= fq + 1) { if (sf != display_frames()) { sf = display_frames(); sn = 0; } stop_here = sn++ >= lim; } }
    const bool nodraw = nodraw_env || stop_here || g_ctl_nodraw;   /* g_ctl_nodraw: comando "nodraw 1" do TF_CTL (duelos de teste ~10x mais rapidos; capturas saem pretas) */
    if (nodraw) { if (t.idx) G.iaddr += count * (t.idx == 1 ? 1 : t.idx == 2 ? 2 : 4); else G.vaddr += count * vs; return; }
    uint32_t va = G.vaddr, ia = G.iaddr;
    bool textured = CMD(0x1e) & 1;
    static int nlog = 0; static const bool gl = getenv("GE_PRIM") != nullptr;
    static const char *gpf = getenv("GE_PRIM_FRAME");   /* GE_PRIM_FRAME=n: todos os PRIMs do quadro n, com textura */
    extern uint64_t display_frames(void);
    static uint64_t ctl_dump_frame = ~0ull;
    if (g_ctl_texdump_arm) { g_ctl_texdump_arm = false; ctl_dump_frame = display_frames() + 1; fprintf(stderr, "[ctl] texdump: alvo quadro %llu\n", (unsigned long long)ctl_dump_frame); }   /* comando texdump do tf.py */
    bool this_frame = (gpf && display_frames() == (uint64_t)atoll(gpf)) || (display_frames() >= ctl_dump_frame && display_frames() <= ctl_dump_frame + 1);   /* o contador pode andar de 2 em 2 */
    if ((gl && nlog++ < 60) || this_frame) {
        fprintf(stderr, "[prim] type=%u n=%u vtype=%06x through=%d tex=%d fb=%08x/%u z=%08x/%u psm=%u clear=%x blend=%d/%03x scissor=%03x,%03x va=%08x",
                type, count, CMD(0x12), t.through, textured, fb_base(), fb_stride(), z_base(), CMD(0x9f) & 0xffff, CMD(0xd2), CMD(0xd3), CMD(0x21) & 1, CMD(0xdf), CMD(0xd4), CMD(0xd5), va);
        if (textured) fprintf(stderr, " tex=%08x tpsm=%u size=%06x tbw=%u func=%05x flt=%05x wrap=%03x clut=%06x mode=%06x lvl=%06x tbp1=%06x/%06x ts1=%04x",
                              tex_addr(), CMD(0xc3), CMD(0xb8), CMD(0xa8) & 0xffff, CMD(0xc9), CMD(0xc6), CMD(0xc7), CMD(0xc5), CMD(0xc2), CMD(0xc8), CMD(0xa1), CMD(0xa9), CMD(0xb9));
        fprintf(stderr, " ztest=%u/%u zmask=%u cull=%u/%u tmap=%03x light=%u atest=%u/%06x", CMD(0x23) & 1, CMD(0xde) & 7, CMD(0xe7) & 1, CMD(0x1d) & 1, CMD(0x9b) & 1,
                CMD(0xc0) & 0x3ff, CMD(0x17) & 1, CMD(0x22) & 1, CMD(0xdb));
        if (CMD(0x21) & 1) fprintf(stderr, " fix=%06x/%06x", CMD(0xe0) & 0xffffff, CMD(0xe1) & 0xffffff);
        if (CMD(0x24) & 1) fprintf(stderr, " stencil=%06x/op%06x", CMD(0xdc) & 0xffffff, CMD(0xdd) & 0xffffff);
        if (CMD(0xe8) & 0xffffff || CMD(0xe9) & 0xff) fprintf(stderr, " pmask=%06x/%02x", CMD(0xe8) & 0xffffff, CMD(0xe9) & 0xff);
        if (count >= 2 && !t.idx) { Vtx a = fetch(ge_ptr(va), t, oc, ot, op, ow), b = fetch(ge_ptr(va + vs * (count - 1)), t, oc, ot, op, ow);
            fprintf(stderr, " (%.0f,%.0f,%.0f)-(%.0f,%.0f,%.0f) col=%08x", a.x, a.y, a.z, b.x, b.y, b.z, b.col);
            if (count <= 8 && this_frame) {   /* PRIMs pequenos (cartas, sprites): todos os vertices com UV */
                fprintf(stderr, "\n    v:");
                for (uint32_t i = 0; i < count; i++) { Vtx q = fetch(ge_ptr(va + vs * i), t, oc, ot, op, ow); fprintf(stderr, " (%.2f,%.2f uv %.4f,%.4f)", q.x, q.y, q.u, q.v); }
            } }
        if (CMD(0x17) & 1) {
            fprintf(stderr, "\n    luz: cmat=%u emc=%06x amc=%06x/%02x dmc=%06x smc=%06x alc=%06x/%02x spow=%.2f",
                    CMD(0x53) & 7, CMD(0x54), CMD(0x55), CMD(0x58) & 255, CMD(0x56), CMD(0x57), CMD(0x5c), CMD(0x5d) & 255, fdata(CMD(0x5b)));
            for (int i = 0; i < 4; i++) if (CMD(0x18 + i) & 1)
                fprintf(stderr, " | L%d tipo=%03x pos=(%.2f,%.2f,%.2f) amb=%06x dif=%06x spc=%06x", i, CMD(0x5f + i) & 0x303,
                        fdata(CMD(0x63 + 3 * i)), fdata(CMD(0x64 + 3 * i)), fdata(CMD(0x65 + 3 * i)), CMD(0x8f + 3 * i), CMD(0x90 + 3 * i), CMD(0x91 + 3 * i));
        }
        fputc(10, stderr);
    }
    uint32_t ts = CMD(0xb8); float tw = (float)(1 << (ts & 15)), th = (float)(1 << ((ts >> 8) & 15));
    if (count == 0 || count > 65535) return;
    std::vector<Vtx> vv(count);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t idx = i;
        if (t.idx == 1) idx = *ge_ptr(ia + i); else if (t.idx == 2) idx = *(uint16_t *)ge_ptr(ia + i * 2); else if (t.idx == 3) idx = *(uint32_t *)ge_ptr(ia + i * 4);
        vv[i] = fetch(ge_ptr(va + idx * vs), t, oc, ot, op, ow);
        if (!t.through) { vv[i].u *= tw; vv[i].v *= th; }
    }
    /* como no hardware: o proximo PRIM continua de onde este parou */
    if (t.idx) G.iaddr += count * (t.idx == 1 ? 1 : t.idx == 2 ? 2 : 4); else G.vaddr += count * vs;
    /* icone de carregamento (Ojama + disco, texturas loading000x embutidas no BOOT): no PSP indicava leitura do UMD, que
     * nao existe aqui; nao e desenhado (TF_LOADING_ICON=1 mantem) */
    { static const bool keep = getenv("TF_LOADING_ICON") != nullptr || game_id_built() != "ULES-00600";   /* enderecos do BOOT europeu */
      if (textured && !keep) { uint32_t ta = tex_addr(); if (ta >= 0x088ad000 && ta < 0x088b3800) return; } }
    bool flat = CMD(0x50) == 0;
    g_cull = (!t.through && (CMD(0x1d) & 1)) ? ((CMD(0x9b) & 1) ? 2 : 1) : 0;
    setup_state(textured);
    if (R.tex) {   /* GE_TEXDUMP_FRAME=n: grava cada textura distinta usada no quadro n (decodificada) em gen/tex_<end>_<fmt>.bmp */
        static const char *tdf = getenv("GE_TEXDUMP_FRAME");
        if ((tdf && display_frames() == (uint64_t)atoll(tdf)) || (display_frames() >= ctl_dump_frame && display_frames() <= ctl_dump_frame + 1)) {
            static std::map<uint64_t, bool> done; uint64_t key = ((uint64_t)R.taddr << 8) | R.tpsm;
            if (!done[key]) {
                done[key] = true;
                char fn[80]; snprintf(fn, sizeof fn, "gen/tex_%08x_%u_%ux%u.bmp", R.taddr, R.tpsm, R.tw, R.th);
                if (FILE *f = fopen(fn, "wb")) {
                    uint32_t w = R.tw, h = R.th, sz = 54 + w * h * 4; uint8_t fh[14] = {'B', 'M'}; memcpy(fh + 2, &sz, 4); fh[10] = 54;
                    int32_t ih[10] = {40, (int32_t)w, -(int32_t)h, 0x00200001, 0, (int32_t)(w * h * 4), 0, 0, 0, 0};
                    fwrite(fh, 1, 14, f); fwrite(ih, 4, 10, f);
                    for (uint32_t y = 0; y < h; y++) for (uint32_t x = 0; x < w; x++) {
                        uint32_t p = tfetch((int)x, (int)y); uint8_t px[4] = {(uint8_t)(p >> 16), (uint8_t)(p >> 8), (uint8_t)p, (uint8_t)(p >> 24)};
                        fwrite(px, 1, 4, f);
                    }
                    fclose(f);
                }
            }
        }
    }
    /* Menu pieces drawn side by side (e.g. the pill behind "MAIN MENU": two caps and a stretched centre) come out of the transform with
     * edges a hair off the pixel grid (33.91 instead of 34.00). On the PSP that covers the same pixels, but on the GPU at 4x/8x it leaves a
     * visible step at each seam. Axis-aligned flat quads get edges within 1/8 px of a whole pixel snapped to it (same pixel coverage at 1x). */
    if (g_gpu_on && !t.through && type == 4 && count == 4 && vv[0].z == vv[1].z && vv[0].z == vv[2].z && vv[0].z == vv[3].z &&
        ((vv[0].x == vv[2].x && vv[1].x == vv[3].x && vv[0].y == vv[1].y && vv[2].y == vv[3].y) ||
         (vv[0].x == vv[1].x && vv[2].x == vv[3].x && vv[0].y == vv[2].y && vv[1].y == vv[3].y))) {   /* either strip order: the Duel Result checkerboard uses the second */
        auto snap = [](float v) { float r = roundf(v); return fabsf(v - r) <= 0.125f ? r : v; };
        for (int i = 0; i < 4; i++) { vv[i].x = snap(vv[i].x); vv[i].y = snap(vv[i].y); }
    }
    /* The screen title bar text ("Change duel settings" and friends: 4-bit font atlas, first line of the bar at y=28) sits a little low
     * in the bar once the HD font is on; raise it by 2 px. Only in the PC look (HD font on), never in the raw PSP mode. */
    if (t.through && type == 6 && textured && R.tpsm == 4 && count >= 2 && hdfont_enabled()) {
        float miny = vv[0].y, maxy = vv[0].y, minx = vv[0].x;
        for (uint32_t i = 1; i < count; i++) { miny = std::min(miny, vv[i].y); maxy = std::max(maxy, vv[i].y); minx = std::min(minx, vv[i].x); }
        if (miny >= 27.5f && miny <= 30.5f && maxy <= 50.f && minx <= 10.f) for (uint32_t i = 0; i < count; i++) vv[i].y -= 1.25f;
    }
    PrimJob job = {type, count, vv.data(), flat, t.through != 0};
    if (type == 4 && count == 4 && !t.through && textured) {   /* carta/quad 3D texturizado: o mod que mostra monstros pergunta se a casa do campo esta na tela (modbox_quad_near) */
        extern void modbox_quad(uint32_t, float, float, float, float);
        float qx0 = vv[0].x, qx1 = vv[0].x, qy0 = vv[0].y, qy1 = vv[0].y;
        for (int i = 1; i < 4; i++) { qx0 = std::min(qx0, vv[i].x); qx1 = std::max(qx1, vv[i].x); qy0 = std::min(qy0, vv[i].y); qy1 = std::max(qy1, vv[i].y); }
        modbox_quad(fb_base(), qx0, qy0, qx1, qy1);
    }
    if (type == 6 && count >= 2 && !(count & 1) && t.through && textured) {   /* sprites 2D texturizados: o modulo do mouse acha barras de menu (2 vertices) e linhas de texto (varios sprites num PRIM) */
        extern bool display_mouse_menus(void); extern void mui_prim(uint32_t, int, int, int, int, float, float, float, float, uint32_t, uint32_t); extern void mui_run(uint32_t, int, int, int, int);
        if (display_mouse_menus()) {
            if (count == 2) mui_prim(fb_base(), (int)vv[0].x, (int)vv[0].y, (int)vv[1].x, (int)vv[1].y, vv[0].u, vv[0].v, vv[1].u, vv[1].v, tex_addr(), vv[1].col);
            else { float a = vv[0].x, b = vv[0].y, c = a, d = b; for (uint32_t i = 1; i < count; i++) { a = std::min(a, vv[i].x); c = std::max(c, vv[i].x); b = std::min(b, vv[i].y); d = std::max(d, vv[i].y); } mui_run(fb_base(), (int)a, (int)b, (int)c, (int)d); }
        }
    }
    static const char *ptf = getenv("GE_PRIMTIME_FRAME"); extern uint64_t display_frames(void);
    bool pt = ptf && display_frames() == (uint64_t)atoll(ptf);
    auto now_us = []() { LARGE_INTEGER c, f; QueryPerformanceCounter(&c); QueryPerformanceFrequency(&f); return (double)c.QuadPart * 1e6 / f.QuadPart; };
    double t0 = pt ? now_us() : 0;
    bool native = true;   /* rasterizar no quadro nativo? (nao, se o buffer esta no modo sem raster nativo e a GPU desenhou) */
    if (g_gpu_on && R.psm == 3) {
        GFbRec *gf = gpu_fb_for(fb_base(), R.fstride, z_base(), R.zstride);
        if (!gf) gf = gpu_rtt_for(fb_base(), R.fstride, z_base(), R.zstride);   /* nao e buffer exibido: alvo de render-to-texture */
        if (gf) {
            if (gf->isRtt && gf->stale) gpu_fb_resync(*gf);   /* uma primitiva sem suporte passou so pelo nativo: a GPU parte dele */
            if (gf->needResync && !gf->prot) { gpu_fb_resync(*gf); gf->needResync = false; }   /* a CPU escreveu no quadro: a GPU parte do nativo */
            if (gf->nativeFresh) {   /* o nativo foi reconstruido so para uma leitura: com a nova primitiva ele volta a ficar desatualizado */
                gf->nativeFresh = false;
                DWORD old; if (g_skip_ok && !gf->stale && !gf->needResync && VirtualProtect(mem_ptr(gf->addr), prot_bytes(*gf), PAGE_NOACCESS, &old)) gf->prot = true;
            }
            bool ok = gpu_shadow_pass(job, gf);
            if (gf->prot) { if (ok) native = false; else gpu_materialize(*gf); }
        }
    }
    if (native && R.tex && g_gpu_on) gpu_unprotect_texture();
    { double tr0 = g_gpu_dbg ? gpu_now_us() : 0; if (native) raster_prim(job); if (g_gpu_dbg) g_gpu_us[4] += gpu_now_us() - tr0; }
    double t1 = pt ? now_us() : 0;
    if (!g_gpu_on && sw_res() > 1 && R.psm == 3) shadow_pass(job);
    if (pt) fprintf(stderr, "[pt] type=%u n=%u through=%d tex=%d blend=%d/%03x atest=%d zt=%d st=%d tpsm=%u lin=%d tw=%u nat=%.0f sh=%.0f us\n", type, count, (int)t.through, (int)R.tex, (int)R.blend, (unsigned)CMD(0xdf) & 0xfff, (int)R.atest, (int)R.ztest, (int)R.st, R.tpsm, (int)R.linear, R.tw, t1 - t0, now_us() - t1);
}
static void transfer(void) {
    uint32_t src = (CMD(0xb2) & 0xffffff) | ((CMD(0xb3) & 0xff0000) << 8), sw = CMD(0xb3) & 0xffff;
    uint32_t dst = (CMD(0xb4) & 0xffffff) | ((CMD(0xb5) & 0xff0000) << 8), dw = CMD(0xb5) & 0xffff;
    uint32_t sp = CMD(0xeb), dp = CMD(0xec), sz = CMD(0xee), bpp = (CMD(0xea) & 1) ? 4 : 2;
    uint32_t sx = sp & 0x3ff, sy = (sp >> 10) & 0x3ff, dx = dp & 0x3ff, dy = (dp >> 10) & 0x3ff, w = (sz & 0x3ff) + 1, h = ((sz >> 10) & 0x3ff) + 1;
    texcache_flush(); g_ep_tr++;
    if (ge_ptr(dst) == g_dummy || ge_ptr(src) == g_dummy || w * bpp > 4096) return;
    for (uint32_t y = 0; y < h; y++) {
        const uint32_t da = dst + ((dy + y) * dw + dx) * bpp, sa = src + ((sy + y) * sw + sx) * bpp;
        if (region_left(da) < w * bpp || region_left(sa) < w * bpp) continue;   /* linha que passaria do fim da VRAM/RAM: a memoria seguinte nao existe (queda) */
        memmove(mem_ptr(da & 0x0fffffffu), mem_ptr(sa & 0x0fffffffu), w * bpp);
    }
    ge_vram_written(dst + (dy * dw + dx) * bpp, ((h - 1) * dw + w) * bpp);
}

/* ------------------------------------------------------------------ interpretador de display list */
/* listas terminadas saem do mapa (antes ficavam para sempre, ~2 por quadro); a remocao espera o ge_run mais externo acabar,
 * porque um callback de sinal pode reentrar no ge_run da mesma lista */
static int g_ge_depth; static std::vector<uint32_t> g_ge_finished;
static void ge_run_list(uint32_t id);
double g_ge_frame_us;   /* tempo gasto no GE (listas, sombra, envio para a GPU) desde o ultimo quadro: divisao do quadro pesado (display.cpp) */
static void ge_run(uint32_t id) {
    static const bool lat_dbg = tf_lat_debug(); const int64_t t_list = g_ge_depth || !lat_dbg ? 0 : pace_now(); const uint64_t p0 = g_prims;
    const bool top = g_ge_depth == 0; const double tge0 = top ? qpc_us() : 0;   /* so a chamada de topo (a lista chamada pelo jogo), nao as aninhadas */
    g_ge_depth++; ge_run_list(id); g_ge_depth--;
    if (t_list && g_prims - p0 > 100 && pace_have_exit()) { pace_stat(PACE_STAT_LIST0, pace_ms(t_list - pace_last_exit())); pace_stat(PACE_STAT_LIST1, pace_ms(pace_now() - pace_last_exit())); }
    if (!g_ge_depth && !g_ge_finished.empty()) {
        for (uint32_t f : g_ge_finished) g_lists.erase(f);
        g_ge_finished.clear();
        /* 60/120 FPS: a lista terminou, a GPU ja comeca o quadro real (o jogo monta tudo no fim do quadro, ~0,5 ms antes do vblank); o intermediario
         * que vem depois dele fica pronto mais cedo e a fase do intermediario (pacing.cpp) encurta */
        /* lista grande (quadro novo) sem alvos de textura: o quadro real fica retido e vai para a GPU logo atras do intermediario (gpu_hold) */
        static uint64_t prims_kick; const bool big = g_prims - prims_kick > 100; prims_kick = g_prims;
        if (g_gpu_on && interp_enabled()) { g_gpu_prof_tag = 'R'; if (big && interp_hold_ok()) gpu_hold(); else gpu_kick(); }
    }
    if (top) g_ge_frame_us += qpc_us() - tge0;
}
static void ge_run_list(uint32_t id) {
    GeList &l = g_lists[id];
    texcache_flush();
    while (!l.done) {
        if (l.stall && (l.pc & 0x0fffffff) == (l.stall & 0x0fffffff)) return;   /* espera o jogo avancar o stall */
        uint32_t w = rd32(l.pc); l.pc += 4;
        uint32_t cmd = w >> 24, data = w & 0xffffff;
        static const bool tr = getenv("GE_TRACE") != nullptr;
        if (tr) fprintf(stderr, "[ge %08x] %02x %06x\n", l.pc - 4, cmd, data);
        if (cmd == 0x9c && data != G.cmd[0x9c]) texcache_flush();   /* novo alvo de render: pode virar textura (RTT) */
        G.cmd[cmd] = data;
        switch (cmd) {
        case 0x10: l.base = (data << 8) & 0x0f000000; break;
        case 0x01: G.vaddr = (l.offset + (l.base | data)) & 0x0fffffffu; break;
        case 0x02: G.iaddr = (l.offset + (l.base | data)) & 0x0fffffffu; break;
        case 0x13: l.offset = data << 8; break;
        case 0x14: l.offset = l.pc - 4; break;   /* ORIGIN: enderecos relativos a este comando */
        case 0x08: l.pc = (l.offset + (l.base | (data & ~3u))) & 0x0fffffffu; break;
        case 0x09: break;   /* BJUMP: sem teste de bbox, nao salta */
        case 0x0a: if (l.sp < 32) l.stack[l.sp++] = l.pc; l.pc = (l.offset + (l.base | (data & ~3u))) & 0x0fffffffu; break;
        case 0x0b: if (l.sp) l.pc = l.stack[--l.sp]; break;
        case 0x0e: {
            auto it = g_gecb.find(l.cbid);
            if (it != g_gecb.end() && it->second.signal_fn) k_call_guest(it->second.signal_fn, data & 0xffff, it->second.signal_arg, 0, 0);
            break; }
        case 0x0f: {
            auto it = g_gecb.find(l.cbid);
            if (it != g_gecb.end() && it->second.finish_fn) k_call_guest(it->second.finish_fn, data & 0xffff, it->second.finish_arg, 0, 0);
            break; }
        case 0x0c: l.done = true; break;
        case 0x04: draw_prim(l, data); break;
        case 0x2a: G.bn = data & 127; break;   /* BONEMATRIXNUMBER (8 matrizes 4x3) */
        case 0x2b: if (G.bn < 96) G.bone[G.bn++] = fdata(data); break;
        case 0x3a: G.wn = data & 15; break;
        case 0x3b: if (G.wn < 12) G.world[G.wn++] = fdata(data); break;
        case 0x3c: G.vn = data & 15; break;
        case 0x3d: if (G.vn < 12) G.view[G.vn++] = fdata(data); break;
        case 0x3e: G.pn = data & 15; break;
        case 0x3f: if (G.pn < 16) G.proj[G.pn++] = fdata(data); break;
        case 0x40: G.tn = data & 15; break;
        case 0x41: if (G.tn < 12) G.tgen[G.tn++] = fdata(data); break;
        case 0xb0: case 0xb1: break;
        case 0xc4: {   /* CLOAD: copia a paleta */
            uint32_t addr = (CMD(0xb0) & 0xffffff) | ((CMD(0xb1) & 0xf0000) << 8), n = (data & 0x3f) * 32;
            if (n > sizeof G.clut) n = sizeof G.clut; memcpy(G.clut, ge_ptr(addr), n); G.clut_bytes = n; break; }
        case 0xea: transfer(); break;
        default: break;
        }
    }
    g_ge_finished.push_back(id);
}

HLE(sceGeEdramGetAddr) { RET(0x04000000); }
HLE(sceGeEdramGetSize) { RET(0x00200000); }
HLE(sceGeEdramSetAddrTranslation) { RET(0x00200000); }
HLE(sceGeSetCallback) {   /* (struct {signal_fn,signal_arg,finish_fn,finish_arg}*) */
    uint32_t p = ARG(0), id = g_next_cb++;
    g_gecb[id] = GeCb{rd32(p), rd32(p + 4), rd32(p + 8), rd32(p + 12)}; RET(id);
}
HLE(sceGeUnsetCallback) { g_gecb.erase(ARG(0)); RET(0); }
static void enqueue(CPU *c) {   /* (list, stall, cbid, arg) */
    uint32_t id = g_next_list++;
    g_lists[id] = GeList{ARG(0) & ~3u, ARG(0) & ~3u, ARG(1), ARG(2), 0, 0, false, {}, 0};
    ge_run(id); RET(id);
}
HLE(sceGeListEnQueue) { enqueue(c); }
HLE(sceGeListEnQueueHead) { enqueue(c); }
HLE(sceGeListUpdateStallAddr) { auto it = g_lists.find(ARG(0)); if (it != g_lists.end()) { it->second.stall = ARG(1); ge_run(ARG(0)); } RET(0); }
HLE(sceGeListSync) { RET(0); }
HLE(sceGeDrawSync) { RET(0); }
HLE(sceGeBreak) { RET(0); }
HLE(sceGeContinue) { RET(0); }

void display_set_frame(uint32_t addr, uint32_t stride, uint32_t fmt);
HLE(sceDisplaySetMode) { RET(0); }
void ge_shadow_add_display(uint32_t addr);
void mui_set_frame(uint32_t fb);
void modbox_quad_frame(uint32_t fb);
HLE(sceDisplaySetFrameBuf) { if (ARG(0)) ge_shadow_add_display(ARG(0)); if (ARG(0)) mui_set_frame(0x04000000u | (ARG(0) & 0x1ffff0u)); if (ARG(0)) modbox_quad_frame(0x04000000u | (ARG(0) & 0x1ffff0u)); display_set_frame(ARG(0), ARG(1), ARG(2)); RET(0); }
/* devolve float (59,94) em $f0: o player de filmes usa para o limite de sincronia audio/video (90000/fps*2); com 0
 * o limite virava lixo e quase todo quadro era descartado (filme "congelado") */
HLE(sceDisplayGetFramePerSec) { c->f[0].f = 59.9400599f; RET(0); }

/* ------------------------------------------------------------------ state save / load (savestate.cpp)
 * Saved: the GE registers/matrices/palette and the display lists that are waiting (stall). Everything else here is derived from memory (texture
 * caches, shadow frames, the GPU's copies of the frame buffers) and is simply dropped and rebuilt from the restored VRAM. */
struct GeSnap { Ge g; std::map<uint32_t, GeCb> cb; std::map<uint32_t, GeList> lists; uint32_t next_list, next_cb; };
void ge_state_prepare(void) {   /* before VRAM is read or written: frame buffers only the GPU holds (protected pages) are copied back to VRAM first */
    if (g_gpu_on) for (auto &f : g_gf) if (f.used) gpu_materialize(f);
}
std::shared_ptr<void> ge_state_save(void) {
    auto s = std::make_shared<GeSnap>();
    s->g = G; s->cb = g_gecb; s->lists = g_lists; s->next_list = g_next_list; s->next_cb = g_next_cb;
    return s;
}
void ge_state_load(const std::shared_ptr<void> &p) {   /* after memory is restored */
    const GeSnap &s = *(const GeSnap *)p.get();
    G = s.g; g_gecb = s.cb; g_lists = s.lists; g_next_list = s.next_list; g_next_cb = s.next_cb;
    g_ge_depth = 0; g_ge_finished.clear();
    texcache_flush(); g_ep_tr++;
    for (auto &sh : g_sh) sh = Shadow();
    g_small_valid = false;
    gpu_reset_fbs();   /* drops the GPU frame buffers (and the 60/120 FPS interpolation history): they are rebuilt from VRAM on the next primitive */
}
