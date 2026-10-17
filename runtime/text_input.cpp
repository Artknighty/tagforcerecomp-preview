/* Entrada de texto nativa (substitui o teclado virtual do PSP, sceUtilityOsk): janelinha com uma caixa de edicao
 * sobre a janela do jogo. Nao e modal: as mensagens sao despachadas pelo laco do display a cada quadro, entao o jogo
 * continua desenhando enquanto o usuario digita. Enter confirma, Esc cancela. */
#include <windows.h>
#include <string>
#include "dark_ui.h"
#include "text_input.h"

HWND display_hwnd(void);

enum { ID_EDIT = 201, ID_OK = 202, ID_CANCEL = 203 };
static HWND g_win, g_edit;
static WNDPROC g_edit_proc;
static int g_state;            /* 0 = nada aberto, 1 = digitando, 2 = confirmado, 3 = cancelado */
static std::wstring g_text;
static HFONT g_font;
static bool g_multi, g_multi_next;   /* caixa grande de varias linhas (notas das marcas): Enter quebra a linha, Ctrl+Enter confirma */

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
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (m == WM_KEYDOWN && w == VK_RETURN && (!g_multi || ctrl)) { finish(2); return 0; }
    if (m == WM_KEYDOWN && w == VK_ESCAPE) { finish(3); return 0; }
    if (m == WM_CHAR && w == 27) return 0;                                       /* evita o bip */
    if (m == WM_CHAR && w == L'\r') { if (g_multi && !ctrl) return CallWindowProcW(g_edit_proc, h, m, w, l); return 0; }
    if (m == WM_CHAR && w == L'\n') return 0;                                    /* Ctrl+Enter chega como LF */
    if (m == WM_CHAR && g_multi && w == 1) { SendMessageW(h, EM_SETSEL, 0, -1); return 0; }   /* Ctrl+A */
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

/* Caixa desenhada pelo proprio jogo (decomp/libtitle): sem janela; as teclas chegam por text_input_key e o jogo le o texto por text_input_live */
static bool g_in_game, g_game_ui;
static std::wstring g_buf, g_desc;
static int g_max;
static bool g_all_selected;   /* o texto inicial esta "selecionado": a primeira letra o substitui, como na caixa de janela */
void text_input_use_game_ui(bool on) { g_game_ui = on; }
bool text_input_in_game(void) { return g_in_game && g_state == 1; }
bool text_input_live(std::wstring *text, bool *caretLit, std::wstring *description, int *maxLength) {
    if (!text_input_in_game()) return false;
    *text = g_buf; *caretLit = (GetTickCount64() / 450) % 2 == 0; *description = g_desc; *maxLength = g_max;
    return true;
}
static void finish_in_game(int st) { g_text = g_buf; g_state = st; g_in_game = false; }
void text_input_finish(bool confirmed) { if (text_input_in_game()) finish_in_game(confirmed ? 2 : 3); }
bool text_input_key(unsigned m, unsigned long long w) {
    if (!text_input_in_game()) return false;
    if (m == WM_KEYDOWN) {
        if (w == VK_RETURN) { finish_in_game(2); return true; }
        if (w == VK_ESCAPE) { finish_in_game(3); return true; }
        if (w >= VK_F1 && w <= VK_F24) return false;   /* F3/F7/F8/F11...: continuam valendo */
        return w != VK_MENU && w != VK_LWIN && w != VK_RWIN;   /* o resto vira WM_CHAR; nao deixa letras acionarem atalhos do jogo */
    }
    if (m == WM_CHAR) {
        const wchar_t ch = (wchar_t)w;
        if (ch == L'\b') { if (!g_buf.empty()) g_buf.pop_back(); return true; }
        if (ch == L'\r' || ch == 27) return true;
        if (!allowed(ch)) { MessageBeep(MB_OK); return true; }
        if (g_all_selected) { g_buf.clear(); g_all_selected = false; }
        if (g_max <= 0 || (int)g_buf.size() < g_max) g_buf += ch; else MessageBeep(MB_OK);
        return true;
    }
    return false;
}

/* Abre a caixa. desc = texto explicativo do jogo (pode ser vazio); maxlen = limite de caracteres (0 = sem limite). */
void text_input_begin(const std::wstring &desc, const std::wstring &initial, int maxlen) {
    if (g_win) { DestroyWindow(g_win); g_win = nullptr; g_edit = nullptr; }
    if (g_game_ui) { g_in_game = true; g_all_selected = true; g_buf = initial; g_desc = desc; g_max = maxlen; g_state = 1; return; }
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
    const bool multi = g_multi_next; g_multi_next = false; g_multi = multi;
    const int cw = multi ? 680 : 420, ch = multi ? 400 : 128, ew = cw - 24, eh = multi ? ch - 112 : 28, by = ch - 48;
    RECT r = {0, 0, cw, ch}; AdjustWindowRect(&r, st, FALSE);
    int ww = r.right - r.left, wh = r.bottom - r.top, x = 100, y = 100;
    RECT orc;
    if (owner && GetWindowRect(owner, &orc)) { x = (orc.left + orc.right - ww) / 2; y = (orc.top + orc.bottom - wh) / 2; }
    g_win = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, L"TagForceTextInput", L"Enter text", st,
                            x, y, ww, wh, owner, nullptr, hi, nullptr);
    if (!g_win) { g_text = initial; g_state = 3; return; }   /* sem janela: "cancelado" (quem espera o texto nao fica preso para sempre) */
    HWND lbl = CreateWindowW(L"STATIC", desc.empty() ? L"Type and press Enter (Esc cancels):" : desc.c_str(),
                             WS_CHILD | WS_VISIBLE | SS_ENDELLIPSIS, 12, 10, ew, 22, g_win, nullptr, hi, nullptr);
    const DWORD editStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP | (multi ? (ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL) : ES_AUTOHSCROLL);
    g_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", initial.c_str(), editStyle,
                             12, 38, ew, eh, g_win, (HMENU)ID_EDIT, hi, nullptr);
    HWND ok = CreateWindowW(L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, cw - 208, by, 94, 32, g_win, (HMENU)ID_OK, hi, nullptr);
    HWND ca = CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, cw - 106, by, 94, 32, g_win, (HMENU)ID_CANCEL, hi, nullptr);
    for (HWND c : {lbl, g_edit, ok, ca}) SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    dark::title_bar(g_win); dark::theme_control(g_edit);
    if (maxlen > 0) SendMessageW(g_edit, EM_LIMITTEXT, (WPARAM)maxlen, 0);
    g_edit_proc = (WNDPROC)SetWindowLongPtrW(g_edit, GWLP_WNDPROC, (LONG_PTR)edit_proc);
    SendMessageW(g_edit, EM_SETSEL, 0, -1);
    ShowWindow(g_win, SW_SHOW); SetForegroundWindow(g_win); SetFocus(g_edit);
    g_state = 1;
}
/* Como text_input_begin, mas numa janela grande de varias linhas: Enter quebra a linha, Ctrl+Enter ou OK confirma (so na caixa de janela). */
void text_input_begin_multiline(const std::wstring &desc, const std::wstring &initial, int maxlen) {
    g_multi_next = !g_game_ui;
    text_input_begin(desc, initial, maxlen);
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
