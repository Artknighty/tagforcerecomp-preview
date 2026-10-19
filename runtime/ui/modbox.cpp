/* Caixa de aviso e paineis dos mods, desenhados pelo host por cima da tela (overlay da GPU, 4x a resolucao do PSP).
 * O mod chama modbox_show() / modbox_panel() a cada quadro em que a caixa / o painel deve aparecer; o display pergunta com modbox_pixels().
 * Desenho com GDI+ (antialias, degrades, cantos arredondados): cada elemento num bitmap proprio, composto com alfa no overlay (alfa reto,
 * como o overlay da GPU espera); a sombra e calculada direto nos pixels. A marcacao das linhas (secoes, linhas de carta, colunas, barra)
 * esta em mods_sdk/tfmod.h. O desenho (~5-10 ms por elemento) roda numa thread propria: a thread do jogo nunca espera; enquanto a imagem
 * nova nao fica pronta (1 quadro), o display recebe a anterior. */
#include "modbox.h"
#include "gpu.h"
#include "dark_ui.h"
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <math.h>
#include <string.h>
#include <map>

namespace G = Gdiplus;

struct Panel { std::wstring title; std::vector<std::wstring> lines; uint64_t frame = ~0ull; };
struct Sprite { const uint32_t *px = nullptr; int w = 0, h = 0; float x = 0, y = 0, dw = 0, dh = 0; int alpha = 255; uint64_t frame = ~0ull;
                bool operator==(const Sprite &o) const { return px == o.px && w == o.w && h == o.h && x == o.x && y == o.y && dw == o.dw && dh == o.dh && alpha == o.alpha; } };
struct Cena { unsigned shown = 0; std::wstring title, foot; std::vector<std::wstring> lines; Panel panel[4]; std::vector<Sprite> sprites; };   /* o que desenhar (copia) */
static Cena g_cena;                         /* estado pedido pelos mods (thread do jogo) */
static uint64_t g_frame = ~0ull; static bool g_dirty; static uint64_t g_version;
static std::map<int, Sprite> g_sprites;     /* sprites pedidos pelos mods (API 5), por id */
static std::vector<Sprite> g_sp_pedido;     /* sprites vivos da ultima imagem pedida */
static unsigned g_pedido = ~0u;             /* elementos da ultima imagem pedida (bit 0 = caixa, bits 1..4 = paineis): mudou = redesenha */
static std::vector<uint32_t> g_front;       /* imagem entregue ao display (so a thread do jogo mexe) */
static std::vector<uint32_t> g_back;        /* a thread de desenho escreve aqui */
static std::atomic<int> g_estado{0};        /* 0 livre, 1 desenhando, 2 pronta (a thread do jogo troca g_back <-> g_front) */
static const float U = (float)(GPU_OVW / 480);   /* 1 pixel do PSP = 4 do overlay: as medidas abaixo sao em pixels do PSP x U */

void modbox_show(uint64_t frame, const wchar_t *title, const std::vector<std::wstring> &lines, const wchar_t *foot) {
    Cena &c = g_cena;
    if (c.title != title || c.lines != lines || c.foot != foot) { c.title = title; c.lines = lines; c.foot = foot; g_dirty = true; }
    g_frame = frame;
}
void modbox_panel(uint64_t frame, int corner, const wchar_t *title, const std::vector<std::wstring> &lines) {
    if (corner < 0 || corner > 3) return;
    Panel &p = g_cena.panel[corner];
    if (p.title != title || p.lines != lines) { p.title = title; p.lines = lines; g_dirty = true; }
    p.frame = frame;
}
/* imagem RGBA (bytes R,G,B,A; o plugin guarda os pixels enquanto durar) desenhada por cima do jogo no retangulo (x, y, dw, dh) em pixels do PSP (480x272); chamar a cada quadro */
void modbox_sprite(uint64_t frame, int id, const uint32_t *rgba, int w, int h, float x, float y, float dw, float dh, int alpha) {
    if (!rgba || w <= 0 || h <= 0 || dw <= 0 || dh <= 0) return;
    Sprite s; s.px = rgba; s.w = w; s.h = h; s.x = x; s.y = y; s.dw = dw; s.dh = dh; s.alpha = std::max(0, std::min(255, alpha)); s.frame = frame;
    g_sprites[id] = s;
}
static bool vivo(uint64_t f, uint64_t frame) { return f != ~0ull && frame <= f + 3; }   /* o mod pediu nos ultimos quadros (a logica do jogo roda a 30 fps) */

/* ------------------------------------------------------------------ GDI+: inicio, fontes, texto e formas */
static G::Color cor(uint32_t rgb, int a = 255) { return G::Color((BYTE)a, (BYTE)(rgb >> 16), (BYTE)(rgb >> 8), (BYTE)rgb); }
/* Segoe UI (e a variante Semibold); sem ela, a sans-serif padrao do sistema. As familias ficam vivas ate o fim (nunca apagadas). */
static std::unique_ptr<G::Font> fonte(float px, int estilo, bool semi = false) {
    static const G::FontFamily *fr = nullptr, *fs = nullptr;
    if (!fr) {
        G::FontFamily *a = new G::FontFamily(L"Segoe UI"), *b = new G::FontFamily(L"Segoe UI Semibold");
        fr = a->IsAvailable() ? a : G::FontFamily::GenericSansSerif(); fs = b->IsAvailable() ? b : fr;
    }
    return std::unique_ptr<G::Font>(new G::Font(semi ? fs : fr, px, estilo, G::UnitPixel));
}
static const G::StringFormat *formato(void) {   /* tipografico (medida justa), sem quebra, "..." quando nao cabe, centrado na vertical */
    static G::StringFormat *f = nullptr;
    if (!f) {
        f = G::StringFormat::GenericTypographic()->Clone();
        f->SetFormatFlags(f->GetFormatFlags() | G::StringFormatFlagsNoWrap | G::StringFormatFlagsMeasureTrailingSpaces);
        f->SetTrimming(G::StringTrimmingEllipsisCharacter); f->SetLineAlignment(G::StringAlignmentCenter);
    }
    return f;
}
static void preparar(G::Graphics &g) {
    g.SetSmoothingMode(G::SmoothingModeAntiAlias); g.SetPixelOffsetMode(G::PixelOffsetModeHalf);
    g.SetTextRenderingHint(G::TextRenderingHintAntiAliasGridFit);   /* tons de cinza: o fundo do bitmap e transparente (ClearType borraria) */
}
static float largura(G::Graphics &g, const std::wstring &s, const G::Font &f) {
    if (s.empty()) return 0;
    G::RectF r; g.MeasureString(s.c_str(), (INT)s.size(), &f, G::PointF(0, 0), formato(), &r); return r.Width;
}
static void texto(G::Graphics &g, const std::wstring &s, const G::Font &f, G::Color c, float x, float y, float w, float h,
                  G::StringAlignment al = G::StringAlignmentNear) {
    if (s.empty() || w <= 0) return;
    G::StringFormat sf(formato()); sf.SetAlignment(al); G::SolidBrush b(c);
    g.DrawString(s.c_str(), (INT)s.size(), &f, G::RectF(x, y, w, h), &sf, &b);
}
static void arredondado(G::GraphicsPath &p, float x, float y, float w, float h, float r) {
    r = std::min(r, std::min(w, h) / 2); const float d = 2 * r;
    p.Reset();
    if (r < 0.5f) { p.AddRectangle(G::RectF(x, y, w, h)); return; }
    p.AddArc(x, y, d, d, 180, 90); p.AddArc(x + w - d, y, d, d, 270, 90); p.AddArc(x + w - d, y + h - d, d, d, 0, 90); p.AddArc(x, y + h - d, d, d, 90, 90);
    p.CloseFigure();
}
/* degrade vertical (ou horizontal) que nao "volta" a cor do inicio nas bordas com antialias */
static G::LinearGradientBrush *degrade(float x0, float y0, float x1, float y1, G::Color a, G::Color b) {
    auto *br = new G::LinearGradientBrush(G::PointF(x0, y0), G::PointF(x1, y1), a, b); br->SetWrapMode(G::WrapModeTileFlipXY); return br;
}
static void preencher(G::Graphics &g, const G::GraphicsPath &p, G::Color c) { G::SolidBrush b(c); g.FillPath(&b, &p); }
static const float RAIO = 3 * U;   /* cantos da caixa e dos paineis */
/* moldura comum da caixa e dos paineis: fundo em degrade, faixa do titulo com brilho, filete dourado sob a faixa e borda dourada */
static void moldura(G::Graphics &g, float x, float y, float w, float h, float faixa, int alfa) {
    const float r = RAIO;
    G::GraphicsPath corpo; arredondado(corpo, x, y, w, h, r);
    { std::unique_ptr<G::LinearGradientBrush> b(degrade(0, y, 0, y + h, cor(0x1a2240, alfa), cor(0x090c19, alfa))); g.FillPath(b.get(), &corpo); }
    if (faixa > 0) {
        g.SetClip(&corpo);
        std::unique_ptr<G::LinearGradientBrush> b(degrade(0, y, 0, y + faixa, cor(0x384a96), cor(0x1a2252)));
        g.FillRectangle(b.get(), x, y, w, faixa);
        std::unique_ptr<G::LinearGradientBrush> br(degrade(0, y, 0, y + faixa * 0.55f, G::Color(46, 255, 255, 255), G::Color(0, 255, 255, 255)));
        g.FillRectangle(br.get(), x, y, w, faixa * 0.55f);
        g.ResetClip();
        G::LinearGradientBrush fl(G::PointF(x, 0), G::PointF(x + w, 0), cor(0xe8c878, 0), cor(0xe8c878, 0));
        G::Color cs[3] = {cor(0xe8c878, 20), cor(0xf8dc94), cor(0xe8c878, 20)}; G::REAL ps[3] = {0, 0.5f, 1};
        fl.SetInterpolationColors(cs, ps, 3); fl.SetWrapMode(G::WrapModeTileFlipXY);
        g.FillRectangle(&fl, x + U, y + faixa - 0.3f * U, w - 2 * U, 0.55f * U);
    }
    std::unique_ptr<G::LinearGradientBrush> bb(degrade(0, y, 0, y + h, cor(0xf4d88e), cor(0x86683a)));
    G::Pen pen(bb.get(), 0.55f * U); G::GraphicsPath borda; arredondado(borda, x + 0.3f * U, y + 0.3f * U, w - 0.6f * U, h - 0.6f * U, r - 0.3f * U);
    g.DrawPath(&pen, &borda);
}
static void estrela(G::Graphics &g, float cx, float cy, float r, G::Color c) {
    G::PointF pt[10];
    for (int i = 0; i < 10; i++) { const float a = -1.5707963f + i * 0.6283185f, rr = (i & 1) ? r * 0.45f : r; pt[i] = G::PointF(cx + rr * cosf(a), cy + rr * sinf(a)); }
    G::SolidBrush b(c); g.FillPolygon(&b, pt, 10);
}
static void olho(G::Graphics &g, float x, float cy, float w, G::Color c) {   /* icone de olho (titulo "@e...") */
    const float h = w * 0.62f; G::GraphicsPath p;
    p.AddBezier(x, cy, x + w * 0.25f, cy - h * 0.75f, x + w * 0.75f, cy - h * 0.75f, x + w, cy);
    p.AddBezier(x + w, cy, x + w * 0.75f, cy + h * 0.75f, x + w * 0.25f, cy + h * 0.75f, x, cy); p.CloseFigure();
    G::Pen pen(c, 0.5f * U); g.DrawPath(&pen, &p);
    G::SolidBrush b(c); const float ir = h * 0.36f; g.FillEllipse(&b, x + w / 2 - ir, cy - ir, 2 * ir, 2 * ir);
    G::SolidBrush brilho(G::Color(200, 255, 255, 255)); const float br = ir * 0.38f; g.FillEllipse(&brilho, x + w / 2 - ir * 0.15f, cy - ir * 0.7f, br, br);
}

/* pixel (sr, sg, sb, alfa sa) "por cima" do pixel d do overlay (ABGR, alfa reto) */
static inline void misturar(uint32_t &d, uint32_t sr, uint32_t sg, uint32_t sb, uint32_t sa) {
    const uint32_t da = d >> 24;
    if (sa == 255 || !da) { d = (sa << 24) | (sb << 16) | (sg << 8) | sr; return; }
    const uint32_t oa = sa + da * (255 - sa) / 255, ws = sa * 255, wd = da * (255 - sa), q = oa * 255;
    const uint32_t dr = d & 255, dg = (d >> 8) & 255, db = (d >> 16) & 255;
    d = (oa << 24) | ((sb * ws + db * wd) / q << 16) | ((sg * ws + dg * wd) / q << 8) | ((sr * ws + dr * wd) / q);
}
/* copia o bitmap (ARGB reto) para o overlay na posicao (ox, oy), por cima do que ja estiver la */
static void compor(std::vector<uint32_t> &dst, G::Bitmap &bmp, int ox, int oy) {
    G::Rect r(0, 0, (INT)bmp.GetWidth(), (INT)bmp.GetHeight()); G::BitmapData bd;
    if (bmp.LockBits(&r, G::ImageLockModeRead, PixelFormat32bppARGB, &bd) != G::Ok) return;
    for (int y = 0; y < r.Height; y++) {
        const int dy = oy + y; if (dy < 0 || dy >= GPU_OVH) continue;
        const uint32_t *s = (const uint32_t *)((const uint8_t *)bd.Scan0 + (ptrdiff_t)y * bd.Stride);
        for (int x = 0; x < r.Width; x++) {
            const int dx = ox + x; const uint32_t p = s[x]; if (dx < 0 || dx >= GPU_OVW || !(p >> 24)) continue;
            misturar(dst[(size_t)dy * GPU_OVW + dx], (p >> 16) & 255, (p >> 8) & 255, p & 255, p >> 24);
        }
    }
    bmp.UnlockBits(&bd);
}
/* sombra suave do retangulo arredondado (x, y, w, h) da tela: alfa pela distancia ate a borda (alcance s), um pouco abaixo (luz de cima).
 * Calculada aqui em vez de camadas do GDI+ (12 preenchimentos com antialias custavam ~8 ms por painel). */
static void sombra(std::vector<uint32_t> &dst, float x, float y, float w, float h, float s) {
    y += s * 0.35f;
    const float cx = x + w / 2, cy = y + h / 2, hx = w / 2 - RAIO, hy = h / 2 - RAIO;
    const int x0 = std::max(0, (int)(x - s)), x1 = std::min(GPU_OVW, (int)(x + w + s) + 1), y0 = std::max(0, (int)(y - s)), y1 = std::min(GPU_OVH, (int)(y + h + s) + 1);
    for (int py = y0; py < y1; py++) {
        const float dy = std::max(fabsf(py + 0.5f - cy) - hy, 0.f);
        for (int px = x0; px < x1; px++) {
            const float dx = std::max(fabsf(px + 0.5f - cx) - hx, 0.f), d = sqrtf(dx * dx + dy * dy) - RAIO;
            const float t = d <= 0 ? 0 : d / s; if (t >= 1) continue;
            misturar(dst[(size_t)py * GPU_OVW + px], 0, 0, 0, (uint32_t)(130 * (1 - t) * (1 - t)));
        }
    }
}

/* ------------------------------------------------------------------ marcacao das linhas (mods_sdk/tfmod.h) */
struct Linha { char tipo = 'p'; wchar_t moldura = 0, atrib = L'-'; std::wstring selo, texto; std::vector<std::wstring> cols; };   /* p simples, h secao, c carta */
static Linha ler(std::wstring s) {
    Linha r;
    if (!s.empty() && s[0] == L'#') { r.tipo = 'h'; s.erase(0, 1); }
    else if (s.size() >= 3 && s[0] == L'@') { r.tipo = 'c'; r.moldura = s[1]; r.atrib = s[2]; s.erase(0, 3); }
    if (r.tipo != 'h' && !s.empty() && s[0] == L'{') { const size_t e = s.find(L'}'); if (e != std::wstring::npos) { r.selo = s.substr(1, e - 1); s.erase(0, e + 1); } }
    std::vector<std::wstring> c; size_t i = 0, t;   /* texto \t coluna \t coluna ... */
    while ((t = s.find(L'\t', i)) != std::wstring::npos) { c.push_back(s.substr(i, t - i)); i = t + 1; }
    c.push_back(s.substr(i));
    r.texto = c[0]; r.cols.assign(c.begin() + 1, c.end());
    if (r.tipo == 'p') while (!r.texto.empty() && r.texto[0] == L' ') r.texto.erase(0, 1);
    return r;
}
static uint32_t cor_moldura(wchar_t k) {   /* cores das molduras das cartas */
    switch (k) {
    case L'n': return 0xdcb65a; case L'e': return 0xe27a38; case L'f': return 0x9c6cd8; case L'r': return 0x5b8ff0;
    case L's': return 0x2bb58c; case L't': return 0xd4508f; case L'k': return 0x9aa3ad; default: return 0x7486b0;
    }
}
static bool cor_atrib(wchar_t k, uint32_t *c) {
    switch (k) {
    case L'l': *c = 0xf6d860; return true; case L'd': *c = 0x9d5cd8; return true; case L'w': *c = 0x48a2f6; return true;
    case L'f': *c = 0xf05a3a; return true; case L'e': *c = 0xb88c55; return true; case L'v': *c = 0x5ccc78; return true;
    case L'g': *c = 0xf2c23c; return true; default: return false;
    }
}
struct Fontes { std::unique_ptr<G::Font> tit, dir, sec, cont, nome, val, rot, selo, simples; };
/* celula: "*4" = estrela + 4; "ATK|1800" = rotulo pequeno + valor; senao o texto. Desenhada alinhada a direita em xd; numa coluna de
 * tabela (colw > 0) o rotulo fica na borda esquerda da coluna e o valor na direita ("ATK 1600" / "ATK    ?") */
static float celula(G::Graphics &g, const Fontes &f, const std::wstring &s, float xd, float y, float h, G::Color cv, bool desenhar, float colw = 0) {
    std::wstring rot, val = s; bool est = s.size() > 1 && s[0] == L'*';
    if (est) val = s.substr(1); else { const size_t b = s.find(L'|'); if (b != std::wstring::npos) { rot = s.substr(0, b); val = s.substr(b + 1); } }
    const float wv = largura(g, val, *f.val), wr = rot.empty() ? 0 : largura(g, rot, *f.rot) + 0.9f * U, we = est ? 3.6f * U + 0.6f * U : 0, w = we + wr + wv;
    if (desenhar) {
        float x = colw > w ? xd - colw : xd - w;
        if (est) { estrela(g, x + 1.8f * U, y + h / 2, 1.8f * U, cor(0xf4b43c)); x += we; }
        if (!rot.empty()) { texto(g, rot, *f.rot, cor(0x8e9cc0), x, y + 0.35f * U, wr + U, h); x += wr; }
        texto(g, val, *f.val, cv, xd - wv, y, wv + U, h);
    }
    return w;
}

/* painel: faixa de titulo, secoes e linhas de carta coloridas com colunas alinhadas, num canto (os de cima abaixo da faixa de 20 px do topo) */
static void render_panel(std::vector<uint32_t> &dst, int corner, const Panel &p) {
    Fontes f;
    f.tit = fonte(6.6f * U, G::FontStyleBold); f.dir = fonte(6 * U, G::FontStyleRegular, true); f.sec = fonte(4.3f * U, G::FontStyleBold);
    f.cont = fonte(4.2f * U, G::FontStyleBold); f.nome = fonte(5.8f * U, G::FontStyleRegular, true); f.val = fonte(5.4f * U, G::FontStyleRegular, true);
    f.rot = fonte(3.5f * U, G::FontStyleBold); f.selo = fonte(3.9f * U, G::FontStyleBold); f.simples = fonte(5 * U, G::FontStyleItalic);
    std::vector<Linha> ls; for (auto &l : p.lines) ls.push_back(ler(l));
    std::wstring tit = p.title, tdir; bool ico = false;
    if (tit.size() >= 2 && tit[0] == L'@' && tit[1] == L'e') { ico = true; tit.erase(0, 2); }
    if (const size_t t = tit.find(L'\t'); t != std::wstring::npos) { tdir = tit.substr(t + 1); tit.resize(t); }
    for (auto &l : ls) if (l.tipo == 'h') CharUpperBuffW(&l.texto[0], (DWORD)l.texto.size());

    const float pad = 3.4f * U, faixa = 13 * U, secH = 8.5f * U, rowH = 10 * U, rowGap = 1.1f * U, plainH = 8 * U, fim = 3.2f * U;
    const float listra = 1.3f * U, dentro = 2.6f * U, ponto = 3.2f * U, gapCol = 3 * U, colPad = 2.6f * U, M = U;   /* M: margem do bitmap (antialias da borda) */
    G::Bitmap medir(1, 1, PixelFormat32bppPARGB); G::Graphics gm(&medir); preparar(gm);
    /* colunas como tabela: as linhas com o maior numero de colunas definem as larguras (alinhadas a direita) */
    size_t K = 0; for (auto &l : ls) if (l.tipo != 'h') K = std::max(K, l.cols.size());
    std::vector<float> cw(K, 0.f);
    for (auto &l : ls) if (l.tipo != 'h' && l.cols.size() == K) for (size_t j = 0; j < K; j++) cw[j] = std::max(cw[j], celula(gm, f, l.cols[j], 0, 0, 0, G::Color(), false));
    bool algum_ponto = false; for (auto &l : ls) if (l.tipo == 'c' && l.atrib != L'-') algum_ponto = true;
    float wselo = 0; for (auto &l : ls) if (!l.selo.empty()) wselo = std::max(wselo, largura(gm, l.selo, *f.selo));   /* selos da mesma largura: nomes alinhados */
    auto cols_w = [&](const Linha &l) {
        if (l.cols.empty()) return 0.f;
        float w = 0; for (size_t j = 0; j < l.cols.size(); j++) w += (l.cols.size() == K ? cw[j] : celula(gm, f, l.cols[j], 0, 0, 0, G::Color(), false)) + (j ? gapCol : 0);
        return w;
    };
    auto esquerda = [&](const Linha &l) {   /* da borda da linha ate o inicio do nome */
        float x = l.tipo == 'c' ? listra + dentro + (algum_ponto ? ponto + 1.8f * U : 0) : dentro;
        if (!l.selo.empty()) x += wselo + 2.4f * U + 1.6f * U;
        return x;
    };
    float W = (ico ? 9 * U : 0) + largura(gm, tit, *f.tit) + (tdir.empty() ? 0 : 6 * U + largura(gm, tdir, *f.dir)) + 2 * pad;
    for (auto &l : ls) {
        if (l.tipo == 'h') W = std::max(W, largura(gm, l.texto, *f.sec) + 14 * U + 2 * pad);
        else W = std::max(W, esquerda(l) + largura(gm, l.texto, l.tipo == 'c' ? *f.nome : *f.simples) + (l.cols.empty() ? 0 : gapCol + cols_w(l)) + colPad + 2 * pad);
    }
    W = std::min(std::max(W, 132 * U), 214 * U);
    /* altura; o que nao couber vira "+N" no fim */
    const float maxH = (272 - 20 - 17) * U;
    float H = faixa + 2.6f * U; size_t mostrar = ls.size(); int escondidas = 0;
    for (size_t i = 0; i < ls.size(); i++) {
        const float a = ls[i].tipo == 'h' ? (i ? 2.2f * U : 0) + secH : ls[i].tipo == 'c' ? rowH + rowGap : plainH;
        if (H + a + fim + (i + 1 < ls.size() ? plainH : 0) > maxH) { mostrar = i; break; }
        H += a;
    }
    for (size_t i = mostrar; i < ls.size(); i++) if (ls[i].tipo == 'c') escondidas++;
    if (escondidas) H += plainH;
    H += fim;

    G::Bitmap bmp((INT)(W + 2 * M + 1), (INT)(H + 2 * M + 1), PixelFormat32bppPARGB); G::Graphics g(&bmp); preparar(g); g.Clear(G::Color(0, 0, 0, 0));
    const float x0 = M, y0 = M;
    moldura(g, x0, y0, W, H, faixa, 232);
    float tx = x0 + pad;
    if (ico) { olho(g, tx, y0 + faixa / 2, 6.4f * U, cor(0xf4d88e)); tx += 9 * U; }
    const float wdir = tdir.empty() ? 0 : largura(g, tdir, *f.dir) + U;
    texto(g, tit, *f.tit, cor(0xffffff), tx, y0, x0 + W - pad - wdir - (wdir ? 3 * U : 0) - tx, faixa);
    if (!tdir.empty()) texto(g, tdir, *f.dir, cor(0xf8d88a), x0 + W - pad - wdir, y0, wdir, faixa, G::StringAlignmentFar);
    float y = y0 + faixa + 2.6f * U;
    const float rx = x0 + pad, rw = W - 2 * pad;
    for (size_t i = 0; i < mostrar; i++) {
        const Linha &l = ls[i];
        if (l.tipo == 'h') {   /* SECAO ----------- (N) */
            if (i) y += 2.2f * U;
            const float wt = largura(g, l.texto, *f.sec);
            texto(g, l.texto, *f.sec, cor(0x9fb4e6), rx + 0.6f * U, y, wt + U, secH);
            float xr = rx + rw;
            if (!l.cols.empty()) {
                const float wc = largura(g, l.cols[0], *f.cont), pw = std::max(wc + 3.2f * U, 6.4f * U), ph = 6 * U;
                G::GraphicsPath pill; arredondado(pill, xr - pw, y + (secH - ph) / 2, pw, ph, ph / 2); preencher(g, pill, G::Color(34, 255, 255, 255));
                texto(g, l.cols[0], *f.cont, cor(0xe4ebfa), xr - pw, y + 0.2f * U, pw, secH, G::StringAlignmentCenter);
                xr -= pw + 1.6f * U;
            }
            const float lx = rx + 0.6f * U + wt + 2 * U;
            if (xr > lx) { G::LinearGradientBrush b(G::PointF(lx, 0), G::PointF(xr + 1, 0), G::Color(70, 159, 180, 230), G::Color(10, 159, 180, 230)); b.SetWrapMode(G::WrapModeTileFlipXY); g.FillRectangle(&b, lx, y + secH / 2, xr - lx, 0.3f * U); }
            y += secH;
            continue;
        }
        const float h = l.tipo == 'c' ? rowH : plainH;
        float xl = rx;
        if (l.tipo == 'c') {   /* cartao: fundo tingido com a cor da moldura, listra a esquerda, ponto do atributo */
            const uint32_t mc = cor_moldura(l.moldura);
            G::GraphicsPath cart; arredondado(cart, rx, y, rw, h, 2 * U);
            { G::LinearGradientBrush b(G::PointF(rx, 0), G::PointF(rx + rw, 0), cor(mc, 62), cor(mc, 14)); b.SetWrapMode(G::WrapModeTileFlipXY); g.FillPath(&b, &cart); }
            { std::unique_ptr<G::LinearGradientBrush> b(degrade(0, y, 0, y + h * 0.5f, G::Color(22, 255, 255, 255), G::Color(0, 255, 255, 255))); g.SetClip(&cart); g.FillRectangle(b.get(), rx, y, rw, h * 0.5f);
              G::SolidBrush sb(cor(mc)); g.FillRectangle(&sb, rx, y, listra, h); g.ResetClip(); }
            xl = rx + listra + dentro;
            if (algum_ponto) {
                uint32_t ac; const float cy = y + h / 2, r = ponto / 2;
                if (cor_atrib(l.atrib, &ac)) {
                    G::SolidBrush b(cor(ac)); g.FillEllipse(&b, xl, cy - r, ponto, ponto);
                    G::Pen pn(G::Color(110, 255, 255, 255), 0.3f * U); g.DrawEllipse(&pn, xl, cy - r, ponto, ponto);
                } else if (l.atrib == L's' || l.atrib == L't') {   /* losango: magia / armadilha */
                    G::PointF pt[4] = {G::PointF(xl + r, cy - r * 1.1f), G::PointF(xl + ponto, cy), G::PointF(xl + r, cy + r * 1.1f), G::PointF(xl, cy)};
                    G::SolidBrush b(cor(cor_moldura(l.atrib))); g.FillPolygon(&b, pt, 4);
                    G::Pen pn(G::Color(110, 255, 255, 255), 0.3f * U); g.DrawPolygon(&pn, pt, 4);
                }
                xl += ponto + 1.8f * U;
            }
        } else xl = rx + dentro;
        if (!l.selo.empty()) {   /* selo (ex.: posicao no campo) */
            const float ws = wselo + 2.4f * U, hs = 5.8f * U;
            G::GraphicsPath s; arredondado(s, xl, y + (h - hs) / 2, ws, hs, 1.4f * U); preencher(g, s, G::Color(40, 255, 255, 255));
            texto(g, l.selo, *f.selo, cor(0xd6def0), xl, y + 0.2f * U, ws, h, G::StringAlignmentCenter);
            xl += ws + 1.6f * U;
        }
        /* colunas da direita para a esquerda */
        const bool mt = l.tipo == 'c' && (l.moldura == L's' || l.moldura == L't');
        const G::Color cv = mt ? cor(l.moldura == L's' ? 0x7fe0bf : 0xf29cc6) : cor(0xe6ebf5);
        float xr = rx + rw - colPad;
        for (size_t j = l.cols.size(); j-- > 0;) {   /* linha com todas as colunas: largura fixa da tabela; as outras, a do proprio texto */
            const float w = celula(g, f, l.cols[j], xr, y, h, cv, true, l.cols.size() == K ? cw[j] : 0);
            xr -= (l.cols.size() == K ? cw[j] : w) + gapCol;
        }
        const float xn = l.cols.empty() ? rx + rw - colPad : xr + gapCol - 2 * U;
        texto(g, l.texto, l.tipo == 'c' ? *f.nome : *f.simples, l.tipo == 'c' ? cor(0xffffff) : cor(0x95a2c4), xl, y, xn - xl, h);
        y += h + (l.tipo == 'c' ? rowGap : 0);
    }
    if (escondidas) { wchar_t b[32]; swprintf(b, 32, L"+%d", escondidas); texto(g, b, *f.simples, cor(0x95a2c4), rx + dentro, y, rw, plainH); }

    const int px = (int)((corner & 1) ? GPU_OVW - 3 * U - W : 3 * U), py = (int)((corner & 2) ? GPU_OVH - 17 * U - H : 20 * U);
    sombra(dst, (float)px, (float)py, W, H, 4 * U);
    compor(dst, bmp, px - (int)M, py - (int)M);
}

/* caixa de aviso (centro da tela): titulo na faixa, linhas centradas ("=N/M" = barra de N de M), rodape com a dica */
static void render_box(std::vector<uint32_t> &dst, const Cena &c) {
    std::unique_ptr<G::Font> ft = fonte(8 * U, G::FontStyleBold), fl = fonte(6.6f * U, G::FontStyleRegular, true), fr = fonte(5 * U, G::FontStyleRegular);
    const float W = 300 * U, faixa = 20 * U, lh = 15 * U, bh = 12 * U, rod = c.foot.empty() ? 4 * U : 17 * U, M = U;
    float H = faixa + 6 * U + rod;
    for (auto &l : c.lines) H += (!l.empty() && l[0] == L'=') ? bh : lh;
    G::Bitmap bmp((INT)(W + 2 * M + 1), (INT)(H + 2 * M + 1), PixelFormat32bppPARGB); G::Graphics g(&bmp); preparar(g); g.Clear(G::Color(0, 0, 0, 0));
    const float x0 = M, y0 = M;
    moldura(g, x0, y0, W, H, faixa, 246);
    texto(g, c.title, *ft, cor(0xfff0c4), x0 + 6 * U, y0, W - 12 * U, faixa, G::StringAlignmentCenter);
    float y = y0 + faixa + 3 * U;
    for (auto &l : c.lines) {
        int n = 0, m = 0;
        if (!l.empty() && l[0] == L'=' && swscanf(l.c_str() + 1, L"%d/%d", &n, &m) == 2 && m > 0 && m <= 40) {   /* barra segmentada */
            const float gap = 1.2f * U, sw = std::min(18 * U, (W - 60 * U - gap * (m - 1)) / m), sh = 5 * U, tw = sw * m + gap * (m - 1);
            float x = x0 + (W - tw) / 2; const float sy = y + (bh - sh) / 2;
            for (int i = 0; i < m; i++, x += sw + gap) {
                G::GraphicsPath s; arredondado(s, x, sy, sw, sh, 1.3f * U);
                if (i < n) { std::unique_ptr<G::LinearGradientBrush> b(degrade(0, sy, 0, sy + sh, cor(0xffe39a), cor(0xc8913a))); g.FillPath(b.get(), &s); }
                else preencher(g, s, G::Color(34, 255, 255, 255));
            }
            y += bh;
            continue;
        }
        texto(g, l, *fl, cor(0xffffff), x0 + 10 * U, y, W - 20 * U, lh, G::StringAlignmentCenter);
        y += lh;
    }
    if (!c.foot.empty()) {
        const float fy = y0 + H - rod;
        G::LinearGradientBrush b(G::PointF(x0, 0), G::PointF(x0 + W, 0), G::Color(0, 255, 255, 255), G::Color(0, 255, 255, 255));
        G::Color cs[3] = {G::Color(0, 255, 255, 255), G::Color(40, 255, 255, 255), G::Color(0, 255, 255, 255)}; G::REAL ps[3] = {0, 0.5f, 1};
        b.SetInterpolationColors(cs, ps, 3); b.SetWrapMode(G::WrapModeTileFlipXY); g.FillRectangle(&b, x0 + 10 * U, fy, W - 20 * U, 0.3f * U);
        texto(g, c.foot, *fr, cor(0xa8bce8), x0 + 6 * U, fy, W - 12 * U, rod, G::StringAlignmentCenter);
    }
    const int px = (int)((GPU_OVW - W) / 2), py = (int)((GPU_OVH - H) / 2 - 8 * U);
    sombra(dst, (float)px, (float)py, W, H, 6 * U);
    compor(dst, bmp, px - (int)M, py - (int)M);
}
/* sprites (API 5): amostragem bilinear da imagem do plugin no retangulo de destino, com o alfa dela x o alfa do sprite */
static void render_sprites(std::vector<uint32_t> &dst, const std::vector<Sprite> &v) {
    for (const Sprite &sp : v) {
        const float X = sp.x * U, Y = sp.y * U, W = sp.dw * U, H = sp.dh * U;
        const int x0 = std::max(0, (int)floorf(X)), x1 = std::min(GPU_OVW, (int)ceilf(X + W)), y0 = std::max(0, (int)floorf(Y)), y1 = std::min(GPU_OVH, (int)ceilf(Y + H));
        for (int py = y0; py < y1; py++) {
            const float fy = std::max(0.f, std::min((float)sp.h - 1, (py + 0.5f - Y) / H * sp.h - 0.5f)); const int iy = (int)fy; const float ty = fy - iy; const int iy1 = std::min(sp.h - 1, iy + 1);
            for (int px = x0; px < x1; px++) {
                const float fx = std::max(0.f, std::min((float)sp.w - 1, (px + 0.5f - X) / W * sp.w - 0.5f)); const int ix = (int)fx; const float tx = fx - ix; const int ix1 = std::min(sp.w - 1, ix + 1);
                const uint32_t p00 = sp.px[(size_t)iy * sp.w + ix], p10 = sp.px[(size_t)iy * sp.w + ix1], p01 = sp.px[(size_t)iy1 * sp.w + ix], p11 = sp.px[(size_t)iy1 * sp.w + ix1];
                float a = 0, r = 0, g = 0, b = 0;   /* mistura com alfa pre-multiplicado (evita franja escura nas bordas) */
                const uint32_t ps[4] = {p00, p10, p01, p11}; const float ws[4] = {(1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty};
                for (int k = 0; k < 4; k++) { const float pa = (ps[k] >> 24) * ws[k]; a += pa; r += (ps[k] & 255) * pa; g += ((ps[k] >> 8) & 255) * pa; b += ((ps[k] >> 16) & 255) * pa; }
                if (a < 0.5f) continue;
                const uint32_t sa = (uint32_t)(a * sp.alpha / 255.f + 0.5f);
                if (sa) misturar(dst[(size_t)py * GPU_OVW + px], (uint32_t)(r / a + 0.5f), (uint32_t)(g / a + 0.5f), (uint32_t)(b / a + 0.5f), sa);
            }
        }
    }
}
/* thread de desenho: uma cena por vez, no g_back; no fim marca "pronta" (a thread do jogo troca as imagens no proximo modbox_pixels) */
static void desenhar(Cena c) {
    g_back.assign((size_t)GPU_OVW * GPU_OVH, 0);
    render_sprites(g_back, c.sprites);   /* por baixo dos paineis e da caixa */
    if (dark::gdiplus()) {
        for (int k = 0; k < 4; k++) if (c.shown & (2u << k)) render_panel(g_back, k, c.panel[k]);
        if (c.shown & 1) render_box(g_back, c);   /* a caixa (pergunta/aviso) fica por cima dos paineis */
    }
    g_estado.store(2, std::memory_order_release);
}
/* quadros 3D texturizados (4 vertices) desenhados no buffer exibido: o plugin pergunta se a carta de uma casa do campo esta na tela (API 5, quad_near) */
struct Quad { float cx, cy, w, h; };
static std::map<uint32_t, std::vector<Quad>> g_qcur; static std::vector<Quad> g_qsnap;
void modbox_quad(uint32_t fb, float x0, float y0, float x1, float y1) {
    std::vector<Quad> &v = g_qcur[fb & 0x1ffff0u]; if (v.size() < 400) v.push_back({(x0 + x1) / 2, (y0 + y1) / 2, fabsf(x1 - x0), fabsf(y1 - y0)});
}
void modbox_quad_frame(uint32_t fb) { std::vector<Quad> &v = g_qcur[fb & 0x1ffff0u]; g_qsnap.swap(v); v.clear(); }
int modbox_quad_near(int x, int y, int tol, int wmin, int wmax) {
    for (const Quad &q : g_qsnap) if (fabsf(q.cx - x) <= tol && fabsf(q.cy - y) <= tol && q.w >= wmin && q.w <= wmax) return 1;
    return 0;
}
const uint32_t *modbox_pixels(uint64_t frame) {
    unsigned shown = vivo(g_frame, frame) ? 1u : 0u;
    for (int c = 0; c < 4; c++) if (vivo(g_cena.panel[c].frame, frame)) shown |= 2u << c;
    std::vector<Sprite> vivos; for (auto &kv : g_sprites) if (vivo(kv.second.frame, frame)) vivos.push_back(kv.second);
    if (!vivos.empty()) shown |= 32u;
    if (vivos != g_sp_pedido) { g_sp_pedido = vivos; g_dirty = true; }
    if (!shown) return nullptr;   /* nenhum mod pediu neste quadro */
    if (g_estado.load(std::memory_order_acquire) == 2) { g_front.swap(g_back); g_estado.store(0); g_version++; }
    if ((g_dirty || shown != g_pedido) && g_estado.load() == 0) {   /* algo mudou e a thread esta livre: desenha a cena atual (copia) */
        Cena c = g_cena; c.shown = shown; c.sprites = g_sp_pedido; g_dirty = false; g_pedido = shown; g_estado.store(1);
        std::thread(desenhar, std::move(c)).detach();
    }
    return g_front.empty() ? nullptr : g_front.data();   /* ate a primeira imagem ficar pronta: nada */
}
uint64_t modbox_version(void) { return g_version; }
