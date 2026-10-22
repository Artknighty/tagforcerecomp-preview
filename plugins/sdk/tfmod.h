/* Tag Force Recomp - behavior mod (plugin) API.
 *
 * A behavior mod is a folder in mods/ (like texture and file mods) containing:
 *     mods/MyMod/mod.txt       name=, author=, version=, description=
 *     mods/MyMod/plugin.dll    uses this API; exports tfmod_init
 * At startup the game loads plugin.dll from the enabled mods (Mods menu) and calls tfmod_init(api). The plugin installs hooks on game
 * functions (by their PSP address) and, inside them, reads/writes PSP memory and registers, calls game functions and shows message
 * boxes. Example source: plugins/src/BoosterBox. Build with: sh tools/build/build_plugins.sh (MSYS2 UCRT64, g++).
 *
 * Game version: hook addresses are valid for ONE version of the game. By default a plugin is valid only for the European one
 * (ULES-00600); for another version, export `const char *tfmod_game_ids = "ULES-00600,ULUS-10136";` (the list of versions the
 * addresses are correct for).
 * Rules: C only (stable ABI); the API only grows (new fields at the end, `version` goes up); a plugin built for version N runs on any
 * runtime with version >= N. Everything runs on the game thread, inside the frame: never block or wait. */
#pragma once
#include <stdint.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TFMOD_API_VERSION 6   /* 2: guest_alloc; 3: add_slider; 4: add_toggle, panel, use_menu; 5: sprite, quad_near; 6: set_speed */

typedef struct CPU TfCpu;                     /* PSP registers (opaque: use reg/set_reg) */
/* hook: called on ENTRY of the game function. Return 0 to let the original body run next (the hook only observed/prepared), or
 * 1 = handled (the original does NOT run; put the result in r2 with set_reg). To "wrap" the function: call call_original and
 * return 1. Arguments in r4..r7, return value in r2, return address in r31. */
typedef int (*TfHook)(TfCpu *c);

typedef struct TfModApi {
    uint32_t version;                                         /* TFMOD_API_VERSION of the runtime */
    const wchar_t *mod_dir;                                   /* the mod's folder (e.g. mods\BoosterBox) */

    /* PSP memory: pointer to the address (0x08000000.. user RAM, 0x04000000.. VRAM). Valid for the whole run. */
    uint8_t *(*mem)(uint32_t addr);
    /* registers: 0..31 = r0..r31 (MIPS) */
    uint32_t (*reg)(TfCpu *c, int i);
    void (*set_reg)(TfCpu *c, int i, uint32_t v);

    /* hooks: several plugins can hook the same address (called in the order the mods load; the first one to return 1 ends the chain).
     * Install them in tfmod_init: addresses apply to whichever game module is loaded at that address when the function runs
     * (check with module_loaded). */
    void (*hook)(uint32_t addr, TfHook fn);
    int (*call_original)(TfCpu *c, uint32_t addr);           /* runs the original body of the hooked function (without firing the hook) */
    void (*call)(TfCpu *c, uint32_t addr);                    /* calls a game function (arguments/return value in the registers) */

    /* state */
    uint32_t (*buttons)(void);                                /* PSP buttons this frame (0x200 = R, 0x100 = L, 0x4000 = X ...) */
    uint64_t (*frame)(void);                                  /* vblank number (60 per second) */
    int (*module_loaded)(const char *name);                   /* 1 if the game module (e.g. "libshop") is loaded */

    /* message box over the game (title, lines, footer); call it every frame in which it should be visible.
     * A line "=N/M" becomes a bar of M segments with N lit (e.g. L"=3/10"). */
    void (*message_box)(const wchar_t *title, const wchar_t *const *lines, int nlines, const wchar_t *foot);
    /* session log (gen/logs), prefixed with the mod name; printf format */
    void (*log)(const char *fmt, ...);

    /* ---- version 2 ---- */
    /* reserves n bytes of PSP memory for the plugin (e.g. a UTF-16 text that a game function will read) and returns the PSP
     * address (use mem() to write), or 0 if exhausted. Zeroed, 16-byte aligned, never freed: reserve once in tfmod_init. */
    uint32_t (*guest_alloc)(uint32_t n);

    /* ---- version 3 ---- */
    /* (Since the window menu bar was removed this is accepted but not shown; keep settings in config.ini and the launcher.)
     * A setting the player changes in the window's Mods menu (before or during play): an item "<title>..." that opens a dark window with an
     * integer slider from min to max (suffix `unit`, e.g. L"x"). Register in tfmod_init. The runtime reads the current value with get()
     * when the window opens (the plugin may change it by other means) and calls set(new) when the player confirms (OK), on the game
     * thread, with the game paused. `def` is the value of the "Default" button. Storing the value in a file is up to the plugin
     * (mod_dir). The menu shows the title up to the first " (" and the current value beside it; a caption in parentheses (e.g.
     * L"Position (0 = left, 1 = right)") only appears in the setting's window. */
    void (*add_slider)(const wchar_t *title, const wchar_t *unit, int min, int max, int def, int (*get)(void), void (*set)(int));

    /* ---- version 4 ---- */
    /* On/off switch: a checkmark item in the Mods menu and, optionally, a key during play (Windows virtual-key code, e.g. 0x75 = F6;
     * 0 = no key; F3/F5/F7/F8/F9/F10/F11, Esc, Enter and the keys of the PSP buttons in both keyboard layouts - arrows, W A S D,
     * Z X C E F R Q, Space, Backspace, Tab, Shift, Ctrl - belong to the game and exist only in the menu). get() reads the state; set(new) is called on the game thread when the player toggles it (menu or
     * key). Register in tfmod_init; storing the state in a file is up to the plugin (mod_dir). */
    void (*add_toggle)(const wchar_t *title, int vk, int (*get)(void), void (*set)(int));
    /* Information panel over the game (translucent; it neither pauses nor blocks anything). Call it every frame in which it should be
     * visible. corner: 0 = top left, 1 = top right, 2 = bottom left, 3 = bottom right (the top ones sit below the 20 px strip at the top,
     * where the game usually has bars). One panel per corner; short lines (~40 characters). Plain text works; the markup below
     * (optional) makes the panel look like a table of cards (full example: plugins/src/Cheats):
     *   title     L"@eText\tRight"        @e = eye icon; whatever follows the \t goes on the right, in gold (e.g. L"LP 8000")
     *   section   L"#Hand\t5"             uppercase header with a counter on the right (optional)
     *   card      L"@MA{seal}Name\tc1\tc2..."  colored row: M = frame (n normal, e effect, f fusion, r ritual, s spell, t trap,
     *                                    k token; any other letter = neutral), A = attribute (l light, d dark, w water, f fire, e earth,
     *                                    v wind, g divine, s/t = spell/trap diamond, - = none); optional {seal} (e.g. {M3})
     *   plain     L"text\tc1..."          row without a card (italic text)
     *   columns   after each \t, right-aligned as in a table (the rows with the most columns set the widths):
     *             L"*4" = star + 4 (level), L"ATK|1600" = small label + value, any other text = as is.
     * Whatever does not fit in the height becomes a "+N" line at the end. */
    void (*panel)(int corner, const wchar_t *title, const wchar_t *const *lines, int nlines);
    /* Which window menu receives this plugin's next add_slider/add_toggle: TFMOD_MENU_MODS (default) or TFMOD_MENU_CHEATS (the
     * menu-bar Cheats menu, separate from the mods: cheats and testing aids). */
    void (*use_menu)(int menu);

    /* ---- version 5 ---- */
    /* Image over the game (GPU renderer only): `rgba` = w x h pixels, 4 bytes R,G,B,A per pixel (straight alpha; the plugin keeps the
     * array while the game runs), drawn in the rectangle (x, y, dw, dh) in PSP pixels (480x272 screen; may be fractional) with opacity
     * `alpha` (0..255). `id` identifies the sprite: call it every frame in which it should be visible (like panel); stop calling and it
     * disappears. They sit below the panels and the message box. The image is rebuilt on a thread (it shows up 1-3 frames after it
     * changes); it is not meant for per-frame animation. */
    void (*sprite)(int id, const uint32_t *rgba, int w, int h, float x, float y, float dw, float dh, int alpha);
    /* 1 if, in the frame on screen, the game drew a textured 3D quad (a card on the field etc.) whose center is within `tol` pixels of
     * (x, y) and whose width is wmin..wmax pixels. Used to tell whether a field slot is really visible (and not covered by a cut-in). */
    int (*quad_near)(int x, int y, int tol, int wmin, int wmax);

    /* ---- version 6 ---- */
    /* Game speed (fast-forward): 1 = normal, 2..8 = that many times faster. The frame limiter is replaced by one at n times the game's own rate and
     * the music is time-stretched (same pitch). The plugin owns the setting (it is not saved by the runtime): call it again on load. */
    void (*set_speed)(int n);
} TfModApi;
#define TFMOD_MENU_MODS 0
#define TFMOD_MENU_CHEATS 1

/* The plugin exports this function. Return 0 if it loaded; any other value = error (the mod is ignored). */
#ifdef _WIN32
__declspec(dllexport)
#endif
int tfmod_init(const TfModApi *api);
/* Optional: void tfmod_marca(int n), exported the same way, called (on the game thread, with the game paused) when the player records the
 * "the CPU made a mistake" mark number n (F7 or L3 + R3, after typing the note): a good place to dump into the log the state that
 * explains the move (e.g. plugins/src/CPUvsCPU dumps the board). tools/analysis/marcas_cpu.py gathers everything into a report. */

#ifdef __cplusplus
}
#endif
