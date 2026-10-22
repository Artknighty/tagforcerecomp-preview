/* Previa da caixa e dos paineis dos mods (runtime/ui/modbox.cpp) sem abrir o jogo: desenha um exemplo por cima de uma captura e salva PNG.
 * Compilar: g++ -std=c++17 -O1 -municode -Iruntime -o gen/modbox_preview.exe tools/mods/modbox_preview.cpp runtime/ui/modbox.cpp runtime/ui/dark_ui.cpp -lgdiplus -lgdi32 -lole32 -ldwmapi
 * Uso:      gen/modbox_preview.exe fundo.png saida.png [cantos: 0..3, ex. 1 ou 01] [caixa: 1]
 * O fundo (captura do jogo em qualquer tamanho) e esticado para 1920x1088; o exemplo e o painel dos Cheats. */
#include "modbox.h"
#include "gpu.h"
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <stdio.h>
#include <string.h>
namespace G = Gdiplus;

static int encoder_png(CLSID *id) {
    UINT n = 0, sz = 0; G::GetImageEncodersSize(&n, &sz); if (!sz) return 0;
    std::vector<uint8_t> b(sz); auto *e = (G::ImageCodecInfo *)b.data(); G::GetImageEncoders(n, sz, e);
    for (UINT i = 0; i < n; i++) if (!wcscmp(e[i].MimeType, L"image/png")) { *id = e[i].Clsid; return 1; }
    return 0;
}
int wmain(int argc, wchar_t **argv) {
    if (argc < 3) { fprintf(stderr, "uso: modbox_preview fundo.png saida.png [cantos] [caixa]\n"); return 1; }
    ULONG_PTR tok; G::GdiplusStartupInput in; G::GdiplusStartup(&tok, &in, nullptr);
    const wchar_t *cantos = argc > 3 ? argv[3] : L"1"; const bool caixa = argc > 4 && argv[4][0] == L'1';
    for (const wchar_t *c = cantos; *c; c++) {
        const int k = *c - L'0';
        std::vector<std::wstring> l = {
            L"#Hand\t5",
            L"@ed" L"Drillago\t*4\tATK|1600\tDEF|1100",
            L"@nl" L"Blue-Eyes White Dragon\t*8\tATK|3000\tDEF|2500",
            L"@ss" L"Dark Hole\tSpell",
            L"@ss" L"Mystical Space Typhoon\tQuick-Play Spell",
            L"@tt" L"Sakuretsu Armor\tTrap",
            L"#Face-down\t3",
            L"@fv{M2}" L"Elemental HERO Avian\t*3\tATK|1000\tDEF|1000",
            L"@rw{M4}" L"Ritual Beast\t*7\tATK|?\tDEF|800",
            L"@tt{S3}" L"Mirror Force\tTrap",
        };
        if (k == 0 || k == 2) l = {L"#Hand\t1", L"@ss" L"Pot of Greed\tSpell", L"#Face-down\t0"};
        modbox_panel(0, k, k == 0 || k == 2 ? L"@eCPU 1\tLP 4000" : L"@eOpponent\tLP 8000", l);
    }
    if (caixa) modbox_show(0, L"Multiplicador de DP", {L"Ganho de DP nos duelos: 3x", L"=3/10"}, L"L + R + Cima / Baixo ajusta");
    /* o desenho roda numa thread: chama ate a imagem nova chegar (como o display faz a cada quadro) e mede quanto levou */
    auto esperar = [](double &ms, double &pior_chamada) {
        LARGE_INTEGER t0, t1, fq; QueryPerformanceFrequency(&fq); QueryPerformanceCounter(&t0); const uint64_t v = modbox_version(); const uint32_t *p;
        pior_chamada = 0;
        do { LARGE_INTEGER a, b; QueryPerformanceCounter(&a); p = modbox_pixels(0); QueryPerformanceCounter(&b);
             pior_chamada = std::max(pior_chamada, (b.QuadPart - a.QuadPart) * 1e3 / fq.QuadPart); if (modbox_version() == v) Sleep(1); } while (modbox_version() == v);
        QueryPerformanceCounter(&t1); ms = (t1.QuadPart - t0.QuadPart) * 1e3 / fq.QuadPart; return p;
    };
    double ms1, ms2, c1, c2; const uint32_t *ov = esperar(ms1, c1);
    if (caixa) {   /* 2o desenho (GDI+ ja iniciado) */
        modbox_show(0, L"Multiplicador de DP", {L"Ganho de DP nos duelos: 3x", L"=3/10"}, L"L + R + Cima / Baixo ajusta ");
        ov = esperar(ms2, c2);
        printf("desenho: %.1f ms (1o, inclui iniciar o GDI+), %.1f ms (redesenho); chamada mais lenta da thread do jogo: %.2f ms\n", ms1, ms2, std::max(c1, c2));
    } else printf("desenho: %.1f ms; chamada mais lenta da thread do jogo: %.2f ms\n", ms1, c1);
    G::Bitmap fundo(argv[1]); G::Bitmap out(GPU_OVW, GPU_OVH, PixelFormat32bppARGB);
    { G::Graphics g(&out); g.SetInterpolationMode(G::InterpolationModeHighQualityBicubic); g.DrawImage(&fundo, 0, 0, GPU_OVW, GPU_OVH); }
    G::Rect r(0, 0, GPU_OVW, GPU_OVH); G::BitmapData bd; out.LockBits(&r, G::ImageLockModeRead | G::ImageLockModeWrite, PixelFormat32bppARGB, &bd);
    for (int y = 0; ov && y < GPU_OVH; y++) {   /* mesmo calculo do compositor (alfa reto) */
        uint32_t *d = (uint32_t *)((uint8_t *)bd.Scan0 + (ptrdiff_t)y * bd.Stride);
        for (int x = 0; x < GPU_OVW; x++) {
            const uint32_t o = ov[(size_t)y * GPU_OVW + x], a = o >> 24; if (!a) continue;
            const uint32_t s[3] = {(o >> 16) & 255, (o >> 8) & 255, o & 255};   /* B, G, R */
            uint32_t v = 0xff000000;
            for (int k = 0; k < 3; k++) { const uint32_t dv = (d[x] >> (8 * k)) & 255; v |= ((s[k] * a + dv * (255 - a)) / 255) << (8 * k); }
            d[x] = v;
        }
    }
    out.UnlockBits(&bd);
    CLSID png; if (!encoder_png(&png) || out.Save(argv[2], &png, nullptr) != G::Ok) { fprintf(stderr, "falha ao salvar\n"); return 1; }
    return 0;
}
