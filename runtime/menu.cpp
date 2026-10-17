/* Barra de menu nativa da janela (versao de PC): Arquivo / Exibir / Mods / Cheats / Ajuda. Para acrescentar um item:
 * um ID no enum, a chamada (acao/alterna/opcao/submenu) em menu_create e o caso em menu_command. As acoes ficam no display.cpp
 * (display_*). O menu some em tela cheia; enquanto um menu esta aberto o Windows prende a thread num laco modal e o jogo fica
 * pausado (o display avisa o watchdog). Textos em UTF-16 com escapes \x (acentos).
 * Visual: os itens sao desenhados aqui (MF_OWNERDRAW) na paleta de dark_ui.h - icone (fonte de icones do Windows), texto, atalho
 * ou descricao a direita, chave liga/desliga, opcao com bolinha e submenu com o valor escolhido ao lado; a barra de menu tambem. */
#include <windows.h>
#include <algorithm>
#include <deque>
#include <map>
#include <string>
#include <wctype.h>
#include "dark_ui.h"
#include "hotkeys.h"
#include "menu.h"
#include "texrepl.h"
#include "mods.h"
#include "gfx.h"

void display_toggle_fullscreen(void);
void display_toggle_fps(void);
void display_set_scale(int s);
void display_ask_exit(void);
void display_ask_reset(void);
bool display_pc_input(void); void display_set_pc_input(bool on); bool display_mouse_menus(void); void display_set_mouse_menus(bool on);   /* display.cpp: esquema de teclado/mouse de PC */
void display_ask_reset_text(const wchar_t *text);

enum {
    ID_RESET = 1001, ID_EXIT,
    ID_FULLSCREEN = 1101, ID_FPS, ID_SCALE1, ID_SCALE2, ID_SCALE3, ID_SCALE4,
    ID_FILTER0 = 1150, ID_FILTER1, ID_FILTER2, ID_ASPECT0 = 1160, ID_ASPECT1, ID_ASPECT2, ID_RES1 = 1180, ID_RES2, ID_RES3, ID_RES4, ID_RES5, ID_RES6, ID_MSAA1 = 1210, ID_MSAA2, ID_MSAA4, ID_MSAA8, ID_FPS30 = 1224, ID_FPS60, ID_FPS120, ID_SYNC0 = 1230, ID_SYNC1, ID_SYNC2, ID_MODS_DUMP = 1288, ID_RENDER_SW = 1190, ID_RENDER_D3D, ID_RENDER_VK,
    ID_CONTROLS = 1201, ID_ABOUT, ID_PCINPUT = 1205, ID_MOUSEMENU,
    ID_MODS_FOLDER = 1290, ID_MODS_APPLY, ID_MOD_FIRST = 1300,   /* 1300 + i = liga/desliga o mod i */
};
static HMENU g_bar, g_view, g_scale, g_mods, g_gfxm, g_cheats;
int plugin_slider_count(void); std::wstring plugin_slider_title(int i); std::wstring plugin_slider_value(int i); bool plugin_slider_edit(HWND owner, int i); int plugin_slider_menu(int i);   /* plugins.cpp: ajustes registrados pelos mods (API 3) */
static const int ID_MOD_SLIDER = 1400;   /* 1400 + i = ajuste i dos plugins */
int plugin_toggle_count(void); bool plugin_toggle_on(int i); std::wstring plugin_toggle_label(int i); void plugin_toggle_flip(int i); int plugin_toggle_menu(int i);   /* plugins.cpp: liga/desliga dos mods (API 4) */
static const int ID_MOD_TOGGLE = 1450;   /* 1450 + i = liga/desliga i dos plugins */

/* ------------------------------------------------------------------ itens desenhados aqui (MF_OWNERDRAW) */
enum Tipo { T_ACAO, T_ALTERNA, T_OPCAO, T_SUB, T_TITULO, T_SEP };
/* texto (com & do atalho de teclado); dir = atalho ou descricao a direita; curto = como a opcao aparece ao lado do submenu pai */
struct Item { Tipo tipo; wchar_t icone; std::wstring texto, dir, curto; HMENU sub; bool valor = false; };   /* valor: dir e um valor (dourado), nao um atalho */
static std::deque<Item> g_itens;              /* enderecos estaveis: o Windows guarda o ponteiro (itemData) */
static std::map<UINT, Item *> g_item_id;
/* icones (Segoe Fluent Icons no Windows 11, Segoe MDL2 Assets no 10) */
enum : wchar_t { IC_REINICIAR = 0xE72C, IC_SAIR = 0xE7E8, IC_TELA_CHEIA = 0xE740, IC_FPS = 0xE9D9, IC_JANELA = 0xE7C4, IC_GPU = 0xE964, IC_RES = 0xE7F4,
                 IC_AA = 0xE771, IC_TEXTURA = 0xEB9F, IC_QUADROS = 0xEC4A, IC_SYNC = 0xE8AB, IC_FILTRO = 0xE793, IC_PROPORCAO = 0xE7FB,
                 IC_INTEIRA = 0xE80A, IC_MOD = 0xEA86, IC_PASTA = 0xE838, IC_DESPEJO = 0xE896, IC_CHEAT = 0xE945, IC_AJUSTE = 0xE9E9, IC_CONTROLE = 0xE7FC,
                 IC_SOBRE = 0xE946 };

static Item *novo_item(Tipo t, wchar_t ic, const std::wstring &txt, const std::wstring &dir = L"", const std::wstring &curto = L"") {
    g_itens.push_back({t, ic, txt, dir, curto, nullptr, false}); return &g_itens.back();
}
static void inserir(HMENU m, UINT ftype, UINT id, HMENU sub, Item *it, UINT state) {
    MENUITEMINFOW mi = {sizeof mi}; mi.fMask = MIIM_FTYPE | MIIM_DATA | MIIM_ID | MIIM_STATE | (sub ? MIIM_SUBMENU : 0);
    mi.fType = MFT_OWNERDRAW | ftype; mi.wID = id; mi.hSubMenu = sub; mi.dwItemData = (ULONG_PTR)it; mi.fState = state;
    InsertMenuItemW(m, (UINT)GetMenuItemCount(m), TRUE, &mi);
    if (id) g_item_id[id] = it;
}
static void acao(HMENU m, UINT id, wchar_t ic, const std::wstring &txt, const std::wstring &dir = L"", bool cinza = false) { inserir(m, 0, id, nullptr, novo_item(T_ACAO, ic, txt, dir), cinza ? MFS_DISABLED : 0); }
static void alterna(HMENU m, UINT id, wchar_t ic, const std::wstring &txt, const std::wstring &dir, bool on) { inserir(m, 0, id, nullptr, novo_item(T_ALTERNA, ic, txt, dir), on ? MFS_CHECKED : 0); }
static void opcao(HMENU m, UINT id, const std::wstring &txt, const std::wstring &dir, const std::wstring &curto) { inserir(m, MFT_RADIOCHECK, id, nullptr, novo_item(T_OPCAO, 0, txt, dir, curto), 0); }
static void submenu(HMENU m, HMENU sub, wchar_t ic, const std::wstring &txt) { Item *it = novo_item(T_SUB, ic, txt); it->sub = sub; inserir(m, 0, 0, sub, it, 0); }
static void titulo(HMENU m, const std::wstring &txt) { inserir(m, 0, 0, nullptr, novo_item(T_TITULO, 0, txt), MFS_DISABLED); }
static void separador(HMENU m) { inserir(m, MFT_SEPARATOR, 0, nullptr, novo_item(T_SEP, 0, L""), 0); }

static bool fonte_existe(const wchar_t *nome) {
    LOGFONTW lf = {}; lf.lfCharSet = DEFAULT_CHARSET; wcsncpy(lf.lfFaceName, nome, LF_FACESIZE - 1); bool ok = false;
    HDC dc = GetDC(nullptr);
    EnumFontFamiliesExW(dc, &lf, [](const LOGFONTW *, const TEXTMETRICW *, DWORD, LPARAM p) -> int { *(bool *)p = true; return 0; }, (LPARAM)&ok, 0);
    ReleaseDC(nullptr, dc); return ok;
}
struct Fontes { HFONT txt, dir, val, tit, ico; };
static const Fontes &fontes(void) {   /* em pixels de 96 dpi (a janela do jogo nao e DPI-aware: o Windows escala o menu junto) */
    static const Fontes f = [] {
        auto mk = [](int px, int peso, const wchar_t *face) { return CreateFontW(-px, 0, 0, 0, peso, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, face); };
        const wchar_t *ic = fonte_existe(L"Segoe Fluent Icons") ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
        return Fontes{mk(14, FW_NORMAL, L"Segoe UI"), mk(12, FW_NORMAL, L"Segoe UI"), mk(13, FW_SEMIBOLD, L"Segoe UI"), mk(11, FW_BOLD, L"Segoe UI"),
                      mk(16, FW_NORMAL, ic)};
    }();
    return f;
}
static int largura(HFONT f, const std::wstring &s, bool atalho = false) {
    if (s.empty()) return 0;
    HDC dc = GetDC(nullptr); HGDIOBJ o = SelectObject(dc, f); RECT r = {0, 0, 0, 0};
    DrawTextW(dc, s.c_str(), -1, &r, DT_CALCRECT | DT_SINGLELINE | (atalho ? 0 : DT_NOPREFIX));
    SelectObject(dc, o); ReleaseDC(nullptr, dc); return r.right;
}
static const Item *dado(HMENU m, int pos, UINT *estado) {
    MENUITEMINFOW mi = {sizeof mi}; mi.fMask = MIIM_DATA | MIIM_FTYPE | MIIM_STATE;
    if (!GetMenuItemInfoW(m, (UINT)pos, TRUE, &mi) || !(mi.fType & MFT_OWNERDRAW) || !mi.dwItemData) return nullptr;
    if (estado) *estado = mi.fState;
    return (const Item *)mi.dwItemData;
}
static const Item *opcao_marcada(HMENU sub) {   /* o valor que o submenu mostra ao lado: a opcao marcada dentro dele */
    for (int i = 0, n = GetMenuItemCount(sub); i < n; i++) { UINT st = 0; const Item *it = dado(sub, i, &st); if (it && it->tipo == T_OPCAO && (st & MFS_CHECKED)) return it; }
    return nullptr;
}
/* medidas (pixels de 96 dpi) */
static const int ALT = 30, ALT_SEP = 9, ALT_TIT = 24, X_TEXTO = 42, MARGEM_D = 14, CHAVE_W = 30, CHAVE_H = 16, GAP = 28;
static void medir(MEASUREITEMSTRUCT *mi) {
    const Item &it = *(const Item *)mi->itemData; const Fontes &f = fontes();
    if (it.tipo == T_SEP) { mi->itemWidth = 40; mi->itemHeight = ALT_SEP; return; }
    if (it.tipo == T_TITULO) { mi->itemWidth = X_TEXTO + largura(f.tit, it.texto); mi->itemHeight = ALT_TIT; return; }
    int dir = 0;
    if (it.tipo == T_ALTERNA) dir = CHAVE_W + (it.dir.empty() ? 0 : largura(f.dir, it.dir) + 12);
    else if (it.tipo == T_SUB) {   /* reserva o mais largo dos valores possiveis */
        int v = 0; for (int i = 0, n = GetMenuItemCount(it.sub); i < n; i++) if (const Item *o = dado(it.sub, i, nullptr)) if (o->tipo == T_OPCAO) v = std::max(v, largura(f.val, o->curto));
        dir = 6 + (v ? v + 12 : 0);
    } else dir = largura(it.valor ? f.val : f.dir, it.dir);
    const int w = X_TEXTO + largura(f.txt, it.texto, true) + (dir ? GAP + dir : 0) + MARGEM_D - (GetSystemMetrics(SM_CXMENUCHECK) - 1);   /* o Windows soma a largura da marca */
    mi->itemWidth = (UINT)std::max(w, 200); mi->itemHeight = ALT;
}
static void desenhar(const DRAWITEMSTRUCT *d) {
    const Item &it = *(const Item *)d->itemData; const Fontes &f = fontes(); HDC dc = d->hDC; const RECT r = d->rcItem;
    const bool cinza = (d->itemState & (ODS_GRAYED | ODS_DISABLED)) != 0, marca = (d->itemState & ODS_CHECKED) != 0;
    const bool sel = (d->itemState & ODS_SELECTED) && !cinza && it.tipo != T_TITULO && it.tipo != T_SEP;
    const COLORREF fundo = sel ? dark::HOT : dark::BG;
    FillRect(dc, &r, dark::brush(dark::BG));
    if (it.tipo == T_SEP) { const int y = (r.top + r.bottom) / 2; RECT ln = {r.left + 12, y, r.right - 12, y + 1}; FillRect(dc, &ln, dark::brush(dark::LINE)); return; }
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ of = SelectObject(dc, f.tit);
    if (it.tipo == T_TITULO) {   /* cabecalho de secao: pequeno, em dourado apagado */
        SetTextColor(dc, RGB(178, 152, 98)); RECT tr = {r.left + 16, r.top + 6, r.right - 12, r.bottom};
        DrawTextW(dc, it.texto.c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, of); return;
    }
    if (sel) {   /* realce arredondado com um traco dourado na esquerda */
        RECT hl = {r.left + 4, r.top + 1, r.right - 4, r.bottom - 1}; dark::round_rect(dc, hl, dark::HOT, dark::HOT, 8);
        RECT tr = {r.left + 6, r.top + 8, r.left + 9, r.bottom - 8}; dark::round_rect(dc, tr, dark::ACC, dark::ACC, 3);
    }
    const int cy = (r.top + r.bottom) / 2;
    if (it.tipo == T_OPCAO) {   /* bolinha: anel e, marcada, o miolo dourado */
        RECT o = {r.left + 15, cy - 7, r.left + 29, cy + 7}; dark::round_rect(dc, o, fundo, marca ? dark::ACC : (cinza ? dark::OFF_TXT : dark::DIM), 14);
        if (marca) { RECT in = {r.left + 18, cy - 4, r.left + 26, cy + 4}; dark::round_rect(dc, in, dark::ACC, dark::ACC, 8); }
    } else if (it.icone) {
        SelectObject(dc, f.ico); SetTextColor(dc, cinza ? dark::OFF_TXT : (it.tipo == T_ALTERNA && marca) ? dark::ACC : sel ? dark::ACC_HOT : dark::ICON);
        const wchar_t g[2] = {it.icone, 0}; RECT ir = {r.left + 11, r.top, r.left + 33, r.bottom}; DrawTextW(dc, g, 1, &ir, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    int xr = r.right - MARGEM_D;
    if (it.tipo == T_SUB) {   /* o valor escolhido no submenu (a seta e a do Windows, pintada depois na ponta direita) */
        xr -= 6;
        if (const Item *o = opcao_marcada(it.sub)) {
            SelectObject(dc, f.val); SetTextColor(dc, cinza ? dark::OFF_TXT : dark::ACC);
            const int w = largura(f.val, o->curto); RECT vr = {xr - w, r.top, xr, r.bottom}; DrawTextW(dc, o->curto.c_str(), -1, &vr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            xr -= w + 12;
        }
    } else if (it.tipo == T_ALTERNA) {   /* chave: dourada e com o botao a direita quando ligada */
        RECT sw = {xr - CHAVE_W, cy - CHAVE_H / 2, xr, cy + CHAVE_H / 2};
        if (marca) { dark::round_rect(dc, sw, cinza ? dark::OFF : dark::ACC, cinza ? dark::OFF : dark::ACC, CHAVE_H); RECT k = {sw.right - 14, cy - 6, sw.right - 2, cy + 6}; dark::round_rect(dc, k, dark::BG, dark::BG, 12); }
        else { dark::round_rect(dc, sw, fundo, cinza ? dark::OFF_TXT : dark::DIM, CHAVE_H); RECT k = {sw.left + 3, cy - 5, sw.left + 13, cy + 5}; dark::round_rect(dc, k, cinza ? dark::OFF_TXT : dark::DIM, cinza ? dark::OFF_TXT : dark::DIM, 10); }
        xr -= CHAVE_W + 12;
    }
    if (it.tipo != T_SUB && !it.dir.empty()) {   /* atalho (F11, Esc) ou descricao, apagado */
        SelectObject(dc, it.valor ? f.val : f.dir); SetTextColor(dc, cinza ? dark::OFF_TXT : it.valor ? dark::ACC : dark::DIM);
        const int w = largura(it.valor ? f.val : f.dir, it.dir); RECT dr = {xr - w, r.top, xr, r.bottom}; DrawTextW(dc, it.dir.c_str(), -1, &dr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        xr -= w + 12;
    }
    SelectObject(dc, f.txt); SetTextColor(dc, cinza ? dark::OFF_TXT : dark::TXT);
    RECT tr = {r.left + X_TEXTO, r.top, xr, r.bottom};
    DrawTextW(dc, it.texto.c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | ((d->itemState & ODS_NOACCEL) ? DT_HIDEPREFIX : 0));
    SelectObject(dc, of);
}

/* Modo escuro: janela (barra de titulo via DWM), menus suspensos (uxtheme, ordinais nao documentados 133/135/136, Windows
 * 10 1903+; se faltarem, fica tudo claro como antes) e a barra de menu, que o Windows nao escurece sozinho: desenhada aqui
 * pelas mensagens nao documentadas WM_UAHDRAWMENU (0x91) e WM_UAHDRAWMENUITEM (0x92). Os itens dos menus suspensos sao
 * desenhados por WM_MEASUREITEM / WM_DRAWITEM (acima). */
struct UahMenu { HMENU hmenu; HDC hdc; DWORD flags; };
struct UahDrawItem { DRAWITEMSTRUCT dis; UahMenu um; int pos; };
static bool g_dark;
void menu_dark_init(void) {
    HMODULE ux = LoadLibraryW(L"uxtheme.dll"); if (!ux) return;
    typedef int (WINAPI *SetMode)(int); typedef void (WINAPI *Flush)(void);
    SetMode setmode = (SetMode)(void *)GetProcAddress(ux, MAKEINTRESOURCEA(135)); Flush flush = (Flush)(void *)GetProcAddress(ux, MAKEINTRESOURCEA(136));
    if (!setmode) return;
    setmode(2);   /* ForceDark */
    if (flush) flush();
    g_dark = true;
}
void menu_dark_window(HWND h) {
    if (!g_dark) return;
    HMODULE ux = LoadLibraryW(L"uxtheme.dll");
    typedef BOOL (WINAPI *Allow)(HWND, BOOL);
    if (ux) if (Allow a = (Allow)(void *)GetProcAddress(ux, MAKEINTRESOURCEA(133))) a(h, TRUE);
    dark::title_bar(h);
    SetWindowPos(h, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
}
static void dk_bar_bottom_line(HWND h) {   /* linha de 1 px entre a barra de menu e a area do jogo (o Windows pinta clara) */
    MENUBARINFO mbi = {sizeof mbi}; if (!GetMenuBarInfo(h, OBJID_MENU, 0, &mbi)) return;
    RECT cr, wr; GetClientRect(h, &cr); MapWindowPoints(h, nullptr, (POINT *)&cr, 2); GetWindowRect(h, &wr);
    OffsetRect(&cr, -wr.left, -wr.top);
    RECT ln = cr; ln.top--; ln.bottom = cr.top;
    HDC dc = GetWindowDC(h); FillRect(dc, &ln, dark::brush(dark::LINE)); ReleaseDC(h, dc);
}
bool menu_dark_message(HWND h, UINT m, WPARAM w, LPARAM l, LRESULT *res) {
    if (m == WM_MEASUREITEM && ((MEASUREITEMSTRUCT *)l)->CtlType == ODT_MENU && ((MEASUREITEMSTRUCT *)l)->itemData) { medir((MEASUREITEMSTRUCT *)l); *res = TRUE; return true; }
    if (m == WM_DRAWITEM && ((DRAWITEMSTRUCT *)l)->CtlType == ODT_MENU && ((DRAWITEMSTRUCT *)l)->itemData) { desenhar((DRAWITEMSTRUCT *)l); *res = TRUE; return true; }
    if (m == WM_MENUCHAR && (HIWORD(w) & MF_POPUP)) {   /* letra sublinhada (&) dos itens desenhados aqui: o Windows so procura nos itens de texto */
        HMENU menu = (HMENU)l; const wchar_t ch = (wchar_t)towlower((wchar_t)LOWORD(w));
        for (int i = 0, n = GetMenuItemCount(menu); i < n; i++) {
            UINT st = 0; const Item *it = dado(menu, i, &st); if (!it || (st & MFS_DISABLED)) continue;
            const size_t a = it->texto.find(L'&');
            if (a != std::wstring::npos && a + 1 < it->texto.size() && towlower(it->texto[a + 1]) == ch) { *res = MAKELRESULT(i, MNC_EXECUTE); return true; }
        }
        return false;
    }
    if (!g_dark) return false;
    if (m == 0x91) {   /* WM_UAHDRAWMENU: fundo da barra */
        UahMenu *um = (UahMenu *)l; MENUBARINFO mbi = {sizeof mbi}; if (!GetMenuBarInfo(h, OBJID_MENU, 0, &mbi)) return false;
        RECT wr; GetWindowRect(h, &wr); RECT r = mbi.rcBar; OffsetRect(&r, -wr.left, -wr.top);
        FillRect(um->hdc, &r, dark::brush(dark::BAR)); *res = 1; return true;
    }
    if (m == 0x92) {   /* WM_UAHDRAWMENUITEM: um titulo da barra (aberto: fundo e traco dourado embaixo; sob o mouse: fundo) */
        UahDrawItem *d = (UahDrawItem *)l;
        wchar_t txt[64] = {}; MENUITEMINFOW mi = {sizeof mi}; mi.fMask = MIIM_STRING; mi.dwTypeData = txt; mi.cch = 63;
        GetMenuItemInfoW(d->um.hmenu, d->pos, TRUE, &mi);
        const UINT st = d->dis.itemState; const bool sel = (st & ODS_SELECTED) != 0, hot = (st & ODS_HOTLIGHT) != 0;
        const RECT r = d->dis.rcItem; FillRect(d->um.hdc, &r, dark::brush(dark::BAR));
        if (sel || hot) {
            RECT hl = {r.left + 1, r.top + 2, r.right - 1, r.bottom - 2}; dark::round_rect(d->um.hdc, hl, sel ? dark::HOT : RGB(28, 36, 64), sel ? dark::HOT : RGB(28, 36, 64), 8);
            if (sel) { RECT u = {hl.left + 8, hl.bottom - 3, hl.right - 8, hl.bottom - 1}; dark::round_rect(d->um.hdc, u, dark::ACC, dark::ACC, 2); }
        }
        SetBkMode(d->um.hdc, TRANSPARENT); SetTextColor(d->um.hdc, (st & (ODS_GRAYED | ODS_DISABLED)) ? dark::OFF_TXT : (sel ? dark::ACC_HOT : dark::TXT));
        DWORD f = DT_CENTER | DT_SINGLELINE | DT_VCENTER; if (st & ODS_NOACCEL) f |= DT_HIDEPREFIX;
        RECT tr = r; DrawTextW(d->um.hdc, txt, -1, &tr, f);
        *res = 1; return true;
    }
    if (m == WM_NCPAINT || m == WM_NCACTIVATE) { *res = DefWindowProcA(h, m, w, l); dk_bar_bottom_line(h); return true; }
    return false;
}

void ge_set_res(int n);
void ge_set_msaa(void);
void display_set_renderer(int r);
static void gfx_menu_sync(void) {   /* (MF_BYCOMMAND procura tambem nos submenus) */
    for (int i = 0; i < 3; i++) { CheckMenuItem(g_gfxm, ID_FILTER0 + i, i == g_gfx.filter ? MF_CHECKED : MF_UNCHECKED); CheckMenuItem(g_gfxm, ID_ASPECT0 + i, i == g_gfx.aspect ? MF_CHECKED : MF_UNCHECKED); }
    static const int resv[6] = {1, 3, 4, 6, 8, 0};   /* 0 = a do monitor (gfx.h) */
    for (int i = 0; i < 6; i++) CheckMenuItem(g_gfxm, ID_RES1 + i, (resv[i] ? !g_gfx.res_auto && resv[i] == g_gfx.res : g_gfx.res_auto) ? MF_CHECKED : MF_UNCHECKED);
    { static const int mv[4] = {1, 2, 4, 8}; for (int i = 0; i < 4; i++) CheckMenuItem(g_gfxm, ID_MSAA1 + i, mv[i] == g_gfx.msaa ? MF_CHECKED : MF_UNCHECKED); }
    for (int i = 0; i < 3; i++) CheckMenuItem(g_gfxm, ID_SYNC0 + i, i == g_gfx.sync ? MF_CHECKED : MF_UNCHECKED);
    for (int i = 0; i < 3; i++) CheckMenuItem(g_gfxm, ID_FPS30 + i, i == g_gfx.fps ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(g_gfxm, ID_RENDER_SW, g_gfx.renderer == GFX_RENDER_SOFTWARE ? MF_CHECKED : MF_UNCHECKED); CheckMenuItem(g_gfxm, ID_RENDER_D3D, g_gfx.renderer == GFX_RENDER_D3D11 ? MF_CHECKED : MF_UNCHECKED); CheckMenuItem(g_gfxm, ID_RENDER_VK, g_gfx.renderer == GFX_RENDER_VULKAN ? MF_CHECKED : MF_UNCHECKED);
}
void menu_gfx_refresh(void) { if (g_gfxm) gfx_menu_sync(); }   /* the in-game Graphics screen changed an option */
HMENU menu_create(void) {
    HMENU file = CreatePopupMenu();
    acao(file, ID_RESET, IC_REINICIAR, L"&Restart game...");
    separador(file);
    acao(file, ID_EXIT, IC_SAIR, L"&Quit...", L"Esc");

    g_scale = CreatePopupMenu();
    opcao(g_scale, ID_SCALE1, L"&1x", L"480 x 272", L"1x");
    opcao(g_scale, ID_SCALE2, L"&2x", L"960 x 544", L"2x");
    opcao(g_scale, ID_SCALE3, L"&3x", L"1440 x 816", L"3x");
    opcao(g_scale, ID_SCALE4, L"&4x", L"1920 x 1088", L"4x");
    g_view = CreatePopupMenu();
    alterna(g_view, ID_FULLSCREEN, IC_TELA_CHEIA, L"&Fullscreen", hotkey_key(VK_F11), false);
    alterna(g_view, ID_FPS, IC_FPS, L"Show &FPS", hotkey_key(VK_F3), false);
    alterna(g_view, ID_PCINPUT, IC_CONTROLE, L"PC &keyboard and mouse", L"WASD + clicks", display_pc_input());
    alterna(g_view, ID_MOUSEMENU, IC_CONTROLE, L"Mouse in m&enus", L"experimental", display_mouse_menus());
    separador(g_view);
    submenu(g_view, g_scale, IC_JANELA, L"&Window size");

    /* Graphics: in sections, each choice in a submenu that shows the current value beside it */
    HMENU render = CreatePopupMenu(), res = CreatePopupMenu(), msaa = CreatePopupMenu(), fps = CreatePopupMenu(), sync = CreatePopupMenu(),
          filtro = CreatePopupMenu(), prop = CreatePopupMenu();
    opcao(render, ID_RENDER_SW, L"&Software", L"CPU", L"Software");
    opcao(render, ID_RENDER_D3D, L"&Direct3D 11", L"GPU", L"Direct3D 11");
    opcao(render, ID_RENDER_VK, L"&Vulkan", L"GPU", L"Vulkan");
    opcao(res, ID_RES1, L"&480 x 272", L"original PSP", L"480x272");
    opcao(res, ID_RES2, L"1&280 x 720", L"HD", L"720p");
    opcao(res, ID_RES3, L"1&920 x 1080", L"Full HD", L"1080p");
    opcao(res, ID_RES4, L"&2560 x 1440", L"QHD \x00B7 GPU only", L"1440p");
    opcao(res, ID_RES5, L"&3840 x 2160", L"4K \x00B7 GPU only", L"4K");
    opcao(res, ID_RES6, L"&Match monitor", L"", L"Monitor");
    opcao(msaa, ID_MSAA1, L"&Off", L"", L"Off");
    opcao(msaa, ID_MSAA2, L"&2x", L"", L"2x");
    opcao(msaa, ID_MSAA4, L"&4x", L"", L"4x");
    opcao(msaa, ID_MSAA8, L"&8x", L"", L"8x");
    opcao(fps, ID_FPS120, L"&120 FPS", L"interpolated (GPU) \x00B7 120 Hz monitor", L"120 FPS");
    opcao(fps, ID_FPS60, L"&60 FPS", L"interpolated (GPU)", L"60 FPS");
    opcao(fps, ID_FPS30, L"&30 FPS", L"original", L"30 FPS");
    opcao(sync, ID_SYNC0, L"&Default", L"no tearing, without waiting for the monitor", L"Default");
    opcao(sync, ID_SYNC1, L"&VSync", L"waits for the monitor", L"VSync");
    opcao(sync, ID_SYNC2, L"&Lowest latency", L"may tear \x00B7 GPU only", L"Lowest latency");
    opcao(filtro, ID_FILTER0, L"&Sharp pixels", L"", L"Sharp");
    opcao(filtro, ID_FILTER1, L"Sharp, s&moothed", L"", L"Smoothed");
    opcao(filtro, ID_FILTER2, L"&Bilinear", L"soft", L"Bilinear");
    opcao(prop, ID_ASPECT0, L"&Original", L"16:9", L"16:9");
    opcao(prop, ID_ASPECT1, L"&Stretch to window", L"", L"Stretch");
    opcao(prop, ID_ASPECT2, L"&4:3", L"", L"4:3");
    g_gfxm = CreatePopupMenu();
    titulo(g_gfxm, L"QUALITY");
    submenu(g_gfxm, render, IC_GPU, L"&Renderer");
    submenu(g_gfxm, res, IC_RES, L"&Resolution");
    submenu(g_gfxm, msaa, IC_AA, L"&Anti-aliasing (MSAA)");
    titulo(g_gfxm, L"MOTION");
    submenu(g_gfxm, fps, IC_QUADROS, L"&Frame rate");
    submenu(g_gfxm, sync, IC_SYNC, L"S&ynchronization");
    titulo(g_gfxm, L"SCREEN");
    submenu(g_gfxm, filtro, IC_FILTRO, L"Fi&lter");
    submenu(g_gfxm, prop, IC_PROPORCAO, L"Aspect &ratio");
    gfx_menu_sync();

    g_mods = CreatePopupMenu();   /* list of the mods in mods/ (checked = on; applies on restart) */
    g_cheats = CreatePopupMenu();   /* Cheats menu: settings that plugins registered with use_menu(TFMOD_MENU_CHEATS) (Cheats mod) */
    const int nm = mods_count();
    int visible_mods = 0;
    for (int i = 0; i < nm; i++) if (mods_menu_visible(i)) visible_mods++;
    titulo(g_mods, L"INSTALLED MODS");
    if (!visible_mods) acao(g_mods, 0, IC_MOD, nm ? L"(texture packs: use in-game Graphics)" : L"(no mods installed)", L"", true);
    for (int i = 0, shown = 0; i < nm && shown < 90; i++) {
        if (!mods_menu_visible(i)) continue;
        alterna(g_mods, ID_MOD_FIRST + i, IC_MOD, mods_label(i), mods_detail(i), mods_enabled(i));
        shown++;
    }
    /* plugin settings, each in its own menu: on/off switches (with the in-game key beside them) and sliders (open a window, value beside them) */
    bool tit_mods = false, sep_c = false;
    auto cabeca = [&](HMENU m) { if (m == g_mods && !tit_mods) { titulo(g_mods, L"MOD SETTINGS"); tit_mods = true; } };
    for (int i = 0; i < plugin_toggle_count(); i++) {
        HMENU m = plugin_toggle_menu(i) ? g_cheats : g_mods; cabeca(m);
        std::wstring t = plugin_toggle_label(i), k; const size_t tab = t.find(L'\t'); if (tab != std::wstring::npos) { k = t.substr(tab + 1); t.resize(tab); }
        alterna(m, ID_MOD_TOGGLE + i, m == g_cheats ? IC_CHEAT : IC_MOD, t, k, plugin_toggle_on(i));
    }
    sep_c = GetMenuItemCount(g_cheats) > 0;
    for (int i = 0; i < plugin_slider_count(); i++) {
        HMENU m = plugin_slider_menu(i) ? g_cheats : g_mods; cabeca(m);
        if (m == g_cheats && sep_c) { separador(g_cheats); sep_c = false; }
        acao(m, ID_MOD_SLIDER + i, IC_AJUSTE, plugin_slider_title(i), plugin_slider_value(i)); g_item_id[ID_MOD_SLIDER + i]->valor = true;
    }
    if (!GetMenuItemCount(g_cheats)) acao(g_cheats, 0, IC_CHEAT, L"(no cheats: enable the Cheats mod in the Mods menu and restart)", L"", true);
    separador(g_mods);
    acao(g_mods, ID_MODS_APPLY, IC_REINICIAR, L"&Restart to apply...");
    acao(g_mods, ID_MODS_FOLDER, IC_PASTA, L"&Open mods folder");
    alterna(g_mods, ID_MODS_DUMP, IC_DESPEJO, L"&Dump used textures", L"folder texture_dump", texrepl_dumping());

    HMENU help = CreatePopupMenu();
    acao(help, ID_CONTROLS, IC_CONTROLE, L"&Controls...");
    acao(help, ID_ABOUT, IC_SOBRE, L"&About...");

    g_bar = CreateMenu();
    AppendMenuW(g_bar, MF_POPUP, (UINT_PTR)file, L"&File");
    AppendMenuW(g_bar, MF_POPUP, (UINT_PTR)g_view, L"&View");
    /* no Graphics menu in the bar: the options live in the game (Graphics item of the main menu, gfx_options.h) */
    AppendMenuW(g_bar, MF_POPUP, (UINT_PTR)g_mods, L"&Mods");
    AppendMenuW(g_bar, MF_POPUP, (UINT_PTR)g_cheats, L"&Cheats");
    AppendMenuW(g_bar, MF_POPUP, (UINT_PTR)help, L"&Help");
    MENUINFO mi = {sizeof mi}; mi.fMask = MIM_BACKGROUND | MIM_APPLYTOSUBMENUS; mi.hbrBack = dark::brush(dark::BG); SetMenuInfo(g_bar, &mi);   /* margins of the drop-down menus */
    return g_bar;
}

void menu_sync(bool fullscreen, bool fps, int scale) {   /* selection marks (called when the state changes) */
    if (!g_bar) return;
    CheckMenuItem(g_view, ID_FULLSCREEN, fullscreen ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(g_view, ID_FPS, fps ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(g_view, ID_PCINPUT, display_pc_input() ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(g_view, ID_MOUSEMENU, display_mouse_menus() ? MF_CHECKED : MF_UNCHECKED);
    for (int s = 1; s <= 4; s++) CheckMenuItem(g_scale, ID_SCALE1 + s - 1, s == scale ? MF_CHECKED : MF_UNCHECKED);
}

static void show_controls(HWND h) {
    tf_msgbox(h, (std::wstring(
        L"PC keyboard and mouse (View menu; on by default)\n"
        L"   W A S D or arrows: D-pad (move / choose)\n"
        L"   Space, E or left click = \x2715 (confirm)\n"
        L"   Backspace, C or right click = \x25CB (back)\n"
        L"   F = \x25A1   R or middle click = \x25B3\n"
        L"   Q / Tab = L / R (or the mouse side buttons)\n"
        L"   Mouse wheel = up / down\n"
        L"   View > Mouse in menus (experimental, off): hovering highlights the item, clicking confirms\n"
        L"   Shift = run (holds \x25CB)   Enter = START   Ctrl = SELECT\n\n"
        L"Classic keyboard (with the option off)\n"
        L"   Arrows, Z = \x2715   X = \x25CB   A = \x25A1   S = \x25B3, Q / W = L / R\n\n"
        L"Controller (XInput)\n"
        L"   A = \x2715   B = \x25CB   X = \x25A1   Y = \x25B3\n"
        L"   LB / RB (or triggers) = L / R\n"
        L"   Start = START   Back = SELECT\n"
        L"   D-pad or left analog stick\n\n"
        L"Window\n")
        + hotkey_help_line(VK_F11) + hotkey_help_line(VK_F3) + hotkey_help_line(VK_F5)
        + std::wstring(L"   Esc: leave fullscreen / close the game\n\n"
                       L"Report problems (asks for a note; Esc or an empty note discards)\n")
        + hotkey_help_line(VK_F8) + hotkey_help_line(VK_F7) + hotkey_help_line(VK_F9)).c_str(),
        L"Controls", MB_OK | MB_ICONINFORMATION);
}
static void show_about(HWND h) {
    tf_msgbox(h,
        L"Tag Force Recomp\n\n"
        L"Static recompilation of Yu-Gi-Oh! GX Tag Force (PSP) for Windows. Preview release.\n"
        L"The game data is read from your own copy of the game (ISO); nothing from the game is included with this program.\n\n"
        L"Sound (ATRAC3/ATRAC3+) and movies (H.264): FFmpeg.",
        L"About", MB_OK | MB_ICONINFORMATION);
}

void menu_sync_toggles(void) {   /* checkmarks of the mods' on/off switches (the plugin may have changed them via a key) */
    if (!g_mods) return;
    for (int i = 0; i < plugin_toggle_count(); i++) CheckMenuItem(plugin_toggle_menu(i) ? g_cheats : g_mods, ID_MOD_TOGGLE + i, MF_BYCOMMAND | (plugin_toggle_on(i) ? MF_CHECKED : MF_UNCHECKED));
}

bool menu_command(HWND h, int id) {
    switch (id) {
    case ID_RESET: display_ask_reset(); return true;
    case ID_EXIT: display_ask_exit(); return true;
    case ID_FULLSCREEN: display_toggle_fullscreen(); return true;
    case ID_FPS: display_toggle_fps(); return true;
    case ID_SCALE1: case ID_SCALE2: case ID_SCALE3: case ID_SCALE4: display_set_scale(id - ID_SCALE1 + 1); return true;
    case ID_FILTER0: case ID_FILTER1: case ID_FILTER2: g_gfx.filter = id - ID_FILTER0; gfx_save(); gfx_menu_sync(); InvalidateRect(h, nullptr, FALSE); return true;
    case ID_ASPECT0: case ID_ASPECT1: case ID_ASPECT2: g_gfx.aspect = id - ID_ASPECT0; gfx_save(); gfx_menu_sync(); InvalidateRect(h, nullptr, TRUE); return true;
    case ID_RES1: case ID_RES2: case ID_RES3: case ID_RES4: case ID_RES5: case ID_RES6: { static const int resv[6] = {1, 3, 4, 6, 8, 0}; const int v = resv[id - ID_RES1]; g_gfx.res_auto = v == 0; g_gfx.res = v ? v : gfx_res_monitor(); ge_set_res(g_gfx.res); gfx_save(); gfx_menu_sync(); return true; }
    case ID_RENDER_SW: case ID_RENDER_D3D: case ID_RENDER_VK: display_set_renderer(id == ID_RENDER_VK ? GFX_RENDER_VULKAN : id == ID_RENDER_D3D ? GFX_RENDER_D3D11 : GFX_RENDER_SOFTWARE); gfx_menu_sync(); return true;
    case ID_MSAA1: case ID_MSAA2: case ID_MSAA4: case ID_MSAA8: { static const int mv[4] = {1, 2, 4, 8}; g_gfx.msaa = mv[id - ID_MSAA1]; ge_set_msaa(); gfx_save(); gfx_menu_sync(); return true; }
    case ID_MODS_DUMP: texrepl_set_dump(!texrepl_dumping()); CheckMenuItem(g_mods, ID_MODS_DUMP, texrepl_dumping() ? MF_CHECKED : MF_UNCHECKED); return true;
    case ID_SYNC0: case ID_SYNC1: case ID_SYNC2: g_gfx.sync = id - ID_SYNC0; gfx_save(); gfx_menu_sync(); return true;   /* a GPU recria a swapchain no proximo quadro */
    case ID_FPS30: case ID_FPS60: case ID_FPS120: g_gfx.fps = id - ID_FPS30; gfx_save(); gfx_menu_sync(); return true;
    case ID_PCINPUT: display_set_pc_input(!display_pc_input()); CheckMenuItem(g_view, ID_PCINPUT, display_pc_input() ? MF_CHECKED : MF_UNCHECKED); return true;
    case ID_MOUSEMENU: display_set_mouse_menus(!display_mouse_menus()); CheckMenuItem(g_view, ID_MOUSEMENU, display_mouse_menus() ? MF_CHECKED : MF_UNCHECKED); return true;
    case ID_CONTROLS: show_controls(h); return true;
    case ID_ABOUT: show_about(h); return true;
    case ID_MODS_FOLDER: mods_open_folder(); return true;
    case ID_MODS_APPLY: display_ask_reset(); return true;
    }
    if (id >= ID_MOD_TOGGLE && id < ID_MOD_TOGGLE + plugin_toggle_count()) { plugin_toggle_flip(id - ID_MOD_TOGGLE); menu_sync_toggles(); return true; }
    if (id >= ID_MOD_SLIDER && id < ID_MOD_SLIDER + plugin_slider_count()) {
        const int i = id - ID_MOD_SLIDER;
        if (plugin_slider_edit(h, i)) { auto it = g_item_id.find((UINT)id); if (it != g_item_id.end()) it->second->dir = plugin_slider_value(i); }   /* o item mostra o valor atual */
        return true;
    }
    if (id >= ID_MOD_FIRST && id < ID_MOD_FIRST + mods_count()) {   /* liga/desliga: grava agora, aplica ao reiniciar */
        int i = id - ID_MOD_FIRST; bool on = !mods_enabled(i);
        mods_set_enabled(i, on);
        CheckMenuItem(g_mods, id, on ? MF_CHECKED : MF_UNCHECKED);
        display_ask_reset_text(on ? L"Mod enabled: it takes effect after restarting the game.\n\nRestart now? Anything not saved inside the game will be lost."
                                  : L"Mod disabled: it takes effect after restarting the game.\n\nRestart now? Anything not saved inside the game will be lost.");
        return true;
    }
    return false;
}
