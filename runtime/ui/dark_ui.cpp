/* Interface escura das janelas nativas: ver dark_ui.h. */
#include "dark_ui.h"
#include <dwmapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <map>
#include <mutex>
#include <vector>

namespace dark {
HBRUSH brush(COLORREF c) {
    static std::mutex mu; std::lock_guard<std::mutex> lk(mu);   /* (os paineis dos mods desenham numa thread propria) */
    static std::map<COLORREF, HBRUSH> m;
    auto it = m.find(c); if (it != m.end()) return it->second;
    return m[c] = CreateSolidBrush(c);
}
void title_bar(HWND h) {
    BOOL on = TRUE;
    if (FAILED(DwmSetWindowAttribute(h, 20, &on, sizeof on))) DwmSetWindowAttribute(h, 19, &on, sizeof on);   /* DWMWA_USE_IMMERSIVE_DARK_MODE (19 = antes do Windows 10 20H1) */
    /* Windows 11: barra de titulo na cor da barra de menu, texto claro e borda discreta (no Windows 10 as chamadas so falham) */
    const COLORREF cap = BAR, txt = TXT, bord = LINE;
    DwmSetWindowAttribute(h, 35, &cap, sizeof cap); DwmSetWindowAttribute(h, 36, &txt, sizeof txt); DwmSetWindowAttribute(h, 34, &bord, sizeof bord);
}
bool gdiplus(void) {
    static const bool ok = [] { ULONG_PTR tok; Gdiplus::GdiplusStartupInput in; return Gdiplus::GdiplusStartup(&tok, &in, nullptr) == Gdiplus::Ok; }();   /* nunca desligado */
    return ok;
}
void round_rect(HDC dc, RECT r, COLORREF fill, COLORREF edge, int rad) {
    if (!gdiplus()) {
        HPEN p = CreatePen(PS_SOLID, 1, edge); HGDIOBJ op = SelectObject(dc, p), ob = SelectObject(dc, brush(fill));
        RoundRect(dc, r.left, r.top, r.right, r.bottom, rad, rad); SelectObject(dc, op); SelectObject(dc, ob); DeleteObject(p); return;
    }
    namespace G = Gdiplus;
    G::Graphics g(dc); g.SetSmoothingMode(G::SmoothingModeAntiAlias); g.SetPixelOffsetMode(G::PixelOffsetModeHalf);
    const float x = (float)r.left, y = (float)r.top, w = (float)(r.right - r.left), h = (float)(r.bottom - r.top), d = (float)std::min(rad, (int)std::min(w, h));
    G::GraphicsPath p;
    if (d < 1) p.AddRectangle(G::RectF(x, y, w, h));
    else { p.AddArc(x, y, d, d, 180, 90); p.AddArc(x + w - d, y, d, d, 270, 90); p.AddArc(x + w - d, y + h - d, d, d, 0, 90); p.AddArc(x, y + h - d, d, d, 90, 90); p.CloseFigure(); }
    G::SolidBrush b(G::Color(255, GetRValue(fill), GetGValue(fill), GetBValue(fill))); g.FillPath(&b, &p);
    if (edge != fill) { G::Pen pn(G::Color(255, GetRValue(edge), GetGValue(edge), GetBValue(edge)), 1.0f); g.DrawPath(&pn, &p); }
}
void theme_control(HWND h) {
    typedef BOOL (WINAPI *Allow)(HWND, BOOL); typedef HRESULT (WINAPI *SetTheme)(HWND, LPCWSTR, LPCWSTR);
    static HMODULE ux = LoadLibraryW(L"uxtheme.dll"); if (!ux) return;
    static Allow allow = (Allow)(void *)GetProcAddress(ux, MAKEINTRESOURCEA(133));   /* AllowDarkModeForWindow (nao documentada, Windows 10 1809+) */
    static SetTheme set = (SetTheme)(void *)GetProcAddress(ux, "SetWindowTheme");
    if (allow) allow(h, TRUE);
    if (set) set(h, L"DarkMode_CFD", nullptr);
}
void accent_line(HDC dc, int x0, int x1, int y, int h) {
    if (x1 <= x0 || h <= 0) return;
    if (!gdiplus()) { RECT r = {x0, y, x1, y + h}; FillRect(dc, &r, brush(ACC)); return; }
    namespace G = Gdiplus;
    G::Graphics g(dc);
    G::LinearGradientBrush b(G::PointF((float)x0 - 1, 0), G::PointF((float)x1 + 1, 0), G::Color(0, 0, 0, 0), G::Color(0, 0, 0, 0));
    const G::Color cs[3] = {G::Color(0, GetRValue(ACC), GetGValue(ACC), GetBValue(ACC)), G::Color(255, GetRValue(ACC_HOT), GetGValue(ACC_HOT), GetBValue(ACC_HOT)),
                            G::Color(0, GetRValue(ACC), GetGValue(ACC), GetBValue(ACC))};
    const G::REAL ps[3] = {0, 0.5f, 1}; b.SetInterpolationColors(cs, ps, 3);
    g.FillRectangle(&b, x0, y, x1 - x0, h);
}
static void draw_button_dc(const DRAWITEMSTRUCT *d, HDC dc, bool primary, HFONT font, int scale, COLORREF around) {
    auto S = [scale](int v) { return MulDiv(v, scale, 1000); };
    RECT r = d->rcItem;
    const bool off = (d->itemState & ODS_DISABLED) != 0, down = (d->itemState & ODS_SELECTED) != 0;
    POINT pt; GetCursorPos(&pt); RECT wr; GetWindowRect(d->hwndItem, &wr);
    const bool hot = !off && PtInRect(&wr, pt) && WindowFromPoint(pt) == d->hwndItem;
    FillRect(dc, &r, brush(around));   /* cantos fora do arredondado: a cor de tras */
    const COLORREF bg = off ? OFF : primary ? (hot || down ? ACC_HOT : ACC) : (hot || down ? BTN_HOT : BTN);
    round_rect(dc, r, bg, (primary || off) ? bg : (hot ? RGB(78, 92, 140) : LINE), S(8));
    wchar_t t[64]; GetWindowTextW(d->hwndItem, t, 64);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, off ? OFF_TXT : primary ? ACC_TXT : TXT);
    HGDIOBJ of = font ? SelectObject(dc, font) : nullptr; RECT tr = r; if (down) OffsetRect(&tr, 0, 1);
    DrawTextW(dc, t, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (of) SelectObject(dc, of);
    if (d->itemState & ODS_FOCUS) {   /* foco do teclado: anel por fora do botao */
        RECT f = r; InflateRect(&f, -S(2), -S(2));
        HPEN p = CreatePen(PS_DOT, 1, primary ? ACC_TXT : RGB(200, 206, 226)); HGDIOBJ op = SelectObject(dc, p), ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        RoundRect(dc, f.left, f.top, f.right, f.bottom, S(6), S(6)); SelectObject(dc, op); SelectObject(dc, ob); DeleteObject(p);
    }
}
void draw_button(const DRAWITEMSTRUCT *d, bool primary, HFONT font, int scale, COLORREF around) {   /* compoe num bitmap e copia de uma vez: pintar direto na tela piscava (fundo, caixa e texto em sequencia) */
    const int w = d->rcItem.right - d->rcItem.left, h = d->rcItem.bottom - d->rcItem.top; if (w <= 0 || h <= 0) return;
    HDC mdc = CreateCompatibleDC(d->hDC); HBITMAP bm = CreateCompatibleBitmap(d->hDC, w, h); HGDIOBJ obm = SelectObject(mdc, bm);
    DRAWITEMSTRUCT m = *d; m.hDC = mdc; SetRect(&m.rcItem, 0, 0, w, h);
    draw_button_dc(&m, mdc, primary, font, scale, around);
    BitBlt(d->hDC, d->rcItem.left, d->rcItem.top, w, h, mdc, 0, 0, SRCCOPY);
    SelectObject(mdc, obm); DeleteObject(bm); DeleteDC(mdc);
}
}  // namespace dark

/* ------------------------------------------------------------------ caixa de mensagem */
namespace {
struct Box {
    const wchar_t *text; HICON icon; HFONT font; int dpi;
    RECT textRc; int footer;
    std::vector<HWND> btn; std::vector<int> ids; int def, cancel, result; bool done; HWND hot;
};
int S(const Box *b, int v) { return MulDiv(v, b->dpi, 96); }
LRESULT CALLBACK box_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    Box *b = (Box *)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (m) {
    case WM_NCCREATE: SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW *)l)->lpCreateParams); break;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT cr; GetClientRect(h, &cr);
        RECT top = cr; top.bottom -= b->footer; FillRect(dc, &top, dark::brush(dark::BG));
        RECT foot = cr; foot.top = cr.bottom - b->footer; FillRect(dc, &foot, dark::brush(dark::CARD));
        RECT line = foot; line.bottom = line.top + 1; FillRect(dc, &line, dark::brush(dark::LINE));
        dark::accent_line(dc, 0, cr.right, 0, S(b, 2));
        if (b->icon) DrawIconEx(dc, S(b, 22), b->textRc.top + ((b->textRc.bottom - b->textRc.top) > S(b, 32) ? 0 : ((b->textRc.bottom - b->textRc.top) - S(b, 32)) / 2), b->icon, S(b, 32), S(b, 32), 0, nullptr, DI_NORMAL);
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, dark::TXT); HGDIOBJ of = SelectObject(dc, b->font);
        RECT tr = b->textRc; DrawTextW(dc, b->text, -1, &tr, DT_WORDBREAK | DT_NOPREFIX | DT_EXPANDTABS);
        SelectObject(dc, of); EndPaint(h, &ps); return 0;
    }
    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT *d = (const DRAWITEMSTRUCT *)l;
        dark::draw_button(d, (int)d->CtlID == b->ids[b->def], b->font, MulDiv(1000, b->dpi, 96), dark::CARD);   /* botoes sobre o rodape */
        return TRUE;
    }
    case WM_COMMAND: if (HIWORD(w) == BN_CLICKED) { b->result = LOWORD(w); b->done = true; } return 0;
    case WM_CLOSE: b->result = b->ids[b->cancel]; b->done = true; return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
}  // namespace

void set_app_icon(HWND h) {
    if (!h) return;
    typedef HANDLE (WINAPI *SetCtx)(HANDLE);
    typedef UINT (WINAPI *GetDpi)(HWND);
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    static SetCtx setctx = (SetCtx)(void *)GetProcAddress(u32, "SetThreadDpiAwarenessContext");
    static GetDpi gd = (GetDpi)(void *)GetProcAddress(u32, "GetDpiForWindow");
    HANDLE oldctx = setctx ? setctx((HANDLE)-4) : nullptr;   /* PER_MONITOR_AWARE_V2: DPI real da tela, mesmo com o processo nao DPI-aware */
    int real = 96; { HDC sdc = GetDC(nullptr); real = GetDeviceCaps(sdc, LOGPIXELSX); ReleaseDC(nullptr, sdc); }
    if (setctx) setctx(oldctx);
    int own = gd ? (int)gd(h) : 96;   /* 96 na janela do jogo (nao DPI-aware: o Windows estica a barra de titulo) */
    if (real < 96) real = 96; if (own < 96) own = 96;
    HINSTANCE hi = GetModuleHandleW(nullptr);
    HICON big = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(1), IMAGE_ICON, MulDiv(32, real, 96), MulDiv(32, real, 96), 0);
    HICON sm = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(1), IMAGE_ICON, MulDiv(16, own, 96), MulDiv(16, own, 96), 0);
    if (big) SendMessageW(h, WM_SETICON, ICON_BIG, (LPARAM)big);   /* (ficam vivos ate o fim do processo: uma janela de cada) */
    if (sm) SendMessageW(h, WM_SETICON, ICON_SMALL, (LPARAM)sm);
}

int tf_msgbox(HWND owner, const wchar_t *text, const wchar_t *title, UINT flags) {
    /* DPI proprio da caixa (o processo nao e DPI-aware: sem isso o texto sai borrado em telas com escala) */
    typedef HANDLE (WINAPI *SetCtx)(HANDLE);
    static SetCtx setctx = (SetCtx)(void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetThreadDpiAwarenessContext");
    HANDLE oldctx = setctx ? setctx((HANDLE)-4) : nullptr;   /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */
    Box b = {}; b.text = text ? text : L""; b.result = 0;
    { HDC sdc = GetDC(nullptr); b.dpi = GetDeviceCaps(sdc, LOGPIXELSX); ReleaseDC(nullptr, sdc); }
    if (owner) { typedef UINT (WINAPI *GetDpi)(HWND); static GetDpi gd = (GetDpi)(void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"); if (gd && gd(owner)) b.dpi = (int)gd(owner); }
    if (b.dpi < 96) b.dpi = 96;
    b.font = CreateFontW(-S(&b, 15), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    LPCWSTR ic = nullptr;   /* icones do sistema: 32513 erro, 32514 pergunta, 32515 aviso, 32516 informacao */
    switch (flags & MB_ICONMASK) {
    case MB_ICONERROR: ic = MAKEINTRESOURCEW(32513); break; case MB_ICONWARNING: ic = MAKEINTRESOURCEW(32515); break;
    case MB_ICONQUESTION: ic = MAKEINTRESOURCEW(32514); break; case MB_ICONINFORMATION: ic = MAKEINTRESOURCEW(32516); break;
    }
    if (ic) b.icon = (HICON)LoadImageW(nullptr, ic, IMAGE_ICON, S(&b, 32), S(&b, 32), LR_SHARED);
    /* botoes */
    std::vector<const wchar_t *> labels;
    switch (flags & MB_TYPEMASK) {
    case MB_YESNO: labels = {L"Yes", L"No"}; b.ids = {IDYES, IDNO}; b.cancel = 1; break;
    case MB_OKCANCEL: labels = {L"OK", L"Cancel"}; b.ids = {IDOK, IDCANCEL}; b.cancel = 1; break;
    default: labels = {L"OK"}; b.ids = {IDOK}; b.cancel = 0; break;
    }
    b.def = ((flags & MB_DEFMASK) == MB_DEFBUTTON2 && b.ids.size() > 1) ? 1 : 0;
    /* medidas */
    const int pad = S(&b, 22), iconW = b.icon ? S(&b, 32) + S(&b, 16) : 0, bw = S(&b, 100), bh = S(&b, 32), gap = S(&b, 10);
    HDC mdc = GetDC(nullptr); HGDIOBJ of = SelectObject(mdc, b.font);
    RECT tr = {0, 0, S(&b, 460), 0}; DrawTextW(mdc, b.text, -1, &tr, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EXPANDTABS);
    SelectObject(mdc, of); ReleaseDC(nullptr, mdc);
    const int textH = std::max<int>(tr.bottom, b.icon ? S(&b, 32) : 0);
    b.footer = bh + 2 * S(&b, 14);
    int cw = pad + iconW + tr.right + pad; const int bwAll = (int)labels.size() * bw + ((int)labels.size() - 1) * gap + 2 * pad;
    cw = std::max(cw, std::max(bwAll, S(&b, 320)));
    const int ch = pad + textH + pad + b.footer;
    b.textRc = {pad + iconW, pad + (textH - tr.bottom) / 2, pad + iconW + tr.right, pad + (textH - tr.bottom) / 2 + tr.bottom};
    /* janela */
    static bool reg = false; HINSTANCE hi = GetModuleHandleW(nullptr);
    if (!reg) { WNDCLASSW wc = {}; wc.lpfnWndProc = box_proc; wc.hInstance = hi; wc.lpszClassName = L"TfDarkMsgBox"; wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW); RegisterClassW(&wc); reg = true; }
    const DWORD st = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN, ex = WS_EX_DLGMODALFRAME | (owner ? 0 : WS_EX_TOPMOST);   /* CLIPCHILDREN: o fundo nao repinta por cima dos botoes */
    RECT wr = {0, 0, cw, ch}; AdjustWindowRectEx(&wr, st, FALSE, ex);
    const int ww = wr.right - wr.left, wh = wr.bottom - wr.top;
    RECT area; if (!owner || !GetWindowRect(owner, &area) || IsIconic(owner)) SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
    const int x = (area.left + area.right - ww) / 2, y = (area.top + area.bottom - wh) / 2;
    HWND h = CreateWindowExW(ex, L"TfDarkMsgBox", title ? title : L"", st, x, y, ww, wh, owner, nullptr, hi, &b);
    if (!h) { if (oldctx) setctx(oldctx); DeleteObject(b.font); return MessageBoxW(owner, text, title, flags); }
    dark::title_bar(h);
    int bx = cw - pad - (int)labels.size() * bw - ((int)labels.size() - 1) * gap; const int by = ch - b.footer + S(&b, 14);
    for (size_t i = 0; i < labels.size(); i++, bx += bw + gap)
        b.btn.push_back(CreateWindowW(L"BUTTON", labels[i], WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, bx, by, bw, bh, h, (HMENU)(INT_PTR)b.ids[i], hi, nullptr));
    if (flags & MB_ICONMASK) MessageBeep(flags & MB_ICONMASK);
    /* modal: desabilita o dono e roda o proprio laco de mensagens */
    const bool ownerOn = owner && IsWindowEnabled(owner);
    if (ownerOn) EnableWindow(owner, FALSE);
    ShowWindow(h, SW_SHOW); SetForegroundWindow(h); SetFocus(b.btn[b.def]);
    int focus = b.def; MSG msg; bool quit = false; int quitCode = 0;
    while (!b.done) {
        if (!GetMessageW(&msg, nullptr, 0, 0)) { quit = true; quitCode = (int)msg.wParam; b.result = b.ids[b.cancel]; break; }
        if (msg.message == WM_KEYDOWN && (msg.hwnd == h || IsChild(h, msg.hwnd))) {
            const WPARAM k = msg.wParam; const int n = (int)b.btn.size();
            if (k == VK_RETURN || k == VK_SPACE) { b.result = b.ids[focus]; b.done = true; continue; }
            if (k == VK_ESCAPE) { b.result = b.ids[b.cancel]; b.done = true; continue; }
            if (k == VK_LEFT || k == VK_RIGHT || k == VK_TAB) { focus = (focus + (k == VK_LEFT || (k == VK_TAB && GetKeyState(VK_SHIFT) < 0) ? n - 1 : 1)) % n; SetFocus(b.btn[focus]); continue; }
            if ((flags & MB_TYPEMASK) == MB_YESNO && (k == 'S' || k == 'Y')) { b.result = IDYES; b.done = true; continue; }
            if ((flags & MB_TYPEMASK) == MB_YESNO && k == 'N') { b.result = IDNO; b.done = true; continue; }
        }
        if (msg.message == WM_MOUSEMOVE) {   /* realce do botao sob o mouse */
            POINT p; GetCursorPos(&p); HWND under = WindowFromPoint(p);
            if (under != b.hot) { for (HWND bt : b.btn) if (bt == under || bt == b.hot) InvalidateRect(bt, nullptr, FALSE); b.hot = under; }
        }
        for (size_t i = 0; i < b.btn.size(); i++) if (GetFocus() == b.btn[i]) focus = (int)i;
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    if (ownerOn) EnableWindow(owner, TRUE);   /* antes de destruir: o foco volta para o dono, nao para outro programa */
    DestroyWindow(h);
    if (owner) SetForegroundWindow(owner);
    DeleteObject(b.font);
    if (oldctx) setctx(oldctx);
    if (quit) PostQuitMessage(quitCode);
    return b.result;
}
