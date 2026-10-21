/* Dark Windows theme for the game window and the launcher (title bar, dialog boxes). The window has no menu bar: everything that was in it
 * (graphics, mods, cheats, controls...) lives in the launcher, and the F-keys / in-game combos stay (hotkeys.h). */
#include <windows.h>
#include "dark_ui.h"
#include "menu.h"

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
