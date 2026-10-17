/* Fonte em alta resolucao (ver hdfont.h). */
#include "hdfont.h"
#include "gpu.h"
#include "gfx.h"
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

namespace {
/* O jogo pede o glifo num rascunho 32x32 (4 bits) e copia para o atlas so a JANELA que comeca em (1,1) do rascunho (28x28 nos textos vistos);
 * glifos que passam da janela (descendentes de g, y, p...) chegam cortados. Por isso o indice e feito sobre o recorte da janela. */
const int WIN0 = 1;
struct Rec {                       /* um glifo pedido pelo jogo */
    uint32_t code; int xp, yp, top, gw, gh;   /* posicao do bitmap no rascunho (canto superior esquerdo) e tamanho */
    std::vector<uint8_t> nib;            /* gw*gh niveis 0..15 */
};
std::vector<Rec> g_recs;
std::unordered_set<uint64_t> g_rec_seen;
struct Hd { int w = 0, h = 0, top = 0; std::vector<uint8_t> a8; bool tried = false; };
std::unordered_map<uint32_t, Hd> g_hd;   /* codigo -> glifo desenhado em HDS x */
struct Hit { int rec, cx, cy; };          /* glifo e onde fica o recorte justo na janela */
std::map<uint32_t, std::unordered_map<uint64_t, Hit>> g_idx;   /* (rw<<8|rh) da janela -> hash do recorte justo -> glifo */
struct Atlas {
    int W = 0, H = 0; std::vector<uint32_t> px;   /* (W*HDS) x (H*HDS), 0xAABBGGRR */
    std::unordered_map<uint64_t, uint64_t> rects;  /* retangulo -> hash do conteudo nativo quando o atlas sombra foi atualizado */
    uint32_t gpu = 0; bool dirty = true, qualified = false;
};
std::map<uint32_t, Atlas> g_at;
const bool g_dbg = getenv("TF_FONT_DBG") != nullptr;
uint64_t g_hits, g_miss;

/* hash de conteudo (so interno: indices por glifo/retangulo): 8 bytes por passo (o FNV byte a byte era o maior custo proprio da fonte HD) */
inline uint64_t fnv(uint64_t h, const uint8_t *p, size_t n) {
    size_t i = 0;
    for (; i + 8 <= n; i += 8) { uint64_t w; memcpy(&w, p + i, 8); h = (h ^ w) * 0x9E3779B97F4A7C15ull; h ^= h >> 32; }
    for (; i < n; i++) h = (h ^ p[i]) * 1099511628211ull;
    return h;
}
bool trim(const uint8_t *nib, int w, int h, int *x0, int *y0, int *tw, int *th) {   /* recorte justo; false se vazio */
    int ax = w, ay = h, bx = -1, by = -1;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) if (nib[y * w + x]) { ax = std::min(ax, x); bx = std::max(bx, x); ay = std::min(ay, y); by = std::max(by, y); }
    if (bx < 0) return false;
    *x0 = ax; *y0 = ay; *tw = bx - ax + 1; *th = by - ay + 1; return true;
}
uint64_t crop_hash(const uint8_t *nib, int w, int x0, int y0, int tw, int th) {
    uint64_t h = 1469598103934665603ull; uint8_t hd[2] = {(uint8_t)tw, (uint8_t)th}; h = fnv(h, hd, 2);
    for (int y = 0; y < th; y++) h = fnv(h, nib + (size_t)(y0 + y) * w + x0, (size_t)tw);
    return h;
}
/* janela rw x rh do rascunho (origem WIN0) com o glifo r */
void window_image(const Rec &r, int rw, int rh, std::vector<uint8_t> &out) {
    out.assign((size_t)rw * rh, 0);
    for (int y = 0; y < rh; y++) for (int x = 0; x < rw; x++) {
        int gx = x + WIN0 - r.xp, gy = y + WIN0 - r.yp;
        if (gx >= 0 && gy >= 0 && gx < r.gw && gy < r.gh) out[(size_t)y * rw + x] = r.nib[(size_t)gy * r.gw + gx];
    }
}
void index_rec(uint32_t wkey, int rw, int rh, int ri) {
    static std::vector<uint8_t> img; window_image(g_recs[ri], rw, rh, img);
    int x0, y0, tw, th; if (!trim(img.data(), rw, rh, &x0, &y0, &tw, &th)) return;
    auto &m = g_idx[wkey]; uint64_t k = crop_hash(img.data(), rw, x0, y0, tw, th);
    if (!m.count(k)) m[k] = {ri, x0, y0};
}
}

bool hdfont_enabled(void) {
    static const char *e = getenv("TF_HDFONT");
    return e ? atoi(e) != 0 : !gfx_raw_psp();
}

void hdfont_record(uint32_t code, int xp, int yp, int top, const uint8_t *nib, int w, int h) {
    if (w <= 0 || h <= 0 || w > 64 || h > 64) return;   /* sempre guarda (mesmo desligada: ligar pelo menu no meio do jogo ja acha os glifos) */
    uint64_t k = fnv(fnv(1469598103934665603ull, (const uint8_t *)&code, 4), nib, (size_t)w * h) ^ ((uint64_t)(uint8_t)(xp + 64) << 40) ^ ((uint64_t)(uint8_t)(yp + 64) << 48) ^ ((uint64_t)w << 56);
    if (!g_rec_seen.insert(k).second) return;
    Rec r; r.code = code; r.xp = xp; r.yp = yp; r.top = top; r.gw = w; r.gh = h; r.nib.assign(nib, nib + (size_t)w * h);
    g_recs.push_back(std::move(r)); int ri = (int)g_recs.size() - 1;
    for (auto &kv : g_idx) index_rec(kv.first, (int)(kv.first >> 8), (int)(kv.first & 255), ri);
    Hd &hd = g_hd[code];
    if (!hd.tried) { hd.tried = true; if (!font_hd_glyph(code, &hd.w, &hd.h, &hd.top, &hd.a8)) { hd.w = hd.h = 0; hd.a8.clear(); } }
    if (g_dbg) { static int nr; if (nr++ < 4) fprintf(stderr, "[hdfont] glifo U+%04x guardado: bitmap %dx%d em (%d,%d) do rascunho, HD %dx%d\n", code, w, h, xp, yp, hd.w, hd.h); }
}

void hdfont_reset(void) { for (auto &kv : g_at) if (kv.second.gpu) gpu_tex_release(kv.second.gpu); g_at.clear(); }

uint32_t hdfont_texture(uint32_t addr, int W, int H, const uint8_t *tp, bool swz, uint32_t rowb, uint32_t swzrow, uint32_t tlimit, const HdRect *rects, int n) {
    if (!hdfont_enabled() || g_recs.empty() || W <= 0 || H <= 0 || W > 1024 || H > 1024) return 0;
    Atlas &A = g_at[addr];
    if (A.W != W || A.H != H) { A = Atlas(); A.W = W; A.H = H; A.px.assign((size_t)W * HDS * H * HDS, 0); }
    auto nibat = [&](int x, int y) -> int {
        if (x < 0 || y < 0 || x >= W || y >= H) return 0;
        uint32_t xb = (uint32_t)x >> 1, off = swz ? ((uint32_t)y >> 3) * swzrow + (xb >> 4) * 128 + ((uint32_t)y & 7) * 16 + (xb & 15) : (uint32_t)y * rowb + xb;
        if (off + 1 > tlimit) return 0;
        uint8_t b = tp[off]; return (x & 1) ? b >> 4 : b & 15;
    };
    const int PW = W * HDS;
    uint8_t buf[64 * 64]; int bx0 = 1 << 30, by0 = 1 << 30, bx1 = 0, by1 = 0;
    for (int i = 0; i < n; i++) {
        HdRect r = rects[i]; r.x0 = std::max(r.x0, 0); r.y0 = std::max(r.y0, 0); r.x1 = std::min(r.x1, W); r.y1 = std::min(r.y1, H);
        int rw = r.x1 - r.x0, rh = r.y1 - r.y0; if (rw <= 0 || rh <= 0 || rw > 64 || rh > 64) continue;
        for (int y = 0; y < rh; y++) for (int x = 0; x < rw; x++) buf[y * rw + x] = (uint8_t)nibat(r.x0 + x, r.y0 + y);
        uint64_t key = ((uint64_t)r.x0 << 48) | ((uint64_t)r.y0 << 32) | ((uint64_t)r.x1 << 16) | (uint64_t)r.y1;
        uint64_t hn = fnv(1469598103934665603ull, buf, (size_t)rw * rh) ^ ((uint64_t)rw << 56);
        auto it = A.rects.find(key); if (it != A.rects.end() && it->second == hn) continue;
        A.rects[key] = hn; A.dirty = true;
        bx0 = std::min(bx0, r.x0 * HDS); by0 = std::min(by0, r.y0 * HDS); bx1 = std::max(bx1, r.x1 * HDS); by1 = std::max(by1, r.y1 * HDS);   /* area trocada (HD) */
        for (int y = r.y0 * HDS; y < r.y1 * HDS; y++) memset(&A.px[(size_t)y * PW + r.x0 * HDS], 0, (size_t)rw * HDS * 4);   /* regiao HD do retangulo */
        int tx, ty, tw, th; if (!trim(buf, rw, rh, &tx, &ty, &tw, &th)) continue;   /* vazio */
        uint32_t wkey = ((uint32_t)rw << 8) | (uint32_t)rh;
        if (!g_idx.count(wkey)) for (size_t ri = 0; ri < g_recs.size(); ri++) index_rec(wkey, rw, rh, (int)ri);   /* janela de tamanho novo: indexa os glifos ja vistos */
        auto &m = g_idx[wkey]; auto gi = m.find(crop_hash(buf, rw, tx, ty, tw, th));
        const Hd *hd = gi != m.end() ? &g_hd[g_recs[gi->second.rec].code] : nullptr;
        if (hd && hd->w > 0) {   /* glifo conhecido: a versao HD, alinhada pelo centro do glifo INTEIRO (o recorte nativo pode estar cortado pela janela) */
            g_hits++; A.qualified = true;
            const Rec &rc = g_recs[gi->second.rec];
            int dx = tx - gi->second.cx, dy = ty - gi->second.cy;   /* origem da janela dentro do retangulo (0 se o retangulo e a janela) */
            float cxn = (rc.xp - WIN0 + rc.gw * 0.5f + dx) * HDS, base = (rc.yp + rc.top - WIN0 + dy) * HDS;   /* horizontal: centro do glifo; vertical: linha de base (centralizar pela altura desalinhava as letras, cada uma com altura diferente) */
            int hx = r.x0 * HDS + (int)floorf(cxn - hd->w * 0.5f + 0.5f), hy = r.y0 * HDS + (int)floorf(base - hd->top + 0.5f);
            for (int y = 0; y < hd->h; y++) {
                int py = hy + y; if (py < r.y0 * HDS || py >= r.y1 * HDS) continue;
                for (int x = 0; x < hd->w; x++) {
                    int pxx = hx + x; if (pxx < r.x0 * HDS || pxx >= r.x1 * HDS) continue;
                    uint8_t a = hd->a8[(size_t)y * hd->w + x]; if (a) A.px[(size_t)py * PW + pxx] = ((uint32_t)a << 24) | 0x00ffffffu;
                }
            }
        } else {   /* glifo desconhecido: bilinear do bitmap nativo (nivel * 17 = alfa da paleta) */
            g_miss++;
            if (g_dbg) { static int nm; if (nm++ < 6) { fprintf(stderr, "[hdfont] SEM par: ret (%d,%d)-(%d,%d) recorte %dx%d\n", r.x0, r.y0, r.x1, r.y1, tw, th); for (int y = 0; y < th; y++) { fprintf(stderr, "   "); for (int x = 0; x < tw; x++) fprintf(stderr, "%x", buf[(ty + y) * rw + tx + x]); fputc(10, stderr); } } }
            for (int y = 0; y < rh * HDS; y++) {
                float fy = (y + 0.5f) / HDS - 0.5f; int y0 = (int)floorf(fy); float ty2 = fy - y0; int ya = std::max(0, y0), yb = std::min(rh - 1, y0 + 1);
                for (int x = 0; x < rw * HDS; x++) {
                    float fx = (x + 0.5f) / HDS - 0.5f; int x0 = (int)floorf(fx); float tx2 = fx - x0; int xa = std::max(0, x0), xb = std::min(rw - 1, x0 + 1);
                    float v = (buf[ya * rw + xa] * (1 - tx2) + buf[ya * rw + xb] * tx2) * (1 - ty2) + (buf[yb * rw + xa] * (1 - tx2) + buf[yb * rw + xb] * tx2) * ty2;
                    uint8_t a = (uint8_t)std::min(255.f, v * 17.f + 0.5f); if (a) A.px[(size_t)(r.y0 * HDS + y) * PW + r.x0 * HDS + x] = ((uint32_t)a << 24) | 0x00ffffffu;
                }
            }
        }
    }
    if (g_dbg) { static uint64_t last; if (g_hits + g_miss >= last + 100) { last = g_hits + g_miss; fprintf(stderr, "[hdfont] retangulos: %llu com glifo HD, %llu sem; glifos guardados %zu; atlas %08x %s\n", (unsigned long long)g_hits, (unsigned long long)g_miss, g_recs.size(), addr, A.qualified ? "qualificado" : "nao"); } }
    if (!A.qualified) return 0;   /* nenhum retangulo casou com um glifo: nao e um atlas de fonte */
    if (A.dirty && A.gpu && bx1 > bx0 && by1 > by0 && gpu_tex_update(A.gpu, bx0, by0, bx1 - bx0, by1 - by0, A.px.data(), (uint32_t)PW)) A.dirty = false;   /* so a area trocada (Vulkan) */
    if (A.dirty || !A.gpu) {
        uint32_t id = gpu_tex_create(PW, H * HDS, A.px.data(), false);
        if (id) { static const bool leak = getenv("TF_HDF_LEAK") != nullptr; if (A.gpu && !leak) gpu_tex_release(A.gpu); A.gpu = id; A.dirty = false; }
    }
    return A.gpu;
}
