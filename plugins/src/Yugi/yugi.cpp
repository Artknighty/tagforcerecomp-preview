/* Mod "Yugi" (plugin, API 2; the versus portrait uses API 5): adds Yugi Muto as a Free Duel opponent, without touching the game files.
 *
 * The game has room for 150 characters (ids 2..151) but ids 47, 48 and 49 are empty slots: no name, class 0 (hidden from the duelist lists) and the
 * placeholder deck. This mod gives one of them (default 47) a name, a class and a deck, all in memory:
 *   - names: BOOT keeps the names of the characters in one string bank per language; each bank has a table of 301 u32 offsets (from the end of the table)
 *     that starts with 0x30, 0x46, 0x52, ...; character `id` has its full name at entry 2*(id-2) and its short name at the next one. We point those
 *     two entries at strings of our own (guest_alloc) in every language bank.
 *   - class: the table at 0x088ecc48 (6 bytes per character from id 41: u8 id, u8 low nibble = class 1..6 (0 = hidden), u16 picture id, u16 pad) is
 *     patched so the slot gets a class and shows up in the lists. The picture id stays the slot's own.
 *   - progress record: the Free Duel opponent list shows characters whose record (save + 0x68 + id * 0x12) has a non-zero deck count; we keep
 *     count = 1 and deck 0 = the slot's deck (id + 199) while a save is loaded (written from the checksum hook, see hk_checksum). Nothing else in the save is touched, and without this mod the slot is
 *     still hidden (class 0), so a save that went through the mod plays like an untouched one.
 *   - deck: the Free Duel asks libtitle FUN_08f1e28c(character) which deck the opponent plays (answered by hk_deck_of_character); libehsys_85A292D7 (0884a694) reads deck RCPnnn into 0x108 bytes (see BetterDecks); for the slot's deck we return deck.txt.
 *   - versus portrait: on the Free Duel versus screen the game draws a class-coloured jacket strip over the bottom of every head portrait, which spoils a
 *     full-size picture. When the selected opponent is the slot (hk_deck_of_character sees it) and the versus screen is up (versus_screen_up), the mod draws
 *     portrait.rgba (a 256x256 picture with its own background: 8 bytes w,h as u32, then RGBA; made by tools/mods/mod_yugi_portrait.py) over the frame.
 *   - theme music (API 7): config.ini `bgm=theme.wav` (PCM 16-bit stereo 44100 Hz, in the mod folder). While the versus screen shows the slot as the opponent
 *     the mod arms set_bgm, so the music the game loads for that duel is the WAV; it disarms when the opponent list comes back or another opponent is picked.
 * Everything happens from the end-of-frame hook (08833fc4, called every logical frame); turning the mod off restores the game as it was. */
#include "tfmod.h"
#include <windows.h>
#include <string>
#include <vector>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const TfModApi *A;
static const uint32_t F_FRAME = 0x08833fc4, F_LER_DECK = 0x0884a694, F_CHECKSUM = 0x0883d6a4, F_DECK_OF_CHARACTER = 0x08f1e28c;
static const uint32_t SAVE = 0x08a65418, SAVE_VERSION = 0x10002, RECORDS = 0x68, RECORD_SIZE = 0x12;
static const uint32_t CLASS_TABLE = 0x088ecc48, CLASS_FIRST_ID = 41;
static const uint32_t BANK_SCAN_FROM = 0x08860000, BANK_SCAN_TO = 0x08890000, BANK_ENTRIES = 301;

static int g_slot = 47, g_class = 4, g_picture = 0;   /* picture 0 = keep the slot's own */
static std::wstring g_name = L"Yugi Muto", g_short = L"Yugi";
struct Deck { std::vector<uint16_t> principal, extra, side; };
static Deck g_deck;
static bool g_deck_ok;
static uint32_t g_name_addr, g_short_addr;
static int g_names_done, g_class_done;
static std::vector<uint32_t> g_portrait; static int g_portrait_w, g_portrait_h;
static std::wstring g_bgm; static bool g_bgm_armed;   /* g_bgm: full path of the theme WAV ('' = none) */
static bool g_versus;   /* the Free Duel's selected opponent is the slot */
static const float PORTRAIT_X = 377.0f, PORTRAIT_Y = 64.0f, PORTRAIT_W = 63.0f, PORTRAIT_H = 64.0f;   /* inner part of the opponent's portrait frame (PSP pixels) */

static std::wstring cfg_path(void) { return std::wstring(A->mod_dir) + L"\\config.ini"; }
static void cfg_load(void) {
    FILE *f = _wfopen(cfg_path().c_str(), L"r"); if (!f) return;
    char l[256];
    while (fgets(l, sizeof l, f)) {
        char *nl = strpbrk(l, "\r\n"); if (nl) *nl = 0;
        int v;
        if (sscanf(l, "slot=%d", &v) == 1 && v >= 47 && v <= 49) g_slot = v;
        else if (sscanf(l, "class=%d", &v) == 1 && v >= 1 && v <= 6) g_class = v;
        else if (sscanf(l, "picture=%d", &v) == 1 && v >= 0 && v < 2000) g_picture = v;
        else if (!strncmp(l, "name=", 5)) { wchar_t w[48]; MultiByteToWideChar(CP_UTF8, 0, l + 5, -1, w, 48); g_name = w; }
        else if (!strncmp(l, "bgm=", 4)) { wchar_t w[128]; MultiByteToWideChar(CP_UTF8, 0, l + 4, -1, w, 128); if (w[0]) g_bgm = std::wstring(A->mod_dir) + L"\\" + w; }
        else if (!strncmp(l, "short_name=", 11)) { wchar_t w[48]; MultiByteToWideChar(CP_UTF8, 0, l + 11, -1, w, 48); g_short = w; }
    }
    fclose(f);
}
static void deck_load(void) {
    const std::wstring path = std::wstring(A->mod_dir) + L"\\deck.txt";
    FILE *f = _wfopen(path.c_str(), L"r"); if (!f) { A->log("deck.txt not found: the slot keeps the placeholder deck"); return; }
    char l[4096];
    while (fgets(l, sizeof l, f)) {
        char *h = strchr(l, '#'); if (h) *h = 0;
        std::vector<uint16_t> *dst = nullptr; char *p = l;
        if (!strncmp(p, "principal:", 10)) { dst = &g_deck.principal; p += 10; }
        else if (!strncmp(p, "extra:", 6)) { dst = &g_deck.extra; p += 6; }
        else if (!strncmp(p, "side:", 5)) { dst = &g_deck.side; p += 5; }
        if (!dst) continue;
        for (char *q = p; *q;) { while (*q == ' ' || *q == '\t' || *q == ',') q++; if (*q < '0' || *q > '9') break; dst->push_back((uint16_t)strtoul(q, &q, 10)); }
    }
    fclose(f);
    g_deck_ok = !g_deck.principal.empty() && g_deck.principal.size() <= 80 && g_deck.extra.size() <= 30 && g_deck.side.size() <= 15;
    A->log("deck.txt: %zu main / %zu extra / %zu side%s", g_deck.principal.size(), g_deck.extra.size(), g_deck.side.size(), g_deck_ok ? "" : " (invalid, ignored)");
}
static void portrait_load(void) {
    const std::wstring path = std::wstring(A->mod_dir) + L"\\portrait.rgba";
    FILE *f = _wfopen(path.c_str(), L"rb"); if (!f) return;
    uint32_t hdr[2] = {0, 0};
    if (fread(hdr, 4, 2, f) == 2 && hdr[0] > 0 && hdr[0] <= 1024 && hdr[1] > 0 && hdr[1] <= 1024) {
        g_portrait.resize((size_t)hdr[0] * hdr[1]);
        if (fread(g_portrait.data(), 4, g_portrait.size(), f) == g_portrait.size()) { g_portrait_w = (int)hdr[0]; g_portrait_h = (int)hdr[1]; }
        else g_portrait.clear();
    }
    fclose(f);
    A->log("portrait.rgba: %s", g_portrait.empty() ? "not found or invalid (the sheet head is used)" : "loaded");
}
static uint32_t put_string(const std::wstring &s) {
    const uint32_t bytes = (uint32_t)(s.size() + 1) * 2, addr = A->guest_alloc((bytes + 15) & ~15u);
    if (addr) memcpy(A->mem(addr), s.c_str(), bytes);
    return addr;
}
static uint32_t &u32(uint32_t a) { return *(uint32_t *)A->mem(a); }

/* every language bank: table of offsets starting 0x30, 0x46, 0x52 (the first name sits 0x30 bytes after the end of the table) */
static void patch_names(void) {
    g_name_addr = put_string(g_name); g_short_addr = put_string(g_short);
    if (!g_name_addr || !g_short_addr) { A->log("out of guest memory: names not set"); g_names_done = -1; return; }
    for (uint32_t t = BANK_SCAN_FROM; t < BANK_SCAN_TO; t += 4) {
        if (u32(t) != 0x30 || u32(t + 4) != 0x46 || u32(t + 8) != 0x52) continue;
        const uint32_t base = t + BANK_ENTRIES * 4;
        if (wcsncmp((const wchar_t *)A->mem(base + 0x30), L"Jaden", 5) != 0) continue;   /* the first character of every language bank */
        const uint32_t e = t + 4 * 2 * (g_slot - 2);
        u32(e) = g_name_addr - base; u32(e + 4) = g_short_addr - base;
        g_names_done++;
        A->log("name bank at %08x: entry %d -> \"%ls\"", t, 2 * (g_slot - 2), g_name.c_str());
    }
    if (!g_names_done) { A->log("no name bank found (a different game version?): the slot stays nameless"); g_names_done = -1; }
}
static void patch_class(void) {
    const uint32_t row = CLASS_TABLE + (g_slot - CLASS_FIRST_ID) * 6;
    if (*A->mem(row) != (uint8_t)g_slot) { A->log("class table row of slot %d not where expected: not patched", g_slot); g_class_done = -1; return; }
    uint8_t &b = *A->mem(row + 1); b = (uint8_t)((b & 0xf0) | g_class);
    if (g_picture) *(uint16_t *)A->mem(row + 2) = (uint16_t)g_picture;
    g_class_done = 1;
}
/* libtitle's flow state (0x09b29730: +8 screen, +0xc arg0): screen 12 with arg0 3 is the Free Duel versus screen (arg0 6 is the opponent list) */
static bool versus_screen_up(void) { return *(int16_t *)A->mem(0x09b29738) == 12 && *(int16_t *)A->mem(0x09b2973c) == 3; }
static bool list_screen_up(void) { return *(int16_t *)A->mem(0x09b29738) == 12 && *(int16_t *)A->mem(0x09b2973c) == 6; }
/* The duel's music is the first looping track the game opens after the duel modules load (libtitle is unloaded by then, so its state is only read while it is loaded):
 * latch = "the versus screen showed the slot"; armed = latch while the duel is loaded. */
static bool g_bgm_latch;
static void bgm_update(void) {
    static const bool always = getenv("TF_YUGI_BGM_ALWAYS") != nullptr;   /* test aid: as if the versus screen had shown Yugi */
    const bool title = A->module_loaded("libtitle") != 0;
    if (title) { if (!g_versus || list_screen_up()) g_bgm_latch = false; else if (versus_screen_up()) g_bgm_latch = true; }
    if (always) g_bgm_latch = true;
    const bool want = (g_bgm_latch && A->module_loaded("libduel_eng"));
    if (want == g_bgm_armed) return;
    g_bgm_armed = want; A->set_bgm(want ? g_bgm.c_str() : nullptr);
    A->log("theme music %s", want ? "armed" : "disarmed");
}
static int hk_frame(TfCpu *) {
    if (!g_bgm.empty() && A->version >= 7) bgm_update();
    if (!g_names_done) patch_names();
    if (!g_class_done) patch_class();
    if (g_versus && !g_portrait.empty() && A->version >= 5 && versus_screen_up())
        A->sprite(0x7967, g_portrait.data(), g_portrait_w, g_portrait_h, PORTRAIT_X, PORTRAIT_Y, PORTRAIT_W, PORTRAIT_H, 255);
    return 0;
}
/* save_checksum_atualizar (0883d6a4): the game recomputes the checksum of the save block and returns 1 when it changed, which makes the title screen
 * ask "Save all contents acquired so far?". We write the slot's record just before it runs and report "unchanged" for that one call (the stored
 * checksum is updated by the original, so later calls agree). */
static int hk_checksum(TfCpu *c) {
    bool wrote = false;
    if (u32(SAVE) == SAVE_VERSION) {
        uint16_t *r = (uint16_t *)A->mem(SAVE + RECORDS + g_slot * RECORD_SIZE);
        if (r[1] != 1 || r[2] != g_slot + 199) { r[1] = 1; r[2] = (uint16_t)(g_slot + 199); wrote = true; }
    }
    A->call_original(c, F_CHECKSUM);
    if (wrote) A->set_reg(c, 2, 0);
    return 1;
}
/* libtitle FUN_08f1e28c(character) (loaded at 08efca00): the deck the Free Duel gives the opponent. The game builds it from recipe tables that have nothing for
 * our slot, so we answer the slot's own deck number. */
static int hk_deck_of_character(TfCpu *c) {
    g_versus = (int)A->reg(c, 4) == g_slot;
    if (!g_versus) return 0;
    A->set_reg(c, 2, (uint32_t)(g_slot + 199));
    return 1;
}
static int hk_ler_deck(TfCpu *c) {   /* libehsys_85A292D7(index, out) */
    if (!g_deck_ok || (int)A->reg(c, 4) != g_slot + 199) return 0;
    const uint32_t out = A->reg(c, 5);
    if (out < 0x08000000u || out > 0x0a000000u) return 0;
    A->log("deck RCP%03d requested: serving deck.txt (%zu cards)", g_slot + 199, g_deck.principal.size());
    uint8_t *o = A->mem(out); memset(o, 0, 0x108);
    uint32_t *w = (uint32_t *)o; w[0] = (uint32_t)g_deck.principal.size(); w[1] = (uint32_t)g_deck.side.size(); w[2] = (uint32_t)g_deck.extra.size();
    for (size_t i = 0; i < g_deck.principal.size(); i++) ((uint16_t *)(o + 0x0c))[i] = g_deck.principal[i];
    for (size_t i = 0; i < g_deck.side.size(); i++) ((uint16_t *)(o + 0xac))[i] = g_deck.side[i];
    for (size_t i = 0; i < g_deck.extra.size(); i++) ((uint16_t *)(o + 0xca))[i] = g_deck.extra[i];
    return 1;
}

extern "C" __declspec(dllexport) int tfmod_init(const TfModApi *api) {
    A = api; TFMOD_REQUIRE(api, 2);   /* the versus portrait needs API 5 (checked where it is used) */
    cfg_load(); deck_load(); portrait_load();
    A->hook(F_FRAME, hk_frame); A->hook(F_LER_DECK, hk_ler_deck); A->hook(F_CHECKSUM, hk_checksum); A->hook(F_DECK_OF_CHARACTER, hk_deck_of_character);
    A->log("Yugi: slot %d as \"%ls\" (class %d)", g_slot, g_name.c_str(), g_class);
    return 0;
}
