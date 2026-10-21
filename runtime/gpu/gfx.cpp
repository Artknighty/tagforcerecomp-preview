#include "gfx.h"
#include "raster_pool.h"
#include "util.h"
#include <vector>
#include <stdio.h>
#include <stdlib.h>
#include <cmath>
#include <string>
#include <algorithm>

GfxSettings g_gfx = {GFX_FILTER_SHARP, GFX_ASPECT_ORIGINAL, false, 1, GFX_RENDER_D3D11, 1, 1, 1, 0};   /* GPU por padrao; sem D3D11 volta ao software */

static std::wstring ini_path() { return app_ini_path(); }
int gfx_res_monitor(void) {
    const int h = GetSystemMetrics(SM_CYSCREEN);
    for (int f : {1, 2, 3, 4, 6, 8}) if (272 * f >= h - 8) return f;   /* 720 -> 3 (816), 1080 -> 4 (1088), 1440 -> 6 (1632), 2160 -> 8 (2176) */
    return 8;
}
void gfx_load(void) {
    std::wstring ini = ini_path();
    g_gfx.filter = (int)GetPrivateProfileIntW(L"graficos", L"filtro", g_gfx.filter, ini.c_str());
    g_gfx.aspect = (int)GetPrivateProfileIntW(L"graficos", L"proporcao", g_gfx.aspect, ini.c_str());
    if (getenv("GFX_DEBUG")) fprintf(stderr, "[gfx] ini %ls -> filtro %d\n", ini.c_str(), g_gfx.filter);
    {   /* resolucao=0 (ou sem a chave) = a do monitor; os fatores antigos sem nome de resolucao de PC (2x, 5x, 7x) sobem para o seguinte */
        const int rv = (int)GetPrivateProfileIntW(L"graficos", L"resolucao", 0, ini.c_str());
        g_gfx.res_auto = rv <= 0; g_gfx.res = g_gfx.res_auto ? gfx_res_monitor() : rv == 2 ? 3 : rv == 5 ? 6 : rv == 7 ? 8 : rv;
    }
    if (getenv("TF_RES")) { g_gfx.res = atoi(getenv("TF_RES")); g_gfx.res_auto = false; }
    g_gfx.renderer = (int)GetPrivateProfileIntW(L"graficos", L"renderizador", g_gfx.renderer, ini.c_str());
    if (getenv("TF_GPU")) g_gfx.renderer = atoi(getenv("TF_GPU"));   /* testes: TF_GPU=0 software, 1 Direct3D 11, 2 Vulkan */
    if (g_gfx.renderer < 0 || g_gfx.renderer > 2) g_gfx.renderer = GFX_RENDER_SOFTWARE;
    if (g_gfx.res < 1 || g_gfx.res > 8) g_gfx.res = 1;
    g_gfx.msaa = (int)GetPrivateProfileIntW(L"graficos", L"msaa", g_gfx.msaa, ini.c_str());
    if (getenv("TF_MSAA")) g_gfx.msaa = atoi(getenv("TF_MSAA"));
    if (g_gfx.msaa != 2 && g_gfx.msaa != 4 && g_gfx.msaa != 8) g_gfx.msaa = 1;
    g_gfx.texscale = getenv("TF_TEXSCALE") ? atoi(getenv("TF_TEXSCALE")) : 1;   /* o ajuste saiu do menu (o pacote Enhanced cobre as texturas); so por teste, via variavel */
    if (g_gfx.texscale != 2 && g_gfx.texscale != 4) g_gfx.texscale = 1;
    g_gfx.fps = (int)GetPrivateProfileIntW(L"graficos", L"fps60", g_gfx.fps, ini.c_str());   /* a chave continua fps60: 0 = 30, 1 = 60, 2 = 120 */
    if (const char *e = getenv("TF_FPS")) g_gfx.fps = atoi(e) >= 120 ? GFX_FPS_120 : atoi(e) >= 60 ? GFX_FPS_60 : GFX_FPS_30;   /* testes: TF_FPS=30/60/120 */
    if (g_gfx.fps < GFX_FPS_30 || g_gfx.fps > GFX_FPS_120) g_gfx.fps = GFX_FPS_60;
    g_gfx.sync = (int)GetPrivateProfileIntW(L"graficos", L"sincronizacao", g_gfx.sync, ini.c_str());
    if (getenv("TF_SYNC")) g_gfx.sync = atoi(getenv("TF_SYNC"));   /* testes: 0 padrao, 1 vsync, 2 menor latencia */
    if (g_gfx.sync < 0 || g_gfx.sync > 2) g_gfx.sync = 0;
    if (g_gfx.filter < 0 || g_gfx.filter > 2) g_gfx.filter = GFX_FILTER_SHARP;
    if (g_gfx.aspect < 0 || g_gfx.aspect > 2) g_gfx.aspect = GFX_ASPECT_ORIGINAL;
}
/* modo de janela: [graficos] tela = 0 janela, 1 tela cheia (lancador, menu Graphics e F11 gravam a mesma chave) */
bool gfx_ini_fullscreen(void) { return GetPrivateProfileIntW(L"graficos", L"tela", 0, ini_path().c_str()) == 1; }
void gfx_ini_set_fullscreen(bool fs) { WritePrivateProfileStringW(L"graficos", L"tela", fs ? L"1" : L"0", ini_path().c_str()); }
void gfx_save(void) {
    std::wstring ini = ini_path(); wchar_t b[16];
    swprintf(b, 16, L"%d", g_gfx.filter); WritePrivateProfileStringW(L"graficos", L"filtro", b, ini.c_str());
    swprintf(b, 16, L"%d", g_gfx.aspect); WritePrivateProfileStringW(L"graficos", L"proporcao", b, ini.c_str());
    swprintf(b, 16, L"%d", g_gfx.res_auto ? 0 : g_gfx.res); WritePrivateProfileStringW(L"graficos", L"resolucao", b, ini.c_str());
    swprintf(b, 16, L"%d", g_gfx.msaa); WritePrivateProfileStringW(L"graficos", L"msaa", b, ini.c_str());
    swprintf(b, 16, L"%d", g_gfx.renderer); WritePrivateProfileStringW(L"graficos", L"renderizador", b, ini.c_str());
    swprintf(b, 16, L"%d", g_gfx.fps); WritePrivateProfileStringW(L"graficos", L"fps60", b, ini.c_str());
    swprintf(b, 16, L"%d", g_gfx.sync); WritePrivateProfileStringW(L"graficos", L"sincronizacao", b, ini.c_str());
}

/* interpolacao bilinear separavel: para cada coluna/linha de destino, o par de origem e o peso (0..256) */
struct Axis { std::vector<int> i0, i1, t; };
static void make_axis(Axis &a, int dst, int src, int filter) {
    a.i0.resize(dst); a.i1.resize(dst); a.t.resize(dst);
    double k = (double)dst / src;   /* pixels de destino por pixel de origem */
    for (int d = 0; d < dst; d++) {
        double s = (d + 0.5) / k - 0.5;
        int i = (int)std::floor(s); double f = s - i;
        if (filter == GFX_FILTER_SHARP) f = std::min(1.0, std::max(0.0, (f - 0.5) * std::max(1.0, k) + 0.5));   /* transicao de ~1 pixel de destino */
        a.i0[d] = std::min(src - 1, std::max(0, i)); a.i1[d] = std::min(src - 1, std::max(0, i + 1)); a.t[d] = (int)(f * 256 + 0.5);
    }
}
struct ScaleCtx { const uint32_t *src; uint32_t *dst; int w, sw; const Axis *ax, *ay; };
static void scale_band(void *vc, int y0, int y1) {
    const ScaleCtx &c = *(const ScaleCtx *)vc;
    for (int y = y0; y < y1; y++) {
        const uint32_t *r0 = c.src + (size_t)c.ay->i0[y] * c.sw, *r1 = c.src + (size_t)c.ay->i1[y] * c.sw; int ty = c.ay->t[y];
        uint32_t *o = c.dst + (size_t)y * c.w;
        for (int x = 0; x < c.w; x++) {
            int a = c.ax->i0[x], b = c.ax->i1[x], tx = c.ax->t[x];
            uint32_t p00 = r0[a], p01 = r0[b], p10 = r1[a], p11 = r1[b], out = 0;
            for (int sh = 0; sh < 24; sh += 8) {
                int top = (int)((p00 >> sh) & 255) * (256 - tx) + (int)((p01 >> sh) & 255) * tx;
                int bot = (int)((p10 >> sh) & 255) * (256 - tx) + (int)((p11 >> sh) & 255) * tx;
                out |= (uint32_t)((top * (256 - ty) + bot * ty) >> 16) << sh;
            }
            o[x] = out;
        }
    }
}

/* retangulo da imagem do jogo dentro da area cw x ch (mesma conta de gfx_paint e dos apresentadores da GPU): usado para levar o mouse a 480x272 */
void gfx_dest_rect(int cw, int ch, int *ox, int *oy, int *ow, int *oh) {
    int w, h;
    if (g_gfx.aspect == GFX_ASPECT_STRETCH) { w = cw; h = ch; }
    else {
        int an = g_gfx.aspect == GFX_ASPECT_4_3 ? 4 : 480, ad = g_gfx.aspect == GFX_ASPECT_4_3 ? 3 : 272;
        w = cw; h = (int)((long long)cw * ad / an); if (h > ch) { h = ch; w = (int)((long long)ch * an / ad); }
        if (g_gfx.integer) { int n = std::min(cw / 480, ch / 272); if (n >= 1) { w = 480 * n; h = 272 * n; if (g_gfx.aspect == GFX_ASPECT_4_3) { w = h * 4 / 3; if (w > cw) w = cw; } } }
    }
    *ox = (cw - w) / 2; *oy = (ch - h) / 2; *ow = w; *oh = h;
}

void gfx_paint(HDC dc, const uint32_t *pix, int sw, int sh, int cw, int ch) {
    if (cw <= 0 || ch <= 0) return;
    static bool once; if (!once && getenv("GFX_DEBUG")) { once = true; fprintf(stderr, "[gfx] filtro %d proporcao %d inteira %d area %dx%d\n", g_gfx.filter, g_gfx.aspect, g_gfx.integer, cw, ch); }
    int w, h;
    if (g_gfx.aspect == GFX_ASPECT_STRETCH) { w = cw; h = ch; }
    else {
        int an = g_gfx.aspect == GFX_ASPECT_4_3 ? 4 : 480, ad = g_gfx.aspect == GFX_ASPECT_4_3 ? 3 : 272;   /* largura:altura da imagem exibida */
        w = cw; h = (int)((long long)cw * ad / an); if (h > ch) { h = ch; w = (int)((long long)ch * an / ad); }
        if (g_gfx.integer) {   /* multiplo inteiro do tamanho original, se couber */
            int n = std::min(cw / 480, ch / 272);
            if (n >= 1) {
                w = 480 * n; h = 272 * n;
                if (g_gfx.aspect == GFX_ASPECT_4_3) { w = h * 4 / 3; if (w > cw) w = cw; }
            }
        }
    }
    int x = (cw - w) / 2, y = (ch - h) / 2;
    HBRUSH blk = (HBRUSH)GetStockObject(BLACK_BRUSH);
    if (x > 0) { RECT a = {0, 0, x, ch}, b = {x + w, 0, cw, ch}; FillRect(dc, &a, blk); FillRect(dc, &b, blk); }
    if (y > 0) { RECT a = {0, 0, cw, y}, b = {0, y + h, cw, ch}; FillRect(dc, &a, blk); FillRect(dc, &b, blk); }
    BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    if (g_gfx.filter == GFX_FILTER_NEAREST || (w == sw && h == sh)) {
        bi.bmiHeader.biWidth = sw; bi.bmiHeader.biHeight = -sh;
        SetStretchBltMode(dc, COLORONCOLOR);
        StretchDIBits(dc, x, y, w, h, 0, 0, sw, sh, pix, &bi, DIB_RGB_COLORS, SRCCOPY);
        return;
    }
    static std::vector<uint32_t> out; static Axis ax, ay; static int lw, lh, lf, lsw, lsh;
    if (lw != w || lh != h || lf != g_gfx.filter || lsw != sw || lsh != sh) { make_axis(ax, w, sw, g_gfx.filter); make_axis(ay, h, sh, g_gfx.filter); lw = w; lh = h; lf = g_gfx.filter; lsw = sw; lsh = sh; }
    out.resize((size_t)w * h);
    ScaleCtx c = {pix, out.data(), w, sw, &ax, &ay};
    raster_parallel(0, h, scale_band, &c);
    bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    SetDIBitsToDevice(dc, x, y, w, h, 0, 0, 0, h, out.data(), &bi, DIB_RGB_COLORS);
}

/* ---- the options as choices, for the in-game Graphics screen (gfx_options.h) ---- */
#include "gfx_options.h"
#include "hdfont.h"
void ge_set_res(int n);
void ge_set_msaa(void);
void ge_set_texscale(void);
void display_request_renderer(int r);
int mods_find(const char *folder);
void mods_set_enabled(int i, bool on);
void texrepl_reload(void);
bool mods_enabled(int i);
bool mods_enabled_has_file(const char *relative);

namespace {
struct OptionInfo { const char *label; const char *help; int count; const char *names[6]; const int *values; };   /* values: what each choice stores (null = the index itself) */
const int kResValues[6] = {1, 3, 4, 6, 8, 0}, kMsaaValues[4] = {1, 2, 4, 8};   /* resolucao: fator interno (0 = do monitor; ver gfx.h) */
const OptionInfo kOptions[GFXOPT_COUNT] = {
    {"Renderer", "Software uses the CPU. Direct3D 11 and Vulkan use the GPU.", 3, {"Software", "Direct3D 11", "Vulkan"}, nullptr},
    {"Resolution", "Screen resolution, as in any PC game. 2560x1440 and 3840x2160 need the GPU.", 6, {"480x272", "1280x720", "1920x1080", "2560x1440", "3840x2160", "Monitor"}, kResValues},
    {"Anti-aliasing (MSAA)", "Smooths jagged edges (GPU only).", 4, {"Off", "2x", "4x", "8x"}, kMsaaValues},
    {"Textures", "Original is the plain PSP look. Enhanced is the AI upscaled package.", 2, {"Original", "Enhanced"}, nullptr},
    {"Frame rate", "60 and 120 FPS are interpolated from the original 30 (GPU). 120 needs a 120 Hz monitor.", 3, {"30 FPS", "60 FPS", "120 FPS"}, nullptr},
    {"Synchronization", "Default avoids tearing. VSync waits for the monitor.", 3, {"Default", "VSync", "Lowest latency"}, nullptr},
    {"Filter", "How the 480 x 272 image is scaled to the window.", 3, {"Sharp pixels", "Smoothed", "Bilinear"}, nullptr},
    {"Aspect ratio", "Proportion of the image: original 16:9, stretched or 4:3.", 3, {"16:9", "Stretch", "4:3"}, nullptr},
};
int &stored(int option, int &scratch) {   /* the int field of g_gfx behind an option (integer is a bool: goes through scratch) */
    switch (option) {
    case GFXOPT_RENDERER: return g_gfx.renderer;
    case GFXOPT_RESOLUTION: scratch = g_gfx.res_auto ? 0 : g_gfx.res; return scratch;
    case GFXOPT_MSAA: return g_gfx.msaa;
    case GFXOPT_FRAME_RATE: return g_gfx.fps;
    case GFXOPT_SYNC: return g_gfx.sync;
    case GFXOPT_FILTER: return g_gfx.filter;
    case GFXOPT_ASPECT: return g_gfx.aspect;
    default: scratch = 0; return scratch;
    }
}
}  // namespace

const char *gfx_option_help(int option) { return option >= 0 && option < GFXOPT_COUNT ? kOptions[option].help : ""; }
const char *gfx_option_label(int option) { return option >= 0 && option < GFXOPT_COUNT ? kOptions[option].label : ""; }
int gfx_option_choice_count(int option) { return option >= 0 && option < GFXOPT_COUNT ? kOptions[option].count : 0; }
const char *gfx_option_choice_name(int option, int choice) {
    if (option < 0 || option >= GFXOPT_COUNT || choice < 0 || choice >= kOptions[option].count) return "";
    return kOptions[option].names[choice];
}
namespace {
int g_rawPsp = -1;        /* same idea, for gfx_raw_psp() */
int g_modernMenus = -1;   /* -1 = look again (at the start and after the texture package changes) */
}
bool gfx_modern_menus() {
    if (g_modernMenus < 0) {
        const char *e = getenv("TF_MODERN_MENUS");
        g_modernMenus = e && *e ? atoi(e) != 0 : mods_enabled_has_file("menu_estilo_pc.txt");
    }
    return g_modernMenus != 0;
}
bool gfx_raw_psp() {   /* no texture package on: the plain PSP picture, so the HD font and the texture upscaling stay off whatever the saved options say */
    if (g_rawPsp < 0) {
        bool pack = false;
        for (const char *f : {"EnhancedTextures", "TexturasPC"}) { const int i = mods_find(f); if (i >= 0 && mods_enabled(i)) pack = true; }
        g_rawPsp = pack ? 0 : 1;
    }
    return g_rawPsp != 0;
}
int gfx_texscale() { return gfx_raw_psp() ? 1 : g_gfx.texscale; }
int gfx_option_get(int option) {
    if (option < 0 || option >= GFXOPT_COUNT) return 0;
    if (option == GFXOPT_TEXTURE_PACK) { const int e = mods_find("EnhancedTextures"); return e >= 0 && mods_enabled(e) ? 1 : 0; }
    int scratch = 0;
    const int value = stored(option, scratch);
    const OptionInfo &o = kOptions[option];
    if (!o.values) return value < o.count ? value : 0;
    for (int i = 0; i < o.count; i++) if (o.values[i] == value) return i;
    return 0;
}
void gfx_option_set(int option, int choice) {
    if (option < 0 || option >= GFXOPT_COUNT || choice < 0 || choice >= kOptions[option].count || choice == gfx_option_get(option)) return;
    const OptionInfo &o = kOptions[option];
    const int value = o.values ? o.values[choice] : choice;
    switch (option) {
    case GFXOPT_TEXTURE_PACK: {   /* one package or the other (both would load every texture twice); applied right away, the packages only hold textures and the opening movie */
        const int enhanced = mods_find("EnhancedTextures"), original = mods_find("TexturasPC");
        if (enhanced >= 0) mods_set_enabled(enhanced, value == 1);
        if (original >= 0) mods_set_enabled(original, false);
        hdfont_reset();        /* the font atlas on the GPU belongs to the old look */
        texrepl_reload();      /* the index of replacement textures, from the package now on */
        ge_set_texscale();     /* the GPU forgets the textures it made from the old package: each one is looked up again when next drawn */
        g_rawPsp = -1;         /* Original = no package at all (raw PSP look, own font and cards) */
        g_modernMenus = -1;    /* the PC-style menus come with the Enhanced package (marker file) */
        break;
    }
    case GFXOPT_RENDERER: display_request_renderer(value); break;   /* applied between two frames; saves by itself, and falls back to another renderer if this one does not start */
    case GFXOPT_RESOLUTION: g_gfx.res_auto = value == 0; g_gfx.res = value ? value : gfx_res_monitor(); ge_set_res(g_gfx.res); gfx_save(); break;
    case GFXOPT_MSAA: g_gfx.msaa = value; ge_set_msaa(); gfx_save(); break;
    case GFXOPT_FRAME_RATE: g_gfx.fps = value; gfx_save(); break;
    case GFXOPT_SYNC: g_gfx.sync = value; gfx_save(); break;
    case GFXOPT_FILTER: g_gfx.filter = value; gfx_save(); break;
    case GFXOPT_ASPECT: g_gfx.aspect = value; gfx_save(); break;
    }
}
