/* Janela inicial. Tres estados:
 *  - jogo ainda nao instalado: escolhe a ISO e "Instalar e jogar" extrai tudo para a pasta jogo/ (uma unica vez, com barra de progresso);
 *    "Jogar direto da ISO" continua existindo (sem instalar);
 *  - instalando: barra de progresso;
 *  - jogo instalado: so "Jogar" (a ISO nao e mais necessaria); "Reinstalar da ISO..." refaz a pasta.
 * A ultima ISO escolhida fica em tagforce_recomp.ini (ao lado do .exe). Tema escuro, botoes desenhados aqui e DPI proprio desta janela. */
#include <windows.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include "dark_ui.h"
#include "util.h"
#include "gameid.h"
#include "menu.h"
#include "hle.h"

typedef void (*InstProgress)(double frac, const wchar_t *msg, void *ctx);
bool iso_install(const wchar_t *iso, const wchar_t *dest, InstProgress cb, void *ctx, std::wstring *err);

enum { ID_PICK = 101, ID_PLAY = 102, ID_QUIT = 103, ID_DIRECT = 104, TIMER_UI = 1 };
static HWND g_wnd, g_bpick, g_play, g_bquit, g_bdirect;
static std::wstring g_path;                 /* ISO escolhida */
static std::wstring g_data;                 /* pasta jogo/ */
static bool g_installed;                    /* jogo/ pronta */
static bool g_go, g_direct;                 /* sair para jogar; direto da ISO */
static int g_dpi = 96;
static HFONT g_fTitle, g_fSub, g_fText, g_fSmall, g_fBtn;
/* instalacao em segundo plano */
static std::thread g_thr; static std::atomic<bool> g_installing{false}, g_inst_done{false}, g_inst_ok{false};
static std::atomic<int> g_prog{0}; static std::mutex g_mu; static std::wstring g_msg, g_err;

static const COLORREF C_BG = dark::BG, C_CARD = dark::CARD, C_LINE = dark::LINE, C_TXT = dark::TXT, C_DIM = dark::DIM,   /* paleta comum (dark_ui.h) */
                      C_OK = RGB(98, 200, 130), C_WARN = RGB(240, 190, 90), C_ERR = RGB(240, 110, 110), C_BTN = dark::BTN, C_BTN_HOT = dark::BTN_HOT,
                      C_ACC = dark::ACC, C_ACC_HOT = dark::ACC_HOT, C_OFF = dark::OFF, C_BAR = dark::LINE;
static int S(int v) { return MulDiv(v, g_dpi, 96); }

static std::wstring ini_path() { return app_ini_path(); }
static bool file_exists(const std::wstring &p) { DWORD a = GetFileAttributesW(p.c_str()); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }
static bool iso_ok() { return !g_path.empty() && file_exists(g_path); }
static void update_data_dir() {   /* a pasta de dados depende da versao do jogo da ISO escolhida (gameid.cpp) */
    std::string id = iso_ok() ? game_id_iso(g_path) : ""; g_data = exe_dir() + game_data_dir_name(id.empty() ? game_id_built() : id);
}
static bool check_installed() { g_installed = file_exists(g_data + L"\\_disco.tsv"); return g_installed; }
static void refresh() {
    bool busy = g_installing;
    EnableWindow(g_play, !busy && (g_installed || iso_ok())); EnableWindow(g_bpick, !busy); EnableWindow(g_bdirect, !busy && !g_installed && iso_ok());
    ShowWindow(g_bdirect, g_installed || jit_generic() ? SW_HIDE : SW_SHOW);
    SetWindowTextW(g_play, g_installed ? L"Play" : L"Install and play"); SetWindowTextW(g_bpick, g_installed ? L"Reinstall from ISO..." : L"Select ISO...");
    InvalidateRect(g_wnd, nullptr, FALSE); for (HWND b : {g_bpick, g_play, g_bquit, g_bdirect}) InvalidateRect(b, nullptr, FALSE);
}
static bool pick(HWND owner) {
    wchar_t buf[1024] = L"";
    OPENFILENAMEW of = {}; of.lStructSize = sizeof of; of.hwndOwner = owner;
    of.lpstrFilter = L"Imagem de UMD (*.iso)\0*.iso\0Todos os arquivos\0*.*\0";
    of.lpstrFile = buf; of.nMaxFile = 1024; of.lpstrTitle = L"Select the Yu-Gi-Oh! GX Tag Force ISO";
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;   /* nao mudar a pasta atual (ms0/) */
    if (!GetOpenFileNameW(&of)) return false;
    g_path = buf; WritePrivateProfileStringW(L"geral", L"iso", buf, ini_path().c_str()); update_data_dir(); check_installed(); return true;
}
static void start_install(bool install = true) {
    if (g_thr.joinable()) g_thr.join();
    g_installing = true; g_inst_done = false; g_inst_ok = false; g_prog = 0; { std::lock_guard<std::mutex> lk(g_mu); g_err.clear(); g_msg = L"preparing..."; }
    g_thr = std::thread([install] {
        std::wstring err; bool ok = true; const bool gen = jit_generic();
        auto prog = [](double f, const wchar_t *m, void *c) { double lo = c ? *(double *)c : 0, hi = c ? ((double *)c)[1] : 1; g_prog = (int)((lo + f * (hi - lo)) * 1000); std::lock_guard<std::mutex> lk(g_mu); g_msg = m; };
        double inst[2] = {0, gen ? 0.4 : 1}, code[2] = {gen && install ? 0.4 : 0, 1};
        if (install) ok = iso_install(g_path.c_str(), g_data.c_str(), prog, inst, &err);
        if (ok && gen) {   /* the release has no game code: translate it from the installed files (a few minutes, only once) */
            std::string id = game_id_dir(g_data); if (id.empty()) id = game_id_built();
            game_id_set_built(id);
            ok = code_prepare(id, g_data, prog, code, &err);
        }
        { std::lock_guard<std::mutex> lk(g_mu); g_err = err; } g_inst_ok = ok; g_inst_done = true;
    });
    refresh();
}
static HFONT make_font(int px, int weight) { return CreateFontW(-S(px), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI"); }

static void round_fill(HDC dc, RECT r, COLORREF fill, COLORREF line, int rad) { dark::round_rect(dc, r, fill, line, rad); }   /* antialias (GDI+) */
static bool is_hot(HWND b) {
    if (!IsWindowEnabled(b) || !IsWindowVisible(b)) return false;
    POINT pt; GetCursorPos(&pt); RECT wr; GetWindowRect(b, &wr);
    return PtInRect(&wr, pt) && WindowFromPoint(pt) == b;
}
static void draw_button_dc(const DRAWITEMSTRUCT *d, HDC dc) {
    RECT r = d->rcItem; bool primary = d->CtlID == ID_PLAY, flat = d->CtlID == ID_DIRECT, off = (d->itemState & ODS_DISABLED) != 0, down = (d->itemState & ODS_SELECTED) != 0;
    bool hot = !off && is_hot(d->hwndItem);
    static HBRUSH bgb = CreateSolidBrush(C_BG); FillRect(dc, &r, bgb);
    wchar_t t[64]; GetWindowTextW(d->hwndItem, t, 64);
    SetBkMode(dc, TRANSPARENT);
    if (flat) {   /* texto sublinhado, sem caixa */
        SetTextColor(dc, off ? RGB(100, 100, 108) : hot ? C_TXT : C_DIM);
        HFONT u = CreateFontW(-S(12), 0, 0, 0, FW_NORMAL, 0, 1, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI"); HGDIOBJ of = SelectObject(dc, u);
        DrawTextW(dc, t, -1, &r, DT_RIGHT | DT_VCENTER | DT_SINGLELINE); SelectObject(dc, of); DeleteObject(u); return;
    }
    COLORREF bg = off ? C_OFF : primary ? (hot || down ? C_ACC_HOT : C_ACC) : (hot || down ? C_BTN_HOT : C_BTN);
    round_fill(dc, r, bg, (off || primary) ? bg : (hot ? RGB(78, 92, 140) : C_LINE), S(8));
    SetTextColor(dc, off ? dark::OFF_TXT : primary ? dark::ACC_TXT : C_TXT);
    HGDIOBJ of = SelectObject(dc, g_fBtn); RECT tr = r; if (down) OffsetRect(&tr, 0, 1);
    DrawTextW(dc, t, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE); SelectObject(dc, of);
    if (d->itemState & ODS_FOCUS) { RECT f = r; InflateRect(&f, -S(3), -S(3)); HPEN p = CreatePen(PS_DOT, 1, RGB(200, 200, 210)); HGDIOBJ op = SelectObject(dc, p), ob = SelectObject(dc, GetStockObject(NULL_BRUSH)); RoundRect(dc, f.left, f.top, f.right, f.bottom, S(6), S(6)); SelectObject(dc, op); SelectObject(dc, ob); DeleteObject(p); }
}
static void draw_button(const DRAWITEMSTRUCT *d) {   /* compoe num bitmap e copia de uma vez: sem piscar */
    int w = d->rcItem.right - d->rcItem.left, h = d->rcItem.bottom - d->rcItem.top; if (w <= 0 || h <= 0) return;
    HDC mdc = CreateCompatibleDC(d->hDC); HBITMAP bm = CreateCompatibleBitmap(d->hDC, w, h); HGDIOBJ obm = SelectObject(mdc, bm);
    DRAWITEMSTRUCT m = *d; m.hDC = mdc; SetRect(&m.rcItem, 0, 0, w, h);
    draw_button_dc(&m, mdc);
    BitBlt(d->hDC, d->rcItem.left, d->rcItem.top, w, h, mdc, 0, 0, SRCCOPY);
    SelectObject(mdc, obm); DeleteObject(bm); DeleteDC(mdc);
}
static void paint(HWND h) {
    PAINTSTRUCT ps; HDC wdc = BeginPaint(h, &ps); RECT cr; GetClientRect(h, &cr);
    HDC dc = CreateCompatibleDC(wdc); HBITMAP bm = CreateCompatibleBitmap(wdc, cr.right, cr.bottom); HGDIOBJ obm = SelectObject(dc, bm);   /* sem tremulo */
    HBRUSH bg = CreateSolidBrush(C_BG); FillRect(dc, &cr, bg); DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT);
    int m = S(28);
    HGDIOBJ of = SelectObject(dc, g_fTitle); SetTextColor(dc, C_TXT);
    RECT t = {m, S(22), cr.right - m, S(58)}; DrawTextW(dc, L"Yu-Gi-Oh! GX Tag Force", -1, &t, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    dark::accent_line(dc, 0, cr.right, 0, S(2));
    SelectObject(dc, g_fSub); SetTextColor(dc, C_DIM);
    RECT s = {m, S(58), cr.right - m, S(80)}; DrawTextW(dc, L"PC recompilation \x2014 installation uses your own copy of the game", -1, &s, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    RECT card = {m, S(100), cr.right - m, S(194)}; round_fill(dc, card, C_CARD, C_LINE, S(10));
    int cx = card.left + S(16), cw = card.right - S(16);
    SelectObject(dc, g_fSmall); SetTextColor(dc, C_DIM);
    bool busy = g_installing; std::wstring msg, err; { std::lock_guard<std::mutex> lk(g_mu); msg = g_msg; err = g_err; }
    RECT l = {cx, card.top + S(10), cw, card.top + S(28)};
    DrawTextW(dc, busy ? L"INSTALLING" : g_installed ? L"GAME INSTALLED" : L"GAME IMAGE (ISO)", -1, &l, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    SelectObject(dc, g_fText); SetTextColor(dc, C_TXT);
    if (busy) {   /* barra de progresso */
        RECT nm = {cx, card.top + S(30), cw, card.top + S(52)}; SelectObject(dc, g_fSmall); SetTextColor(dc, C_DIM); DrawTextW(dc, msg.c_str(), -1, &nm, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_PATH_ELLIPSIS);
        RECT bar = {cx, card.top + S(58), cw, card.top + S(70)}; round_fill(dc, bar, C_BAR, C_BAR, S(8));
        RECT fill = bar; fill.right = bar.left + MulDiv(bar.right - bar.left, g_prog, 1000); if (fill.right > bar.left + S(8)) round_fill(dc, fill, C_ACC, C_ACC, S(8));
        wchar_t pc[32]; swprintf(pc, 32, L"%d%%  \x2014  this only happens once", g_prog / 10); RECT pr = {cx, card.top + S(72), cw, card.bottom - S(4)}; SelectObject(dc, g_fSmall); DrawTextW(dc, pc, -1, &pr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    } else {
        std::wstring txt = g_installed ? g_data : g_path.empty() ? L"No ISO selected" : g_path;
        SetTextColor(dc, (g_installed || iso_ok()) ? C_TXT : C_WARN);
        RECT p = {cx, card.top + S(30), cw, card.top + S(56)}; DrawTextW(dc, txt.c_str(), -1, &p, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_PATH_ELLIPSIS);
        SelectObject(dc, g_fSmall);
        RECT w = {cx, card.top + S(54), cw, card.bottom - S(4)};
        if (!err.empty()) { SetTextColor(dc, C_ERR); DrawTextW(dc, err.c_str(), -1, &w, DT_LEFT | DT_WORDBREAK); }
        else if (g_installed) { SetTextColor(dc, C_OK); DrawTextW(dc, L"\x2713 Ready to play \x2014 the ISO is no longer needed", -1, &w, DT_LEFT | DT_SINGLELINE | DT_VCENTER); }
        else if (!g_path.empty() && !iso_ok()) { SetTextColor(dc, C_WARN); DrawTextW(dc, L"File not found \x2014 choose the ISO again.", -1, &w, DT_LEFT | DT_SINGLELINE | DT_VCENTER); }
        else if (iso_ok()) { SetTextColor(dc, C_DIM); DrawTextW(dc, jit_generic() ? L"First run: copies the game files (about 760 MB) and prepares the game. A few minutes, only once." : L"Installation copies the game files to the jogo/ folder (about 760 MB).", -1, &w, DT_LEFT | DT_SINGLELINE | DT_VCENTER); }
    }
    SelectObject(dc, of);
    BitBlt(wdc, 0, 0, cr.right, cr.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, obm); DeleteObject(bm); DeleteDC(dc); EndPaint(h, &ps);
}
static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    LRESULT dr; if (menu_dark_message(h, m, w, l, &dr)) return dr;   /* (sem barra de menu aqui; a funcao so trata mensagens da barra) */
    switch (m) {
    case WM_COMMAND:
        if (g_installing && LOWORD(w) != ID_QUIT) return 0;
        switch (LOWORD(w)) {
        case ID_PICK: if (pick(h)) { { std::lock_guard<std::mutex> lk(g_mu); g_err.clear(); } if (g_installed && iso_ok()) { if (tf_msgbox(h, L"Rebuild the jogo/ folder from this ISO? The files currently in the folder will be replaced.", L"Reinstall", MB_YESNO | MB_ICONQUESTION) == IDYES) start_install(); } refresh(); } return 0;
        case ID_PLAY:
            if (g_installed) { if (jit_generic() && !code_ready(game_id_dir(g_data))) start_install(false); else { g_go = true; DestroyWindow(h); } }
            else if (iso_ok()) start_install();
            return 0;
        case ID_DIRECT: if (iso_ok()) { g_go = true; g_direct = true; DestroyWindow(h); } return 0;
        case ID_QUIT:
            if (g_installing && tf_msgbox(h, L"Installation is in progress. Cancel and quit?", L"Tag Force Recomp", MB_YESNO | MB_ICONQUESTION) != IDYES) return 0;
            if (g_installing) ExitProcess(0);   /* a instalacao incompleta nao tem _disco.tsv: o jogo nao a usa */
            DestroyWindow(h); return 0;
        }
        break;
    case WM_ERASEBKGND: return 1;
    case WM_CTLCOLORBTN: { static HBRUSH bgb = CreateSolidBrush(C_BG); return (LRESULT)bgb; }   /* o fundo do botao nao pisca em cor de sistema */
    case WM_PAINT: paint(h); return 0;
    case WM_DRAWITEM: draw_button((const DRAWITEMSTRUCT *)l); return TRUE;
    case WM_TIMER:
        if (w == TIMER_UI) {
            { static bool was[4]; int i = 0;   /* hover dos botoes: so redesenha quando muda */
              for (HWND b : {g_bpick, g_play, g_bquit, g_bdirect}) { bool hot = is_hot(b); if (hot != was[i]) { was[i] = hot; InvalidateRect(b, nullptr, FALSE); } i++; } }
            if (g_installing) InvalidateRect(h, nullptr, FALSE);
            if (g_installing && g_inst_done) {   /* terminou */
                g_installing = false; if (g_thr.joinable()) g_thr.join();
                if (g_inst_ok) { check_installed(); g_go = true; DestroyWindow(h); }   /* instalou: ja joga */
                else { if (g_err.empty()) g_err = L"Installation failed."; refresh(); }
            }
        }
        return 0;
    case WM_CLOSE: SendMessageW(h, WM_COMMAND, ID_QUIT, 0); return 0;
    case WM_DESTROY: KillTimer(h, TIMER_UI); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

/* Mostra a janela. Devolve o caminho da ISO (jogar direto da ISO) ou "" e preenche *data com a pasta instalada; ambos vazios = o usuario saiu. */
std::string launcher_pick_iso(std::wstring *data) {
    wchar_t last[1024] = L"";
    GetPrivateProfileStringW(L"geral", L"iso", L"", last, 1024, ini_path().c_str());
    g_path = last; g_go = g_direct = false; update_data_dir(); check_installed();

    /* DPI proprio so desta janela (o processo nao e DPI-aware; o jogo cuida da propria janela) */
    typedef HANDLE (WINAPI *SetCtx)(HANDLE);
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    SetCtx setctx = (SetCtx)(void *)GetProcAddress(u32, "SetThreadDpiAwarenessContext");
    HANDLE oldctx = setctx ? setctx((HANDLE)-4) : nullptr;   /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */
    { HDC sdc = GetDC(nullptr); g_dpi = GetDeviceCaps(sdc, LOGPIXELSX); ReleaseDC(nullptr, sdc); if (g_dpi < 96) g_dpi = 96; }

    menu_dark_init();   /* tema escuro do Windows (barra de titulo, caixas de dialogo) */
    HINSTANCE hi = GetModuleHandleW(nullptr);
    WNDCLASSW wc = {}; wc.lpfnWndProc = proc; wc.hInstance = hi; wc.lpszClassName = L"TagForceLauncher";
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW); wc.hbrBackground = CreateSolidBrush(C_BG);
    wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(1)); if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, (LPCWSTR)IDI_APPLICATION);
    RegisterClassW(&wc);
    DWORD st = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;   /* o fundo nao repinta por cima dos botoes */
    RECT r = {0, 0, S(560), S(304)};
    { typedef BOOL (WINAPI *AdjDpi)(LPRECT, DWORD, BOOL, DWORD, UINT); AdjDpi adj = (AdjDpi)(void *)GetProcAddress(u32, "AdjustWindowRectExForDpi"); if (adj) adj(&r, st, FALSE, 0, g_dpi); else AdjustWindowRect(&r, st, FALSE); }
    int ww = r.right - r.left, wh = r.bottom - r.top;
    g_wnd = CreateWindowW(L"TagForceLauncher", L"Tag Force Recomp", st, (GetSystemMetrics(SM_CXSCREEN) - ww) / 2, (GetSystemMetrics(SM_CYSCREEN) - wh) / 2, ww, wh, nullptr, nullptr, hi, nullptr);
    menu_dark_window(g_wnd);
    g_fTitle = make_font(26, FW_SEMIBOLD); g_fSub = make_font(13, FW_NORMAL); g_fText = make_font(15, FW_NORMAL); g_fSmall = make_font(12, FW_SEMIBOLD); g_fBtn = make_font(15, FW_SEMIBOLD);
    RECT cr; GetClientRect(g_wnd, &cr); int m = S(28), bh = S(42), by = cr.bottom - S(24) - bh;
    g_bpick = CreateWindowW(L"BUTTON", L"Select ISO...", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, m, by, S(176), bh, g_wnd, (HMENU)ID_PICK, hi, nullptr);
    g_bquit = CreateWindowW(L"BUTTON", L"Quit", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, cr.right - m - S(150) - S(12) - S(90), by, S(90), bh, g_wnd, (HMENU)ID_QUIT, hi, nullptr);
    g_play = CreateWindowW(L"BUTTON", L"Play", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, cr.right - m - S(150), by, S(150), bh, g_wnd, (HMENU)ID_PLAY, hi, nullptr);
    g_bdirect = CreateWindowW(L"BUTTON", L"Play directly from the ISO (no install)", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, cr.right - m - S(260), by - S(26), S(260), S(20), g_wnd, (HMENU)ID_DIRECT, hi, nullptr);
    SendMessageW(g_wnd, DM_SETDEFID, ID_PLAY, 0);
    SetTimer(g_wnd, TIMER_UI, 60, nullptr);
    refresh();
    ShowWindow(g_wnd, SW_SHOW); UpdateWindow(g_wnd);
    if (!g_installed && !iso_ok()) PostMessageW(g_wnd, WM_COMMAND, ID_PICK, 0);   /* primeira vez: ja abre o seletor */

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(g_wnd, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    if (g_thr.joinable()) g_thr.join();
    for (HFONT f : {g_fTitle, g_fSub, g_fText, g_fSmall, g_fBtn}) DeleteObject(f);
    if (setctx) setctx(oldctx);
    if (!g_go) return "";
    if (g_direct) return to_utf8(g_path);
    *data = g_data; return "";
}
