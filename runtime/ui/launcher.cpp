/* Start window (sidebar layout, in the style of PC port launchers): the pages on the left (Play, Graphics, Game, Mods, Cheats, About) and Quit;
 * content on the right; a fixed bottom bar with the status and the Stop/PLAY buttons.
 *  - Play: title banner, "Ready check" and the three main options;
 *  - Graphics: every graphics option (the same tagforce_recomp.ini keys the game reads);
 *  - Game: ISO / installation. Not installed yet: "INSTALL AND PLAY" copies the game files to the jogo/ folder and translates the game code
 *    (once, with progress); installed: just "PLAY" ("Reinstall from ISO..." redoes the folder);
 *  - About: credits and shortcuts to the logs and saves folders.
 * PLAY starts the game as a child process (TagForceRecomp.exe --dados <folder>) and the launcher stays open (Stop closes the game).
 * The last ISO chosen is kept in tagforce_recomp.ini (next to the .exe). Dark theme, everything drawn here, per-window DPI. */
#include <windows.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>
#include <algorithm>
#include "dark_ui.h"
#include "util.h"
#include "gameid.h"
#include "menu.h"
#include "hle.h"
#include "imgio.h"
#include "mods.h"

typedef void (*InstProgress)(double frac, const wchar_t *msg, void *ctx);
bool iso_install(const wchar_t *iso, const wchar_t *dest, InstProgress cb, void *ctx, std::wstring *err);

enum { ID_PICK = 101, ID_PLAY = 102, ID_QUIT = 103, ID_STOP = 105, ID_LOGS = 106, ID_SAVES = 107, ID_MODDIR = 108, ID_COPT0 = 400, ID_OPT0 = 200, ID_QOPT0 = 300, TIMER_UI = 1 };
enum { PG_PLAY, PG_GRAPHICS, PG_GAME, PG_MODS, PG_CHEATS, PG_ABOUT, N_PAGES };
enum { NAV_QUIT = N_PAGES };   /* item extra da sidebar (nao e pagina) */
static const wchar_t *kNavName[N_PAGES] = {L"Play", L"Graphics", L"Game", L"Mods", L"Cheats", L"About"};
static HWND g_wnd, g_bpick, g_play, g_bstop, g_blogs, g_bsaves, g_bmoddir;
static HANDLE g_proc;                       /* jogo em andamento (processo filho); a janela continua aberta */
static int g_page = PG_PLAY, g_navhot = -1;

/* Opcoes de graficos: mesmas chaves e valores do tagforce_recomp.ini que o jogo le (gfx.cpp / gfx_options.h) */
struct LOpt { const wchar_t *label, *hint, *key; int count; const wchar_t *names[6]; const int *values; int def; };
static const int kResV[6] = {1, 3, 4, 6, 8, 0};   /* resolucao: fator interno, 0 = a do monitor (gfx.h) */
static const int kWinV[3] = {0, 2, 1};   /* modo de janela: janela, exclusiva (tela=2), sem bordas (tela=1, o "tela cheia" antigo) */
static const LOpt kOpts[] = {
    {L"Renderer", L"Software uses the CPU. Direct3D 11 and Vulkan use the GPU.", L"renderizador", 3, {L"Software", L"Direct3D 11", L"Vulkan"}, nullptr, 1},
    {L"Resolution", L"Screen resolution, as in any PC game. 2560x1440 and 3840x2160 need the GPU.", L"resolucao", 6, {L"480 x 272 (PSP)", L"1280 x 720", L"1920 x 1080", L"2560 x 1440", L"3840 x 2160", L"Match monitor"}, kResV, 0},
    {L"Window mode", L"Fullscreen is exclusive (Direct3D 11 only; Vulkan uses borderless). Borderless covers the screen with a window. In game, F11 or Alt+Enter switches too.", L"tela", 3, {L"Windowed", L"Fullscreen", L"Fullscreen borderless"}, kWinV, 0},
    {L"Textures", L"Original is the plain PSP look. Enhanced is the remastered package: textures, menus and text in high resolution.", nullptr, 2, {L"Original", L"Enhanced"}, nullptr, 1},
    {L"Frame rate", L"60 and 120 FPS are interpolated from the original 30 (GPU). 120 needs a 120 Hz monitor.", L"fps60", 3, {L"30 FPS", L"60 FPS", L"120 FPS"}, nullptr, 1},
    {L"Synchronization", L"Default avoids tearing. VSync waits for the monitor.", L"sincronizacao", 3, {L"Default", L"VSync", L"Lowest latency"}, nullptr, 0},
    {L"Filter", L"How the 480 x 272 image is scaled to the window.", L"filtro", 3, {L"Sharp pixels", L"Smoothed", L"Bilinear"}, nullptr, 1},
    {L"Aspect ratio", L"Proportion of the image: original 16:9, stretched or 4:3.", L"proporcao", 3, {L"16:9", L"Stretch", L"4:3"}, nullptr, 0},
};
static const int N_OPT = (int)(sizeof kOpts / sizeof kOpts[0]);
enum { NQ = 4, OPT_FPS = 4 };
static const int kQuick[NQ] = {0, 1, 2, OPT_FPS};     /* opcoes repetidas na pagina Play: renderer, resolucao, modo de janela, FPS */
static HWND g_sel[N_OPT], g_qsel[NQ]; static int g_cur[N_OPT];
static std::wstring g_path;                 /* ISO escolhida */
static std::wstring g_data;                 /* pasta jogo/ */
static bool g_installed;                    /* jogo/ pronta */
static std::vector<uint32_t> g_thumbnail; static int g_thumbnail_w, g_thumbnail_h;
static int g_dpi = 96;
static HFONT g_fHero, g_fHead, g_fTitle, g_fSub, g_fText, g_fSmall, g_fBtn, g_fPlay;
/* instalacao em segundo plano */
static std::thread g_thr; static std::atomic<bool> g_installing{false}, g_inst_done{false}, g_inst_ok{false};
static std::atomic<int> g_prog{0}; static std::mutex g_mu; static std::wstring g_msg, g_err;

static const COLORREF C_BG = dark::BG, C_CARD = dark::CARD, C_LINE = dark::LINE, C_TXT = dark::TXT, C_DIM = dark::DIM,   /* paleta comum (dark_ui.h) */
                      C_OK = RGB(98, 200, 130), C_WARN = RGB(240, 190, 90), C_ERR = RGB(240, 110, 110), C_BTN = dark::BTN, C_BTN_HOT = dark::BTN_HOT,
                      C_ACC = dark::ACC, C_ACC_HOT = dark::ACC_HOT, C_OFF = dark::OFF, C_BAR = dark::LINE, C_SIDE = dark::BAR, C_NAVHOT = RGB(22, 28, 48);
/* pagina Mods: lista na ordem de prioridade (topo = vence), liga/desliga e sobe/desce; grava mods/disabled e mods/load_order.txt */
static std::vector<ModEntry> g_modlist; static int g_modscroll;   /* g_modlist: so os visiveis, da menor para a maior prioridade */
static std::wstring mods_dir() { return exe_dir() + L"mods"; }
static void mods_reload() { g_modlist.clear(); for (auto &e : mods_list_dir(mods_dir())) if (e.visible) g_modlist.push_back(e); }
static bool file_exists(const std::wstring &p); static bool dir_exists(const std::wstring &p);
/* Cheats page: cheats.ini next to the exe (cheats.cpp reads it at start; the master switch is its "enabled" key) */
struct CheatOpt { const wchar_t *label, *hint; int count; const wchar_t *const *names; };
static const int N_CHEAT = 6;
static const wchar_t *const kOnOff[2] = {L"Off", L"On"}, *const kCorner[4] = {L"Top left", L"Top right", L"Bottom left", L"Bottom right"},
    *const kMult[10] = {L"x1 (normal)", L"x2", L"x3", L"x4", L"x5", L"x6", L"x7", L"x8", L"x9", L"x10"};
static const CheatOpt kCheat[N_CHEAT] = {
    {L"Cheats", L"Master switch. Off: none of the cheats below is loaded.", 2, kOnOff},
    {L"See the opponent's cards", L"Shows the opponent's hand and field in a panel during duels (F6 in game).", 2, kOnOff},
    {L"Card panel position", L"Corner of the screen where that panel is drawn.", 4, kCorner},
    {L"DP multiplier", L"Multiplies the DP you win in duels.", 10, kMult},
    {L"Unlock everything", L"Unlocks all cards, packs and duelists each time the game loads your save. In game, F2 does it once.", 2, kOnOff},
    {L"Fast-forward", L"Game speed when the game starts (the music keeps its pitch). In game: Cheats menu, or L + R + Left / Right.", 8, kMult},
};
static HWND g_csel[N_CHEAT]; static int g_cc[N_CHEAT] = {1, 0, 1, 0, 0, 0};
std::wstring cheats_ini_path(void);   /* cheats.cpp */
static bool cheat_read(const std::wstring &path) {
    FILE *f = _wfopen(path.c_str(), L"r"); if (!f) return false;
    char l[96]; int v;
    while (fgets(l, sizeof l, f)) {
        if (sscanf(l, "enabled=%d", &v) == 1) g_cc[0] = v != 0;
        else if (sscanf(l, "show_opponent_cards=%d", &v) == 1) g_cc[1] = v != 0;
        else if (sscanf(l, "unlock_on_start=%d", &v) == 1) g_cc[4] = v != 0;
        else if (sscanf(l, "panel_corner=%d", &v) == 1) g_cc[2] = std::max(0, std::min(3, v));
        else if (sscanf(l, "dp_multiplier=%d", &v) == 1) g_cc[3] = std::max(1, std::min(10, v)) - 1;
        else if (sscanf(l, "fast_forward=%d", &v) == 1) g_cc[5] = std::max(1, std::min(8, v)) - 1;
    }
    fclose(f); return true;
}
static void cheat_load() {
    g_cc[0] = 1; g_cc[1] = 0; g_cc[2] = 1; g_cc[3] = 0; g_cc[4] = 0; g_cc[5] = 0;
    if (cheat_read(cheats_ini_path())) return;
    const std::wstring old = exe_dir() + L"mods/Cheats";   /* settings from when Cheats was a mod: cheats.cpp imports the same files on the first start */
    if (cheat_read(old + L"/config.ini") && file_exists(old + L"/disabled")) g_cc[0] = 0;
}
static void cheat_save() {
    if (FILE *f = _wfopen(cheats_ini_path().c_str(), L"w")) { fprintf(f, "enabled=%d\nshow_opponent_cards=%d\npanel_corner=%d\ndp_multiplier=%d\nunlock_on_start=%d\nfast_forward=%d\n", g_cc[0], g_cc[1], g_cc[2], g_cc[3] + 1, g_cc[4], g_cc[5] + 1); fclose(f); }
}
static void cheat_text() { for (int i = 0; i < N_CHEAT; i++) { SetWindowTextW(g_csel[i], kCheat[i].names[g_cc[i]]); InvalidateRect(g_csel[i], nullptr, FALSE); } }
static void cheat_step(int i, int dir) { g_cc[i] = (g_cc[i] + dir + kCheat[i].count) % kCheat[i].count; cheat_save(); cheat_text(); }
static int S(int v) { return MulDiv(v, g_dpi, 96); }
/* medidas em 96 dpi */
static const int W96 = 1120, H96 = 720, SIDE96 = 220, BAR96 = 72;

static std::wstring ini_path() { return app_ini_path(); }
static bool file_exists(const std::wstring &p) { DWORD a = GetFileAttributesW(p.c_str()); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }
static bool dir_exists(const std::wstring &p) { DWORD a = GetFileAttributesW(p.c_str()); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY); }
static bool iso_ok() { return !g_path.empty() && file_exists(g_path); }
static void update_data_dir() {   /* a pasta de dados depende da versao do jogo da ISO escolhida (gameid.cpp) */
    std::string id = iso_ok() ? game_id_iso(g_path) : ""; g_data = exe_dir() + game_data_dir_name(id.empty() ? game_id_built() : id);
}
static bool check_installed() { g_installed = file_exists(g_data + L"\\_disco.tsv"); return g_installed; }

static bool game_running() {
    if (g_proc && WaitForSingleObject(g_proc, 0) != WAIT_TIMEOUT) { CloseHandle(g_proc); g_proc = nullptr; }
    return g_proc != nullptr;
}
/* Textures: nao e chave do .ini, e o mod EnhancedTextures ligado/desligado (arquivo "disabled" na pasta do mod, como o jogo faz) */
static std::wstring pack_marker(const wchar_t *mod) { return exe_dir() + L"mods/" + mod + L"/disabled"; }
static void pack_set(bool enhanced) {
    const std::wstring on = pack_marker(L"EnhancedTextures"), old = pack_marker(L"TexturasPC");
    if (enhanced) DeleteFileW(on.c_str()); else if (dir_exists(exe_dir() + L"mods/EnhancedTextures")) { if (HANDLE h = CreateFileW(on.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)) CloseHandle(h); }
    if (enhanced && dir_exists(exe_dir() + L"mods/TexturasPC")) { if (HANDLE h = CreateFileW(old.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)) CloseHandle(h); }   /* um pacote so (os dois carregariam cada textura duas vezes) */
}
static void opt_load() {
    for (int i = 0; i < N_OPT; i++) {
        const LOpt &o = kOpts[i];
        if (!o.key) { g_cur[i] = dir_exists(exe_dir() + L"mods/EnhancedTextures") && !file_exists(pack_marker(L"EnhancedTextures")) ? 1 : 0; continue; }
        int v = (int)GetPrivateProfileIntW(L"graficos", o.key, -9999, ini_path().c_str()), c = -1;
        for (int k = 0; k < o.count; k++) if ((o.values ? o.values[k] : k) == v) c = k;
        if (c < 0) for (int k = 0; k < o.count; k++) if ((o.values ? o.values[k] : k) == o.def) c = k;
        g_cur[i] = c < 0 ? 0 : c;
    }
}
static void opt_step(int i, int dir) {
    const LOpt &o = kOpts[i]; g_cur[i] = (g_cur[i] + dir + o.count) % o.count;
    if (!o.key) pack_set(g_cur[i] == 1);
    else { wchar_t b[16]; swprintf(b, 16, L"%d", o.values ? o.values[g_cur[i]] : g_cur[i]); WritePrivateProfileStringW(L"graficos", o.key, b, ini_path().c_str()); }
    SetWindowTextW(g_sel[i], o.names[g_cur[i]]); InvalidateRect(g_sel[i], nullptr, FALSE);
    for (int k = 0; k < NQ; k++) if (kQuick[k] == i) { SetWindowTextW(g_qsel[k], o.names[g_cur[i]]); InvalidateRect(g_qsel[k], nullptr, FALSE); }
    InvalidateRect(g_wnd, nullptr, FALSE);   /* "Ready check" mostra o renderizador */
}
static void launch_game() {
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(exe) + L"\" " + L"--dados \"" + g_data + L"\"";
    STARTUPINFOW si = {sizeof si}; PROCESS_INFORMATION pi;
    if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, exe_dir().c_str(), &si, &pi)) { tf_msgbox(g_wnd, L"Could not start the game.", L"Tag Force Recomp", MB_ICONERROR); return; }
    CloseHandle(pi.hThread); g_proc = pi.hProcess;
}
/* Stop: pede o fechamento pela janela do jogo (WM_CLOSE -> o jogo grava o resumo [perf], a lista de texturas recentes e o fim do log); so mata se nao fechar em 4 s */
static BOOL CALLBACK close_game_window(HWND h, LPARAM lp) {
    DWORD pid = 0; GetWindowThreadProcessId(h, &pid); wchar_t cls[32] = L"";
    if (pid == (DWORD)lp && GetClassNameW(h, cls, 32) && !wcscmp(cls, L"TagForceRecomp")) PostMessageW(h, WM_CLOSE, 0, 0);
    return TRUE;
}
static void stop_game() {
    if (!g_proc) return;
    EnumWindows(close_game_window, (LPARAM)GetProcessId(g_proc));
    if (WaitForSingleObject(g_proc, 4000) == WAIT_TIMEOUT) TerminateProcess(g_proc, 0);
}
static bool is_sel(int id) { return (id >= ID_COPT0 && id < ID_COPT0 + N_CHEAT) ||  (id >= ID_OPT0 && id < ID_OPT0 + N_OPT) || (id >= ID_QOPT0 && id < ID_QOPT0 + NQ); }
static int sel_option(int id) { return id >= ID_QOPT0 ? kQuick[id - ID_QOPT0] : id - ID_OPT0; }

/* which controls each page shows */
static void apply_page() {
    for (int i = 0; i < N_OPT; i++) ShowWindow(g_sel[i], g_page == PG_GRAPHICS ? SW_SHOW : SW_HIDE);
    for (int i = 0; i < N_CHEAT; i++) ShowWindow(g_csel[i], g_page == PG_CHEATS ? SW_SHOW : SW_HIDE);
    for (int k = 0; k < NQ; k++) ShowWindow(g_qsel[k], g_page == PG_PLAY ? SW_SHOW : SW_HIDE);
    ShowWindow(g_bpick, g_page == PG_GAME ? SW_SHOW : SW_HIDE);
    ShowWindow(g_bmoddir, g_page == PG_MODS ? SW_SHOW : SW_HIDE);
    ShowWindow(g_blogs, g_page == PG_ABOUT ? SW_SHOW : SW_HIDE); ShowWindow(g_bsaves, g_page == PG_ABOUT ? SW_SHOW : SW_HIDE);
}
static void refresh() {
    bool busy = g_installing, run = game_running();
    EnableWindow(g_play, !busy && !run && (g_installed || iso_ok())); EnableWindow(g_bpick, !busy && !run);
    EnableWindow(g_bstop, run); for (int i = 0; i < N_CHEAT; i++) EnableWindow(g_csel[i], !run);
    for (int i = 0; i < N_OPT; i++) EnableWindow(g_sel[i], !run); for (int k = 0; k < NQ; k++) EnableWindow(g_qsel[k], !run);
    SetWindowTextW(g_play, run ? L"PLAYING..." : g_installed ? L"PLAY" : L"INSTALL AND PLAY"); SetWindowTextW(g_bpick, g_installed ? L"Reinstall from ISO..." : L"Select ISO...");
    apply_page();
    InvalidateRect(g_wnd, nullptr, FALSE);
    for (HWND b : {g_bpick, g_play, g_bstop, g_blogs, g_bsaves, g_bmoddir}) InvalidateRect(b, nullptr, FALSE);
    for (int i = 0; i < N_OPT; i++) InvalidateRect(g_sel[i], nullptr, FALSE); for (int k = 0; k < NQ; k++) InvalidateRect(g_qsel[k], nullptr, FALSE);
}
static void show_page(int p) { if (p == g_page) return; g_page = p; if (p == PG_MODS) { mods_reload(); g_modscroll = 0; } if (p == PG_CHEATS) { cheat_load(); cheat_text(); } refresh(); }
static bool pick(HWND owner) {
    wchar_t buf[1024] = L"";
    OPENFILENAMEW of = {}; of.lStructSize = sizeof of; of.hwndOwner = owner;
    of.lpstrFilter = L"UMD image (*.iso)\0*.iso\0All files\0*.*\0";
    of.lpstrFile = buf; of.nMaxFile = 1024; of.lpstrTitle = L"Select the Yu-Gi-Oh! GX Tag Force ISO";
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;   /* nao mudar a pasta atual (ms0/) */
    if (!GetOpenFileNameW(&of)) return false;
    g_path = buf; WritePrivateProfileStringW(L"geral", L"iso", buf, ini_path().c_str()); update_data_dir(); check_installed(); return true;
}
static void start_install(bool install = true) {
    if (g_thr.joinable()) g_thr.join();
    g_installing = true; g_inst_done = false; g_inst_ok = false; g_prog = 0; { std::lock_guard<std::mutex> lk(g_mu); g_err.clear(); g_msg = L"preparing..."; }
    g_thr = std::thread([install] {
        std::wstring err; bool ok = true;
        auto prog = [](double f, const wchar_t *m, void *c) { double lo = c ? *(double *)c : 0, hi = c ? ((double *)c)[1] : 1; g_prog = (int)((lo + f * (hi - lo)) * 1000); std::lock_guard<std::mutex> lk(g_mu); g_msg = m; };
        double inst[2] = {0, 0.4}, code[2] = {install ? 0.4 : 0, 1};
        if (install) ok = iso_install(g_path.c_str(), g_data.c_str(), prog, inst, &err);
        if (ok) {   /* the release has no game code: translate it from the installed files (a few minutes, only once) */
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
static void text(HDC dc, HFONT f, COLORREF c, RECT r, const wchar_t *t, UINT fl = DT_LEFT | DT_SINGLELINE | DT_VCENTER) {
    HGDIOBJ o = SelectObject(dc, f); SetTextColor(dc, c); DrawTextW(dc, t, -1, &r, fl); SelectObject(dc, o);
}
static void focus_ring(HDC dc, RECT r) {
    RECT f = r; InflateRect(&f, -S(3), -S(3)); HPEN p = CreatePen(PS_DOT, 1, RGB(200, 200, 210)); HGDIOBJ op = SelectObject(dc, p), ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, f.left, f.top, f.right, f.bottom, S(6), S(6)); SelectObject(dc, op); SelectObject(dc, ob); DeleteObject(p);
}
static void draw_button_dc(const DRAWITEMSTRUCT *d, HDC dc) {
    RECT r = d->rcItem; const int id = (int)d->CtlID; bool primary = id == ID_PLAY, off = (d->itemState & ODS_DISABLED) != 0, down = (d->itemState & ODS_SELECTED) != 0;
    bool hot = !off && is_hot(d->hwndItem);
    bool onbar = id == ID_PLAY || id == ID_STOP;   /* estes ficam sobre a barra inferior */
    HBRUSH bgb = CreateSolidBrush(onbar ? C_SIDE : C_BG); FillRect(dc, &r, bgb); DeleteObject(bgb);
    wchar_t t[64]; GetWindowTextW(d->hwndItem, t, 64);
    SetBkMode(dc, TRANSPARENT);
    if (is_sel(id)) {   /* seletor: < valor >  (clique na ponta esquerda volta, no resto avanca) */
        round_fill(dc, r, off ? C_OFF : hot ? C_BTN_HOT : C_BTN, off ? C_OFF : hot ? RGB(78, 92, 140) : C_LINE, S(8));
        POINT pt; GetCursorPos(&pt); ScreenToClient(d->hwndItem, &pt);
        bool lh = hot && pt.x < (r.right - r.left) / 3, rh = hot && !lh;
        RECT tr = r; tr.left += S(30); tr.right -= S(30); text(dc, g_fBtn, off ? dark::OFF_TXT : C_TXT, tr, t, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        RECT lr = r; lr.right = lr.left + S(30); text(dc, g_fBtn, off ? dark::OFF_TXT : lh ? C_ACC_HOT : C_ACC, lr, L"\x2039", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        RECT rr = r; rr.left = rr.right - S(30); text(dc, g_fBtn, off ? dark::OFF_TXT : rh ? C_ACC_HOT : C_ACC, rr, L"\x203A", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (d->itemState & ODS_FOCUS) focus_ring(dc, r);
        return;
    }
    COLORREF bg = off ? C_OFF : primary ? (hot || down ? C_ACC_HOT : C_ACC) : (hot || down ? C_BTN_HOT : C_BTN);
    round_fill(dc, r, bg, (off || primary) ? bg : (hot ? RGB(78, 92, 140) : C_LINE), S(8));
    RECT tr = r; if (down) OffsetRect(&tr, 0, 1);
    text(dc, primary ? g_fPlay : g_fBtn, off ? dark::OFF_TXT : primary ? dark::ACC_TXT : C_TXT, tr, t, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (d->itemState & ODS_FOCUS) focus_ring(dc, r);
}
static void draw_button(const DRAWITEMSTRUCT *d) {   /* compoe num bitmap e copia de uma vez: sem piscar */
    int w = d->rcItem.right - d->rcItem.left, h = d->rcItem.bottom - d->rcItem.top; if (w <= 0 || h <= 0) return;
    HDC mdc = CreateCompatibleDC(d->hDC); HBITMAP bm = CreateCompatibleBitmap(d->hDC, w, h); HGDIOBJ obm = SelectObject(mdc, bm);
    DRAWITEMSTRUCT m = *d; m.hDC = mdc; SetRect(&m.rcItem, 0, 0, w, h);
    draw_button_dc(&m, mdc);
    BitBlt(d->hDC, d->rcItem.left, d->rcItem.top, w, h, mdc, 0, 0, SRCCOPY);
    SelectObject(mdc, obm); DeleteObject(bm); DeleteDC(mdc);
}

/* ---- geometria (px reais) ---- */
static int nav_y(int i) { return S(116) + i * S(40); }
static int nav_at(int x, int y) {
    if (x < 0 || x >= S(SIDE96)) return -1;
    for (int i = 0; i < N_PAGES; i++) if (y >= nav_y(i) && y < nav_y(i) + S(40)) return i;
    RECT cr; GetClientRect(g_wnd, &cr); int qy = cr.bottom - S(BAR96);
    if (y >= qy && y < cr.bottom) return NAV_QUIT;   /* Quit ocupa a altura da barra inferior, na sidebar */
    return -1;
}
/* pagina Mods: linha k (0 = topo = maior prioridade) mostra g_modlist[n-1-k] */
static int mod_row_h() { return S(56); }
static RECT mod_area() { RECT cr; GetClientRect(g_wnd, &cr); return RECT{S(SIDE96) + S(28), S(100), cr.right - S(28), cr.bottom - S(BAR96) - S(10)}; }
static int mod_rows_visible() { RECT a = mod_area(); return std::max(1, (int)((a.bottom - a.top) / mod_row_h())); }
static void mod_clamp_scroll() { int mx = std::max(0, (int)g_modlist.size() - mod_rows_visible()); g_modscroll = std::min(std::max(g_modscroll, 0), mx); }
static RECT mod_row_rect(int k) { RECT a = mod_area(); int y = a.top + (k - g_modscroll) * mod_row_h(); return RECT{a.left, y, a.right, y + mod_row_h() - S(8)}; }
static RECT mod_btn_rect(RECT row, int part) {   /* part 1 = subir, 2 = descer */
    int sz = S(30), x = row.right - S(14) - sz * 2 - S(8) + (part == 2 ? sz + S(8) : 0), y = (row.top + row.bottom - sz) / 2; return RECT{x, y, x + sz, y + sz};
}
/* devolve a linha (0 = topo) e a parte: 0 liga/desliga, 1 subir prioridade, 2 descer; -1 fora */
static int mod_hit(int x, int y, int *part) {
    RECT a = mod_area(); POINT pt = {x, y}; if (!PtInRect(&a, pt)) return -1;
    for (int k = g_modscroll; k < (int)g_modlist.size() && k < g_modscroll + mod_rows_visible(); k++) {
        RECT r = mod_row_rect(k); if (!PtInRect(&r, pt)) continue;
        for (int p = 1; p <= 2; p++) { RECT b = mod_btn_rect(r, p); if (PtInRect(&b, pt)) { *part = p; return k; } }
        *part = 0; return k;
    }
    return -1;
}
static void mod_click(int x, int y) {
    int part = 0, k = mod_hit(x, y, &part); if (k < 0 || game_running() || g_installing) return;
    const int n = (int)g_modlist.size(); const std::string folder = g_modlist[n - 1 - k].folder;
    if (part == 0) mods_dir_set_enabled(mods_dir(), folder, !g_modlist[n - 1 - k].enabled);
    else if ((part == 1 && k > 0) || (part == 2 && k < n - 1)) mods_dir_move(mods_dir(), folder, part == 1 ? 1 : -1);
    else return;
    mods_reload(); InvalidateRect(g_wnd, nullptr, FALSE);
}
static void draw_mods_page(HDC dc, int cx, int cw) {
    text(dc, g_fHead, C_TXT, RECT{cx, S(22), cx + cw - S(210), S(58)}, L"Mods");
    text(dc, g_fSub, C_DIM, RECT{cx, S(58), cx + cw - S(190), S(96)}, game_running() ? L"The game is running \x2014 close it to change mods." : L"Top = highest priority: when two mods change the same file, the upper one wins. Changes apply on the next start.", DT_LEFT | DT_WORDBREAK);
    if (g_modlist.empty()) { text(dc, g_fText, C_DIM, RECT{cx, S(110), cx + cw, S(140)}, L"No mods installed. Use \"Open mods folder\" to add some."); return; }
    mod_clamp_scroll(); const int n = (int)g_modlist.size(); const bool lock = game_running();
    RECT a = mod_area(); int saved = SaveDC(dc); IntersectClipRect(dc, a.left, a.top, a.right, a.bottom);
    for (int k = g_modscroll; k < n && k < g_modscroll + mod_rows_visible(); k++) {
        const ModEntry &e = g_modlist[n - 1 - k]; RECT r = mod_row_rect(k);
        round_fill(dc, r, C_CARD, C_LINE, S(10));
        wchar_t num[16]; swprintf(num, 16, L"%d", k + 1);
        text(dc, g_fHead, e.enabled ? C_ACC : C_DIM, RECT{r.left + S(14), r.top, r.left + S(52), r.bottom}, num, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        RECT tg = {r.left + S(62), (r.top + r.bottom) / 2 - S(12), r.left + S(62) + S(46), (r.top + r.bottom) / 2 + S(12)};   /* interruptor */
        round_fill(dc, tg, e.enabled && !lock ? C_ACC : C_OFF, e.enabled && !lock ? C_ACC : C_LINE, S(12));
        int kn = S(18), kx = e.enabled ? tg.right - S(3) - kn : tg.left + S(3), ky = (tg.top + tg.bottom - kn) / 2;
        round_fill(dc, RECT{kx, ky, kx + kn, ky + kn}, e.enabled ? dark::ACC_TXT : C_DIM, e.enabled ? dark::ACC_TXT : C_DIM, S(9));
        RECT nr = {tg.right + S(16), r.top + S(6), mod_btn_rect(r, 1).left - S(12), r.top + S(28)};
        text(dc, g_fText, e.enabled ? C_TXT : C_DIM, nr, e.label.c_str(), DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        std::wstring d = e.detail.empty() ? std::wstring(e.enabled ? L"On" : L"Off") : e.detail + (e.enabled ? L"" : L"  \x00B7  off");
        text(dc, g_fSmall, C_DIM, RECT{nr.left, r.top + S(28), nr.right, r.bottom - S(4)}, d.c_str(), DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        for (int p = 1; p <= 2; p++) {
            RECT b = mod_btn_rect(r, p); bool can = !lock && (p == 1 ? k > 0 : k < n - 1);
            round_fill(dc, b, can ? C_BTN : C_OFF, can ? C_LINE : C_OFF, S(8));
            text(dc, g_fBtn, can ? C_ACC : dark::OFF_TXT, b, p == 1 ? L"\x25B2" : L"\x25BC", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }
    RestoreDC(dc, saved);
}
static void draw_progress(HDC dc, RECT bar) {
    round_fill(dc, bar, C_BAR, C_BAR, S(8));
    RECT fill = bar; fill.right = bar.left + MulDiv(bar.right - bar.left, g_prog, 1000); if (fill.right > bar.left + S(8)) round_fill(dc, fill, C_ACC, C_ACC, S(8));
}
static void section_label(HDC dc, int x, int y, int w, const wchar_t *t) { text(dc, g_fSmall, C_ACC, RECT{x, y, x + w, y + S(18)}, t); }
static void check_line(HDC dc, int x, int y, int w, COLORREF c, const wchar_t *mark, const std::wstring &t) {
    text(dc, g_fText, c, RECT{x, y, x + S(22), y + S(24)}, mark);
    text(dc, g_fText, C_TXT, RECT{x + S(24), y, x + w, y + S(24)}, t.c_str(), DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_PATH_ELLIPSIS);
}
static std::wstring gpu_name() {
    DISPLAY_DEVICEW dd = {sizeof dd}; if (EnumDisplayDevicesW(nullptr, 0, &dd, 0) && dd.DeviceString[0]) return dd.DeviceString; return L"Unknown";
}
static void load_thumbnail() {
    g_thumbnail.clear(); g_thumbnail_w = g_thumbnail_h = 0;
    std::wstring path = exe_dir() + L"yugioh.png";
    if (!img_load(to_utf8(path).c_str(), &g_thumbnail_w, &g_thumbnail_h, g_thumbnail)) { g_thumbnail_w = g_thumbnail_h = 0; return; }
    /* WIC devolve 0xAABBGGRR; StretchDIBits com BI_RGB espera 0xAARRGGBB. Escurece para servir de fundo ao titulo. */
    for (uint32_t &p : g_thumbnail) {
        uint32_t bgra = (p & 0xff00ff00u) | ((p & 0xffu) << 16) | ((p >> 16) & 0xffu);
        uint32_t r = ((bgra >> 16) & 0xffu) * 48 / 100, g = ((bgra >> 8) & 0xffu) * 48 / 100, b = (bgra & 0xffu) * 48 / 100;
        p = (bgra & 0xff000000u) | (r << 16) | (g << 8) | b;
    }
}
static void draw_thumbnail(HDC dc, RECT r) {
    if (g_thumbnail.empty() || !g_thumbnail_w || !g_thumbnail_h) return;

    const int dw = r.right - r.left, dh = r.bottom - r.top;
    int sx = 0, sy = 0, sw = g_thumbnail_w, sh = g_thumbnail_h;
    if ((long long)sw * dh > (long long)sh * dw) { sw = (int)((long long)sh * dw / dh); sx = (g_thumbnail_w - sw) / 2; }
    else { sh = (int)((long long)sw * dh / dw); sy = (g_thumbnail_h - sh) / 4; }   /* mantem o enquadramento que preserva a logo */

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth = g_thumbnail_w; bi.bmiHeader.biHeight = -g_thumbnail_h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    int saved = SaveDC(dc); SetStretchBltMode(dc, HALFTONE); SetBrushOrgEx(dc, 0, 0, nullptr);
    StretchDIBits(dc, r.left, r.top, dw, dh, sx, sy, sw, sh, g_thumbnail.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
    RestoreDC(dc, saved);
}

static void paint(HWND h) {
    PAINTSTRUCT ps; HDC wdc = BeginPaint(h, &ps); RECT cr; GetClientRect(h, &cr);
    HDC dc = CreateCompatibleDC(wdc); HBITMAP bm = CreateCompatibleBitmap(wdc, cr.right, cr.bottom); HGDIOBJ obm = SelectObject(dc, bm);   /* sem tremulo */
    HBRUSH bg = CreateSolidBrush(C_BG); FillRect(dc, &cr, bg); DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT);
    const int SW = S(SIDE96), BH = S(BAR96), cx = SW + S(28), cw = cr.right - S(28) - cx;
    bool busy = g_installing, run = game_running(); std::wstring msg, err; { std::lock_guard<std::mutex> lk(g_mu); msg = g_msg; err = g_err; }

    /* sidebar */
    { RECT sb = {0, 0, SW, cr.bottom}; HBRUSH b = CreateSolidBrush(C_SIDE); FillRect(dc, &sb, b); DeleteObject(b);
      RECT ln = {SW - 1, 0, SW, cr.bottom}; b = CreateSolidBrush(C_LINE); FillRect(dc, &ln, b); DeleteObject(b); }
    text(dc, g_fSub, C_TXT, RECT{S(24), S(26), SW - S(12), S(46)}, L"Yu-Gi-Oh! GX");
    text(dc, g_fHead, C_ACC, RECT{S(24), S(44), SW - S(12), S(76)}, L"TAG FORCE");
    text(dc, g_fSmall, C_DIM, RECT{S(24), S(78), SW - S(12), S(96)}, L"PC recompilation");
    for (int i = 0; i <= N_PAGES; i++) {
        bool quit = i == NAV_QUIT, act = !quit && i == g_page, hot = i == g_navhot;
        RECT it = quit ? RECT{0, cr.bottom - BH, SW - 1, cr.bottom} : RECT{0, nav_y(i), SW - 1, nav_y(i) + S(40)};
        if (quit) { RECT ln = {0, it.top, SW - 1, it.top + 1}; HBRUSH b = CreateSolidBrush(C_LINE); FillRect(dc, &ln, b); DeleteObject(b); }
        if (act || hot) { HBRUSH b = CreateSolidBrush(act ? C_BG : C_NAVHOT); FillRect(dc, &it, b); DeleteObject(b); }
        if (act) { RECT bar = {0, it.top, S(3), it.bottom}; HBRUSH b = CreateSolidBrush(C_ACC); FillRect(dc, &bar, b); DeleteObject(b); }
        RECT tr = it; tr.left += S(28);
        text(dc, g_fText, act ? C_ACC : hot ? C_TXT : quit ? C_DIM : C_TXT, tr, quit ? L"Quit" : kNavName[i]);
    }

    /* barra inferior: status a esquerda, Stop/PLAY a direita */
    { RECT bar = {SW, cr.bottom - BH, cr.right, cr.bottom}; HBRUSH b = CreateSolidBrush(C_SIDE); FillRect(dc, &bar, b); DeleteObject(b);
      RECT ln = {SW, bar.top, cr.right, bar.top + 1}; b = CreateSolidBrush(C_LINE); FillRect(dc, &ln, b); DeleteObject(b);
      RECT st = {cx, bar.top + S(10), cr.right - S(28) - S(310), cr.bottom - S(10)};
      if (busy) {
          wchar_t pc[32]; swprintf(pc, 32, L"%d%%", g_prog / 10);
          text(dc, g_fSmall, C_DIM, RECT{st.left, st.top, st.right - S(46), st.top + S(24)}, msg.c_str(), DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_PATH_ELLIPSIS);
          text(dc, g_fSmall, C_TXT, RECT{st.right - S(44), st.top, st.right, st.top + S(24)}, pc, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
          draw_progress(dc, RECT{st.left, st.top + S(28), st.right, st.top + S(38)});
      } else if (!err.empty()) text(dc, g_fSmall, C_ERR, st, err.c_str(), DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
      else if (run) text(dc, g_fText, C_OK, st, L"\x25CF Game running \x2014 close it to change the options");
      else if (g_installed) text(dc, g_fText, C_OK, st, L"\x2713 Ready to play");
      else text(dc, g_fText, C_WARN, st, iso_ok() ? L"Not installed yet \x2014 press INSTALL AND PLAY" : L"Choose the game ISO in the Game page"); }

    if (g_page == PG_PLAY) {
        /* faixa de titulo (degrade vertical) */
        int bh = S(220);
        for (int y = 0; y < bh; y++) {
            int t = y * 256 / bh; COLORREF c = RGB(GetRValue(C_SIDE) + (GetRValue(C_CARD) - GetRValue(C_SIDE)) * t / 256 + 6 * t / 256, GetGValue(C_SIDE) + (GetGValue(C_CARD) - GetGValue(C_SIDE)) * t / 256 + 6 * t / 256, GetBValue(C_SIDE) + (GetBValue(C_CARD) - GetBValue(C_SIDE)) * t / 256 + 14 * t / 256);
            RECT l = {SW, y, cr.right, y + 1}; HBRUSH b = CreateSolidBrush(c); FillRect(dc, &l, b); DeleteObject(b);
        }
        draw_thumbnail(dc, RECT{SW, 0, cr.right, bh});
        dark::accent_line(dc, SW, cr.right, bh - S(2), S(2));
        std::string gid = game_id_built(); std::wstring ver = to_wide(gid.empty() ? std::string("Yu-Gi-Oh! GX Tag Force") : gid);
        text(dc, g_fHero, C_TXT, RECT{cx, S(110), cr.right - S(28), S(160)}, L"Yu-Gi-Oh! GX Tag Force");
        text(dc, g_fSub, C_ACC, RECT{cx, S(162), cr.right - S(28), S(186)}, (L"Static recompilation for PC  \x00B7  " + ver).c_str());
        int top = bh + S(26), colw = (cw - S(40)) * 11 / 20, rx = cx + colw + S(40);
        section_label(dc, cx, top, colw, L"READY CHECK");
        int y = top + S(30), gap = S(34);
        std::wstring gtxt = g_installed ? L"Game installed (" + g_data + L")" : iso_ok() ? L"ISO selected \x2014 not installed yet" : L"No game ISO selected";
        check_line(dc, cx, y, colw, g_installed ? C_OK : iso_ok() ? C_WARN : C_ERR, g_installed ? L"\x2713" : L"!", gtxt); y += gap;
        bool sv = dir_exists(exe_dir() + L"ms0");
        check_line(dc, cx, y, colw, sv ? C_OK : C_DIM, sv ? L"\x2713" : L"\x2022", sv ? L"Saves found (ms0 folder)" : L"No saves yet (created on the first run)"); y += gap;
        check_line(dc, cx, y, colw, C_OK, L"\x2713", L"Graphics card: " + gpu_name()); y += gap;
        check_line(dc, cx, y, colw, C_OK, L"\x2713", std::wstring(L"Renderer: ") + kOpts[0].names[g_cur[0]] + L"  \x00B7  " + kOpts[1].names[g_cur[1]] + L"  \x00B7  " + kOpts[OPT_FPS].names[g_cur[OPT_FPS]]);
        section_label(dc, rx, top, cx + cw - rx, L"QUICK SETTINGS");
        for (int k = 0; k < NQ; k++) {   /* rotulo de cada seletor rapido */
            RECT w; GetWindowRect(g_qsel[k], &w); POINT p = {w.left, w.top}; ScreenToClient(h, &p);
            text(dc, g_fText, C_TXT, RECT{rx, p.y - S(24), rx + S(120), p.y - S(2)}, kOpts[kQuick[k]].label);
        }
    } else if (g_page == PG_GRAPHICS) {
        text(dc, g_fHead, C_TXT, RECT{cx, S(22), cx + cw, S(58)}, L"Graphics");
        text(dc, g_fSub, C_DIM, RECT{cx, S(58), cx + cw, S(80)}, L"Saved in tagforce_recomp.ini; the in-game Graphics screen changes the same values.");
        for (int i = 0; i < N_OPT; i++) {
            int y = S(100) + i * S(56);
            text(dc, g_fText, C_TXT, RECT{cx, y, cx + cw - S(300), y + S(24)}, kOpts[i].label);
            text(dc, g_fSmall, C_DIM, RECT{cx, y + S(24), cx + cw - S(290), y + S(44)}, kOpts[i].hint, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            RECT l = {cx, y + S(50), cx + cw, y + S(51)}; HBRUSH b = CreateSolidBrush(C_LINE); FillRect(dc, &l, b); DeleteObject(b);
        }
    } else if (g_page == PG_GAME) {
        text(dc, g_fHead, C_TXT, RECT{cx, S(22), cx + cw, S(58)}, L"Game");
        text(dc, g_fSub, C_DIM, RECT{cx, S(58), cx + cw, S(80)}, L"The game is installed once from your own ISO into the jogo folder next to the program.");
        RECT card = {cx, S(100), cx + cw, S(200)}; round_fill(dc, card, C_CARD, C_LINE, S(10));
        int ix = card.left + S(18), iw = card.right - S(18);
        text(dc, g_fSmall, C_DIM, RECT{ix, card.top + S(10), iw, card.top + S(28)}, busy ? L"INSTALLING" : g_installed ? L"GAME INSTALLED" : L"GAME IMAGE (ISO)");
        if (busy) {
            text(dc, g_fSmall, C_DIM, RECT{ix, card.top + S(32), iw, card.top + S(52)}, msg.c_str(), DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_PATH_ELLIPSIS);
            draw_progress(dc, RECT{ix, card.top + S(58), iw, card.top + S(70)});
            wchar_t pc[64]; swprintf(pc, 64, L"%d%%  \x2014  this only happens once", g_prog / 10); text(dc, g_fSmall, C_DIM, RECT{ix, card.top + S(72), iw, card.bottom - S(4)}, pc);
        } else {
            std::wstring t = g_installed ? g_data : g_path.empty() ? L"No ISO selected" : g_path;
            text(dc, g_fText, (g_installed || iso_ok()) ? C_TXT : C_WARN, RECT{ix, card.top + S(32), iw, card.top + S(58)}, t.c_str(), DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_PATH_ELLIPSIS);
            RECT w = {ix, card.top + S(58), iw, card.bottom - S(6)};
            if (!err.empty()) text(dc, g_fSmall, C_ERR, w, err.c_str(), DT_LEFT | DT_WORDBREAK);
            else if (g_installed) text(dc, g_fSmall, C_OK, w, L"\x2713 Ready to play \x2014 the ISO is no longer needed");
            else if (!g_path.empty() && !iso_ok()) text(dc, g_fSmall, C_WARN, w, L"File not found \x2014 choose the ISO again.");
            else if (iso_ok()) text(dc, g_fSmall, C_DIM, w, L"First run: copies the game files (about 760 MB) and prepares the game. A few minutes, only once.");
        }
    } else if (g_page == PG_MODS) {
        draw_mods_page(dc, cx, cw);
    } else if (g_page == PG_CHEATS) {
        text(dc, g_fHead, C_TXT, RECT{cx, S(22), cx + cw, S(58)}, L"Cheats");
        text(dc, g_fSub, C_DIM, RECT{cx, S(58), cx + cw, S(80)}, L"For fun and testing. Part of the recomp (not a mod). Saved in cheats.ini; changes apply on the next start.");
        for (int i = 0; i < N_CHEAT; i++) {
            int y = S(100) + i * S(56);
            text(dc, g_fText, C_TXT, RECT{cx, y, cx + cw - S(300), y + S(24)}, kCheat[i].label);
            text(dc, g_fSmall, C_DIM, RECT{cx, y + S(24), cx + cw - S(290), y + S(44)}, kCheat[i].hint, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            RECT l = {cx, y + S(50), cx + cw, y + S(51)}; HBRUSH b = CreateSolidBrush(C_LINE); FillRect(dc, &l, b); DeleteObject(b);
        }
} else {
        text(dc, g_fHead, C_TXT, RECT{cx, S(22), cx + cw, S(58)}, L"About");
        text(dc, g_fSub, C_DIM, RECT{cx, S(58), cx + cw, S(80)}, L"Yu-Gi-Oh! GX Tag Force \x2014 PC recompilation");
        RECT p = {cx, S(104), cx + cw, S(280)};
        text(dc, g_fText, C_TXT, p, L"The original PSP game is statically recompiled to native Windows code. You need your own copy of the game: nothing from it is distributed with this program.\n\n"
                                    L"Graphics options can also be changed inside the game (Options > Graphics). Texture packages and mods live in the mods folder (the Mods page here turns them on or off and sets their priority) next to the program.", DT_LEFT | DT_WORDBREAK);
        section_label(dc, cx, S(300), cw, L"FOLDERS");
    }
    BitBlt(wdc, 0, 0, cr.right, cr.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, obm); DeleteObject(bm); DeleteDC(dc); EndPaint(h, &ps);
}
static void open_folder(const std::wstring &p) { CreateDirectoryW(p.c_str(), nullptr); ShellExecuteW(nullptr, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_COMMAND:
        if (g_installing && LOWORD(w) != ID_QUIT && LOWORD(w) != ID_LOGS && LOWORD(w) != ID_SAVES) return 0;
        switch (LOWORD(w)) {
        case ID_PICK: if (pick(h)) { { std::lock_guard<std::mutex> lk(g_mu); g_err.clear(); } if (g_installed && iso_ok()) { if (tf_msgbox(h, L"Rebuild the jogo/ folder from this ISO? The files currently in the folder will be replaced.", L"Reinstall", MB_YESNO | MB_ICONQUESTION) == IDYES) start_install(); } refresh(); } return 0;
        case ID_PLAY:
            if (game_running()) return 0;
            if (g_installed) { if (!code_ready(game_id_dir(g_data))) start_install(false); else { launch_game(); refresh(); } }
            else if (iso_ok()) { show_page(PG_GAME); start_install(); }
            return 0;
        case ID_STOP:
            if (game_running() && tf_msgbox(h, L"Close the game? Anything not saved inside the game will be lost.", L"Tag Force Recomp", MB_YESNO | MB_ICONQUESTION) == IDYES) { stop_game(); refresh(); }
            return 0;
        case ID_LOGS: open_folder(exe_dir() + L"logs"); return 0;
        case ID_SAVES: open_folder(exe_dir() + L"ms0"); return 0;
        case ID_MODDIR: mods_open_folder(); return 0;
        default:
            if (LOWORD(w) >= ID_COPT0 && LOWORD(w) < ID_COPT0 + N_CHEAT) {
                if (!game_running()) { HWND b = (HWND)l; POINT pt; GetCursorPos(&pt); RECT wr; GetWindowRect(b, &wr); cheat_step(LOWORD(w) - ID_COPT0, PtInRect(&wr, pt) && pt.x < wr.left + (wr.right - wr.left) / 3 ? -1 : 1); }
                return 0;
            }
            if (is_sel(LOWORD(w)) && !game_running()) {
                HWND b = (HWND)l; POINT pt; GetCursorPos(&pt); RECT wr; GetWindowRect(b, &wr);
                int back = PtInRect(&wr, pt) && pt.x < wr.left + (wr.right - wr.left) / 3;
                opt_step(sel_option(LOWORD(w)), back ? -1 : 1);
            }
            return 0;
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
    case WM_MOUSEMOVE: {
        int n = nav_at((short)LOWORD(l), (short)HIWORD(l));
        if (n != g_navhot) { g_navhot = n; RECT sb = {0, 0, S(SIDE96), 0}; GetClientRect(h, &sb); sb.right = S(SIDE96); InvalidateRect(h, &sb, FALSE); }
        TRACKMOUSEEVENT te = {sizeof te, TME_LEAVE, h, 0}; TrackMouseEvent(&te);
        return 0; }
    case WM_MOUSELEAVE: if (g_navhot != -1) { g_navhot = -1; RECT sb; GetClientRect(h, &sb); sb.right = S(SIDE96); InvalidateRect(h, &sb, FALSE); } return 0;
    case WM_LBUTTONDOWN: {
        int n = nav_at((short)LOWORD(l), (short)HIWORD(l));
        if (n == NAV_QUIT) SendMessageW(h, WM_COMMAND, ID_QUIT, 0); else if (n >= 0) show_page(n);
        else if (g_page == PG_MODS) mod_click((short)LOWORD(l), (short)HIWORD(l));
        return 0; }
    case WM_MOUSEWHEEL:
        if (g_page == PG_MODS) { g_modscroll -= GET_WHEEL_DELTA_WPARAM(w) / WHEEL_DELTA; mod_clamp_scroll(); InvalidateRect(h, nullptr, FALSE); }
        return 0;
    case WM_TIMER:
        if (w == TIMER_UI) {
            { static bool was[24 + N_OPT]; static POINT lastp; int i = 0; POINT cp; GetCursorPos(&cp); bool moved = cp.x != lastp.x || cp.y != lastp.y; lastp = cp;   /* hover dos botoes: so redesenha quando muda */
              std::vector<HWND> bs = {g_bpick, g_play, g_bstop, g_blogs, g_bsaves, g_bmoddir}; bs.insert(bs.end(), g_qsel, g_qsel + NQ); bs.insert(bs.end(), g_sel, g_sel + N_OPT); bs.insert(bs.end(), g_csel, g_csel + N_CHEAT);
              for (HWND b : bs) { bool hot = is_hot(b); if (hot != was[i] || (hot && moved && i >= 7)) { was[i] = hot; InvalidateRect(b, nullptr, FALSE); } i++; } }
            { static bool wasrun; bool run = game_running(); if (run != wasrun) { wasrun = run; refresh(); } }
            if (g_installing) { RECT bar; GetClientRect(h, &bar); InvalidateRect(h, &bar, FALSE); }
            if (g_installing && g_inst_done) {   /* terminou */
                g_installing = false; if (g_thr.joinable()) g_thr.join();
                if (g_inst_ok) { check_installed(); launch_game(); refresh(); }   /* instalou: ja joga */
                else { if (g_err.empty()) g_err = L"Installation failed."; refresh(); }
            }
        }
        return 0;
    case WM_CLOSE: SendMessageW(h, WM_COMMAND, ID_QUIT, 0); return 0;
    case WM_DESTROY: KillTimer(h, TIMER_UI); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

/* Mostra a janela. Devolve sempre "" (o jogo roda em processo filho); *data nao e preenchido: ao fechar o lancador, o programa sai. */
std::string launcher_pick_iso(std::wstring *data) {
    (void)data;
    load_thumbnail();
    wchar_t last[1024] = L"";
    GetPrivateProfileStringW(L"geral", L"iso", L"", last, 1024, ini_path().c_str());
    g_path = last; update_data_dir(); check_installed();

    /* DPI proprio so desta janela (o processo nao e DPI-aware; o jogo cuida da propria janela) */
    typedef HANDLE (WINAPI *SetCtx)(HANDLE);
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    SetCtx setctx = (SetCtx)(void *)GetProcAddress(u32, "SetThreadDpiAwarenessContext");
    HANDLE oldctx = setctx ? setctx((HANDLE)-4) : nullptr;   /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */
    { HDC sdc = GetDC(nullptr); g_dpi = GetDeviceCaps(sdc, LOGPIXELSX); ReleaseDC(nullptr, sdc); if (g_dpi < 96) g_dpi = 96; }
    { RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);   /* telas pequenas / escala alta: encolhe tudo para a janela caber */
      int needw = MulDiv(W96 + 16, g_dpi, 96), needh = MulDiv(H96 + 48, g_dpi, 96), aw = wa.right - wa.left - 20, ah = wa.bottom - wa.top - 20;
      int d = g_dpi; if (needw > aw) d = std::min(d, MulDiv(g_dpi, aw, needw)); if (needh > ah) d = std::min(d, MulDiv(g_dpi, ah, needh));
      g_dpi = std::max(d, 60); }

    menu_dark_init();   /* tema escuro do Windows (barra de titulo, caixas de dialogo) */
    HINSTANCE hi = GetModuleHandleW(nullptr);
    WNDCLASSW wc = {}; wc.lpfnWndProc = proc; wc.hInstance = hi; wc.lpszClassName = L"TagForceLauncher";
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW); wc.hbrBackground = CreateSolidBrush(C_BG);
    wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(1)); if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, (LPCWSTR)IDI_APPLICATION);
    RegisterClassW(&wc);
    DWORD st = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;   /* o fundo nao repinta por cima dos botoes */
    RECT r = {0, 0, S(W96), S(H96)};
    { typedef BOOL (WINAPI *AdjDpi)(LPRECT, DWORD, BOOL, DWORD, UINT); AdjDpi adj = (AdjDpi)(void *)GetProcAddress(u32, "AdjustWindowRectExForDpi"); if (adj) adj(&r, st, FALSE, 0, g_dpi); else AdjustWindowRect(&r, st, FALSE); }
    int ww = r.right - r.left, wh = r.bottom - r.top;
    g_wnd = CreateWindowW(L"TagForceLauncher", L"Tag Force Recomp", st, (GetSystemMetrics(SM_CXSCREEN) - ww) / 2, (GetSystemMetrics(SM_CYSCREEN) - wh) / 2, ww, wh, nullptr, nullptr, hi, nullptr);
    menu_dark_window(g_wnd);
    set_app_icon(g_wnd);
    g_fHero = make_font(30, FW_SEMIBOLD); g_fHead = make_font(22, FW_SEMIBOLD); g_fTitle = make_font(22, FW_SEMIBOLD); g_fSub = make_font(12, FW_NORMAL); g_fText = make_font(14, FW_NORMAL);
    g_fSmall = make_font(11, FW_SEMIBOLD); g_fBtn = make_font(14, FW_SEMIBOLD); g_fPlay = make_font(17, FW_BOLD);
    RECT cr; GetClientRect(g_wnd, &cr); int cx = S(SIDE96) + S(28), cw = cr.right - S(28) - cx;
    auto btn = [&](const wchar_t *t, int id, int x, int y, int w, int h) { return CreateWindowW(L"BUTTON", t, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, x, y, w, h, g_wnd, (HMENU)(INT_PTR)id, hi, nullptr); };
    opt_load();
    { int selw = S(270), selx = cx + cw - selw, h = S(34);   /* pagina Graphics: uma linha por opcao, seletor a direita */
      for (int i = 0; i < N_OPT; i++) g_sel[i] = btn(kOpts[i].names[g_cur[i]], ID_OPT0 + i, selx, S(100) + i * S(56) + S(4), selw, h); }
    { int colw = (cw - S(40)) * 11 / 20, rx = cx + colw + S(40), w = cx + cw - rx;   /* pagina Play: seletores rapidos na coluna direita */
      for (int k = 0; k < NQ; k++) g_qsel[k] = btn(kOpts[kQuick[k]].names[g_cur[kQuick[k]]], ID_QOPT0 + k, rx, S(220) + S(26) + S(30) + S(24) + k * S(68), w, S(36)); }
    cheat_load();
    for (int i = 0; i < N_CHEAT; i++) g_csel[i] = btn(kCheat[i].names[g_cc[i]], ID_COPT0 + i, cx + cw - S(270), S(100) + i * S(56) + S(4), S(270), S(34));
    g_bpick = btn(L"Select ISO...", ID_PICK, cx, S(220), S(180), S(36));
    g_blogs = btn(L"Open logs folder", ID_LOGS, cx, S(326), S(180), S(36));
    g_bsaves = btn(L"Open saves folder", ID_SAVES, cx + S(192), S(326), S(180), S(36));
    g_bmoddir = btn(L"Open mods folder", ID_MODDIR, cx + cw - S(170), S(22), S(170), S(34));
    { int by = cr.bottom - S(BAR96) + S(14), bh = S(42);
      g_play = btn(L"PLAY", ID_PLAY, cr.right - S(28) - S(190), by, S(190), bh);
      g_bstop = btn(L"Stop", ID_STOP, cr.right - S(28) - S(190) - S(10) - S(80), by, S(80), bh); }
    SendMessageW(g_wnd, DM_SETDEFID, ID_PLAY, 0);
    SetTimer(g_wnd, TIMER_UI, 60, nullptr);
    mods_reload();
    if (!g_installed) g_page = PG_GAME;   /* primeira vez: comeca pela instalacao */
    refresh();
    ShowWindow(g_wnd, SW_SHOW); UpdateWindow(g_wnd);
    if (!g_installed && !iso_ok()) PostMessageW(g_wnd, WM_COMMAND, ID_PICK, 0);   /* primeira vez: ja abre o seletor */

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(g_wnd, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    if (g_thr.joinable()) g_thr.join();
    for (HFONT f : {g_fHero, g_fHead, g_fTitle, g_fSub, g_fText, g_fSmall, g_fBtn, g_fPlay}) DeleteObject(f);
    if (setctx) setctx(oldctx);
    return "";
}
