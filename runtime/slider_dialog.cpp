/* Janela escura com controle deslizante (tf_slider, ver dark_ui.h). Mesmo estilo da caixa de mensagem escura (dark_ui.cpp). */
#include "dark_ui.h"
#include <algorithm>
#include <vector>
#include <stdio.h>
#include <wchar.h>

namespace {
struct Slider {
    const wchar_t *text, *unit; int vmin, vmax, value, def; HFONT font, big, small; int dpi;
    RECT textRc, valRc, trackRc, minRc, maxRc; int footer;
    std::vector<HWND> btn; int result; bool done, drag; HWND hot;
};
int S(const Slider *b, int v) { return MulDiv(v, b->dpi, 96); }
void set_value(Slider *b, HWND h, int v) {
    v = std::max(b->vmin, std::min(b->vmax, v));
    if (v == b->value) return;
    b->value = v; InvalidateRect(h, nullptr, FALSE);
}
int value_at(const Slider *b, int x) {
    const int w = b->trackRc.right - b->trackRc.left; if (w <= 0) return b->value;
    const double t = std::max(0.0, std::min(1.0, (double)(x - b->trackRc.left) / w));
    return b->vmin + (int)(t * (b->vmax - b->vmin) + 0.5);
}
int x_of(const Slider *b, int v) {
    const int span = std::max(1, b->vmax - b->vmin);
    return b->trackRc.left + (int)((long long)(v - b->vmin) * (b->trackRc.right - b->trackRc.left) / span);
}
LRESULT CALLBACK slider_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    Slider *b = (Slider *)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (m) {
    case WM_NCCREATE: SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW *)l)->lpCreateParams); break;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC wdc = BeginPaint(h, &ps); RECT cr; GetClientRect(h, &cr);
        HDC dc = CreateCompatibleDC(wdc); HBITMAP bm = CreateCompatibleBitmap(wdc, cr.right, cr.bottom); HGDIOBJ obm = SelectObject(dc, bm);   /* sem tremulo ao arrastar */
        RECT top = cr; top.bottom -= b->footer; FillRect(dc, &top, dark::brush(dark::BG));
        RECT foot = cr; foot.top = cr.bottom - b->footer; FillRect(dc, &foot, dark::brush(dark::CARD));
        RECT line = foot; line.bottom = line.top + 1; FillRect(dc, &line, dark::brush(dark::LINE));
        dark::accent_line(dc, 0, cr.right, 0, S(b, 2));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, dark::TXT); HGDIOBJ of = SelectObject(dc, b->font);
        RECT tr = b->textRc; DrawTextW(dc, b->text, -1, &tr, DT_WORDBREAK | DT_NOPREFIX);
        wchar_t vt[32]; swprintf(vt, 32, L"%d%ls", b->value, b->unit);   /* valor atual, grande, em dourado */
        SetTextColor(dc, dark::ACC_HOT); SelectObject(dc, b->big); RECT vr = b->valRc; DrawTextW(dc, vt, -1, &vr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        /* trilho arredondado: marcas, parte preenchida e botao */
        const int ty = (b->trackRc.top + b->trackRc.bottom) / 2, th = S(b, 6), tx = x_of(b, b->value);
        if (b->vmax - b->vmin <= 24) for (int v = b->vmin; v <= b->vmax; v++) {
            const int x = x_of(b, v); RECT k = {x - 1, ty + S(b, 12), x + 1, ty + S(b, 18)}; FillRect(dc, &k, dark::brush(v <= b->value ? RGB(150, 124, 66) : dark::LINE));
        }
        RECT tk = {b->trackRc.left - th / 2, ty - th / 2, b->trackRc.right + th / 2, ty + th / 2}; dark::round_rect(dc, tk, dark::LINE, dark::LINE, th);
        RECT fl = tk; fl.right = tx + th / 2; dark::round_rect(dc, fl, dark::ACC, dark::ACC, th);
        const int rad = S(b, 10); RECT kn = {tx - rad, ty - rad, tx + rad, ty + rad};
        dark::round_rect(dc, kn, b->drag ? dark::ACC_HOT : dark::TXT, dark::ACC, 2 * rad);
        SetTextColor(dc, dark::DIM); SelectObject(dc, b->small);   /* extremos */
        wchar_t a[32], z[32]; swprintf(a, 32, L"%d%ls", b->vmin, b->unit); swprintf(z, 32, L"%d%ls", b->vmax, b->unit);
        RECT r1 = b->minRc, r2 = b->maxRc; DrawTextW(dc, a, -1, &r1, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX); DrawTextW(dc, z, -1, &r2, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, of);
        BitBlt(wdc, 0, 0, cr.right, cr.bottom, dc, 0, 0, SRCCOPY); SelectObject(dc, obm); DeleteObject(bm); DeleteDC(dc);
        EndPaint(h, &ps); return 0;
    }
    case WM_LBUTTONDOWN: {
        POINT p = {(short)LOWORD(l), (short)HIWORD(l)}; RECT hit = b->trackRc; InflateRect(&hit, S(b, 12), S(b, 14));
        if (PtInRect(&hit, p)) { b->drag = true; SetCapture(h); set_value(b, h, value_at(b, p.x)); InvalidateRect(h, nullptr, FALSE); }
        return 0;
    }
    case WM_MOUSEMOVE: if (b->drag) set_value(b, h, value_at(b, (short)LOWORD(l))); break;
    case WM_LBUTTONUP: if (b->drag) { b->drag = false; ReleaseCapture(); InvalidateRect(h, nullptr, FALSE); } return 0;
    case WM_CAPTURECHANGED: if (b && b->drag) { b->drag = false; InvalidateRect(h, nullptr, FALSE); } return 0;   /* perdeu o mouse no meio do arraste (Alt+Tab): sem isso o valor seguia o mouse solto */
    case WM_MOUSEWHEEL: set_value(b, h, b->value + (GET_WHEEL_DELTA_WPARAM(w) > 0 ? 1 : -1)); return 0;
    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT *d = (const DRAWITEMSTRUCT *)l;
        dark::draw_button(d, (int)d->CtlID == IDOK, b->font, MulDiv(1000, b->dpi, 96), dark::CARD);
        return TRUE;
    }
    case WM_COMMAND:
        if (HIWORD(w) == BN_CLICKED) {
            const int id = LOWORD(w);
            if (id == IDRETRY) set_value(b, h, b->def);   /* "Padrao" */
            else { b->result = id; b->done = true; }
        }
        return 0;
    case WM_CLOSE: b->result = IDCANCEL; b->done = true; return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
}  // namespace

bool tf_slider(HWND owner, const wchar_t *title, const wchar_t *text, const wchar_t *unit, int vmin, int vmax, int def, int *value) {
    /* DPI proprio da janela (o processo nao e DPI-aware: sem isso o texto sai borrado em telas com escala) */
    typedef HANDLE (WINAPI *SetCtx)(HANDLE);
    static SetCtx setctx = (SetCtx)(void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetThreadDpiAwarenessContext");
    HANDLE oldctx = setctx ? setctx((HANDLE)-4) : nullptr;   /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */
    Slider b = {}; b.text = text ? text : L""; b.unit = unit ? unit : L""; b.vmin = vmin; b.vmax = std::max(vmin + 1, vmax);
    b.def = std::max(b.vmin, std::min(b.vmax, def)); b.value = std::max(b.vmin, std::min(b.vmax, *value)); b.result = IDCANCEL;
    { HDC sdc = GetDC(nullptr); b.dpi = GetDeviceCaps(sdc, LOGPIXELSX); ReleaseDC(nullptr, sdc); }
    if (owner) { typedef UINT (WINAPI *GetDpi)(HWND); static GetDpi gd = (GetDpi)(void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"); if (gd && gd(owner)) b.dpi = (int)gd(owner); }
    if (b.dpi < 96) b.dpi = 96;
    auto font = [&](int px, int weight) { return CreateFontW(-S(&b, px), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI"); };
    b.font = font(15, FW_NORMAL); b.big = font(34, FW_SEMIBOLD); b.small = font(13, FW_NORMAL);
    const int pad = S(&b, 24), bw = S(&b, 104), bh = S(&b, 32), gap = S(&b, 10), cw = S(&b, 440);
    HDC mdc = GetDC(nullptr); HGDIOBJ of = SelectObject(mdc, b.font);
    RECT tr = {0, 0, cw - 2 * pad, 0}; DrawTextW(mdc, b.text, -1, &tr, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(mdc, of); ReleaseDC(nullptr, mdc);
    b.footer = bh + 2 * S(&b, 14);
    b.textRc = {pad, pad, pad + tr.right, pad + tr.bottom};
    b.valRc = {pad, b.textRc.bottom + S(&b, 8), cw - pad, b.textRc.bottom + S(&b, 8) + S(&b, 48)};
    const int trackY = b.valRc.bottom + S(&b, 14);
    b.trackRc = {pad + S(&b, 12), trackY, cw - pad - S(&b, 12), trackY + S(&b, 4)};
    b.minRc = {pad, trackY + S(&b, 26), pad + S(&b, 80), trackY + S(&b, 44)}; b.maxRc = {cw - pad - S(&b, 80), trackY + S(&b, 26), cw - pad, trackY + S(&b, 44)};
    const int ch = b.minRc.bottom + S(&b, 18) + b.footer;
    static bool reg = false; HINSTANCE hi = GetModuleHandleW(nullptr);
    if (!reg) { WNDCLASSW wc = {}; wc.lpfnWndProc = slider_proc; wc.hInstance = hi; wc.lpszClassName = L"TfDarkSlider"; wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW); RegisterClassW(&wc); reg = true; }
    const DWORD st = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN, ex = WS_EX_DLGMODALFRAME | (owner ? 0 : WS_EX_TOPMOST);   /* CLIPCHILDREN: arrastar o controle nao repinta o fundo por cima dos botoes */
    RECT wr = {0, 0, cw, ch}; AdjustWindowRectEx(&wr, st, FALSE, ex);
    const int ww = wr.right - wr.left, wh = wr.bottom - wr.top;
    RECT area; if (!owner || !GetWindowRect(owner, &area) || IsIconic(owner)) SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
    HWND h = CreateWindowExW(ex, L"TfDarkSlider", title ? title : L"", st, (area.left + area.right - ww) / 2, (area.top + area.bottom - wh) / 2, ww, wh, owner, nullptr, hi, &b);
    if (!h) { if (oldctx) setctx(oldctx); DeleteObject(b.font); DeleteObject(b.big); DeleteObject(b.small); return false; }
    dark::title_bar(h);
    const wchar_t *labels[3] = {L"Default", L"Cancel", L"OK"}; const int ids[3] = {IDRETRY, IDCANCEL, IDOK};
    int bx = cw - pad - 3 * bw - 2 * gap; const int by = ch - b.footer + S(&b, 14);
    for (int i = 0; i < 3; i++, bx += bw + gap)
        b.btn.push_back(CreateWindowW(L"BUTTON", labels[i], WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, bx, by, bw, bh, h, (HMENU)(INT_PTR)ids[i], hi, nullptr));
    /* modal: desabilita o dono e roda o proprio laco de mensagens */
    const bool ownerOn = owner && IsWindowEnabled(owner);
    if (ownerOn) EnableWindow(owner, FALSE);
    ShowWindow(h, SW_SHOW); SetForegroundWindow(h); SetFocus(b.btn[2]);
    MSG msg; bool quit = false; int quitCode = 0;
    while (!b.done) {
        if (!GetMessageW(&msg, nullptr, 0, 0)) { quit = true; quitCode = (int)msg.wParam; b.result = IDCANCEL; break; }
        if (msg.message == WM_KEYDOWN && (msg.hwnd == h || IsChild(h, msg.hwnd))) {
            const WPARAM k = msg.wParam;
            if (k == VK_ESCAPE) { b.result = IDCANCEL; b.done = true; continue; }
            if (k == VK_RETURN) {   /* Enter = o botao com foco (Padrao so volta o valor; Cancelar cancela); foco fora dos botoes = OK */
                const HWND f = GetFocus();
                if (f == b.btn[0]) { set_value(&b, h, b.def); continue; }
                b.result = f == b.btn[1] ? IDCANCEL : IDOK; b.done = true; continue;
            }
            if (k == VK_LEFT || k == VK_DOWN) { set_value(&b, h, b.value - 1); continue; }
            if (k == VK_RIGHT || k == VK_UP) { set_value(&b, h, b.value + 1); continue; }
            if (k == VK_HOME) { set_value(&b, h, b.vmin); continue; }
            if (k == VK_END) { set_value(&b, h, b.vmax); continue; }
            if (k == VK_PRIOR) { set_value(&b, h, b.value + 5); continue; }
            if (k == VK_NEXT) { set_value(&b, h, b.value - 5); continue; }
            if (k == VK_TAB) {   /* Tab / Shift+Tab: foco entre Padrao, Cancelar e OK (as setas mudam o valor) */
                int i = 2; for (int q = 0; q < 3; q++) if (GetFocus() == b.btn[q]) i = q;
                SetFocus(b.btn[(i + (GetKeyState(VK_SHIFT) < 0 ? 2 : 1)) % 3]); continue;
            }
        }
        if (msg.message == WM_MOUSEMOVE) {   /* realce do botao sob o mouse */
            POINT p; GetCursorPos(&p); HWND under = WindowFromPoint(p);
            if (under != b.hot) { for (HWND bt : b.btn) if (bt == under || bt == b.hot) InvalidateRect(bt, nullptr, FALSE); b.hot = under; }
        }
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    if (ownerOn) EnableWindow(owner, TRUE);   /* antes de destruir: o foco volta para o dono, nao para outro programa */
    DestroyWindow(h);
    if (owner) SetForegroundWindow(owner);
    DeleteObject(b.font); DeleteObject(b.big); DeleteObject(b.small);
    if (oldctx) setctx(oldctx);
    if (quit) PostQuitMessage(quitCode);
    if (b.result == IDOK) { *value = b.value; return true; }
    return false;
}
