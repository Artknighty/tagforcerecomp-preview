#include "gfx.h"
#include "raster_pool.h"
#include "util.h"
#include <vector>
#include <stdio.h>
#include <stdlib.h>
#include <cmath>
#include <string>
#include <algorithm>

GfxSettings g_gfx = {GFX_FILTER_SHARP, GFX_ASPECT_ORIGINAL, false, 1, GFX_RENDER_D3D11, 1, 1, 1, 1, 0};   /* GPU por padrao; sem D3D11 volta ao software */

static std::wstring ini_path() { return app_ini_path(); }
void gfx_load(void) {
    std::wstring ini = ini_path();
    g_gfx.filter = (int)GetPrivateProfileIntW(L"graficos", L"filtro", g_gfx.filter, ini.c_str());
    g_gfx.aspect = (int)GetPrivateProfileIntW(L"graficos", L"proporcao", g_gfx.aspect, ini.c_str());
    g_gfx.integer = GetPrivateProfileIntW(L"graficos", L"escala_inteira", g_gfx.integer, ini.c_str()) != 0;
    if (getenv("GFX_DEBUG")) fprintf(stderr, "[gfx] ini %ls -> filtro %d\n", ini.c_str(), g_gfx.filter);
    g_gfx.res = (int)GetPrivateProfileIntW(L"graficos", L"resolucao", g_gfx.res, ini.c_str());
    if (getenv("TF_RES")) g_gfx.res = atoi(getenv("TF_RES"));
    g_gfx.renderer = (int)GetPrivateProfileIntW(L"graficos", L"renderizador", g_gfx.renderer, ini.c_str());
    if (getenv("TF_GPU")) g_gfx.renderer = atoi(getenv("TF_GPU"));   /* testes: TF_GPU=0 software, 1 Direct3D 11, 2 Vulkan */
    if (g_gfx.renderer < 0 || g_gfx.renderer > 2) g_gfx.renderer = GFX_RENDER_SOFTWARE;
    if (g_gfx.res < 1 || g_gfx.res > 8) g_gfx.res = 1;
    g_gfx.msaa = (int)GetPrivateProfileIntW(L"graficos", L"msaa", g_gfx.msaa, ini.c_str());
    if (getenv("TF_MSAA")) g_gfx.msaa = atoi(getenv("TF_MSAA"));
    if (g_gfx.msaa != 2 && g_gfx.msaa != 4 && g_gfx.msaa != 8) g_gfx.msaa = 1;
    g_gfx.texscale = (int)GetPrivateProfileIntW(L"graficos", L"texturas_escala", g_gfx.texscale, ini.c_str());
    if (getenv("TF_TEXSCALE")) g_gfx.texscale = atoi(getenv("TF_TEXSCALE"));
    if (g_gfx.texscale != 2 && g_gfx.texscale != 4) g_gfx.texscale = 1;
    g_gfx.hdfont = GetPrivateProfileIntW(L"graficos", L"fonte_hd", g_gfx.hdfont, ini.c_str()) != 0;
    g_gfx.fps60 = (int)GetPrivateProfileIntW(L"graficos", L"fps60", g_gfx.fps60, ini.c_str());
    g_gfx.fps60 = g_gfx.fps60 != 0;   /* fps60=2 de versoes anteriores = ligado */
    g_gfx.sync = (int)GetPrivateProfileIntW(L"graficos", L"sincronizacao", g_gfx.sync, ini.c_str());
    if (getenv("TF_SYNC")) g_gfx.sync = atoi(getenv("TF_SYNC"));   /* testes: 0 padrao, 1 vsync, 2 menor latencia */
    if (g_gfx.sync < 0 || g_gfx.sync > 2) g_gfx.sync = 0;
    if (g_gfx.filter < 0 || g_gfx.filter > 2) g_gfx.filter = GFX_FILTER_SHARP;
    if (g_gfx.aspect < 0 || g_gfx.aspect > 2) g_gfx.aspect = GFX_ASPECT_ORIGINAL;
}
void gfx_save(void) {
    std::wstring ini = ini_path(); wchar_t b[16];
    swprintf(b, 16, L"%d", g_gfx.filter); WritePrivateProfileStringW(L"graficos", L"filtro", b, ini.c_str());
    swprintf(b, 16, L"%d", g_gfx.aspect); WritePrivateProfileStringW(L"graficos", L"proporcao", b, ini.c_str());
    swprintf(b, 16, L"%d", g_gfx.res); WritePrivateProfileStringW(L"graficos", L"resolucao", b, ini.c_str());
    swprintf(b, 16, L"%d", g_gfx.texscale); WritePrivateProfileStringW(L"graficos", L"texturas_escala", b, ini.c_str());
    swprintf(b, 16, L"%d", g_gfx.msaa); WritePrivateProfileStringW(L"graficos", L"msaa", b, ini.c_str());
    swprintf(b, 16, L"%d", g_gfx.renderer); WritePrivateProfileStringW(L"graficos", L"renderizador", b, ini.c_str());
    swprintf(b, 16, L"%d", g_gfx.fps60); WritePrivateProfileStringW(L"graficos", L"fps60", b, ini.c_str());
    swprintf(b, 16, L"%d", g_gfx.sync); WritePrivateProfileStringW(L"graficos", L"sincronizacao", b, ini.c_str());
    WritePrivateProfileStringW(L"graficos", L"fonte_hd", g_gfx.hdfont ? L"1" : L"0", ini.c_str());
    WritePrivateProfileStringW(L"graficos", L"escala_inteira", g_gfx.integer ? L"1" : L"0", ini.c_str());
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
