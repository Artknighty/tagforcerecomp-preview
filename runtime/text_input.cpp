/* Entrada de texto nativa (substitui o teclado virtual do PSP, sceUtilityOsk): janelinha com uma caixa de edicao
 * sobre a janela do jogo. Nao e modal: as mensagens sao despachadas pelo laco do display a cada quadro, entao o jogo
 * continua desenhando enquanto o usuario digita. Enter confirma, Esc cancela. */
#include <windows.h>
#include <string>
#include "dark_ui.h"

HWND display_hwnd(void);

enum { ID_EDIT = 201, ID_OK = 202, ID_CANCEL = 203 };
static HWND g_win, g_edit;
static WNDPROC g_edit_proc;
static int g_state;            /* 0 = nada aberto, 1 = digitando, 2 = confirmado, 3 = cancelado */
static std::wstring g_text;
static HFONT g_font;

static void finish(int st) {
    if (!g_win) return;
    int n = GetWindowTextLengthW(g_edit);
    std::wstring s(n, L'\0');
    if (n) GetWindowTextW(g_edit, &s[0], n + 1);
    g_text = s; g_state = st;
    HWND w = g_win; g_win = nullptr; g_edit = nullptr;
    DestroyWindow(w);
    if (HWND d = display_hwnd()) SetForegroundWindow(d);
}
static bool allowed(wchar_t ch) { return (ch >= 0x20 && ch < 0x7f) || (ch >= 0xa0 && ch <= 0xff); }   /* o que a fonte do jogo cobre */
static LRESULT CALLBACK edit_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_KEYDOWN && w == VK_RETURN) { finish(2); return 0; }
    if (m == WM_KEYDOWN && w == VK_ESCAPE) { finish(3); return 0; }
    if (m == WM_CHAR && (w == L'\r' || w == 27)) return 0;                       /* evita o bip */
    if (m == WM_CHAR && w >= 0x20 && !allowed((wchar_t)w)) { MessageBeep(MB_OK); return 0; }
    return CallWindowProcW(g_edit_proc, h, m, w, l);
}
static LRESULT CALLBACK win_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_COMMAND && LOWORD(w) == ID_OK) { finish(2); return 0; }
    if (m == WM_COMMAND && LOWORD(w) == ID_CANCEL) { finish(3); return 0; }
    if (m == WM_CLOSE) { finish(3); return 0; }
    if (m == WM_ACTIVATE && LOWORD(w) != WA_INACTIVE && g_edit) { SetFocus(g_edit); return 0; }
    /* tema escuro (mesma paleta da caixa de mensagem e da tela de abertura) */
    if (m == WM_CTLCOLORSTATIC) { SetTextColor((HDC)w, dark::TXT); SetBkColor((HDC)w, dark::BG); return (LRESULT)dark::brush(dark::BG); }
    if (m == WM_CTLCOLOREDIT) { SetTextColor((HDC)w, dark::TXT); SetBkColor((HDC)w, dark::CARD); return (LRESULT)dark::brush(dark::CARD); }
    if (m == WM_DRAWITEM) { const DRAWITEMSTRUCT *d = (const DRAWITEMSTRUCT *)l; dark::draw_button(d, d->CtlID == ID_OK, g_font); return TRUE; }
    return DefWindowProcW(h, m, w, l);
}

/* Abre a caixa. desc = texto explicativo do jogo (pode ser vazio); maxlen = limite de caracteres (0 = sem limite). */
void text_input_begin(const std::wstring &desc, const std::wstring &initial, int maxlen) {
    if (g_win) DestroyWindow(g_win);
    HINSTANCE hi = GetModuleHandleW(nullptr);
    static bool reg;
    if (!reg) {
        WNDCLASSW wc = {}; wc.lpfnWndProc = win_proc; wc.hInstance = hi; wc.lpszClassName = L"TagForceTextInput";
        wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW); wc.hbrBackground = dark::brush(dark::BG);
        RegisterClassW(&wc); reg = true;
        g_font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    }
    HWND owner = display_hwnd();
    DWORD st = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;   /* o fundo nao repinta por cima dos botoes */
    RECT r = {0, 0, 420, 128}; AdjustWindowRect(&r, st, FALSE);
    int ww = r.right - r.left, wh = r.bottom - r.top, x = 100, y = 100;
    RECT orc;
    if (owner && GetWindowRect(owner, &orc)) { x = (orc.left + orc.right - ww) / 2; y = (orc.top + orc.bottom - wh) / 2; }
    g_win = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, L"TagForceTextInput", L"Enter text", st,
                            x, y, ww, wh, owner, nullptr, hi, nullptr);
    if (!g_win) { g_text = initial; g_state = 3; return; }   /* sem janela: "cancelado" (quem espera o texto nao fica preso para sempre) */
    HWND lbl = CreateWindowW(L"STATIC", desc.empty() ? L"Type and press Enter (Esc cancels):" : desc.c_str(),
                             WS_CHILD | WS_VISIBLE | SS_ENDELLIPSIS, 12, 10, 396, 22, g_win, nullptr, hi, nullptr);
    g_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", initial.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                             12, 38, 396, 28, g_win, (HMENU)ID_EDIT, hi, nullptr);
    HWND ok = CreateWindowW(L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 212, 80, 94, 32, g_win, (HMENU)ID_OK, hi, nullptr);
    HWND ca = CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 314, 80, 94, 32, g_win, (HMENU)ID_CANCEL, hi, nullptr);
    for (HWND c : {lbl, g_edit, ok, ca}) SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    dark::title_bar(g_win); dark::theme_control(g_edit);
    if (maxlen > 0) SendMessageW(g_edit, EM_LIMITTEXT, (WPARAM)maxlen, 0);
    g_edit_proc = (WNDPROC)SetWindowLongPtrW(g_edit, GWLP_WNDPROC, (LONG_PTR)edit_proc);
    SendMessageW(g_edit, EM_SETSEL, 0, -1);
    ShowWindow(g_win, SW_SHOW); SetForegroundWindow(g_win); SetFocus(g_edit);
    g_state = 1;
}
/* 0 = nada aberto, 1 = digitando, 2 = confirmado, 3 = cancelado. Em 2/3, *out recebe o texto e o estado volta a 0. */
int text_input_poll(std::wstring *out) {
    int s = g_state;
    if (s >= 2) { if (out) *out = g_text; g_state = 0; }
    return s;
}
/* a caixa esta aberta ou o resultado ainda nao foi recolhido (o teclado virtual do jogo e a nota das marcas F7/F8 dividem a mesma caixa:
 * a nota espera a do jogo acabar, senao o texto de uma ia para a outra) */
bool text_input_busy(void) { return g_state != 0; }
