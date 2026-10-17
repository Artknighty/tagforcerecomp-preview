/* Gravador do que REALMENTE chega ao monitor (API de duplicacao de tela do DXGI): cada imagem composta pelo Windows, com a hora
 * da ultima apresentacao e quantas apresentacoes se acumularam desde a imagem anterior (AccumulatedFrames > 1 = quadro do jogo que
 * nunca apareceu). Serve para piscadas/engasgos "entre um quadro e outro", que as capturas internas do jogo nao mostram.
 *
 * Compilar (MSYS2 UCRT64): g++ -O2 -o gen/tela_gravar.exe tools/tela_gravar.cpp -ld3d11 -ldxgi
 * Uso: tela_gravar <saida_sem_extensao> <segundos> [y0 y1] [--esperar <arquivo> <texto>] [--so-tempos]
 *   grava <saida>.tsv (indice, ms da captura, ms da apresentacao, acumulados) e <saida>.bin (por imagem: faixa y0..y1 do canal
 *   verde em largura total + miniatura 480x270 RGB por media 4x4). --esperar: so comeca quando <texto> aparece em <arquivo>.
 * --so-tempos: so o .tsv (sem copiar pixels; para gravar uma sessao inteira e medir o ritmo com tools/ritmo_analisar.py).
 * Analise: tools/tela_analisar.py <saida>. Monitor = o principal. */
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <string>
#include <vector>

static double qpc_ms(LONGLONG v) { static LARGE_INTEGER f; if (!f.QuadPart) QueryPerformanceFrequency(&f); return (double)v * 1000.0 / (double)f.QuadPart; }
static double now_ms(void) { LARGE_INTEGER c; QueryPerformanceCounter(&c); return qpc_ms(c.QuadPart); }

static bool file_has(const char *path, const char *text) {
    FILE *f = fopen(path, "rb"); if (!f) return false;
    std::string s; char b[65536]; size_t n; while ((n = fread(b, 1, sizeof b, f)) > 0) s.append(b, n); fclose(f);
    return s.find(text) != std::string::npos;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "uso: tela_gravar <saida> <segundos> [y0 y1] [--esperar arq texto]\n"); return 1; }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const std::string out = argv[1]; const double secs = atof(argv[2]);
    int y0 = 800, y1 = 1000; const char *wait_file = nullptr, *wait_text = nullptr; bool so_tempos = false;
    for (int i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--esperar") && i + 2 < argc) { wait_file = argv[i + 1]; wait_text = argv[i + 2]; i += 2; }
        else if (!strcmp(argv[i], "--so-tempos")) so_tempos = true;
        else if (i + 1 < argc && isdigit((unsigned char)argv[i][0])) { y0 = atoi(argv[i]); y1 = atoi(argv[i + 1]); i++; }
    }
    /* saida principal (a que contem 0,0) e seu adaptador */
    IDXGIFactory1 *fac = nullptr; if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void **)&fac))) { fprintf(stderr, "sem DXGI\n"); return 1; }
    IDXGIAdapter1 *ad = nullptr; IDXGIOutput *op = nullptr; DXGI_OUTPUT_DESC od = {};
    for (UINT a = 0; !op && fac->EnumAdapters1(a, &ad) != DXGI_ERROR_NOT_FOUND; a++) {
        for (UINT o = 0; ad->EnumOutputs(o, &op) != DXGI_ERROR_NOT_FOUND; o++) {
            op->GetDesc(&od);
            if (od.DesktopCoordinates.left == 0 && od.DesktopCoordinates.top == 0) break;
            op->Release(); op = nullptr;
        }
        if (!op) { ad->Release(); ad = nullptr; }
    }
    if (!op) { fprintf(stderr, "monitor principal nao encontrado\n"); return 1; }
    DEVMODEW dm = {}; dm.dmSize = sizeof dm; EnumDisplaySettingsW(od.DeviceName, ENUM_CURRENT_SETTINGS, &dm);
    const int W = od.DesktopCoordinates.right - od.DesktopCoordinates.left, H = od.DesktopCoordinates.bottom - od.DesktopCoordinates.top;
    fprintf(stderr, "[tela] monitor %ls %dx%d a %lu Hz\n", od.DeviceName, W, H, dm.dmDisplayFrequency);
    if (y0 < 0) y0 = 0; if (y1 > H) y1 = H; if (y1 <= y0) { y0 = 0; y1 = H; }
    ID3D11Device *dev = nullptr; ID3D11DeviceContext *ctx = nullptr; D3D_FEATURE_LEVEL fl;
    if (FAILED(D3D11CreateDevice(ad, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev, &fl, &ctx))) { fprintf(stderr, "sem D3D11\n"); return 1; }
    IDXGIOutput1 *op1 = nullptr; op->QueryInterface(__uuidof(IDXGIOutput1), (void **)&op1);
    IDXGIOutputDuplication *dup = nullptr;
    HRESULT hr = op1->DuplicateOutput(dev, &dup);
    if (FAILED(hr)) { fprintf(stderr, "DuplicateOutput falhou: %08lx\n", (unsigned long)hr); return 1; }
    if (wait_file) {
        fprintf(stderr, "[tela] esperando '%s' em %s\n", wait_text, wait_file);
        while (!file_has(wait_file, wait_text)) Sleep(100);
    }
    D3D11_TEXTURE2D_DESC td = {}; td.Width = W; td.Height = H; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_STAGING; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D *stg = nullptr; if (FAILED(dev->CreateTexture2D(&td, nullptr, &stg))) { fprintf(stderr, "sem textura de leitura\n"); return 1; }
    FILE *fb = fopen((out + ".bin").c_str(), "wb"), *ft = fopen((out + ".tsv").c_str(), "wb");
    if (!fb || !ft) { fprintf(stderr, "nao consegui criar %s.*\n", out.c_str()); return 1; }
    fprintf(ft, "#monitor %dx%d %luHz faixa %d %d miniatura 480x270\n", W, H, dm.dmDisplayFrequency, y0, y1);
    const int TW = 480, TH = 270, sx = W / TW, sy = H / TH;
    std::vector<uint8_t> strip((size_t)W * (y1 - y0)), thumb((size_t)TW * TH * 3);
    const double t0 = now_ms(); unsigned n = 0, acc_total = 0, timeouts = 0;
    fprintf(ft, "#t0 %.3f (QPC em ms: o jogo com TF_PRESENT_LOG=1 registra [present] no mesmo relogio)\n", t0);
    while (now_ms() - t0 < secs * 1000.0) {
        DXGI_OUTDUPL_FRAME_INFO fi = {}; IDXGIResource *res = nullptr;
        hr = dup->AcquireNextFrame(100, &fi, &res);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) { timeouts++; continue; }
        if (hr == DXGI_ERROR_ACCESS_LOST) {   /* troca de modo/tela cheia exclusiva: recria */
            dup->Release(); dup = nullptr;
            while (FAILED(op1->DuplicateOutput(dev, &dup)) && now_ms() - t0 < secs * 1000.0) Sleep(20);
            if (!dup) break; fprintf(ft, "#acesso perdido em %.2f ms (recriado)\n", now_ms() - t0); continue;
        }
        if (FAILED(hr)) { fprintf(stderr, "AcquireNextFrame: %08lx\n", (unsigned long)hr); break; }
        const double ta = now_ms() - t0;
        if (fi.LastPresentTime.QuadPart && so_tempos) {
            fprintf(ft, "%u\t%.3f\t%.3f\t%u\n", n, ta, qpc_ms(fi.LastPresentTime.QuadPart) - t0, fi.AccumulatedFrames);
            n++; acc_total += fi.AccumulatedFrames; if ((n & 63) == 0) fflush(ft);   /* o roteiro pode encerrar o gravador quando o jogo fecha */
        } else if (fi.LastPresentTime.QuadPart) {
            ID3D11Texture2D *tex = nullptr; res->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&tex);
            ctx->CopyResource(stg, tex); tex->Release();
            D3D11_MAPPED_SUBRESOURCE m;
            if (SUCCEEDED(ctx->Map(stg, 0, D3D11_MAP_READ, 0, &m))) {
                const uint8_t *p = (const uint8_t *)m.pData;
                for (int y = y0; y < y1; y++) { const uint8_t *r = p + (size_t)y * m.RowPitch; uint8_t *o = &strip[(size_t)(y - y0) * W]; for (int x = 0; x < W; x++) o[x] = r[x * 4 + 1]; }
                for (int ty = 0; ty < TH; ty++) for (int tx = 0; tx < TW; tx++) {
                    unsigned s[3] = {0, 0, 0};
                    for (int yy = 0; yy < sy; yy++) { const uint8_t *r = p + (size_t)(ty * sy + yy) * m.RowPitch + (size_t)tx * sx * 4; for (int xx = 0; xx < sx; xx++) { s[0] += r[xx * 4 + 2]; s[1] += r[xx * 4 + 1]; s[2] += r[xx * 4]; } }
                    uint8_t *o = &thumb[((size_t)ty * TW + tx) * 3]; for (int k = 0; k < 3; k++) o[k] = (uint8_t)(s[k] / (sx * sy));
                }
                ctx->Unmap(stg, 0);
                fwrite(strip.data(), 1, strip.size(), fb); fwrite(thumb.data(), 1, thumb.size(), fb);
                fprintf(ft, "%u\t%.3f\t%.3f\t%u\n", n, ta, qpc_ms(fi.LastPresentTime.QuadPart) - t0, fi.AccumulatedFrames);
                n++; acc_total += fi.AccumulatedFrames;
            }
        }
        res->Release(); dup->ReleaseFrame();
    }
    fclose(fb); fclose(ft);
    fprintf(stderr, "[tela] %u imagens em %.1f s (%u apresentacoes acumuladas, %u esperas sem imagem nova)\n", n, (now_ms() - t0) / 1000.0, acc_total, timeouts);
    return 0;
}
