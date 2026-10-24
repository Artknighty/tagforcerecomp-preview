/* Mods de comportamento (plugins): carrega mods/<pasta>/plugin.dll dos mods ligados e entrega a API de plugins/sdk/tfmod.h.
 * Os ganchos dos plugins entram na cadeia de ganchos do host (hooks.h), na ordem de carga dos mods, com o plugin como dono. */
#include "hle.h"
#include "hooks.h"
#include "mods.h"
#include "modbox.h"
#include "dark_ui.h"
#include "hotkeys.h"
#include "util.h"
#include "gameid.h"
#include "../../plugins/sdk/tfmod.h"
#include <windows.h>
#include <algorithm>
#include <stdarg.h>
#include <stdio.h>
#include <map>
#include <string>
#include <vector>

uint32_t display_buttons(void);
uint64_t display_frames(void);
void display_set_speed(int n);
void hle_set_bgm(const wchar_t *path);
bool host_module_loaded(const char *name);
int cheats_init(const TfModApi *api);   /* cheats.cpp */

namespace {
struct Plugin { std::string name; std::wstring dir; HMODULE dll; TfModApi api; int menu = 0; };   /* menu: onde entram os proximos ajustes (use_menu) */
std::vector<Plugin *> g_plugins;   /* ponteiros estaveis: a API de cada um fica viva */
Plugin *g_init;                    /* plugin dentro do tfmod_init (os ganchos ficam em nome dele) */
Plugin *current(void) { return g_init ? g_init : (Plugin *)g_hook_owner; }   /* para o prefixo do log */
void api_log(const char *fmt, ...);
/* API v3 add_slider: kept so older plugins still load, but the game window has no menu any more (settings live in the launcher and config.ini) */
void api_add_slider(const wchar_t *, const wchar_t *, int, int, int, int (*)(void), void (*)(int)) {}
struct Toggle { Plugin *p; std::wstring title; int vk; int (*get)(void); void (*set)(int); int menu; };
std::vector<Toggle> g_toggles;     /* liga/desliga registrados pelos plugins (menu Mods com marca; tecla opcional no jogo) */
void api_add_toggle(const wchar_t *title, int vk, int (*get)(void), void (*set)(int)) {
    if (!title || !get || !set || g_toggles.size() >= 20) return;
    /* teclas da janela (tabela kHotkeys, hotkeys.h; mais F10 = menu do Windows e Esc) e os botoes do PSP no teclado (display.cpp, read_keys):
     * uma delas trocaria o liga/desliga E apertaria o botao no jogo */
    static const int reservadas[] = {VK_F10, VK_ESCAPE, VK_RETURN, VK_SHIFT, VK_CONTROL, VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, 'Z', 'X', 'A', 'S', 'Q', 'W', 'D', 'E', 'F', 'R', 'C', VK_SPACE, VK_BACK, VK_TAB};
    for (int r : reservadas) if (vk == r || hotkey_reserved(vk)) { api_log("tecla %d reservada pelo jogo: liga/desliga so pelo menu", vk); vk = 0; }
    Plugin *p = current(); const int menu = p ? p->menu : 0;
    g_toggles.push_back({p, title, vk, get, set, menu});
    api_log("liga/desliga no menu %s%s (atual: %s)", menu ? "Cheats" : "Mods", vk ? " e por tecla" : "", get() ? "ligado" : "desligado");
}
void api_panel(int corner, const wchar_t *title, const wchar_t *const *lines, int n) {
    std::vector<std::wstring> l; for (int i = 0; i < n; i++) l.push_back(lines && lines[i] ? lines[i] : L"");
    modbox_panel(display_frames(), corner, title ? title : L"", l);
}
void api_sprite(int id, const uint32_t *rgba, int w, int h, float x, float y, float dw, float dh, int alpha) { modbox_sprite(display_frames(), id, rgba, w, h, x, y, dw, dh, alpha); }
int api_quad_near(int x, int y, int tol, int wmin, int wmax) { return modbox_quad_near(x, y, tol, wmin, wmax); }
void api_use_menu(int m) { if (Plugin *p = current()) p->menu = m == TFMOD_MENU_CHEATS ? 1 : 0; }
uint8_t *api_mem(uint32_t a) { return mem_ptr(a); }
uint32_t api_reg(TfCpu *c, int i) { return i > 0 && i < 32 ? ((CPU *)c)->r[i] : 0; }
void api_set_reg(TfCpu *c, int i, uint32_t v) { if (i > 0 && i < 32) ((CPU *)c)->r[i] = v; }
void api_hook(uint32_t addr, TfHook fn) { Plugin *p = current(); if (fn && p) hook_install_c(addr, (int (*)(CPU *))fn, p); }
int api_call_original(TfCpu *c, uint32_t addr) { return hook_call_original((CPU *)c, addr) ? 1 : 0; }
void api_call(TfCpu *c, uint32_t addr) { psp_call((CPU *)c, addr); }
uint32_t api_buttons(void) { return display_buttons(); }
uint64_t api_frame(void) { return display_frames(); }
int api_module_loaded(const char *n) { return n && host_module_loaded(n) ? 1 : 0; }
void api_message_box(const wchar_t *title, const wchar_t *const *lines, int n, const wchar_t *foot) {
    std::vector<std::wstring> l; for (int i = 0; i < n; i++) l.push_back(lines && lines[i] ? lines[i] : L"");
    modbox_show(display_frames(), title ? title : L"", l, foot ? foot : L"");
}
/* memoria do PSP para o plugin (textos, tabelas que o jogo vai ler): area do kernel 0x08400000..0x08800000, que o jogo (modo
 * usuario, a partir de 0x08800000) nunca usa nem libera. Blocos zerados, alinhados em 16 bytes; nao ha liberacao. */
uint32_t g_galloc = 0x08400000u;
uint32_t api_guest_alloc(uint32_t n) {
    const uint32_t a = (g_galloc + 15) & ~15u;
    if (!n || n > 0x08800000u - a) { api_log("guest_alloc: sem espaco para %u bytes", n); return 0; }
    g_galloc = a + n; memset(mem_ptr(a), 0, n); return a;
}
void api_log(const char *fmt, ...) {
    char b[1024]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    size_t n = strlen(b); while (n && (b[n - 1] == '\n' || b[n - 1] == '\r')) b[--n] = 0;
    Plugin *p = current(); fprintf(stderr, "[mod %s] %s\n", p ? p->name.c_str() : "?", b);
}
}

void plugins_marca(int n) {   /* tecla F7 / L3+R3: chama tfmod_marca(n) dos plugins que a exportam (na thread do jogo) */
    for (Plugin *p : g_plugins) {
        auto f = p->dll ? (void (*)(int))(void *)GetProcAddress(p->dll, "tfmod_marca") : nullptr; if (!f) continue;
        void *prev = g_hook_owner; g_hook_owner = p; f(n); g_hook_owner = prev;
    }
}

/* fills the API for p and runs its init; on failure removes what it installed and returns false (p is then the caller's to free) */
static bool plugin_start(Plugin *p, int (*init)(const TfModApi *)) {
    TfModApi &a = p->api;
    a.version = TFMOD_API_VERSION; a.mod_dir = p->dir.c_str();
    a.mem = api_mem; a.reg = api_reg; a.set_reg = api_set_reg;
    a.hook = api_hook; a.call_original = api_call_original; a.call = api_call;
    a.buttons = api_buttons; a.frame = api_frame; a.module_loaded = api_module_loaded;
    a.message_box = api_message_box; a.log = api_log;
    a.guest_alloc = api_guest_alloc;   /* versao 2 */
    a.add_slider = api_add_slider;     /* versao 3 */
    a.add_toggle = api_add_toggle; a.panel = api_panel; a.use_menu = api_use_menu;   /* versao 4 */
    a.sprite = api_sprite; a.quad_near = api_quad_near;   /* versao 5 */
    a.set_speed = display_set_speed;   /* versao 6 */
    a.set_bgm = hle_set_bgm;           /* versao 7 */
    g_init = p; const int r = init(&a); g_init = nullptr;
    if (r != 0) {   /* falhou: tira os ganchos e os ajustes que ele tenha posto (as funcoes deles somem com a DLL: o menu chamaria get() de codigo descarregado) */
        hook_remove_owner(p);
        g_toggles.erase(std::remove_if(g_toggles.begin(), g_toggles.end(), [p](const Toggle &t) { return t.p == p; }), g_toggles.end());
        fprintf(stderr, "[mods] %s: init devolveu %d (ignorado)\n", p->name.c_str(), r); return false;
    }
    g_plugins.push_back(p);
    fprintf(stderr, "[mods] %s: plugin carregado\n", p->name.c_str());
    return true;
}

void plugins_load(void) {
    std::vector<std::pair<std::string, std::wstring>> mods; mods_plugin_dirs(mods);
    for (auto &m : mods) {
        wchar_t full[MAX_PATH * 2]; const std::wstring rel = m.second + L"\\plugin.dll";
        if (!GetFullPathNameW(rel.c_str(), MAX_PATH * 2, full, nullptr)) continue;   /* caminho absoluto: um relativo seria procurado a partir da pasta do .exe */
        const std::wstring dll = full;
        if (GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
        HMODULE h = LoadLibraryExW(dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);   /* dependencias do plugin: procuradas na pasta dele */
        if (!h) { fprintf(stderr, "[mods] %s: nao consegui carregar plugin.dll (erro %lu)\n", m.first.c_str(), GetLastError()); continue; }
        {   /* os ganchos usam enderecos de UMA versao do jogo: sem tfmod_game_ids o plugin vale so para a europeia (ULES-00600) */
            const char *const *ids = (const char *const *)(void *)GetProcAddress(h, "tfmod_game_ids");
            const std::string gid = game_id_built();
            const bool ok = ids && *ids ? ("," + std::string(*ids) + ",").find("," + gid + ",") != std::string::npos : gid == "ULES-00600";
            if (!ok) { fprintf(stderr, "[mods] %s: plugin nao e para o jogo %s (ignorado)\n", m.first.c_str(), gid.c_str()); FreeLibrary(h); continue; }
        }
        auto init = (int (*)(const TfModApi *))(void *)GetProcAddress(h, "tfmod_init");
        if (!init) { fprintf(stderr, "[mods] %s: plugin.dll sem tfmod_init (ignorado)\n", m.first.c_str()); FreeLibrary(h); continue; }
        Plugin *p = new Plugin{m.first, m.second, h, {}};
        if (!plugin_start(p, init)) { FreeLibrary(h); delete p; continue; }
    }
    if (game_id_built() == "ULES-00600") {   /* Cheats is a feature of the recomp (cheats.cpp, compiled into the exe), started through the same API; its hooks are for the European game */
        Plugin *p = new Plugin{"Cheats", exe_dir(), nullptr, {}};
        if (!plugin_start(p, cheats_init)) delete p;
    }
}

/* ---- liga/desliga dos plugins (menu Mods com marca; tecla no jogo) ---- */
int plugin_toggle_count(void) { return (int)g_toggles.size(); }
int plugin_toggle_menu(int i) { return i >= 0 && i < (int)g_toggles.size() ? g_toggles[i].menu : 0; }
bool plugin_toggle_on(int i) { return i >= 0 && i < (int)g_toggles.size() && g_toggles[i].get() != 0; }
std::wstring plugin_toggle_label(int i) {   /* "<titulo>\t<tecla>" */
    if (i < 0 || i >= (int)g_toggles.size()) return L"";
    const Toggle &t = g_toggles[i]; std::wstring s = t.title;
    if (t.vk >= VK_F1 && t.vk <= VK_F24) { wchar_t k[8]; swprintf(k, 8, L"\tF%d", t.vk - VK_F1 + 1); s += k; }
    else if (t.vk) { wchar_t k[32] = L""; const UINT sc = MapVirtualKeyW((UINT)t.vk, MAPVK_VK_TO_VSC); if (sc && GetKeyNameTextW((LONG)(sc << 16), k, 32)) { s += L"\t"; s += k; } }
    return s;
}
void plugin_toggle_flip(int i) {
    if (i < 0 || i >= (int)g_toggles.size()) return;
    const Toggle &t = g_toggles[i];
    void *prev = g_hook_owner; g_hook_owner = t.p; t.set(t.get() ? 0 : 1); g_hook_owner = prev;
}
int plugin_toggle_by_key(int vk) {   /* tecla apertada no jogo: troca o liga/desliga dela; devolve o indice ou -1 */
    for (int i = 0; i < (int)g_toggles.size(); i++) if (vk && g_toggles[i].vk == vk) { plugin_toggle_flip(i); return i; }
    return -1;
}
