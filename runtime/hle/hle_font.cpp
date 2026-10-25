/* HLE: sceLibFont. Os PGF do firmware nao estao no ISO; os glifos vem das fontes do Windows (GDI) em tempo de execucao. */
#include "hle.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <windows.h>
#include <vector>
#include <map>
#include "hdfont.h"

/* Metricas derivadas da fonte do Windows (init_gdi). O jogo desenha cada glifo numa celula de 32x32 com a linha de base
 * em 8 + ASC, entao ASC + DESC precisa caber em 24. Tamanho/peso calibrados contra capturas do PPSSPP (menu principal):
 * FONT_PX (altura em pixels do em) e FONT_W (peso GDI) permitem ajustar sem recompilar. */
static int CELL = 20, ASC = 16, DESC = 4;

/* Os numeros da fonte, com nome (cada um continua ajustavel por variavel de ambiente, sem recompilar). */
constexpr int kNativeFontPx = 21;          /* Segoe UI Semibold de 21 px imita a NewRodin DB do PSP (calibrado contra o PPSSPP) */
constexpr int kGameGlyphCellRows = 24;     /* o jogo desenha cada glifo numa celula 32x32 com a linha de base em 8 + ASC: ASC + DESC cabe em 24 */
constexpr int kHdFontShrinkPx = 3;         /* a fonte HD e 3 px menor que a nativa (18 px): descendentes (g, y, p...) batiam no limite do retangulo do atlas */
constexpr float kCapitalCentreEm = 0.35f;  /* o centro de uma maiuscula fica ~0,35 em acima da linha de base */
constexpr uint32_t kCjkFirstCode = 0x2e80; /* daqui para cima o glifo vem da fonte japonesa (MS Gothic) */
static int env_int(const char *name, int fallback) { const char *v = getenv(name); return v ? atoi(v) : fallback; }
static HDC g_dc; static HFONT g_latin, g_cjk;
static uint32_t g_uid = 0x7000;

static void init_gdi() {
    if (g_dc) return;
    g_dc = CreateCompatibleDC(nullptr);
    int px = env_int("FONT_PX", kNativeFontPx), wt = env_int("FONT_W", FW_SEMIBOLD);
    wchar_t face[64] = L"Segoe UI";   /* NewRodin DB do PSP ~ Segoe UI Semibold 21px (calibrado vs PPSSPP) */
    if (const char *ff = getenv("FONT_FACE")) MultiByteToWideChar(CP_UTF8, 0, ff, -1, face, 64);
    g_latin = CreateFontW(-px, 0, 0, 0, wt, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, face);
    g_cjk = CreateFontW(-px, 0, 0, 0, wt, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"MS Gothic");
    SelectObject(g_dc, g_latin);
    TEXTMETRICW tm; if (GetTextMetricsW(g_dc, &tm)) {
        ASC = tm.tmAscent - tm.tmInternalLeading + 1; DESC = tm.tmDescent;
        if (ASC + DESC > kGameGlyphCellRows) ASC = kGameGlyphCellRows - DESC;
        CELL = ASC + DESC;
    }
}
struct Glyph { int w = 0, h = 0, left = 0, top = 0, adv = 0; std::vector<uint8_t> a8; };   /* a8: cobertura 0..255, w*h */
static std::map<uint32_t, Glyph> g_cache;
static Glyph &glyph(uint32_t code) {
    auto it = g_cache.find(code); if (it != g_cache.end()) return it->second;
    init_gdi(); Glyph &g = g_cache[code];
    SelectObject(g_dc, code >= kCjkFirstCode ? g_cjk : g_latin);
    GLYPHMETRICS gm; MAT2 m = {{0, 1}, {0, 0}, {0, 0}, {0, 1}};
    DWORD sz = GetGlyphOutlineW(g_dc, code, GGO_GRAY8_BITMAP, &gm, 0, nullptr, &m);
    g.adv = gm.gmCellIncX ? gm.gmCellIncX : CELL / 2;
    if (sz == GDI_ERROR || sz == 0) return g;
    std::vector<uint8_t> raw(sz);
    if (GetGlyphOutlineW(g_dc, code, GGO_GRAY8_BITMAP, &gm, sz, raw.data(), &m) == GDI_ERROR) return g;
    g.w = gm.gmBlackBoxX; g.h = gm.gmBlackBoxY; g.left = gm.gmptGlyphOrigin.x; g.top = gm.gmptGlyphOrigin.y;
    uint32_t pitch = (g.w + 3) & ~3u; g.a8.assign(g.w * g.h, 0);
    for (int y = 0; y < g.h; y++) for (int x = 0; x < g.w; x++) { uint32_t v = raw[y * pitch + x]; g.a8[y * g.w + x] = (uint8_t)std::min(255u, v * 255 / 64); }
    return g;
}

/* O mesmo glifo desenhado em HDS x o tamanho (fonte em alta resolucao, ver hdfont.h). FONT_HD_FACE troca so a fonte HD (padrao: a mesma do nativo). */
static HFONT g_latin_hd, g_cjk_hd;
constexpr float kDigitX = 1.32f, kDigitY = 0.92f;   /* digit glyph scale (width, height) */
static int g_hd_lift;   /* HD px que o glifo sobe: encolher ancorado na linha de base deixaria o texto colado embaixo; mantem o centro das maiusculas (~0,35 em) no mesmo lugar */
bool font_hd_glyph(uint32_t code, int *w, int *h, int *top, std::vector<uint8_t> *a8) {
    init_gdi();
    if (!g_latin_hd) {
        int px = env_int("FONT_PX", kNativeFontPx), wt = env_int("FONT_HD_W", env_int("FONT_W", FW_BOLD));   /* o traco do glifo de 84 px parece mais fino que o hintado de 21 px: peso maior */
        wchar_t face[64] = L"Segoe UI";
        if (const char *ff = getenv("FONT_HD_FACE")) MultiByteToWideChar(CP_UTF8, 0, ff, -1, face, 64);
        else if (const char *ff2 = getenv("FONT_FACE")) MultiByteToWideChar(CP_UTF8, 0, ff2, -1, face, 64);
        const int shrink = env_int("FONT_HD_SHRINK", kHdFontShrinkPx);
        px -= shrink; g_hd_lift = (int)(kCapitalCentreEm * shrink * HDS + 0.5f);
        g_latin_hd = CreateFontW(-px * HDS, 0, 0, 0, wt, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, face);
        g_cjk_hd = CreateFontW(-px * HDS, 0, 0, 0, wt, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"MS Gothic");
    }
    HFONT old = (HFONT)SelectObject(g_dc, code >= kCjkFirstCode ? g_cjk_hd : g_latin_hd);
    GLYPHMETRICS gm; MAT2 m = {{0, 1}, {0, 0}, {0, 0}, {0, 1}};
    DWORD sz = GetGlyphOutlineW(g_dc, code, GGO_GRAY8_BITMAP, &gm, 0, nullptr, &m);
    bool ok = false;
    if (sz != GDI_ERROR && sz != 0) {
        std::vector<uint8_t> raw(sz);
        if (GetGlyphOutlineW(g_dc, code, GGO_GRAY8_BITMAP, &gm, sz, raw.data(), &m) != GDI_ERROR) {
            *w = gm.gmBlackBoxX; *h = gm.gmBlackBoxY; *top = gm.gmptGlyphOrigin.y + g_hd_lift; uint32_t pitch = (*w + 3) & ~3u; a8->assign((size_t)*w * *h, 0);
            for (int y = 0; y < *h; y++) for (int x = 0; x < *w; x++) (*a8)[(size_t)y * *w + x] = (uint8_t)std::min(255u, raw[y * pitch + x] * 255u / 64u);
            if (code >= '0' && code <= '9') {   /* the native digits fill their 8 px cell; Segoe's are narrower and leave gaps in tabular counters ("4 2"): widen them, and shrink a little so counters sit inside their boxes */
                int nw = (int)(*w * kDigitX + 0.5f), nh = (int)(*h * kDigitY + 0.5f); std::vector<uint8_t> src = *a8; a8->assign((size_t)nw * nh, 0);
                for (int y = 0; y < nh; y++) {
                    float fy = (y + 0.5f) / kDigitY - 0.5f; int y0 = (int)floorf(fy); float ty = fy - y0; int ya = std::max(0, y0), yb = std::min(*h - 1, y0 + 1);
                    for (int x = 0; x < nw; x++) {
                        float fx = (x + 0.5f) / kDigitX - 0.5f; int x0 = (int)floorf(fx); float tx = fx - x0; int xa = std::max(0, x0), xb = std::min(*w - 1, x0 + 1);
                        float v = (src[(size_t)ya * *w + xa] * (1 - tx) + src[(size_t)ya * *w + xb] * tx) * (1 - ty) + (src[(size_t)yb * *w + xa] * (1 - tx) + src[(size_t)yb * *w + xb] * tx) * ty;
                        (*a8)[(size_t)y * nw + x] = (uint8_t)(v + 0.5f);
                    }
                }
                *w = nw; *h = nh; *top = (int)(*top * kDigitY + 0.5f);
            }
            ok = true;
        }
    }
    SelectObject(g_dc, old);
    return ok;
}

HLE(sceFontNewLib) { if (ARG(1)) wr32(ARG(1), 0); RET(g_uid++); }
HLE(sceFontFindOptimumFont) {   /* (lib, FontStyle*, erro*): FontStyle = fontH, fontV, fontHRes, fontVRes, peso (floats), familia... */
    uint32_t st = ARG(1);
    static const bool font_trace = getenv("FONT_TRACE") != nullptr;
    if (st && font_trace) {
        float f[5]; for (int i = 0; i < 5; i++) { uint32_t u = rd32(st + i * 4); memcpy(&f[i], &u, 4); }
        fprintf(stderr, "[font] estilo pedido: H=%.2f V=%.2f resH=%.1f resV=%.1f peso=%.1f familia=%u estilo=%u\n",
                f[0], f[1], f[2], f[3], f[4], *(uint16_t *)mem_ptr(st + 20), *(uint16_t *)mem_ptr(st + 22));
    }
    if (ARG(2)) wr32(ARG(2), 0); RET(0);
}
HLE(sceFontOpen) { if (ARG(3)) wr32(ARG(3), 0); RET(g_uid++); }
HLE(sceFontSetAltCharacterCode) { RET(0); }
HLE(sceFontGetFontInfo) {   /* (font, FontInfo*): escreve so os campos de metricas, sem estourar o buffer do jogo */
    init_gdi(); uint32_t p = ARG(1);
    int32_t I[10] = {CELL << 6, CELL << 6, ASC << 6, -(DESC << 6), 0, ASC << 6, (CELL / 2) << 6, ASC << 6, CELL << 6, CELL << 6};
    for (int i = 0; i < 10; i++) { wr32(p + i * 4, (uint32_t)I[i]); float f = I[i] / 64.f; uint32_t u; memcpy(&u, &f, 4); wr32(p + 0x28 + i * 4, u); }
    *(uint16_t *)mem_ptr(p + 0x50) = CELL; *(uint16_t *)mem_ptr(p + 0x52) = CELL;
    *(uint16_t *)mem_ptr(p + 0x54) = 0xffff; *(uint16_t *)mem_ptr(p + 0x56) = 0;
    float fh = 12.f, fr = 128.f; uint32_t u;
    memcpy(&u, &fh, 4); wr32(p + 0x58, u); wr32(p + 0x5c, u); memcpy(&u, &fr, 4); wr32(p + 0x60, u); wr32(p + 0x64, u);
    RET(0);
}
HLE(sceFontGetCharInfo) {   /* (font, charCode, CharInfo*) */
    Glyph &g = glyph(ARG(1)); uint32_t p = ARG(2);
    wr32(p + 0x00, g.w); wr32(p + 0x04, g.h); wr32(p + 0x08, (uint32_t)g.left); wr32(p + 0x0c, (uint32_t)g.top);
    wr32(p + 0x10, g.w << 6); wr32(p + 0x14, g.h << 6); wr32(p + 0x18, g.top << 6); wr32(p + 0x1c, (uint32_t)-((g.h - g.top) << 6));
    wr32(p + 0x20, g.left << 6); wr32(p + 0x24, g.top << 6); wr32(p + 0x28, 0); wr32(p + 0x2c, 0);
    wr32(p + 0x30, g.adv << 6); wr32(p + 0x34, CELL << 6); wr32(p + 0x38, 0);
    RET(0);
}
HLE(sceFontGetCharGlyphImage) {   /* (font, charCode, GlyphImage*) */
    Glyph &g = glyph(ARG(1)); uint32_t p = ARG(2);
    uint32_t fmt = rd32(p); int32_t xp = (int32_t)rd32(p + 4) >> 6, yp = (int32_t)rd32(p + 8) >> 6;
    uint32_t bw = *(uint16_t *)mem_ptr(p + 0xc), bh = *(uint16_t *)mem_ptr(p + 0xe), bpl = *(uint16_t *)mem_ptr(p + 0x10), buf = rd32(p + 0x14);
    { static int n; static const bool ft = getenv("FONT_TRACE") != nullptr; if (ft && n++ < 80) fprintf(stderr, "[font] glifo U+%04x fmt=%u pos=(%d,%d) buf=%08x bw=%u bh=%u bpl=%u glifo %dx%d left=%d top=%d adv=%d\n", ARG(1), fmt, xp, yp, buf, bw, bh, bpl, g.w, g.h, g.left, g.top, g.adv); }
    for (int y = 0; y < g.h; y++) for (int x = 0; x < g.w; x++) {
        /* (xPos, yPos) ja e o canto superior esquerdo do bitmap: o jogo soma bitmapLeft/bitmapTop do CharInfo */
        int dx = xp + x, dy = yp + y;
        if (dx < 0 || dy < 0 || (uint32_t)dx >= bw || (uint32_t)dy >= bh) continue;
        uint8_t a = g.a8[y * g.w + x];
        switch (fmt) {
        case 0: case 1: {   /* 4 bits: 0 = nibble baixo primeiro, 1 = nibble alto primeiro */
            uint8_t *d = mem_ptr(buf + dy * bpl + dx / 2); uint8_t v = a >> 4; bool lo = (fmt == 0) ? (dx & 1) == 0 : (dx & 1) != 0;
            uint8_t cur = lo ? (*d & 15) : (*d >> 4); if (v < cur) v = cur;
            *d = lo ? ((*d & 0xf0) | v) : ((*d & 0x0f) | (v << 4)); break; }
        case 2: { uint8_t *d = mem_ptr(buf + dy * bpl + dx); if (a > *d) *d = a; break; }
        case 3: { uint8_t *d = mem_ptr(buf + dy * bpl + dx * 3); d[0] = d[1] = d[2] = a; break; }
        default: { uint32_t *d = (uint32_t *)mem_ptr(buf + dy * bpl + dx * 4); *d = ((uint32_t)a << 24) | 0x00ffffffu; break; }
        }
    }
    if (fmt <= 1 && g.w > 0 && g.h > 0) {   /* fonte HD: guarda o glifo em alta resolucao, indexado pelo bitmap 4 bits que o jogo recebeu */
        std::vector<uint8_t> nb((size_t)g.w * g.h);
        for (size_t i = 0; i < nb.size(); i++) nb[i] = g.a8[i] >> 4;
        hdfont_record(ARG(1), xp, yp, g.top, nb.data(), g.w, g.h);
    }
    RET(0);
}
