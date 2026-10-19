/* Gravador CONTINUO do monitor para caçar piscadas/flickers do 120 FPS durante uma sessao jogada de verdade (nao reproducao).
 * Usa a duplicacao de tela do DXGI (o que o Windows realmente mostra, a 120 Hz) e nao toca no jogo: custo so neste processo.
 *
 * Compilar (MSYS2 UCRT64): g++ -O2 -o gen/tela_continua.exe tools/analysis/tela_continua.cpp -ld3d11 -ldxgi
 * Uso: tela_continua <pasta> [--manter <min>=6] [--segundos <s>=0 (0 = ate ser encerrado)]
 *   grava <pasta>/seg_NNNN.bin + .tsv em segmentos de 30 s; apaga os mais antigos alem de --manter minutos (um anel em disco).
 *   por imagem NOVA do monitor: miniatura 160x90 RGB (media amostrada). ~5 MB/s a 120 Hz (6 min ~ 1,8 GB).
 *   tsv: "idx  ms_captura  ms_apresentacao  acumulados" (acumulados > 1 = quadro que nunca chegou ao monitor) e linhas
 *   "#marca idx ms origem" quando o jogador aperta L3+R3 (controle XInput 0) ou F7/F8 (teclado): o instante a investigar.
 * Analise: python tools/analysis/piscadas_analisar.py --continuo <pasta> [--png pasta_png]
 * Monitor = o principal. Encerrar com Ctrl+C / taskkill: os arquivos sao descarregados a cada 64 imagens. */
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <algorithm>

static double qpc_ms(LONGLONG v) { static LARGE_INTEGER f; if (!f.QuadPart) QueryPerformanceFrequency(&f); return (double)v * 1000.0 / (double)f.QuadPart; }
static double now_ms(void) { LARGE_INTEGER c; QueryPerformanceCounter(&c); return qpc_ms(c.QuadPart); }

struct XState { DWORD packet; WORD buttons; BYTE lt, rt; SHORT lx, ly, rx, ry; };
typedef DWORD(WINAPI *XInputGetStateFn)(DWORD, XState *);

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "uso: tela_continua <pasta> [--manter min] [--segundos s]\n"); return 1; }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const std::string dir = argv[1]; double manter_min = 6, secs = 0, anel_s = 6, depois_s = 1, marca_teste_s = -1;
    for (int i = 2; i + 1 < argc; i++) {
        if (!strcmp(argv[i], "--manter")) manter_min = atof(argv[++i]);
        else if (!strcmp(argv[i], "--segundos")) secs = atof(argv[++i]);
        else if (!strcmp(argv[i], "--anel-s")) anel_s = atof(argv[++i]);
        else if (!strcmp(argv[i], "--depois")) depois_s = atof(argv[++i]);
        else if (!strcmp(argv[i], "--marca-teste")) marca_teste_s = atof(argv[++i]);   /* so para testar: marca sozinha neste segundo */
    }
    for (size_t k = 0; k < dir.size(); k++) if (dir[k] == '/' || dir[k] == '\\')CreateDirectoryA(dir.substr(0, k).c_str(), nullptr);   /* cria os pais */
    CreateDirectoryA(dir.c_str(), nullptr);
    XInputGetStateFn xget = nullptr;
    if (HMODULE xl = LoadLibraryA("xinput1_4.dll")) xget = (XInputGetStateFn)GetProcAddress(xl, "XInputGetState");

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
    fprintf(stderr, "[tela_continua] monitor %dx%d a %lu Hz, pasta %s, anel de %.1f min\n", W, H, dm.dmDisplayFrequency, dir.c_str(), manter_min);
    ID3D11Device *dev = nullptr; ID3D11DeviceContext *ctx = nullptr; D3D_FEATURE_LEVEL fl;
    if (FAILED(D3D11CreateDevice(ad, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev, &fl, &ctx))) { fprintf(stderr, "sem D3D11\n"); return 1; }
    IDXGIOutput1 *op1 = nullptr; op->QueryInterface(__uuidof(IDXGIOutput1), (void **)&op1);
    IDXGIOutputDuplication *dup = nullptr;
    HRESULT hr = op1->DuplicateOutput(dev, &dup);
    if (FAILED(hr)) { fprintf(stderr, "DuplicateOutput falhou: %08lx\n", (unsigned long)hr); return 1; }
    D3D11_TEXTURE2D_DESC td = {}; td.Width = W; td.Height = H; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_STAGING; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D *stg = nullptr; if (FAILED(dev->CreateTexture2D(&td, nullptr, &stg))) { fprintf(stderr, "sem textura de leitura\n"); return 1; }

    /* ANEL DE QUADROS GRANDES (metade da resolucao, 960x540, media 2x2 feita na GPU): os ultimos anel_s segundos; numa marca vira marca_N.raw */
    const int HW = W / 2, HH = H / 2; const int RN = anel_s > 0 ? (int)(anel_s * (dm.dmDisplayFrequency ? dm.dmDisplayFrequency : 120)) : 0;
    ID3D11Texture2D *mipT = nullptr; ID3D11ShaderResourceView *mipV = nullptr; std::vector<ID3D11Texture2D *> ring; std::vector<double> ring_ms; std::vector<unsigned> ring_idx;
    if (RN > 0) {
        D3D11_TEXTURE2D_DESC md = {}; md.Width = W; md.Height = H; md.MipLevels = 2; md.ArraySize = 1; md.Format = DXGI_FORMAT_B8G8R8A8_UNORM; md.SampleDesc.Count = 1;
        md.Usage = D3D11_USAGE_DEFAULT; md.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET; md.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        if (FAILED(dev->CreateTexture2D(&md, nullptr, &mipT)) || FAILED(dev->CreateShaderResourceView(mipT, nullptr, &mipV))) { fprintf(stderr, "sem textura de mipmaps: anel grande desligado\n"); mipT = nullptr; }
        else {
            D3D11_TEXTURE2D_DESC rd = {}; rd.Width = HW; rd.Height = HH; rd.MipLevels = 1; rd.ArraySize = 1; rd.Format = DXGI_FORMAT_B8G8R8A8_UNORM; rd.SampleDesc.Count = 1;
            rd.Usage = D3D11_USAGE_STAGING; rd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            ring.assign(RN, nullptr); ring_ms.assign(RN, 0); ring_idx.assign(RN, 0);
            for (int k = 0; k < RN; k++) if (FAILED(dev->CreateTexture2D(&rd, nullptr, &ring[k]))) { fprintf(stderr, "sem memoria para o anel grande (%d de %d): desligado\n", k, RN); for (int q = 0; q < k; q++) ring[q]->Release(); ring.clear(); mipT = nullptr; break; }
        }
    }
    unsigned ring_pos = 0; std::vector<std::pair<int, double>> dumps_pend;   /* (numero da marca, hora em que despejar) */
    auto despejar = [&](int num) {
        const unsigned cnt = std::min<unsigned>(ring_pos, (unsigned)RN);
        char nm[512]; snprintf(nm, sizeof nm, "%s\\marca_%d.raw", dir.c_str(), num); FILE *f = fopen(nm, "wb"); if (!f) return;
        const int32_t hd[3] = {HW, HH, (int32_t)cnt}; fputs("TFHI1\n", f); fwrite(hd, 4, 3, f);
        std::vector<uint8_t> lin((size_t)HW * HH * 4);
        for (unsigned k = ring_pos - cnt; k < ring_pos; k++) {
            const int sl = (int)(k % (unsigned)RN); D3D11_MAPPED_SUBRESOURCE m;
            if (FAILED(ctx->Map(ring[sl], 0, D3D11_MAP_READ, 0, &m))) continue;
            for (int y = 0; y < HH; y++) memcpy(&lin[(size_t)y * HW * 4], (const uint8_t *)m.pData + (size_t)y * m.RowPitch, (size_t)HW * 4);
            ctx->Unmap(ring[sl], 0);
            const float ms = (float)ring_ms[sl]; const uint32_t ix = ring_idx[sl]; fwrite(&ms, 4, 1, f); fwrite(&ix, 4, 1, f); fwrite(lin.data(), 1, lin.size(), f);
        }
        fclose(f); fprintf(stderr, "[tela_continua] marca %d: %u quadros grandes (%dx%d) gravados em %s\n", num, cnt, HW, HH, nm);
    };
    const int TW = 160, TH = 90, cw = W / TW, ch = H / TH, amost = 4;   /* amostra 4x4 pontos por celula */
    std::vector<uint8_t> thumb((size_t)TW * TH * 3);
    const double SEG_MS = 30000.0, t0 = now_ms();
    FILE *fb = nullptr, *ft = nullptr; int seg = -1; unsigned n = 0, acc_total = 0, marcas = 0; std::vector<int> segs;
    bool marca_ant = false, f7_ant = false, f8_ant = false;
    auto abrir = [&](int s) {
        if (fb) { fclose(fb); fclose(ft); }
        char nm[512]; snprintf(nm, sizeof nm, "%s\\seg_%04d", dir.c_str(), s);
        fb = fopen((std::string(nm) + ".bin").c_str(), "wb"); ft = fopen((std::string(nm) + ".tsv").c_str(), "wb");
        if (!fb || !ft) { fprintf(stderr, "nao consegui criar %s.*\n", nm); exit(1); }
        fprintf(ft, "#monitor %dx%d %luHz miniatura %dx%d t0 %.3f\n", W, H, dm.dmDisplayFrequency, TW, TH, t0);
        segs.push_back(s);
        const int max_seg = (int)(manter_min * 60000.0 / SEG_MS) + 1;
        while ((int)segs.size() > max_seg) {
            char old[512]; snprintf(old, sizeof old, "%s\\seg_%04d", dir.c_str(), segs.front());
            DeleteFileA((std::string(old) + ".bin").c_str()); DeleteFileA((std::string(old) + ".tsv").c_str()); segs.erase(segs.begin());
        }
    };
    while (secs <= 0 || now_ms() - t0 < secs * 1000.0) {
        /* marcas: L3+R3 no controle (0x40|0x80) ou F7/F8; so na borda de subida */
        bool marca = false; const char *orig = "";
        if (xget) { XState s = {}; if (xget(0, &s) == 0 && (s.buttons & 0xC0) == 0xC0) { marca = true; orig = "L3+R3"; } }
        const bool f7 = (GetAsyncKeyState(VK_F7) & 0x8000) != 0, f8 = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
        const double tnow = now_ms() - t0;
        if (marca_teste_s >= 0 && tnow >= marca_teste_s * 1000.0) { marca_teste_s = -1; marca = true; orig = "teste"; marca_ant = false; }
        if ((marca && !marca_ant) || (f7 && !f7_ant) || (f8 && !f8_ant)) {
            if (!ft) abrir(0);
            fprintf(ft, "#marca\t%u\t%.3f\t%s\n", n, tnow, marca ? orig : f7 ? "F7" : "F8"); fflush(ft); marcas++;
            fprintf(stderr, "[tela_continua] marca #%u na imagem %u (%.1f s)\n", marcas, n, tnow / 1000.0);
            if (!ring.empty()) dumps_pend.push_back({(int)marcas, tnow + depois_s * 1000.0});
        }
        marca_ant = marca; f7_ant = f7; f8_ant = f8;

        DXGI_OUTDUPL_FRAME_INFO fi = {}; IDXGIResource *res = nullptr;
        hr = dup->AcquireNextFrame(50, &fi, &res);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) continue;
        if (hr == DXGI_ERROR_ACCESS_LOST) {   /* troca de modo/tela cheia exclusiva: recria */
            dup->Release(); dup = nullptr;
            while (FAILED(op1->DuplicateOutput(dev, &dup))) Sleep(20);
            if (ft) { fprintf(ft, "#acesso perdido em %.2f ms (recriado)\n", now_ms() - t0); }
            continue;
        }
        if (FAILED(hr)) { fprintf(stderr, "AcquireNextFrame: %08lx\n", (unsigned long)hr); break; }
        if (fi.LastPresentTime.QuadPart) {
            const int s = (int)(tnow / SEG_MS); if (s != seg) { seg = s; abrir(s); }
            ID3D11Texture2D *tex = nullptr; res->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&tex);
            ctx->CopyResource(stg, tex);
            if (!ring.empty()) {   /* quadro grande: copia -> mip 1 (media 2x2 na GPU) -> posicao do anel (memoria de sistema, sem esperar) */
                ctx->CopySubresourceRegion(mipT, 0, 0, 0, 0, tex, 0, nullptr); ctx->GenerateMips(mipV);
                const int sl = (int)(ring_pos % (unsigned)RN); ctx->CopySubresourceRegion(ring[sl], 0, 0, 0, 0, mipT, 1, nullptr);
                ring_ms[sl] = tnow; ring_idx[sl] = n; ring_pos++;
            }
            tex->Release();
            D3D11_MAPPED_SUBRESOURCE m;
            if (SUCCEEDED(ctx->Map(stg, 0, D3D11_MAP_READ, 0, &m))) {
                const uint8_t *p = (const uint8_t *)m.pData;
                for (int ty = 0; ty < TH; ty++) for (int tx = 0; tx < TW; tx++) {
                    unsigned r = 0, g = 0, b = 0;
                    for (int yy = 0; yy < amost; yy++) { const uint8_t *row = p + (size_t)(ty * ch + yy * ch / amost) * m.RowPitch;
                        for (int xx = 0; xx < amost; xx++) { const uint8_t *px = row + (size_t)(tx * cw + xx * cw / amost) * 4; r += px[2]; g += px[1]; b += px[0]; } }
                    uint8_t *o = &thumb[((size_t)ty * TW + tx) * 3]; o[0] = (uint8_t)(r / (amost * amost)); o[1] = (uint8_t)(g / (amost * amost)); o[2] = (uint8_t)(b / (amost * amost));
                }
                ctx->Unmap(stg, 0);
                fwrite(thumb.data(), 1, thumb.size(), fb);
                fprintf(ft, "%u\t%.3f\t%.3f\t%u\n", n, tnow, qpc_ms(fi.LastPresentTime.QuadPart) - t0, fi.AccumulatedFrames);
                n++; acc_total += fi.AccumulatedFrames; if ((n & 63) == 0) { fflush(fb); fflush(ft); }
            }
        }
        res->Release(); dup->ReleaseFrame();
        while (!dumps_pend.empty() && now_ms() - t0 >= dumps_pend.front().second) { despejar(dumps_pend.front().first); dumps_pend.erase(dumps_pend.begin()); }
    }
    while (!dumps_pend.empty()) { despejar(dumps_pend.front().first); dumps_pend.erase(dumps_pend.begin()); }
    if (fb) { fclose(fb); fclose(ft); }
    fprintf(stderr, "[tela_continua] %u imagens em %.1f s (%u apresentacoes acumuladas, %u marcas)\n", n, (now_ms() - t0) / 1000.0, acc_total, marcas);
    return 0;
}
