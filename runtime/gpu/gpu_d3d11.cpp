/* Renderizador por GPU (Direct3D 11): ver gpu.h. Compila os sombreadores HLSL em tempo de execucao (d3dcompiler_47). */
#include "gpu.h"
#include "util.h"
#include "gfx.h"
#include <d3d11.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unordered_map>
#include <algorithm>

void d3d_flush(void);
template <class T> static void rel(T *&p) { if (p) { p->Release(); p = nullptr; } }

volatile bool g_excl_failed;
volatile bool g_gpu_lost;   /* shared with the Vulkan backend (gpu.h) */
static ID3D11Device *g_dev; static ID3D11DeviceContext *g_ctx;
static IDXGISwapChain1 *g_swap; static ID3D11RenderTargetView *g_back; static int g_bw, g_bh;
static bool g_tearing, g_dbg, g_excl;   /* g_excl: swap chain in exclusive fullscreen (Present must not use ALLOW_TEARING there) */ static char g_adapter[128] = "?";
static HWND g_hwnd;

/* ------------------------------------------------------------------ sombreadores */
static const char *HLSL = R"(
cbuffer CB : register(b0) {
    float4 tenv;
    uint flags; uint tfn; uint afunc; uint aref;
    uint amask; uint aover; uint aconst; uint pad1;
    float2 texsz; float2 fbsz;
    float4 sfix;
    float4 uvr;   // retangulo de UV do sprite (texels do PSP): u0, v0, u1, v1
};
Texture2D T : register(t0);
SamplerState S : register(s0);
struct VI { float3 pos : POSITION; float3 uvw : TEXCOORD0; float4 col : COLOR0; };
struct VO { float4 sv : SV_Position; float3 uvw : TEXCOORD0; float4 col : COLOR0; float z : TEXCOORD1; };
VO vs_main(VI i) {
    VO o;
    o.sv = float4(i.pos.x * (2.0 / fbsz.x) - 1.0, 1.0 - i.pos.y * (2.0 / fbsz.y), 0.5, 1.0);
    o.uvw = float3(i.uvw.xy * i.uvw.z, i.uvw.z); o.col = i.col; o.z = i.pos.z;   // u/w, v/w, 1/w: interpolados na tela, o PS divide (perspectiva correta)
    return o;
}
bool tf(uint f, uint a, uint b) {
    switch (f) { case 0: return false; case 1: return true; case 2: return a == b; case 3: return a != b;
                 case 4: return a < b; case 5: return a <= b; case 6: return a > b; default: return a >= b; }
}
struct PO { float4 c : SV_Target0; float d : SV_Depth; };
PO ps_main(VO i) {
    PO o;
    float3 rgb = i.col.rgb; float a = i.col.a;
    if ((flags & 16) == 0) {
        if (flags & 1) {
            float2 uv = (i.uvw.xy / i.uvw.z) / texsz;
            if (flags & 64) {   // sprite de atlas: nao amostra alem do retangulo de UV do sprite (meio texel da textura REAL: a substituta HD tem mais texels)
                float2 sz; T.GetDimensions(sz.x, sz.y);
                float2 ins = min(0.5 / texsz, 1.5 / sz);   // textura nativa: meio texel (= a amostragem do PSP); substituta HD: 1,5 texel dela (a fileira da borda tem halo da ampliacao)
                float2 lo = uvr.xy / texsz + ins, hi = uvr.zw / texsz - ins;
                uv = clamp(uv, lo, max(hi, lo));
            }
            float4 t = T.Sample(S, uv);
            bool ta = (flags & 2) != 0;
            float3 vc = rgb; float va = a;
            if (tfn == 0) { rgb = t.rgb * vc; if (flags & 4) rgb = min(rgb * 2.0, 1.0); a = ta ? t.a * va : va; }
            else if (tfn == 1) { rgb = ta ? t.rgb * t.a + vc * (1.0 - t.a) : t.rgb; a = va; }
            else if (tfn == 2) { rgb = vc * (1.0 - t.rgb) + tenv.rgb * t.rgb; a = ta ? t.a * va : va; }
            else if (tfn == 3) { rgb = t.rgb; a = ta ? t.a : va; }
            else { rgb = min(vc + t.rgb, 1.0); a = ta ? t.a * va : va; }
        }
        if (flags & 8) {
            uint a8 = ((uint)(a * 255.0 + 0.5)) & amask;
            if (!tf(afunc, a8, aref)) discard;
        }
        if (aover == 3) rgb *= sfix.rgb;   // fator de origem = cor fixa (com outra cor fixa no destino)
        if (aover == 1) a = min(1.0, 2.0 * a);
        else if (aover == 2) a = 1.0 - min(1.0, 2.0 * (1.0 - a));
    }
    if (flags & 32) a = aconst / 255.0;   // desenho com stencil sem blend: o alfa gravado e o novo valor do stencil
    o.c = float4(rgb, a);
    o.d = floor(clamp(i.z, 0.0, 65535.0)) / 65535.0;
    return o;
}

// ---- passes de tela cheia
cbuffer C2 : register(b0) { float4 p0; float4 p1; float4 p2; float4 p3; };
struct FO { float4 sv : SV_Position; float2 uv : TEXCOORD0; };
FO vs_full(uint id : SV_VertexID) {
    FO o; float2 p = float2((id << 1) & 2, id & 2);
    o.uv = p; o.sv = float4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.5, 1.0);
    return o;
}
Texture2D NC : register(t0);
Texture2D<float> NZ : register(t1);
struct RO { float4 c : SV_Target0; float d : SV_Depth; };
RO ps_resync(FO i) {   // quadro nativo (cor + profundidade) replicado N x N
    RO o; int2 q = int2(i.sv.xy) / (int)p0.x;
    o.c = NC.Load(int3(q, 0)); o.d = NZ.Load(int3(q, 0));
    return o;
}
float4 ps_down(FO i) : SV_Target {   // media de cada bloco N x N
    int n = (int)p0.x; int2 q = int2(i.sv.xy) * n; float4 s = 0;
    for (int y = 0; y < n; y++) for (int x = 0; x < n; x++) s += NC.Load(int3(q + int2(x, y), 0));
    return s / (float)(n * n);
}
SamplerState SP : register(s0);
SamplerState SL : register(s1);
float4 ps_present(FO i) : SV_Target {   // p0 = (modo, k, larg. fonte, alt. fonte)
    int mode = (int)p0.x;
    if (mode == 0) return float4(NC.SampleLevel(SP, i.uv, 0).rgb, 1.0);
    if (mode == 2) return float4(NC.SampleLevel(SL, i.uv, 0).rgb, 1.0);
    float2 sz = p0.zw;
    if (mode == 3) {   // reducao: media de n x n amostras bilineares dentro do pixel de destino
        float r = p0.y; int n = clamp((int)ceil(r), 2, 8); float3 acc = 0;
        for (int y = 0; y < n; y++) for (int x = 0; x < n; x++) acc += NC.SampleLevel(SL, i.uv + ((float2(x, y) + 0.5) / (float)n - 0.5) * r / sz, 0).rgb;
        return float4(acc / (float)(n * n), 1.0);
    }
    float2 p = i.uv * sz - 0.5; float2 fl = floor(p); float2 f = p - fl;
    f = saturate((f - 0.5) * p0.y + 0.5);
    return float4(NC.SampleLevel(SL, (fl + 0.5 + f) / sz, 0).rgb, 1.0);
}
float4 ps_overlay(FO i) : SV_Target { return NC.SampleLevel(SL, i.uv, 0); }
)";

static ID3D11VertexShader *g_vs, *g_vsFull; static ID3D11PixelShader *g_ps, *g_psResync, *g_psDown, *g_psPresent, *g_psOverlay;
static ID3D11InputLayout *g_il; static ID3D11Buffer *g_cb, *g_cb2, *g_vb;
static ID3D11SamplerState *g_smp[8], *g_sPoint, *g_sLin;
static const int VB_MAX = 400000;   /* vertices por lote antes de executar */

static ID3DBlob *compile(const char *entry, const char *prof) {
    ID3DBlob *code = nullptr, *err = nullptr;
    HRESULT hr = D3DCompile(HLSL, strlen(HLSL), "gpu.hlsl", nullptr, nullptr, entry, prof, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err);
    if (FAILED(hr)) { fprintf(stderr, "[gpu] erro no sombreador %s: %s\n", entry, err ? (const char *)err->GetBufferPointer() : "?"); rel(err); return nullptr; }
    rel(err); return code;
}
static bool build_pipeline(void) {
    ID3DBlob *b;
    if (!(b = compile("vs_main", "vs_5_0"))) return false;
    g_dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &g_vs);
    D3D11_INPUT_ELEMENT_DESC il[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    HRESULT hr = g_dev->CreateInputLayout(il, 3, b->GetBufferPointer(), b->GetBufferSize(), &g_il); rel(b);
    if (FAILED(hr)) return false;
    struct { const char *e, *p; void *dst; } list[] = {
        {"vs_full", "vs_5_0", &g_vsFull}, {"ps_main", "ps_5_0", &g_ps}, {"ps_resync", "ps_5_0", &g_psResync}, {"ps_down", "ps_5_0", &g_psDown},
        {"ps_present", "ps_5_0", &g_psPresent}, {"ps_overlay", "ps_5_0", &g_psOverlay}};
    for (auto &l : list) {
        if (!(b = compile(l.e, l.p))) return false;
        if (l.p[0] == 'v') g_dev->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, (ID3D11VertexShader **)l.dst);
        else g_dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, (ID3D11PixelShader **)l.dst);
        rel(b);
    }
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = 96; bd.Usage = D3D11_USAGE_DYNAMIC; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    g_dev->CreateBuffer(&bd, nullptr, &g_cb); g_dev->CreateBuffer(&bd, nullptr, &g_cb2);
    bd.ByteWidth = VB_MAX * sizeof(GpuVtx); bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    if (FAILED(g_dev->CreateBuffer(&bd, nullptr, &g_vb))) return false;
    for (int i = 0; i < 8; i++) {   /* i = linear*4 + clampu*2 + clampv */
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter = (i & 4) ? D3D11_FILTER_MIN_MAG_MIP_LINEAR : D3D11_FILTER_MIN_MAG_MIP_POINT;
        sd.AddressU = (i & 2) ? D3D11_TEXTURE_ADDRESS_CLAMP : D3D11_TEXTURE_ADDRESS_WRAP;
        sd.AddressV = (i & 1) ? D3D11_TEXTURE_ADDRESS_CLAMP : D3D11_TEXTURE_ADDRESS_WRAP;
        sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD = D3D11_FLOAT32_MAX; sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
        g_dev->CreateSamplerState(&sd, &g_smp[i]);
    }
    D3D11_SAMPLER_DESC sd = {}; sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD = D3D11_FLOAT32_MAX; sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT; g_dev->CreateSamplerState(&sd, &g_sPoint);
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR; g_dev->CreateSamplerState(&sd, &g_sLin);
    return true;
}

/* ------------------------------------------------------------------ dispositivo e swapchain */
static void make_back(void) {
    rel(g_back);
    ID3D11Texture2D *t = nullptr;
    if (g_swap && SUCCEEDED(g_swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&t))) { g_dev->CreateRenderTargetView(t, nullptr, &g_back); rel(t); }
}
static bool ensure_swap(void) {
    if (!g_hwnd || !g_dev) return false;
    RECT cr; GetClientRect(g_hwnd, &cr); int cw = cr.right - cr.left, ch = cr.bottom - cr.top;
    if (cw <= 0 || ch <= 0) return false;   /* minimizada */
    IDXGIDevice *dd = nullptr; IDXGIAdapter *ad = nullptr; IDXGIFactory2 *f2 = nullptr;
    if (!g_swap) {
        if (SUCCEEDED(g_dev->QueryInterface(__uuidof(IDXGIDevice), (void **)&dd)) && SUCCEEDED(dd->GetAdapter(&ad)) && SUCCEEDED(ad->GetParent(__uuidof(IDXGIFactory2), (void **)&f2))) {
            IDXGIFactory5 *f5 = nullptr; BOOL t = FALSE;
            if (SUCCEEDED(f2->QueryInterface(__uuidof(IDXGIFactory5), (void **)&f5))) { if (SUCCEEDED(f5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &t, sizeof t))) g_tearing = t != FALSE && tf_diag_opt("d3d_tearing", 1) != 0; rel(f5); }
            if (!GetParent(g_hwnd)) g_tearing = false;   /* top-level window = exclusive fullscreen candidate: a swap chain with ALLOW_TEARING makes every Present fail (DXGI_ERROR_INVALID_CALL) there, leaving a black screen */
            DXGI_SWAP_CHAIN_DESC1 d = {}; d.Width = cw; d.Height = ch; d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count = 1;
            d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; d.BufferCount = (UINT)std::max(2, std::min(4, tf_diag_opt("d3d_buffers", 3)));   /* 3 by default: with 2, the GPU work queued after a present waits for the back buffer to be released (up to one refresh) and late intermediate frames pile up on slow machines */ d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            d.Flags = g_tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
            HRESULT hr = f2->CreateSwapChainForHwnd(g_dev, g_hwnd, &d, nullptr, nullptr, &g_swap);
            if (FAILED(hr)) fprintf(stderr, "[gpu] CreateSwapChainForHwnd falhou (0x%08lx)\n", (unsigned long)hr);
            else { f2->MakeWindowAssociation(g_hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES); g_bw = cw; g_bh = ch; make_back();
                   if (IDXGIDevice1 *d1 = nullptr; SUCCEEDED(g_dev->QueryInterface(__uuidof(IDXGIDevice1), (void **)&d1))) { d1->SetMaximumFrameLatency((UINT)std::max(1, std::min(3, tf_diag_opt("d3d_latency", 1)))); rel(d1); }
                   fprintf(stderr, "[d3d] swap chain: %u buffers, frame latency %d, tearing %d (diag.txt: d3d_buffers, d3d_latency, d3d_tearing)\n", d.BufferCount, std::max(1, std::min(3, tf_diag_opt("d3d_latency", 1))), (int)g_tearing); }   /* no maximo 1 quadro na fila da apresentacao (padrao do DXGI: 3 = ate 2 quadros de atraso se a CPU adiantar) */
        }
        rel(f2); rel(ad); rel(dd);
        return g_swap != nullptr && g_back != nullptr;
    }
    if (cw != g_bw || ch != g_bh) {
        g_ctx->OMSetRenderTargets(0, nullptr, nullptr); rel(g_back);
        HRESULT hr = g_swap->ResizeBuffers(0, cw, ch, DXGI_FORMAT_UNKNOWN, g_tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);
        if (FAILED(hr)) { fprintf(stderr, "[gpu] ResizeBuffers falhou (0x%08lx)\n", (unsigned long)hr); return false; }
        g_bw = cw; g_bh = ch; make_back();
    }
    return g_back != nullptr;
}
bool d3d_ready(void) { return g_dev != nullptr && g_swap != nullptr; }
static bool d3d_has_stencil(void) { static const bool off = getenv("TF_GPU_NOSTENCIL") != nullptr; return g_dev != nullptr && !off; }   /* D24S8 sempre existe no D3D11 (chamada a cada primitiva: o getenv fica guardado) */
const char *d3d_adapter_name(void) { return g_adapter; }
bool d3d_init(HWND wnd) {
    g_hwnd = wnd; g_dbg = getenv("TF_GPU_DEBUG") != nullptr;
    if (g_dev) return ensure_swap();
    IDXGIFactory6 *f6 = nullptr; IDXGIAdapter1 *ad = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory6), (void **)&f6)))
        if (FAILED(f6->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, __uuidof(IDXGIAdapter1), (void **)&ad))) ad = nullptr;
    if (ad) { DXGI_ADAPTER_DESC1 d; if (SUCCEEDED(ad->GetDesc1(&d))) snprintf(g_adapter, sizeof g_adapter, "%ls", d.Description); }
    D3D_FEATURE_LEVEL fl[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0}, got;
    UINT flags = 0;
    HRESULT hr = D3D11CreateDevice(ad, ad ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, fl, 2, D3D11_SDK_VERSION, &g_dev, &got, &g_ctx);
    if (FAILED(hr)) hr = D3D11CreateDevice(ad, ad ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, fl + 1, 1, D3D11_SDK_VERSION, &g_dev, &got, &g_ctx);
    rel(ad); rel(f6);
    if (FAILED(hr)) { fprintf(stderr, "[gpu] D3D11CreateDevice falhou (0x%08lx)\n", (unsigned long)hr); g_dev = nullptr; return false; }
    if (!build_pipeline()) { fprintf(stderr, "[gpu] falha ao criar os sombreadores\n"); return false; }
    fprintf(stderr, "[gpu] Direct3D 11 em: %s\n", g_adapter);
    return ensure_swap();
}

/* ------------------------------------------------------------------ quadros sombra */
struct GFb { bool used, dirty; int res, w, h, nw, nh, samples; ID3D11Texture2D *col, *dep, *mcol;   /* com MSAA: mcol = cor multiamostrada (alvo), col = resolvida (amostrada/lida), dep multiamostrada */
     ID3D11RenderTargetView *rtv; ID3D11ShaderResourceView *srv; ID3D11DepthStencilView *dsv; };
static const int kMaxFb = 32;   /* 4 de tela + ate 12 alvos de textura + intermediario e copias do 60 FPS */ static GFb g_fb[kMaxFb]; static ID3D11ShaderResourceView *g_lastSrv; static int g_lastW, g_lastH;   /* imagem do ultimo quadro (repintar) */
static void resolve_fb(GFb &f) { if (f.samples > 1 && f.dirty) { g_ctx->OMSetRenderTargets(0, nullptr, nullptr); g_ctx->ResolveSubresource(f.col, 0, f.mcol, 0, DXGI_FORMAT_R8G8B8A8_UNORM); f.dirty = false; } }
int d3d_fb_create(int nw, int nh, int res) {
    if (!g_dev) return -1;
    int id = -1; for (int i = 0; i < kMaxFb; i++) if (!g_fb[i].used) { id = i; break; }
    if (id < 0) return -1;
    GFb &f = g_fb[id]; f = GFb(); f.res = res; f.nw = nw; f.nh = nh; f.w = nw * res; f.h = nh * res;
    D3D11_TEXTURE2D_DESC d = {}; d.Width = f.w; d.Height = f.h; d.MipLevels = 1; d.ArraySize = 1; d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    /* MSAA pedido (ini/menu), limitado ao que a GPU suporta e a ~34 milhoes de amostras por quadro (memoria) */
    int S = std::max(1, g_gfx.msaa);
    while (S > 1) { UINT q1 = 0, q2 = 0; g_dev->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, S, &q1); g_dev->CheckMultisampleQualityLevels(DXGI_FORMAT_D24_UNORM_S8_UINT, S, &q2); if (q1 && q2 && (long long)f.w * f.h * S <= 34000000ll) break; S >>= 1; }
    f.samples = S;
    bool ok = SUCCEEDED(g_dev->CreateTexture2D(&d, nullptr, &f.col));
    if (ok && S > 1) { D3D11_TEXTURE2D_DESC m = d; m.SampleDesc.Count = S; m.BindFlags = D3D11_BIND_RENDER_TARGET; ok = SUCCEEDED(g_dev->CreateTexture2D(&m, nullptr, &f.mcol)); }
    d.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; d.BindFlags = D3D11_BIND_DEPTH_STENCIL; d.SampleDesc.Count = S;   /* z de 16 bits: k/65535 segue distinto e ordenado em 24 bits; o stencil emula o alfa do PSP */
    ok = ok && SUCCEEDED(g_dev->CreateTexture2D(&d, nullptr, &f.dep));
    if (ok) ok = SUCCEEDED(g_dev->CreateRenderTargetView(S > 1 ? f.mcol : f.col, nullptr, &f.rtv)) && SUCCEEDED(g_dev->CreateShaderResourceView(f.col, nullptr, &f.srv)) && SUCCEEDED(g_dev->CreateDepthStencilView(f.dep, nullptr, &f.dsv));
    if (!ok) { rel(f.col); rel(f.dep); rel(f.mcol); rel(f.rtv); rel(f.srv); rel(f.dsv); return -1; }
    f.used = true; return id;
}
void d3d_fb_destroy(int id) {
    if (id < 0 || id >= kMaxFb || !g_fb[id].used) return;
    d3d_flush(); GFb &f = g_fb[id]; if (g_lastSrv == f.srv) g_lastSrv = nullptr; rel(f.col); rel(f.dep); rel(f.mcol); rel(f.rtv); rel(f.srv); rel(f.dsv); f.used = false;
}

static ID3D11Texture2D *g_natC, *g_natZ, *g_downT, *g_downStage; static ID3D11ShaderResourceView *g_natCs, *g_natZs; static ID3D11RenderTargetView *g_downR;
static void ensure_nat(void) {
    if (g_natC) return;
    D3D11_TEXTURE2D_DESC d = {}; d.Width = 512; d.Height = 512; d.MipLevels = 1; d.ArraySize = 1; d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; g_dev->CreateTexture2D(&d, nullptr, &g_natC); g_dev->CreateShaderResourceView(g_natC, nullptr, &g_natCs);
    d.Format = DXGI_FORMAT_R16_UNORM; g_dev->CreateTexture2D(&d, nullptr, &g_natZ); g_dev->CreateShaderResourceView(g_natZ, nullptr, &g_natZs);
    d.Width = 480; d.Height = 272; d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    g_dev->CreateTexture2D(&d, nullptr, &g_downT); g_dev->CreateRenderTargetView(g_downT, nullptr, &g_downR);
    d.BindFlags = 0; d.Usage = D3D11_USAGE_STAGING; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; g_dev->CreateTexture2D(&d, nullptr, &g_downStage);
}
static void set_cb2(float a, float b, float c, float d) {
    D3D11_MAPPED_SUBRESOURCE m; if (FAILED(g_ctx->Map(g_cb2, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
    float *p = (float *)m.pData; memset(p, 0, 64); p[0] = a; p[1] = b; p[2] = c; p[3] = d; g_ctx->Unmap(g_cb2, 0);
}
static ID3D11BlendState *g_bsOpaque; static ID3D11DepthStencilState *g_dsOff; static ID3D11RasterizerState *g_rsFull;
static void ensure_basic_states(void) {
    if (g_bsOpaque) return;
    D3D11_BLEND_DESC bd = {}; bd.RenderTarget[0].RenderTargetWriteMask = 15; g_dev->CreateBlendState(&bd, &g_bsOpaque);
    D3D11_DEPTH_STENCIL_DESC dd = {}; dd.DepthEnable = FALSE; g_dev->CreateDepthStencilState(&dd, &g_dsOff);
    D3D11_RASTERIZER_DESC rd = {}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = FALSE; rd.MultisampleEnable = TRUE; g_dev->CreateRasterizerState(&rd, &g_rsFull);
}
static ID3D11DepthStencilState *g_dsAlways;
static void set_viewport(float x, float y, float w, float h) { D3D11_VIEWPORT v = {x, y, w, h, 0, 1}; g_ctx->RSSetViewports(1, &v); }
static void full_pass_prep(void) {
    ensure_basic_states();
    g_ctx->IASetInputLayout(nullptr); g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_ctx->VSSetShader(g_vsFull, nullptr, 0); g_ctx->RSSetState(g_rsFull);
    g_ctx->OMSetBlendState(g_bsOpaque, nullptr, 0xffffffff);
    g_ctx->PSSetConstantBuffers(0, 1, &g_cb2);
}
void d3d_fb_upload(int id, const uint32_t *col, uint32_t cstride, const uint16_t *z, uint32_t zstride) {
    if (id < 0 || id >= kMaxFb || !g_fb[id].used) return;
    d3d_flush(); ensure_nat(); GFb &f = g_fb[id];
    D3D11_BOX box = {0, 0, 0, (UINT)f.nw, (UINT)f.nh, 1};
    g_ctx->UpdateSubresource(g_natC, 0, &box, col, cstride * 4, 0);
    g_ctx->UpdateSubresource(g_natZ, 0, &box, z, zstride * 2, 0);
    if (!g_dsAlways) { D3D11_DEPTH_STENCIL_DESC dd = {}; dd.DepthEnable = TRUE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; dd.DepthFunc = D3D11_COMPARISON_ALWAYS; g_dev->CreateDepthStencilState(&dd, &g_dsAlways); }
    full_pass_prep();
    f.dirty = true; g_ctx->OMSetRenderTargets(1, &f.rtv, f.dsv); g_ctx->OMSetDepthStencilState(g_dsAlways, 0);
    g_ctx->ClearDepthStencilView(f.dsv, D3D11_CLEAR_STENCIL, 0.f, 0);   /* o stencil volta a 0 (o hle_ge marca como desconhecido) */
    set_viewport(0, 0, (float)f.w, (float)f.h);
    D3D11_RECT sc = {0, 0, f.w, f.h}; g_ctx->RSSetScissorRects(1, &sc);   /* o estado rasterizador nao liga o scissor: ignorado */
    set_cb2((float)f.res, 0, 0, 0);
    ID3D11ShaderResourceView *sv[2] = {g_natCs, g_natZs}; g_ctx->PSSetShaderResources(0, 2, sv);
    g_ctx->PSSetShader(g_psResync, nullptr, 0);
    g_ctx->Draw(3, 0);
    ID3D11ShaderResourceView *nul[2] = {nullptr, nullptr}; g_ctx->PSSetShaderResources(0, 2, nul);
}
static ID3D11Texture2D *g_ckStage[4]; static bool g_ckUsed[4]; static int g_ckNext;
int d3d_check_start(int id) {
    if (id < 0 || id >= kMaxFb || !g_fb[id].used) return -1;
    d3d_flush(); ensure_nat(); GFb &f = g_fb[id]; resolve_fb(f);
    int e = g_ckNext; g_ckNext = (g_ckNext + 1) & 3;
    if (!g_ckStage[e]) {
        D3D11_TEXTURE2D_DESC d = {}; d.Width = 480; d.Height = 272; d.MipLevels = 1; d.ArraySize = 1; d.SampleDesc.Count = 1; d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.Usage = D3D11_USAGE_STAGING; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; if (FAILED(g_dev->CreateTexture2D(&d, nullptr, &g_ckStage[e]))) return -1;
    }
    full_pass_prep();
    g_ctx->OMSetRenderTargets(1, &g_downR, nullptr); g_ctx->OMSetDepthStencilState(g_dsOff, 0);
    set_viewport(0, 0, 480, 272); set_cb2((float)f.res, 0, 0, 0);
    g_ctx->PSSetShaderResources(0, 1, &f.srv); g_ctx->PSSetShader(g_psDown, nullptr, 0);
    g_ctx->Draw(3, 0);
    ID3D11ShaderResourceView *nul = nullptr; g_ctx->PSSetShaderResources(0, 1, &nul);
    g_ctx->OMSetRenderTargets(0, nullptr, nullptr);
    g_ctx->CopyResource(g_ckStage[e], g_downT);
    g_ckUsed[e] = true; return e;
}
bool d3d_check_get(int e, uint32_t *out) {
    if (e < 0 || e > 3 || !g_ckUsed[e]) return false;
    double t0 = gpu_now_us(); g_ckUsed[e] = false;
    D3D11_MAPPED_SUBRESOURCE m; if (FAILED(g_ctx->Map(g_ckStage[e], 0, D3D11_MAP_READ, 0, &m))) return false;
    for (int y = 0; y < 272; y++) memcpy(out + (size_t)y * 480, (const uint8_t *)m.pData + (size_t)y * m.RowPitch, 480 * 4);
    g_ctx->Unmap(g_ckStage[e], 0); g_gpu_us[1] += gpu_now_us() - t0;
    return true;
}
bool d3d_fb_readback(int id, std::vector<uint32_t> &out, int *w, int *h) {
    if (id < 0 || id >= kMaxFb || !g_fb[id].used) return false;
    d3d_flush(); GFb &f = g_fb[id]; resolve_fb(f);
    D3D11_TEXTURE2D_DESC d; f.col->GetDesc(&d); d.BindFlags = 0; d.Usage = D3D11_USAGE_STAGING; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D *st = nullptr; if (FAILED(g_dev->CreateTexture2D(&d, nullptr, &st))) return false;
    g_ctx->CopyResource(st, f.col);
    D3D11_MAPPED_SUBRESOURCE m; if (FAILED(g_ctx->Map(st, 0, D3D11_MAP_READ, 0, &m))) { rel(st); return false; }
    out.resize((size_t)f.w * f.h);
    for (int y = 0; y < f.h; y++) memcpy(&out[(size_t)y * f.w], (const uint8_t *)m.pData + (size_t)y * m.RowPitch, (size_t)f.w * 4);
    g_ctx->Unmap(st, 0); rel(st); *w = f.w; *h = f.h;
    return true;
}
/* anel de quadros reduzidos (caixa-preta do F8, ver gpu.h): reducao na GPU antes da apresentacao; so le de volta quando alguem pede */
static std::vector<ID3D11Texture2D *> g_ringT; static std::vector<ID3D11RenderTargetView *> g_ringR; static std::vector<char> g_ringOk;
static int g_ringW, g_ringH; static ID3D11Texture2D *g_ringStage;
static void ring_free_all(void) { for (auto &t : g_ringT) rel(t); for (auto &r : g_ringR) rel(r); g_ringT.clear(); g_ringR.clear(); g_ringOk.clear(); rel(g_ringStage); g_ringW = g_ringH = 0; }
static bool d3d_ring_push(int id, int slot, int nslots) {
    if (id < 0 || id >= kMaxFb || !g_fb[id].used || slot < 0 || slot >= nslots) return false;
    d3d_flush(); GFb &f = g_fb[id]; resolve_fb(f);
    const int n = nslots > 120 ? std::max(f.res, 1) : (f.res + 1) / 2, w = f.w / n, h = f.h / n;   /* long rings (> 120 slots) keep 480x272 frames: same VRAM as 90 frames at 960 */
    if ((int)g_ringT.size() != nslots || w != g_ringW || h != g_ringH) {   /* resolucao trocada (raro): refaz o anel */
        ring_free_all(); g_ringT.assign(nslots, nullptr); g_ringR.assign(nslots, nullptr); g_ringOk.assign(nslots, 0); g_ringW = w; g_ringH = h;
    }
    if (!g_ringT[slot]) {
        D3D11_TEXTURE2D_DESC d = {}; d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1; d.SampleDesc.Count = 1; d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.Usage = D3D11_USAGE_DEFAULT; d.BindFlags = D3D11_BIND_RENDER_TARGET;
        if (FAILED(g_dev->CreateTexture2D(&d, nullptr, &g_ringT[slot])) || FAILED(g_dev->CreateRenderTargetView(g_ringT[slot], nullptr, &g_ringR[slot]))) { rel(g_ringT[slot]); rel(g_ringR[slot]); return false; }
    }
    full_pass_prep();
    g_ctx->OMSetRenderTargets(1, &g_ringR[slot], nullptr); g_ctx->OMSetDepthStencilState(g_dsOff, 0);
    set_viewport(0, 0, (float)w, (float)h); set_cb2((float)n, 0, 0, 0);
    g_ctx->PSSetShaderResources(0, 1, &f.srv); g_ctx->PSSetShader(g_psDown, nullptr, 0);
    g_ctx->Draw(3, 0);
    ID3D11ShaderResourceView *nul = nullptr; g_ctx->PSSetShaderResources(0, 1, &nul);
    g_ctx->OMSetRenderTargets(0, nullptr, nullptr);
    g_ringOk[slot] = 1; return true;
}
static bool d3d_ring_read(int slot, std::vector<uint32_t> &out, int *w, int *h) {
    if (slot < 0 || slot >= (int)g_ringT.size() || !g_ringOk[slot] || !g_ringT[slot]) return false;
    d3d_flush();
    if (!g_ringStage) {
        D3D11_TEXTURE2D_DESC d = {}; d.Width = g_ringW; d.Height = g_ringH; d.MipLevels = 1; d.ArraySize = 1; d.SampleDesc.Count = 1; d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.Usage = D3D11_USAGE_STAGING; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; if (FAILED(g_dev->CreateTexture2D(&d, nullptr, &g_ringStage))) return false;
    }
    g_ctx->CopyResource(g_ringStage, g_ringT[slot]);
    D3D11_MAPPED_SUBRESOURCE m; if (FAILED(g_ctx->Map(g_ringStage, 0, D3D11_MAP_READ, 0, &m))) return false;
    out.resize((size_t)g_ringW * g_ringH);
    for (int y = 0; y < g_ringH; y++) memcpy(&out[(size_t)y * g_ringW], (const uint8_t *)m.pData + (size_t)y * m.RowPitch, (size_t)g_ringW * 4);
    g_ctx->Unmap(g_ringStage, 0); *w = g_ringW; *h = g_ringH;
    return true;
}

/* ------------------------------------------------------------------ texturas */
struct GTex { ID3D11ShaderResourceView *srv; };
static std::vector<GTex> g_tex(1);   /* indice 0 = sem textura */
static std::vector<uint32_t> g_tex_free;
uint32_t d3d_tex_create(int w, int h, const uint32_t *px, bool mips) {
    if (!g_dev) return 0;
    if (mips && (w > 1 || h > 1)) {   /* piramide de mipmaps gerada pela GPU */
        D3D11_TEXTURE2D_DESC d = {}; d.Width = w; d.Height = h; d.MipLevels = 0; d.ArraySize = 1; d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT;
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET; d.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        ID3D11Texture2D *t = nullptr; ID3D11ShaderResourceView *sv = nullptr;
        if (SUCCEEDED(g_dev->CreateTexture2D(&d, nullptr, &t)) && SUCCEEDED(g_dev->CreateShaderResourceView(t, nullptr, &sv))) {
            g_ctx->UpdateSubresource(t, 0, nullptr, px, (UINT)(w * 4), 0); g_ctx->GenerateMips(sv); rel(t);
            uint32_t id; if (!g_tex_free.empty()) { id = g_tex_free.back(); g_tex_free.pop_back(); g_tex[id].srv = sv; } else { id = (uint32_t)g_tex.size(); g_tex.push_back({sv}); }
            return id;
        }
        rel(t); rel(sv);
    }
    D3D11_TEXTURE2D_DESC d = {}; d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1; d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_IMMUTABLE;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd = {px, (UINT)(w * 4), 0};
    ID3D11Texture2D *t = nullptr; if (FAILED(g_dev->CreateTexture2D(&d, &sd, &t))) return 0;
    ID3D11ShaderResourceView *sv = nullptr; HRESULT hr = g_dev->CreateShaderResourceView(t, nullptr, &sv); rel(t);
    if (FAILED(hr)) return 0;
    uint32_t id; if (!g_tex_free.empty()) { id = g_tex_free.back(); g_tex_free.pop_back(); g_tex[id].srv = sv; } else { id = (uint32_t)g_tex.size(); g_tex.push_back({sv}); }
    return id;
}
static uint32_t d3d_tex_create_bc7(int w, int h, int levels, const uint8_t *data) {   /* BC7 com todos os niveis prontos (bc7.h) */
    if (!g_dev || (w & 3) || (h & 3) || levels < 1 || levels > 16) return 0;
    D3D11_TEXTURE2D_DESC d = {}; d.Width = w; d.Height = h; d.MipLevels = levels; d.ArraySize = 1; d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_IMMUTABLE;
    d.Format = DXGI_FORMAT_BC7_UNORM; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd[16] = {}; int mw = w, mh = h;
    for (int i = 0; i < levels; i++) { sd[i].pSysMem = data; sd[i].SysMemPitch = (UINT)((mw + 3) / 4 * 16); data += (size_t)((mw + 3) / 4) * ((mh + 3) / 4) * 16; mw = std::max(1, mw / 2); mh = std::max(1, mh / 2); }
    ID3D11Texture2D *t = nullptr; if (FAILED(g_dev->CreateTexture2D(&d, sd, &t))) return 0;
    ID3D11ShaderResourceView *sv = nullptr; HRESULT hr = g_dev->CreateShaderResourceView(t, nullptr, &sv); rel(t);
    if (FAILED(hr)) return 0;
    uint32_t id; if (!g_tex_free.empty()) { id = g_tex_free.back(); g_tex_free.pop_back(); g_tex[id].srv = sv; } else { id = (uint32_t)g_tex.size(); g_tex.push_back({sv}); }
    return id;
}
void d3d_tex_release(uint32_t h) {
    if (!h || h >= g_tex.size() || !g_tex[h].srv) return;
    d3d_flush();   /* desenhos acumulados podem usar a textura */
    rel(g_tex[h].srv); g_tex_free.push_back(h);
}

/* ------------------------------------------------------------------ lotes de desenho */
struct Cmd { GpuState st; uint32_t first, count; };
static std::vector<GpuVtx> g_verts; static std::vector<Cmd> g_cmds;
void d3d_draw(const GpuState &s, const GpuVtx *v, int n) {
    if (n <= 0 || !g_dev) return;
    if (!g_cmds.empty() && g_cmds.back().first + g_cmds.back().count == g_verts.size() && !memcmp(&g_cmds.back().st, &s, sizeof s)) g_cmds.back().count += n;
    else g_cmds.push_back({s, (uint32_t)g_verts.size(), (uint32_t)n});
    g_verts.insert(g_verts.end(), v, v + n);
    if (g_verts.size() > (size_t)VB_MAX - 4096) d3d_flush();
}
static std::unordered_map<uint64_t, ID3D11BlendState *> g_bsMap; static std::unordered_map<uint64_t, ID3D11DepthStencilState *> g_dsMap; static ID3D11RasterizerState *g_rsScissor;
static D3D11_BLEND map_factor(uint32_t f, bool is_src) {
    switch (f) {
    case 0: return is_src ? D3D11_BLEND_DEST_COLOR : D3D11_BLEND_SRC_COLOR;
    case 1: return is_src ? D3D11_BLEND_INV_DEST_COLOR : D3D11_BLEND_INV_SRC_COLOR;
    case 2: case 6: return D3D11_BLEND_SRC_ALPHA;
    case 3: case 7: return D3D11_BLEND_INV_SRC_ALPHA;
    case 4: return D3D11_BLEND_DEST_ALPHA;
    case 5: return D3D11_BLEND_INV_DEST_ALPHA;
    case 11: return D3D11_BLEND_ONE;   /* origem = cor fixa, aplicada no sombreador */
    default: return D3D11_BLEND_BLEND_FACTOR;
    }
}
static ID3D11BlendState *get_blend(const GpuState &s) {
    uint32_t mask = (s.clear ? (((s.clear & 0x100) ? 7u : 0u) | ((s.clear & 0x200) ? 8u : 0u)) : 15u) & s.cmask;
    bool bl = s.blend && !s.clear;
    const bool acb = bl && s.aconst;   /* blend + alfa gravado constante (stencil): o alfa de saida vem do canal alfa do fator de blend, o RGB blenda normal */
    uint64_t key = mask | ((uint64_t)bl << 4) | ((uint64_t)s.bsf << 5) | ((uint64_t)s.bdf << 9) | ((uint64_t)s.beq << 13) | ((uint64_t)acb << 20);
    auto it = g_bsMap.find(key); if (it != g_bsMap.end()) return it->second;
    D3D11_BLEND_DESC d = {}; auto &r = d.RenderTarget[0]; r.RenderTargetWriteMask = (UINT8)mask;
    if (bl) {
        r.BlendEnable = TRUE; r.SrcBlend = map_factor(s.bsf, true); r.DestBlend = map_factor(s.bdf, false);
        static const D3D11_BLEND_OP ops[] = {D3D11_BLEND_OP_ADD, D3D11_BLEND_OP_SUBTRACT, D3D11_BLEND_OP_REV_SUBTRACT, D3D11_BLEND_OP_MIN, D3D11_BLEND_OP_MAX, D3D11_BLEND_OP_ADD};
        r.BlendOp = ops[s.beq > 5 ? 0 : s.beq];
        r.SrcBlendAlpha = acb ? D3D11_BLEND_BLEND_FACTOR : D3D11_BLEND_ONE; r.DestBlendAlpha = D3D11_BLEND_ZERO; r.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        if (r.BlendOp == D3D11_BLEND_OP_MIN || r.BlendOp == D3D11_BLEND_OP_MAX) { r.SrcBlend = r.DestBlend = D3D11_BLEND_ONE; }
    }
    ID3D11BlendState *b = nullptr; g_dev->CreateBlendState(&d, &b); g_bsMap[key] = b; return b;
}
static ID3D11DepthStencilState *get_depth(const GpuState &s) {
    bool en = s.clear ? true : s.ztest != 0, wr = s.clear ? (s.clear & 0x400) != 0 : s.zwrite != 0; uint32_t fn = s.clear ? 1 : s.zfunc;
    uint64_t key = (en ? 1u : 0u) | (wr ? 2u : 0u) | (fn << 2);
    if (s.st) key |= 32u | ((uint64_t)(s.sfunc & 7) << 6) | ((uint64_t)(s.sfail & 7) << 9) | ((uint64_t)(s.szfail & 7) << 12) | ((uint64_t)(s.szpass & 7) << 15) | ((uint64_t)(s.smask & 255) << 18) | ((uint64_t)(s.swmask & 255) << 26);
    auto it = g_dsMap.find(key); if (it != g_dsMap.end()) return it->second;
    static const D3D11_COMPARISON_FUNC cf[] = {D3D11_COMPARISON_NEVER, D3D11_COMPARISON_ALWAYS, D3D11_COMPARISON_EQUAL, D3D11_COMPARISON_NOT_EQUAL,
                                               D3D11_COMPARISON_LESS, D3D11_COMPARISON_LESS_EQUAL, D3D11_COMPARISON_GREATER, D3D11_COMPARISON_GREATER_EQUAL};
    D3D11_DEPTH_STENCIL_DESC d = {}; d.DepthEnable = en; d.DepthWriteMask = wr ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO; d.DepthFunc = cf[fn & 7];
    if (s.st) {   /* stencil: mesmas funcoes e operacoes do PSP; a referencia vai no OMSetDepthStencilState */
        static const D3D11_STENCIL_OP so[] = {D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_ZERO, D3D11_STENCIL_OP_REPLACE, D3D11_STENCIL_OP_INVERT, D3D11_STENCIL_OP_INCR_SAT, D3D11_STENCIL_OP_DECR_SAT, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP};
        d.StencilEnable = TRUE; d.StencilReadMask = (UINT8)s.smask; d.StencilWriteMask = (UINT8)s.swmask;
        d.FrontFace.StencilFailOp = so[s.sfail & 7]; d.FrontFace.StencilDepthFailOp = so[s.szfail & 7]; d.FrontFace.StencilPassOp = so[s.szpass & 7]; d.FrontFace.StencilFunc = cf[s.sfunc & 7];
        d.BackFace = d.FrontFace;
    }
    ID3D11DepthStencilState *o = nullptr; g_dev->CreateDepthStencilState(&d, &o); g_dsMap[key] = o; return o;
}
static void d3d_flush_impl(void);
/* GPU timing (diag.txt / TF_LAT_DEBUG): timestamp queries around each batch of draws and around the present pass tell how long the GPU really works,
 * as opposed to waiting (for the display, for a free back buffer, for the driver). Read back a few frames later without stalling. */
struct D3dProf { ID3D11Query *dj, *t0, *t1; bool pend; };
static D3dProf g_pf[64]; static unsigned g_pfw, g_pfr; static D3dProf *g_pfcur;
static double g_pf_busy, g_pf_max, g_pf_wait, g_pf_wait_max, g_pf_pres, g_pf_pres_max; static int g_pf_n, g_pf_wn, g_pf_pn;
static UINT g_pf_lastPresent, g_pf_lastRefresh;
static void prof_poll(void) {
    while (g_pfr < g_pfw) {
        D3dProf &p = g_pf[g_pfr & 63]; D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj; UINT64 a, b;
        if (g_ctx->GetData(p.dj, &dj, sizeof dj, D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) break;
        if (!dj.Disjoint && g_ctx->GetData(p.t0, &a, sizeof a, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK && g_ctx->GetData(p.t1, &b, sizeof b, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK) {
            const double ms = (double)(b - a) * 1000.0 / (double)dj.Frequency; g_pf_busy += ms; g_pf_max = std::max(g_pf_max, ms); g_pf_n++; }
        p.pend = false; g_pfr++;
    }
}
static void prof_begin(void) {
    static const bool on = tf_lat_debug(); if (!on || g_pfcur || !g_ctx || !g_dev) return;
    prof_poll(); D3dProf &p = g_pf[g_pfw & 63]; if (p.pend) return;
    if (!p.dj) { D3D11_QUERY_DESC q = {D3D11_QUERY_TIMESTAMP_DISJOINT, 0}; g_dev->CreateQuery(&q, &p.dj); q.Query = D3D11_QUERY_TIMESTAMP; g_dev->CreateQuery(&q, &p.t0); g_dev->CreateQuery(&q, &p.t1); }
    if (!p.dj || !p.t0 || !p.t1) return;
    g_ctx->Begin(p.dj); g_ctx->End(p.t0); g_pfcur = &p;
}
static void prof_end(void) { if (!g_pfcur) return; g_ctx->End(g_pfcur->t1); g_ctx->End(g_pfcur->dj); g_pfcur->pend = true; g_pfw++; g_pfcur = nullptr; }
static void prof_report(IDXGISwapChain1 *sw) {
    static const bool on = tf_lat_debug(); if (!on) return;
    static double t0; const double now = gpu_now_us() / 1000.0; if (now - t0 < 1000.0) return;
    if (t0) {
        DXGI_FRAME_STATISTICS fs = {}; char st[120] = "n/a";
        if (sw && SUCCEEDED(sw->GetFrameStatistics(&fs))) { snprintf(st, sizeof st, "%u presents, %u refreshes", fs.PresentCount - g_pf_lastPresent, fs.PresentRefreshCount - g_pf_lastRefresh); g_pf_lastPresent = fs.PresentCount; g_pf_lastRefresh = fs.PresentRefreshCount; }
        fprintf(stderr, "[d3d] per s: GPU busy %.1f ms in %d batches (longest %.1f) | wait for GPU %.1f ms in %d waits (longest %.1f) | Present() %.1f ms in %d calls (longest %.1f) | DXGI: %s%c",
                g_pf_busy, g_pf_n, g_pf_max, g_pf_wait, g_pf_wn, g_pf_wait_max, g_pf_pres, g_pf_pn, g_pf_pres_max, st, 10);
    }
    t0 = now; g_pf_busy = g_pf_max = g_pf_wait = g_pf_wait_max = g_pf_pres = g_pf_pres_max = 0; g_pf_n = g_pf_wn = g_pf_pn = 0;
}
void d3d_flush(void) { double t = gpu_now_us(); d3d_flush_impl(); g_gpu_us[0] += gpu_now_us() - t; }
/* envia o que ja foi gravado sem apresentar: a GPU comeca agora; d3d_wait_kicked espera esse envio terminar (consulta de evento; ms esperados) */
static ID3D11Query *g_kickQ; static bool g_kickPend;
static void d3d_kick(void) {
    if (!g_ctx) return;
    d3d_flush();
    if (!g_kickQ) { D3D11_QUERY_DESC qd = {D3D11_QUERY_EVENT, 0}; g_dev->CreateQuery(&qd, &g_kickQ); }
    if (g_kickQ) { g_ctx->End(g_kickQ); g_kickPend = true; }
    g_ctx->Flush();
}
static double d3d_wait_kicked(void) {
    if (!g_kickPend || !g_kickQ || !g_ctx) return 0;
    g_kickPend = false; const double t = gpu_now_us(); BOOL done = FALSE;
    while (g_ctx->GetData(g_kickQ, &done, sizeof done, 0) == S_FALSE) YieldProcessor();
    const double w = (gpu_now_us() - t) / 1000.0; g_pf_wait += w; g_pf_wait_max = std::max(g_pf_wait_max, w); g_pf_wn++;
    return w;
}
static double d3d_wait_kicked_for(double max_ms) {
    if (!g_kickPend || !g_kickQ || !g_ctx) return 0;
    const double t = gpu_now_us(); BOOL done = FALSE;
    while (g_ctx->GetData(g_kickQ, &done, sizeof done, 0) == S_FALSE) { if ((gpu_now_us() - t) / 1000.0 >= max_ms) return -1; YieldProcessor(); }
    g_kickPend = false; return (gpu_now_us() - t) / 1000.0;
}
static void d3d_flush_impl(void) {
    if (g_cmds.empty() || !g_dev) { g_cmds.clear(); g_verts.clear(); return; }
    prof_begin();
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(g_ctx->Map(g_vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        memcpy(m.pData, g_verts.data(), std::min(g_verts.size(), (size_t)VB_MAX) * sizeof(GpuVtx)); g_ctx->Unmap(g_vb, 0);
        if (!g_rsScissor) { D3D11_RASTERIZER_DESC rd = {}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.ScissorEnable = TRUE; rd.DepthClipEnable = FALSE; rd.MultisampleEnable = TRUE; g_dev->CreateRasterizerState(&rd, &g_rsScissor); }
        UINT stride = sizeof(GpuVtx), off = 0;
        g_ctx->IASetInputLayout(g_il); g_ctx->IASetVertexBuffers(0, 1, &g_vb, &stride, &off); g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_ctx->VSSetShader(g_vs, nullptr, 0); g_ctx->PSSetShader(g_ps, nullptr, 0); g_ctx->RSSetState(g_rsScissor);
        g_ctx->PSSetConstantBuffers(0, 1, &g_cb); g_ctx->VSSetConstantBuffers(0, 1, &g_cb);
        int curfb = -1;
        for (const Cmd &c : g_cmds) {
            const GpuState &s = c.st;
            if (s.fb < 0 || s.fb >= kMaxFb || !g_fb[s.fb].used) continue;
            GFb &f = g_fb[s.fb]; f.dirty = true;
            if (curfb != s.fb) { g_ctx->OMSetRenderTargets(1, &f.rtv, f.dsv); set_viewport(0, 0, (float)f.w, (float)f.h); curfb = s.fb; }
            D3D11_RECT sc = {s.sx1 * f.res, s.sy1 * f.res, (s.sx2 + 1) * f.res, (s.sy2 + 1) * f.res};
            sc.left = std::max<LONG>(0, sc.left); sc.top = std::max<LONG>(0, sc.top); sc.right = std::min<LONG>(f.w, sc.right); sc.bottom = std::min<LONG>(f.h, sc.bottom);
            if (sc.right <= sc.left || sc.bottom <= sc.top) continue;
            g_ctx->RSSetScissorRects(1, &sc);
            float bf[4] = {(s.fix & 255) / 255.f, ((s.fix >> 8) & 255) / 255.f, ((s.fix >> 16) & 255) / 255.f, (s.blend && s.aconst && !s.clear) ? (s.aconst & 255) / 255.f : 1.f};
            g_ctx->OMSetBlendState(get_blend(s), bf, 0xffffffff);
            g_ctx->OMSetDepthStencilState(get_depth(s), s.st ? (s.sref & 255) : 0);
            ID3D11ShaderResourceView *tsrv = nullptr;
            if (!s.clear && s.tex) {
                if (s.tex & GPU_FBTEX) { int sid = (int)(s.tex & 0xff); if (sid != s.fb && sid < kMaxFb && g_fb[sid].used) { resolve_fb(g_fb[sid]); tsrv = g_fb[sid].srv; } }   /* imagem da GPU de outro quadro sombra */
                else if (s.tex < g_tex.size()) tsrv = g_tex[s.tex].srv;
            }
            bool tex = tsrv != nullptr;
            if (tex) { g_ctx->PSSetShaderResources(0, 1, &tsrv); g_ctx->PSSetSamplers(0, 1, &g_smp[(s.linear ? 4 : 0) | (s.clampu ? 2 : 0) | (s.clampv ? 1 : 0)]); }
            struct { float tenv[4]; uint32_t flags, tfn, afunc, aref, amask, aover, aconst, p1; float tsz[2], fbsz[2]; float sfix[4]; float uvr[4]; } cb = {};
            cb.tenv[0] = (s.tenv & 255) / 255.f; cb.tenv[1] = ((s.tenv >> 8) & 255) / 255.f; cb.tenv[2] = ((s.tenv >> 16) & 255) / 255.f;
            cb.flags = (tex ? 1u : 0u) | (s.talpha ? 2u : 0u) | (s.tdbl ? 4u : 0u) | ((s.atest && !s.clear) ? 8u : 0u) | (s.clear ? 16u : 0u) | ((s.aconst && !s.clear && !s.blend) ? 32u : 0u) | ((tex && s.uvclamp) ? 64u : 0u);
            cb.uvr[0] = s.cu0; cb.uvr[1] = s.cv0; cb.uvr[2] = s.cu1; cb.uvr[3] = s.cv1;
            cb.aconst = s.aconst & 255; cb.tfn = s.tfn; cb.afunc = s.afunc; cb.aref = s.aref; cb.amask = s.amask;
            cb.aover = (s.blend && s.bsf == 6) ? 1 : (s.blend && s.bsf == 7) ? 2 : (s.blend && s.bsf == 11) ? 3 : 0;
            cb.sfix[0] = (s.sfix & 255) / 255.f; cb.sfix[1] = ((s.sfix >> 8) & 255) / 255.f; cb.sfix[2] = ((s.sfix >> 16) & 255) / 255.f;
            cb.tsz[0] = (float)s.tw; cb.tsz[1] = (float)s.th; cb.fbsz[0] = (float)f.nw; cb.fbsz[1] = (float)f.nh;
            if (SUCCEEDED(g_ctx->Map(g_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) { memcpy(m.pData, &cb, sizeof cb); g_ctx->Unmap(g_cb, 0); }
            g_ctx->Draw(c.count, c.first);
        }
        ID3D11ShaderResourceView *nul = nullptr; g_ctx->PSSetShaderResources(0, 1, &nul);
        g_ctx->OMSetRenderTargets(0, nullptr, nullptr);
    }
    prof_end();
    g_cmds.clear(); g_verts.clear();
}

/* ------------------------------------------------------------------ apresentacao */
static ID3D11Texture2D *g_ovT; static ID3D11ShaderResourceView *g_ovS; static bool g_ovOn;
static ID3D11Texture2D *g_pixT; static ID3D11ShaderResourceView *g_pixS;
static ID3D11BlendState *g_bsAlpha;
void d3d_set_overlay(const uint32_t *rgba) {
    if (!g_dev) return;
    g_ovOn = rgba != nullptr;
    if (!rgba) return;
    if (!g_ovT) {
        D3D11_TEXTURE2D_DESC d = {}; d.Width = GPU_OVW; d.Height = GPU_OVH; d.MipLevels = 1; d.ArraySize = 1; d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT;
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        g_dev->CreateTexture2D(&d, nullptr, &g_ovT); g_dev->CreateShaderResourceView(g_ovT, nullptr, &g_ovS);
    }
    g_ctx->UpdateSubresource(g_ovT, 0, nullptr, rgba, GPU_OVW * 4, 0);
}
static void present_srv(ID3D11ShaderResourceView *srv, int sw, int sh) {
    if (!srv || !ensure_swap()) return;
    ensure_basic_states();
    int cw = g_bw, ch = g_bh, w, h;   /* mesma conta de gfx_paint */
    if (g_gfx.aspect == GFX_ASPECT_STRETCH) { w = cw; h = ch; }
    else {
        int an = g_gfx.aspect == GFX_ASPECT_4_3 ? 4 : 480, ad = g_gfx.aspect == GFX_ASPECT_4_3 ? 3 : 272;
        w = cw; h = (int)((long long)cw * ad / an); if (h > ch) { h = ch; w = (int)((long long)ch * an / ad); }
        if (g_gfx.integer) { int n = std::min(cw / 480, ch / 272); if (n >= 1) { w = 480 * n; h = 272 * n; if (g_gfx.aspect == GFX_ASPECT_4_3) { w = h * 4 / 3; if (w > cw) w = cw; } } }
    }
    int x = (cw - w) / 2, y = (ch - h) / 2;
    prof_begin();
    float black[4] = {0, 0, 0, 1}; g_ctx->OMSetRenderTargets(1, &g_back, nullptr); g_ctx->ClearRenderTargetView(g_back, black);
    full_pass_prep(); g_ctx->OMSetDepthStencilState(g_dsOff, 0);
    set_viewport((float)x, (float)y, (float)w, (float)h);
    D3D11_RECT sc = {0, 0, cw, ch}; g_ctx->RSSetScissorRects(1, &sc);
    float k = std::max(1.f, (float)w / (float)sw);
    set_cb2(g_gfx.filter == GFX_FILTER_NEAREST ? 0.f : g_gfx.filter == GFX_FILTER_BILINEAR ? 2.f : 1.f, k, (float)sw, (float)sh);
    /* filtro "nitidos" com a fonte ja maior que a janela (k = 1) equivale a bilinear; com a fonte em escala inteira exata, ponto */
    if (w == sw && h == sh) set_cb2(0.f, 1.f, (float)sw, (float)sh);
    else if (g_gfx.filter != GFX_FILTER_NEAREST && (w < sw || h < sh)) set_cb2(3.f, std::max((float)sw / (float)w, (float)sh / (float)h), (float)sw, (float)sh);   /* reducao: supersampling no sombreador */
    g_ctx->PSSetShaderResources(0, 1, &srv); g_ctx->PSSetSamplers(0, 1, &g_sPoint); g_ctx->PSSetSamplers(1, 1, &g_sLin);
    g_ctx->PSSetShader(g_psPresent, nullptr, 0); g_ctx->Draw(3, 0);
    if (g_ovOn && g_ovS) {
        if (!g_bsAlpha) {
            D3D11_BLEND_DESC bd = {}; auto &r = bd.RenderTarget[0]; r.BlendEnable = TRUE; r.SrcBlend = D3D11_BLEND_SRC_ALPHA; r.DestBlend = D3D11_BLEND_INV_SRC_ALPHA; r.BlendOp = D3D11_BLEND_OP_ADD;
            r.SrcBlendAlpha = D3D11_BLEND_ONE; r.DestBlendAlpha = D3D11_BLEND_ZERO; r.BlendOpAlpha = D3D11_BLEND_OP_ADD; r.RenderTargetWriteMask = 15; g_dev->CreateBlendState(&bd, &g_bsAlpha);
        }
        g_ctx->OMSetBlendState(g_bsAlpha, nullptr, 0xffffffff);
        g_ctx->PSSetShaderResources(0, 1, &g_ovS); g_ctx->PSSetShader(g_psOverlay, nullptr, 0); g_ctx->Draw(3, 0);
    }
    ID3D11ShaderResourceView *nul = nullptr; g_ctx->PSSetShaderResources(0, 1, &nul);
    prof_end();
    double tp = gpu_now_us(); const HRESULT phr = g_swap->Present(g_gfx.sync == 1 ? 1 : 0, g_gfx.sync != 1 && g_tearing && !g_excl ? DXGI_PRESENT_ALLOW_TEARING : 0);
    if (FAILED(phr) && phr != DXGI_ERROR_DEVICE_REMOVED && phr != DXGI_ERROR_DEVICE_RESET) { static int n; if (n++ < 5) fprintf(stderr, "[d3d] Present failed: 0x%08lx (excl %d, sync %d)\n", (unsigned long)phr, (int)g_excl, (int)g_gfx.sync);
        if (n <= 1) { RECT cr = {}; GetClientRect(g_hwnd, &cr); BOOL fs = FALSE; IDXGIOutput *o = nullptr; g_swap->GetFullscreenState(&fs, &o); DXGI_OUTPUT_DESC od = {}; if (o) o->GetDesc(&od); fprintf(stderr, "[d3d] buffers %dx%d, client %ldx%ld, fullscreen %d, output %ldx%ld, device reason 0x%08lx\n", g_bw, g_bh, cr.right, cr.bottom, (int)fs, od.DesktopCoordinates.right - od.DesktopCoordinates.left, od.DesktopCoordinates.bottom - od.DesktopCoordinates.top, (unsigned long)g_dev->GetDeviceRemovedReason()); rel(o); } }
    if (g_excl && phr == DXGI_ERROR_INVALID_CALL) { static int bad; if (++bad >= 30) { bad = 0; g_excl_failed = true; } }
    else if (SUCCEEDED(phr)) { static int ok; if (ok++ == 0 || ok == 100 || ok == 1000) fprintf(stderr, "[d3d] Present ok (#%d, excl %d)\n", ok, (int)g_excl); }
    if (phr == DXGI_ERROR_DEVICE_REMOVED || phr == DXGI_ERROR_DEVICE_RESET) {
        if (!g_gpu_lost) fprintf(stderr, "[d3d] device lost (Present 0x%08lx, reason 0x%08lx)\n", (unsigned long)phr, (unsigned long)g_dev->GetDeviceRemovedReason());
        g_gpu_lost = true;
    }
    const double pd = gpu_now_us() - tp; g_gpu_us[2] += pd;
    { const double pm = pd / 1000.0; g_pf_pres += pm; g_pf_pres_max = std::max(g_pf_pres_max, pm); g_pf_pn++; prof_report(g_swap); }
    g_lastSrv = srv; g_lastW = sw; g_lastH = sh;
}
void d3d_present_fb(int id) {
    if (id < 0 || id >= kMaxFb || !g_fb[id].used) return;
    d3d_flush(); resolve_fb(g_fb[id]); present_srv(g_fb[id].srv, g_fb[id].w, g_fb[id].h);
}
void d3d_present_pixels(const uint32_t *xrgb, int w, int h) {
    if (!g_dev) return;
    d3d_flush();
    static int pw, ph;
    if (g_pixT && (pw != w || ph != h)) { g_pixS->Release(); g_pixT->Release(); g_pixS = nullptr; g_pixT = nullptr; }   /* tamanho novo (video substituto HD) */
    if (!g_pixT) {
        pw = w; ph = h;
        D3D11_TEXTURE2D_DESC d = {}; d.Width = (UINT)w; d.Height = (UINT)h; d.MipLevels = 1; d.ArraySize = 1; d.SampleDesc.Count = 1; d.Usage = D3D11_USAGE_DEFAULT;
        d.Format = DXGI_FORMAT_B8G8R8X8_UNORM; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        g_dev->CreateTexture2D(&d, nullptr, &g_pixT); g_dev->CreateShaderResourceView(g_pixT, nullptr, &g_pixS);
    }
    g_ctx->UpdateSubresource(g_pixT, 0, nullptr, xrgb, w * 4, 0);
    present_srv(g_pixS, w, h);
}
void d3d_present_last(void) { if (g_lastSrv) present_srv(g_lastSrv, g_lastW, g_lastH); }

/* Solta TUDO (dispositivo inclusive) ao trocar para outro renderizador; d3d_init recria do zero, como o Vulkan. Manter o dispositivo D3D11
 * vivo atrapalhava o Vulkan na mesma janela: depois de trocar D3D11 -> Vulkan pelo menu, redimensionar a janela deixava na tela o ultimo
 * quadro do D3D11 esticado e congelado (so Software -> Vulkan funcionava). */
static void d3d_destroy_all(void) {
    if (!g_dev) return;
    g_cmds.clear(); g_verts.clear();
    for (int i = 0; i < kMaxFb; i++) if (g_fb[i].used) { GFb &f = g_fb[i]; rel(f.col); rel(f.dep); rel(f.mcol); rel(f.rtv); rel(f.srv); rel(f.dsv); f.used = false; }
    g_lastSrv = nullptr;
    for (auto &t : g_tex) rel(t.srv); g_tex.assign(1, GTex{}); g_tex_free.clear();
    for (auto &kv : g_bsMap) kv.second->Release(); g_bsMap.clear();
    for (auto &kv : g_dsMap) kv.second->Release(); g_dsMap.clear();
    for (auto &s : g_ckStage) rel(s); for (auto &u : g_ckUsed) u = false;
    ring_free_all();
    rel(g_natC); rel(g_natZ); rel(g_downT); rel(g_downStage); rel(g_natCs); rel(g_natZs); rel(g_downR);
    rel(g_bsOpaque); rel(g_dsOff); rel(g_rsFull); rel(g_dsAlways); rel(g_rsScissor); rel(g_bsAlpha);
    rel(g_ovT); rel(g_ovS); g_ovOn = false; rel(g_pixT); rel(g_pixS);
    for (auto &s : g_smp) rel(s); rel(g_sPoint); rel(g_sLin);
    rel(g_vs); rel(g_vsFull); rel(g_ps); rel(g_psResync); rel(g_psDown); rel(g_psPresent); rel(g_psOverlay); rel(g_il); rel(g_cb); rel(g_cb2); rel(g_vb);
    rel(g_back); rel(g_kickQ); g_kickPend = false;
    if (g_ctx) { g_ctx->ClearState(); g_ctx->Flush(); }
    if (g_swap) g_swap->SetFullscreenState(FALSE, nullptr);   /* DXGI exige sair do modo exclusivo antes de soltar a swapchain */
    g_excl = false; rel(g_swap); rel(g_ctx); rel(g_dev);
}
static void d3d_release_all(void) { d3d_destroy_all(); }
static bool d3d_set_exclusive(bool on) {
    if (!g_swap) return false;
    const HRESULT hr = g_swap->SetFullscreenState(on ? TRUE : FALSE, nullptr);
    fprintf(stderr, "[d3d] exclusive fullscreen %s: %s (0x%08lx)\n", on ? "on" : "off", SUCCEEDED(hr) ? "ok" : "refused", (unsigned long)hr);
    g_excl = SUCCEEDED(hr) && on;
    return SUCCEEDED(hr);
}
const GpuBackend gpu_backend_d3d11 = {"Direct3D 11", d3d_init, d3d_ready, d3d_release_all, d3d_adapter_name, d3d_fb_create, d3d_fb_destroy, d3d_fb_upload, d3d_check_start, d3d_check_get,
    d3d_fb_readback, d3d_draw, d3d_flush, d3d_tex_create, d3d_tex_release, d3d_present_fb, d3d_present_pixels, d3d_present_last, d3d_set_overlay, d3d_has_stencil,
    d3d_ring_push, d3d_ring_read, d3d_kick, d3d_wait_kicked, nullptr, nullptr, nullptr, nullptr, d3d_tex_create_bc7, d3d_wait_kicked_for, d3d_set_exclusive};
