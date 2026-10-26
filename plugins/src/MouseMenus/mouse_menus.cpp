/* Mouse Menus: hover/click on the game's MenuList widgets (see README.md). Layout of the list block: decomp/libtitle/title_state.h (MenuList). */
#include "tfmod.h"
#include <windows.h>
#include <atomic>
#include <thread>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

extern "C" const char *tfmod_game_ids = "ULES-00600";

static const TfModApi *A;
static const uint32_t F_LIST_UPDATE = 0x08f019ac, F_SOUND = 0x08863294, F_DRAW_TITLE_ITEM = 0x08f040e0;
static const int SND_MOVE = 25106;
static const int TITLE_ROW_TRIM = 2;   /* the PC-style title list is drawn with a 22 px pitch (24 - 2) */
enum { LIST_IDLE = 3, ITEM_DISABLED = 1 };

static std::atomic<unsigned> g_clicks{0};
static std::atomic<bool> g_run{true};
static std::thread g_thread;

/* Short clicks last less than a game frame: a thread watches the button and counts presses over the game window. */
static HWND game_window() {
    static HWND w;
    if (w && IsWindow(w)) return w;
    w = nullptr; const DWORD pid = GetCurrentProcessId();
    EnumWindows([](HWND h, LPARAM) -> BOOL {
        DWORD p; GetWindowThreadProcessId(h, &p); RECT r;
        if (p == GetCurrentProcessId() && IsWindowVisible(h) && !GetWindow(h, GW_OWNER) && GetClientRect(h, &r) && r.right > 200 && r.bottom > 100) { w = h; return FALSE; }
        return TRUE;
    }, 0);
    (void)pid; return w;
}
static bool pointer_in_game(int *gx, int *gy) {
    /* tests: MOUSEMENUS_FAKE="x,y" (game space) replaces the pointer; with a file MOUSEMENUS_FAKE_FILE the position is re-read each call */
    if (const char *f = getenv("MOUSEMENUS_FAKE_FILE")) { FILE *fp = fopen(f, "r"); if (!fp) return false; int x = -1, y = -1; const int n = fscanf(fp, "%d %d", &x, &y); fclose(fp); if (n != 2 || x < 0) return false; *gx = x; *gy = y; return true; }
    HWND w = game_window(); if (!w || GetForegroundWindow() != w) return false;
    POINT p; RECT cr; if (!GetCursorPos(&p) || !ScreenToClient(w, &p) || !GetClientRect(w, &cr) || !PtInRect(&cr, p)) return false;
    static int aspect = -1;   /* 0 original 16:9, 1 stretch, 2 4:3 (Graphics > Aspect ratio) */
    if (aspect < 0) { wchar_t path[MAX_PATH]; GetModuleFileNameW(nullptr, path, MAX_PATH); wchar_t *s = wcsrchr(path, L'\\'); if (s) wcscpy(s + 1, L"tagforce_recomp.ini");
                      aspect = (int)GetPrivateProfileIntW(L"graficos", L"proporcao", 0, path); }
    const int cw = cr.right, ch = cr.bottom; int W = cw, H = ch;
    if (aspect != 1) { const int an = aspect == 2 ? 4 : 480, ad = aspect == 2 ? 3 : 272; H = (int)((long long)cw * ad / an); if (H > ch) { H = ch; W = (int)((long long)ch * an / ad); } }
    const int ox = (cw - W) / 2, oy = (ch - H) / 2;
    if (p.x < ox || p.x >= ox + W || p.y < oy || p.y >= oy + H) return false;
    *gx = (p.x - ox) * 480 / W; *gy = (p.y - oy) * 272 / H; return true;
}
static void watch_clicks() {
    const int vk = GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON;
    bool prev = false;
    while (g_run) {
        const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0; int x, y;
        if (down && !prev && pointer_in_game(&x, &y)) g_clicks++;
        prev = down; Sleep(2);
    }
}

/* plays a game sound keeping every register of the hooked function's caller */
static void sound(TfCpu *c, int id) {
    uint32_t s[32]; for (int i = 1; i < 32; i++) s[i] = A->reg(c, i);
    A->set_reg(c, 4, (uint32_t)id); A->set_reg(c, 5, 0); A->call(c, F_SOUND);
    for (int i = 1; i < 32; i++) A->set_reg(c, i, s[i]);
}
static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static int on_list_update(TfCpu *c) {
    uint8_t *m = A->mem(A->reg(c, 4)); if (!m || m[0] != LIST_IDLE) return 0;
    static int lx = -1, ly = -1; static unsigned seen;
    int px, py; const bool in = pointer_in_game(&px, &py);
    const bool moved = in && (px != lx || py != ly); if (in) { lx = px; ly = py; } else lx = ly = -1;
    const unsigned clicks = g_clicks.load(); const bool click = clicks != seen; seen = clicks;
    if (!in) return 0;
    const int count = m[8], left = (int16_t)u16(m + 70), top = (int16_t)u16(m + 72), width = (int16_t)u16(m + 74), rowH = (int16_t)u16(m + 76);
    uint32_t cb; memcpy(&cb, m + 120, 4);
    const int pitch = rowH - (cb == F_DRAW_TITLE_ITEM ? TITLE_ROW_TRIM : 0);
    if (pitch <= 0 || px < left || px >= left + width || py < top) return 0;
    const int row = (py - top) / pitch; if (row >= count) return 0;
    if (moved && row != m[80] && m[60 + row] != ITEM_DISABLED) { m[80] = (uint8_t)row; sound(c, SND_MOVE); }
    if (click) m[80] = (uint8_t)row;   /* the click is also X (PC scheme): it confirms the row the cursor is on */
    return 0;
}

extern "C" __declspec(dllexport) int tfmod_init(const TfModApi *api) {
    A = api; TFMOD_REQUIRE(api, 7);
    api->hook(F_LIST_UPDATE, on_list_update);
    g_thread = std::thread(watch_clicks); g_thread.detach();
    api->log("Mouse Menus loaded");
    return 0;
}
