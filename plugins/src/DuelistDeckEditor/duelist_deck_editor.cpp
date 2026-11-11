/* Duelist Deck Editor: view and edit the decks of any duelist directly from Database > List of Duelists
 * using an integrated native Tag Force menu with icons and the game's native Deck Editor. */
#include "tfmod.h"
#include "../../decomp/common/duelists.h"
#include <windows.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static const TfModApi *A;

/* Game addresses (ULES-00600) */
static const uint32_t F_FRAME          = 0x08833fc4;
static const uint32_t F_SOUND          = 0x08863294;
static const uint32_t F_CHAR_NAME      = 0x0883f9d0;
static const uint32_t F_RECIPE_NAME    = 0x0884a5cc;
static const uint32_t F_READ_DECK      = 0x0884a694;
static const uint32_t F_CARD_LIMIT_A   = 0x0883e43c;
static const uint32_t F_CARD_LIMIT_B   = 0x0884b8fc;
static const uint32_t PAD_STATE        = 0x08903db4;

/* libcharalist (loaded at 0x08eeb400 inside libtitle) */
static const uint32_t LIST_STATE       = 0x08ef62a0;
static const uint32_t F_CHARALIST_LIST = 0x08eef664; /* FUN_00004264: state 2 (list navigation) */
static const uint32_t F_CHARALIST_DET  = 0x08eefc44; /* FUN_00004844: state 3 (character detail view) */

/* libcharalist native drawing & ehsys stubs (base 0x08eeb400) */
static const uint32_t F_CL_GET_RES     = 0x08eeb6f4; /* FUN_000002f4(key) -> handle */
static const uint32_t F_CL_DRAW_CURSOR = 0x08eed764; /* FUN_00002364(list, x, y, w, h) -> list */
static const uint32_t F_CL_DRAW_NAME   = 0x08eed9f0; /* FUN_000025f0(x, y, maxW, charId) */
static const uint32_t F_CL_DRAW_CLASS  = 0x08eeda98; /* FUN_00002698(list, x, y, offX, cat) -> list */
static const uint32_t F_CL_DRAW_DIM    = 0x08eee36c; /* FUN_00002f6c(list, alpha) -> list */
static const uint32_t F_CL_DRAW_DETAIL = 0x08eee430; /* FUN_00003030(list) -> list */
static const uint32_t F_CL_DRAW_FRAME  = 0x08eee7d8; /* FUN_000033d8(list, x, y) -> list */
static const uint32_t F_CL_GET_STR     = 0x08ef2374; /* libehsys_E58C0FDC(table, idx) -> utf16_addr */
static const uint32_t F_CL_BIND_TEX    = 0x08ef29ec; /* libehsys_9EA6989A(list, tex) -> list */
static const uint32_t F_CL_BEGIN_LIST  = 0x08ef2c04; /* libehsys_DFCA450B(id, flags) -> list */
static const uint32_t F_CL_END_LIST    = 0x08ef2c0c; /* libehsys_1EC5342B(list) */
static const uint32_t F_CL_DRAW_SPR    = 0x08ef2c84; /* libehsys_FDD168CA(list, x, y, z, u, v, rot, flags) -> list */
static const uint32_t F_CL_SPR_SIZE    = 0x08ef2c8c; /* libehsys_8EA6B1E7(list, uw, vh, w, h) -> list */
static const uint32_t F_CL_SET_COLOR   = 0x08ef2d14; /* libehsys_CABEA834(list, abgr) -> list */
static const uint32_t F_CL_SET_FILTER  = 0x08ef2d5c; /* libehsys_E6E4401F(list, minF, magF) -> list */
static const uint32_t F_CL_FONT_SIZE   = 0x08ef340c; /* libehsys_942B03D0(w, h) */
static const uint32_t F_CL_TEXT_COLOR  = 0x08ef341c; /* libehsys_60B55A50(abgr) */
static const uint32_t F_CL_DRAW_TEXT   = 0x08ef343c; /* libehsys_B89D38DC(x64, y64, w64, str) */
static const uint32_t F_CL_BEGIN_TEXT  = 0x08ef34ec; /* libehsys_B8AD96EA(list) */
static const uint32_t F_CL_END_TEXT    = 0x08ef34f4; /* libehsys_A4AFF8E6() -> list */

/* libtitle (base 0x08efca00) */
static const uint32_t TITLE_STATE      = 0x09b29730; /* libtitle::kStateAddr (+0x08 = State::screen, +0x10 = State::arg1) */
static const uint32_t F_SUBLIST6_INPUT = 0x08efddec; /* SubList6Input() */
static const uint32_t F_UPDATE_FLOW    = 0x08efefa8; /* UpdateFlow() */
static const uint32_t F_START_LOGO     = 0x08eff0b8; /* StartLogoWith(screen, arg) */
static const uint32_t F_SWITCH_SCREEN  = 0x08eff100; /* SwitchScreen(code, arg) */
static const uint32_t F_REQ_SCREEN16   = 0x08f01550; /* RequestScreen16(fade, next, pending) */

/* libdeck (base 0x08efca00) */
static const uint32_t LIBDECK          = 0x08efca00;
static const uint32_t F_DECK_INIT      = 0x08f04638; /* FUN_00007c38: finishes setting up trunk & working deck */
static const uint32_t F_DECK_STEP      = 0x08f05514; /* FUN_00008b14: per-frame update of Deck Editor */
static const uint32_t F_DECK_LOAD_RCP  = 0x08efe300; /* FUN_00001900(deck_block, name_ptr): loads recipe into working deck */
static const uint32_t F_DECK_SET_PANE  = 0x08f049b8; /* FUN_00007fb8(slot, pane_idx, mode): switches active pane & updates tab bar */
static const uint32_t F_DECK_PREVIEW   = 0x08f048a0; /* FUN_00007ea0: updates card preview for active pane */
static const uint32_t F_VALIDATE       = 0x08f03094; /* FUN_00006694: exit validator (0 = ok) */
static const uint32_t F_COMMIT         = 0x08efe6f0; /* FUN_00001cf0: exit commit to save */

/* PSP button masks */
static const uint32_t BTN_UP       = 0x0010;
static const uint32_t BTN_RIGHT    = 0x0020;
static const uint32_t BTN_DOWN     = 0x0040;
static const uint32_t BTN_LEFT     = 0x0080;
static const uint32_t BTN_TRIANGLE = 0x1000;
static const uint32_t BTN_CIRCLE   = 0x2000;
static const uint32_t BTN_CROSS    = 0x4000;
static const uint32_t BTN_SQUARE   = 0x8000;

struct DeckData {
    std::vector<uint16_t> main_deck, extra_deck, side_deck;
};

struct DeckEntry {
    uint16_t rcp;
    uint8_t level;
    std::wstring name;
};

static std::map<int, DeckData> g_overrides;
static bool g_overrides_loaded = false;
static bool g_bypass_env = false;

static uint32_t g_guest_deck = 0;
static uint32_t g_guest_str  = 0;
static int g_last_char_id = -1;
static int g_cached_char_id = -1;
static std::vector<DeckEntry> g_cached_decks;
static int g_cursor = 0;
static uint32_t g_prev_pad = 0;
static bool g_simple_detail_open = false;
static bool g_bio_mode = false;

static int g_cur_dl_id = -1;
static uint32_t g_saved_bio_addr = 0;
static uint32_t g_saved_bio_val  = 0;

static int g_editing_rcp = -1;
static int g_editing_char = -1;
static std::wstring g_editing_char_name;
static std::wstring g_editing_deck_name;
static bool g_launching_editor = false;
static bool g_deck_load_pending = false;
static bool g_return_to_charalist = false;
static uint64_t g_status_until = 0;
static std::wstring g_status_msg;

#include "hd_icons.h"

/* High-definition rendered icon buffers (persistent RGBA buffers for A->sprite) */
struct IconBuf {
    const uint32_t *px;
    int w, h;
};
static bool g_icons_ready = false;
static IconBuf g_ico_deck;
static IconBuf g_ico_deck_edit;
static IconBuf g_ico_star;
static IconBuf g_ico_btn_x;
static IconBuf g_ico_btn_sq;
static IconBuf g_ico_btn_tri;
static IconBuf g_ico_btn_cir;

static uint8_t &u8(uint32_t a) { return *A->mem(a); }
static uint16_t &u16(uint32_t a) { return *(uint16_t *)A->mem(a); }
static uint32_t &u32(uint32_t a) { return *(uint32_t *)A->mem(a); }

static uint32_t call_game(TfCpu *c, uint32_t fn, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0) {
    uint32_t saved[32];
    for (int i = 1; i < 32; i++) saved[i] = A->reg(c, i);
    A->set_reg(c, 4, a0);
    A->set_reg(c, 5, a1);
    A->set_reg(c, 6, a2);
    A->call(c, fn);
    const uint32_t ret = A->reg(c, 2);
    for (int i = 1; i < 32; i++) if (i != 26 && i != 27) A->set_reg(c, i, saved[i]);
    return ret;
}

static uint32_t call_game8(TfCpu *c, uint32_t fn, uint32_t r4 = 0, uint32_t r5 = 0, uint32_t r6 = 0, uint32_t r7 = 0,
                           uint32_t r8 = 0, uint32_t r9 = 0, uint32_t r10 = 0, uint32_t r11 = 0) {
    uint32_t saved[32];
    for (int i = 1; i < 32; i++) saved[i] = A->reg(c, i);
    A->set_reg(c, 4, r4);
    A->set_reg(c, 5, r5);
    A->set_reg(c, 6, r6);
    A->set_reg(c, 7, r7);
    A->set_reg(c, 8, r8);
    A->set_reg(c, 9, r9);
    A->set_reg(c, 10, r10);
    A->set_reg(c, 11, r11);
    A->call(c, fn);
    const uint32_t ret = A->reg(c, 2);
    for (int i = 1; i < 32; i++) if (i != 26 && i != 27) A->set_reg(c, i, saved[i]);
    return ret;
}

static std::wstring read_utf16(uint32_t addr, int max_len = 48) {
    std::wstring s;
    if (addr < 0x08000000u || addr >= 0x0a000000u) return s;
    for (int i = 0; i < max_len; i++) {
        uint16_t ch = u16(addr + i * 2);
        if (!ch) break;
        s += (ch == '\n' || ch == '\r') ? L' ' : (wchar_t)ch;
    }
    while (!s.empty() && s.front() == L' ') s.erase(s.begin());
    return s;
}

static uint32_t write_guest_str(const wchar_t *s) {
    if (!g_guest_str) return 0;
    int i = 0;
    for (; s && s[i] && i < 120; i++)
        u16(g_guest_str + i * 2) = (uint16_t)s[i];
    u16(g_guest_str + i * 2) = 0;
    return g_guest_str;
}

static void draw_icon(int id, const IconBuf &ico, float x, float y, float dw, float dh, int alpha = 255) {
    A->sprite(id, ico.px, ico.w, ico.h, x, y, dw, dh, alpha);
}

static void init_icons(void) {
    if (g_icons_ready) return;
    g_icons_ready = true;
    g_ico_deck.px = g_hd_deck_box; g_ico_deck.w = HD_ICON_SIZE; g_ico_deck.h = HD_ICON_SIZE;
    g_ico_deck_edit.px = g_hd_deck_box; g_ico_deck_edit.w = HD_ICON_SIZE; g_ico_deck_edit.h = HD_ICON_SIZE;
    g_ico_star.px = g_hd_star; g_ico_star.w = HD_ICON_SIZE; g_ico_star.h = HD_ICON_SIZE;
    g_ico_btn_x.px = g_hd_btn_x; g_ico_btn_x.w = HD_ICON_SIZE; g_ico_btn_x.h = HD_ICON_SIZE;
    g_ico_btn_sq.px = g_hd_btn_sq; g_ico_btn_sq.w = HD_ICON_SIZE; g_ico_btn_sq.h = HD_ICON_SIZE;
    g_ico_btn_tri.px = g_hd_btn_tri; g_ico_btn_tri.w = HD_ICON_SIZE; g_ico_btn_tri.h = HD_ICON_SIZE;
    g_ico_btn_cir.px = g_hd_btn_cir; g_ico_btn_cir.w = HD_ICON_SIZE; g_ico_btn_cir.h = HD_ICON_SIZE;
}

/* Save-tied override folder: ms0/PSP/SAVEDATA/ULES00600/duelist_decks */
static std::wstring overrides_dir(void) {
    return L"ms0\\PSP\\SAVEDATA\\ULES00600\\duelist_decks";
}

static void ensure_overrides_dir(void) {
    CreateDirectoryW(L"ms0", nullptr);
    CreateDirectoryW(L"ms0\\PSP", nullptr);
    CreateDirectoryW(L"ms0\\PSP\\SAVEDATA", nullptr);
    CreateDirectoryW(L"ms0\\PSP\\SAVEDATA\\ULES00600", nullptr);
    CreateDirectoryW(overrides_dir().c_str(), nullptr);
}

static std::wstring override_path(int rcp) {
    wchar_t buf[128];
    swprintf(buf, 128, L"%ls\\RCP%03d.txt", overrides_dir().c_str(), rcp);
    return buf;
}

static void load_override_file(int idx, const std::wstring &path) {
    FILE *f = _wfopen(path.c_str(), L"r");
    if (!f) return;
    DeckData d;
    char l[4096];
    while (fgets(l, sizeof l, f)) {
        char *h = strchr(l, '#');
        if (h) *h = 0;
        std::vector<uint16_t> *dst = nullptr;
        char *p = l;
        if (!strncmp(p, "principal:", 10)) { dst = &d.main_deck; p += 10; }
        else if (!strncmp(p, "extra:", 6)) { dst = &d.extra_deck; p += 6; }
        else if (!strncmp(p, "side:", 5)) { dst = &d.side_deck; p += 5; }
        if (!dst) continue;
        for (char *q = p; *q;) {
            while (*q == ' ' || *q == '\t' || *q == ',') q++;
            if (*q < '0' || *q > '9') break;
            dst->push_back((uint16_t)strtoul(q, &q, 10));
        }
    }
    fclose(f);
    if (!d.main_deck.empty() && d.main_deck.size() <= 80 && d.extra_deck.size() <= 30 && d.side_deck.size() <= 15)
        g_overrides[idx] = d;
}

static void load_all_overrides(void) {
    if (g_overrides_loaded) return;
    g_overrides_loaded = true;
    g_overrides.clear();
    const std::wstring dir = overrides_dir();
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\RCP*.txt").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        int idx = -1;
        if (swscanf(fd.cFileName, L"RCP%d", &idx) == 1 && idx >= 0 && idx <= 500)
            load_override_file(idx, dir + L"\\" + fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (!g_overrides.empty())
        A->log("Loaded %zu edited duelist deck(s) from save folder", g_overrides.size());
}

static bool save_override_file(int rcp, const DeckData &d) {
    ensure_overrides_dir();
    const std::wstring path = override_path(rcp);
    FILE *f = _wfopen(path.c_str(), L"w");
    if (!f) {
        A->log("Failed to write override file for RCP%03d", rcp);
        return false;
    }
    fprintf(f, "# Duelist Deck Editor override for RCP%03d\nprincipal:", rcp);
    for (uint16_t id : d.main_deck) fprintf(f, " %u", id);
    fprintf(f, "\nextra:");
    for (uint16_t id : d.extra_deck) fprintf(f, " %u", id);
    fprintf(f, "\nside:");
    for (uint16_t id : d.side_deck) fprintf(f, " %u", id);
    fprintf(f, "\n");
    fclose(f);
    g_overrides[rcp] = d;
    return true;
}

static bool delete_override_file(int rcp) {
    g_overrides.erase(rcp);
    return DeleteFileW(override_path(rcp).c_str()) != 0;
}

/* Pure C++ fast lookup of a character's recipe IDs (no guest calls) */
static int get_character_rcp_ids(uint32_t char_id, uint16_t out_rcps[8], uint8_t out_levels[8]) {
    int n = 0;
    auto push = [&](uint16_t rcp, uint8_t lv) {
        if (n < 8) { out_rcps[n] = rcp; out_levels[n] = lv; n++; }
    };
    if (duelist::HasLadder(char_id)) {
        uint8_t mask = duelist::LadderUsedMask(char_id);
        if (char_id == duelist::kAtticus || char_id == duelist::kBelowski) mask |= 1;
        if (mask == 0) mask = 1;
        for (uint32_t lv = 1; lv <= duelist::kDecksPerLadder; lv++) {
            if (mask & (1u << (lv - 1)))
                push((uint16_t)duelist::LadderDeck(char_id, lv), (uint8_t)lv);
        }
        if (char_id == duelist::kBlair)
            push(duelist::kBlairExtraDeck, (uint8_t)(n + 1));
    } else if (char_id == duelist::kNightshroud) {
        push(duelist::kNightshroudDeck, 1);
    } else if (char_id == duelist::kColin) {
        push(duelist::kColinDeckA, 1);
        push(duelist::kColinDeckB, 2);
    } else if (duelist::HasSingleDeck(char_id)) {
        push((uint16_t)duelist::SingleDeck(char_id), 1);
    } else if (char_id == 152) {
        push(400, 1);
    }
    return n;
}

/* Collect all real decks belonging to a character (cached per character) */
static void get_character_decks(TfCpu *c, uint32_t char_id, std::vector<DeckEntry> &out) {
    if ((int)char_id == g_cached_char_id && !g_cached_decks.empty()) {
        out = g_cached_decks;
        return;
    }
    out.clear();
    uint16_t rcps[8];
    uint8_t levels[8];
    const int n = get_character_rcp_ids(char_id, rcps, levels);
    for (int i = 0; i < n; i++) {
        std::wstring rname = read_utf16(call_game(c, F_RECIPE_NAME, rcps[i]));
        if (rname.empty()) {
            wchar_t b[32];
            swprintf(b, 32, L"Deck RCP%03u", rcps[i]);
            rname = b;
        }
        out.push_back({rcps[i], levels[i], rname});
    }
    g_cached_char_id = (int)char_id;
    g_cached_decks = out;
}

/* Reads the currently highlighted ListEntry in libcharalist (Database > List of Duelists) */
static uint32_t current_list_entry(uint32_t *out_char_id, bool *out_no_detail) {
    const uint8_t tab = u8(LIST_STATE + 0x06) & 3;
    const uint32_t row = (uint32_t)u16(LIST_STATE + 0x08 + tab * 2) + (uint32_t)u16(LIST_STATE + 0x0e + tab * 2);
    const uint32_t count = u16(LIST_STATE + 0x16 + tab * 2);
    if (row >= count) return 0;
    const uint32_t list_ptr = u32(LIST_STATE + 0x1c + tab * 4);
    if (list_ptr < 0x08000000u || list_ptr >= 0x0a000000u) return 0;
    const uint32_t entry = list_ptr + row * 0x10;
    if (out_char_id) *out_char_id = u16(entry) & 0x3ff;
    if (out_no_detail) *out_no_detail = (u16(entry + 2) & 0x0400) != 0;
    return entry;
}

static bool in_database_duelist_list(void) {
    if (!A->module_loaded("libcharalist_08eeb400") || !A->module_loaded("libtitle")) return false;
    const uint8_t mode = u8(LIST_STATE) & 0x0f;
    return mode == 0 || mode == 1;
}

/* ---- Native GE Drawing Helpers for libcharalist (0x1003 sprites + ehsys font) ---- */

/* Draw a dark-teal beveled bar from 0x1003 (u=0..13, v=20, vh=23) */
static uint32_t draw_cl_teal_bar(TfCpu *c, uint32_t list, int x, int y, int w, int h, uint32_t abgr = 0xffffffffu) {
    list = call_game8(c, F_CL_SET_COLOR, list, abgr);
    list = call_game8(c, F_CL_SPR_SIZE, list, 12, 23, 8, h);
    list = call_game8(c, F_CL_DRAW_SPR, list, x, y, 0, 0, 0x14, 0, 0);
    list = call_game8(c, F_CL_DRAW_SPR, list, x + w - 8, y, 0, 0x0d, 0x14, 0, 0);
    list = call_game8(c, F_CL_SPR_SIZE, list, 1, 23, w - 16, h);
    list = call_game8(c, F_CL_DRAW_SPR, list, x + 8, y, 0, 0x0c, 0x14, 0, 0);
    return list;
}

/* Draw a 3D sunken teal slot from 0x1003 (u=0xa4..0xa6, v=0x2e, vh=14) */
static uint32_t draw_cl_sunken_slot(TfCpu *c, uint32_t list, int x, int y, int w, int h, uint32_t abgr = 0xffffffffu) {
    list = call_game8(c, F_CL_SET_COLOR, list, abgr);
    list = call_game8(c, F_CL_SPR_SIZE, list, 1, 14, 1, h);
    list = call_game8(c, F_CL_DRAW_SPR, list, x, y, 0, 0xa4, 0x2e, 0, 0);
    list = call_game8(c, F_CL_DRAW_SPR, list, x + w - 1, y, 0, 0xa6, 0x2e, 0, 0);
    list = call_game8(c, F_CL_SPR_SIZE, list, 1, 14, w - 2, h);
    list = call_game8(c, F_CL_DRAW_SPR, list, x + 1, y, 0, 0xa5, 0x2e, 0, 0);
    return list;
}

/* Draw a colored capsule / name plate from 0x1003 (u=100..102, v=0x6f, vh=16) */
static uint32_t draw_cl_capsule(TfCpu *c, uint32_t list, int x, int y, int w, int h, uint32_t abgr) {
    list = call_game8(c, F_CL_SET_COLOR, list, abgr);
    list = call_game8(c, F_CL_SPR_SIZE, list, 1, 16, 1, h);
    list = call_game8(c, F_CL_DRAW_SPR, list, x, y, 0, 100, 0x6f, 0, 0);
    list = call_game8(c, F_CL_DRAW_SPR, list, x + w - 1, y, 0, 102, 0x6f, 0, 0);
    list = call_game8(c, F_CL_SPR_SIZE, list, 1, 16, w - 2, h);
    list = call_game8(c, F_CL_DRAW_SPR, list, x + 1, y, 0, 101, 0x6f, 0, 0);
    return list;
}

/* Draw a UTF-16 string in the currently active BeginText block */
static void draw_cl_text(TfCpu *c, int x, int y, int maxW, const wchar_t *str, int fw = 0x0d, int fh = 0x0d, uint32_t abgr = 0xffffffffu) {
    call_game8(c, F_CL_FONT_SIZE, fw, fh);
    call_game8(c, F_CL_TEXT_COLOR, abgr);
    const uint32_t addr = write_guest_str(str);
    if (addr) call_game8(c, F_CL_DRAW_TEXT, x * 64, y * 64, maxW * 64, addr);
}

/* Draw the Integrated Native Deck Menu inside the Detail Window (State 3 or Simple Detail Window) */
static bool g_in_cursor = false;

static uint32_t draw_charalist_detail_menu(TfCpu *c, uint32_t list, bool is_simple) {
    load_all_overrides();
    init_icons();

    const uint32_t tex1003 = call_game(c, F_CL_GET_RES, 0x1003);
    if (!tex1003) return list;

    uint32_t char_id = 0;
    bool no_detail = false;
    const uint32_t entry = current_list_entry(&char_id, &no_detail);
    if (!entry || !char_id) return list;

    if ((int)char_id != g_last_char_id) {
        g_last_char_id = (int)char_id;
        g_cursor = 0;
        g_bio_mode = false;
    }

    std::vector<DeckEntry> decks;
    get_character_decks(c, char_id, decks);
    if (decks.empty()) return list;
    if (g_cursor >= (int)decks.size()) g_cursor = 0;

    int winX = 8;
    if (!is_simple) {
        const uint16_t aeea = u16(LIST_STATE + 0x4a);
        if (aeea < 12) winX = 8 - ((12 - (int)aeea) * 299) / 12;
    } else {
        /* For Duelist 3 characters without state 3, draw the native dim + brown/cream detail window */
        list = call_game8(c, F_CL_DRAW_DIM, list, 0xc0);
        list = call_game8(c, F_CL_DRAW_FRAME, list, winX, 0x22);
        const uint8_t cat = u8(entry + 8) & 7;
        const uint32_t str_tab = call_game(c, F_CL_GET_RES, 0x1020);
        call_game8(c, F_CL_BEGIN_TEXT, list);
        if (str_tab) {
            draw_cl_text(c, winX + 7, 48, 80, read_utf16(call_game(c, F_CL_GET_STR, str_tab, 10)).c_str(), 0x0e, 0x0e, 0xffffffffu);
            draw_cl_text(c, winX + 7, 64, 80, read_utf16(call_game(c, F_CL_GET_STR, str_tab, 11)).c_str(), 0x0e, 0x0e, 0xffffffffu);
        }
        draw_cl_text(c, winX + 54, 47, 20, L":", 0x0e, 0x0e, 0xffffffffu);
        draw_cl_text(c, winX + 54, 64, 20, L":", 0x0e, 0x0e, 0xffffffffu);
        call_game8(c, F_CL_FONT_SIZE, 0x10, 0x10);
        call_game8(c, F_CL_TEXT_COLOR, 0xffffffffu);
        call_game8(c, F_CL_DRAW_NAME, winX + 68, 46, 200, char_id);
        list = call_game8(c, F_CL_END_TEXT);
        call_game8(c, F_CL_TEXT_COLOR, 0xff000000u);
        list = call_game8(c, F_CL_DRAW_CLASS, list, winX + 65, 0x3e, 3, cat);
    }

    /* If user toggled to Bio Mode (via Triangle), show a small native [Triangle] Decks tab in the header */
    if (g_bio_mode && !is_simple) {
        list = call_game8(c, F_CL_BIND_TEX, list, tex1003);
        list = call_game8(c, F_CL_SET_FILTER, list, 0, 0);
        list = draw_cl_teal_bar(c, list, winX + 195, 38, 90, 17, 0xffffffffu);
        call_game8(c, F_CL_BEGIN_TEXT, list);
        draw_cl_text(c, winX + 215, 40, 66, L"Decks", 0x0c, 0x0d, 0xff80ffffu);
        list = call_game8(c, F_CL_END_TEXT);
        draw_icon(720, g_ico_btn_tri, (float)(winX + 199), 40.0f, 13.0f, 13.0f);
        return list;
    }

    /* ---- Draw the Integrated Native Deck Menu inside the Detail Window (x = winX + 5, y = 79, w = 274, h = 159) ---- */
    list = call_game8(c, F_CL_BIND_TEX, list, tex1003);
    list = call_game8(c, F_CL_SET_FILTER, list, 0, 0);

    const int hdrX = winX + 8;
    const int hdrY = 82;
    const int hdrW = 264;
    const int hdrH = 17;

    /* 2A. Top Section Header Bar */
    list = draw_cl_teal_bar(c, list, hdrX, hdrY, hdrW, hdrH, 0xffffffffu);

    /* 2B. Deck Option Rows (up to 6 rows of 264x20, stride 22) */
    for (size_t i = 0; i < decks.size() && i < 6; i++) {
        const DeckEntry &d = decks[i];
        const bool sel = ((int)i == g_cursor);
        const bool edited = g_overrides.count(d.rcp) != 0;
        const int rx = winX + 8;
        const int ry = 101 + (int)i * 22;
        const int rw = 264;
        const int rh = 20;

        /* Row 3D sunken teal container slot */
        list = draw_cl_sunken_slot(c, list, rx, ry, rw, rh, sel ? 0xffa0ffffu : 0xffffffffu);

        /* Center Recipe Name Plate (like the character name plate in List of Duelists) */
        const uint32_t plate_col = sel ? 0xff88e8ffu : (edited ? 0xffc8f8e0u : 0xffdcf4f8u);
        list = draw_cl_capsule(c, list, rx + 50, ry + 2, 160, 16, plate_col);

        /* Right Status / Action Pill */
        const uint32_t pill_col = edited ? 0xff287028u : (sel ? 0xff2840b8u : 0xff684820u);
        list = draw_cl_capsule(c, list, rx + 212, ry + 2, 50, 16, pill_col);
    }

    /* 2C. Bottom Control Prompt Bar (below the detail window at y = 251) */
    const int botX = winX;
    const int botY = 251;
    const int botW = 291;
    const int botH = 19;
    list = draw_cl_teal_bar(c, list, botX, botY, botW, botH, 0xffffffffu);

    /* 2D. Native Game Font Text for Header, Deck Rows, and Bottom Bar */
    call_game8(c, F_CL_BEGIN_TEXT, list);

    draw_cl_text(c, hdrX + 22, hdrY + 2, 175, L"Select Deck to Edit", 0x0c, 0x0d, 0xffffffffu);
    if (!is_simple) {
        draw_cl_text(c, hdrX + 224, hdrY + 2, 38, L"Bio", 0x0b, 0x0d, 0xff80ffffu);
    }

    for (size_t i = 0; i < decks.size() && i < 6; i++) {
        const DeckEntry &d = decks[i];
        const bool sel = ((int)i == g_cursor);
        const bool edited = g_overrides.count(d.rcp) != 0;
        const int rx = winX + 8;
        const int ry = 101 + (int)i * 22;

        wchar_t lv_buf[16];
        if (decks.size() > 1) swprintf(lv_buf, 16, L"Lv.%u", (unsigned)d.level);
        else wcscpy(lv_buf, L"Deck");
        draw_cl_text(c, rx + 20, ry + 3, 30, lv_buf, 0x0b, 0x0d, sel ? 0xff70ffffu : 0xffffffffu);

        /* Recipe Name in crisp dark ink on the cream/gold name plate */
        draw_cl_text(c, rx + 54, ry + 3, 152, d.name.c_str(), 0x0c, 0x0d, 0xff101820u);

        /* Right Status / Action label */
        if (edited) {
            draw_cl_text(c, rx + 227, ry + 3, 34, L"Edit", 0x0b, 0x0d, 0xff70ffffu);
        } else if (sel) {
            draw_cl_text(c, rx + 228, ry + 3, 33, L"Edit", 0x0b, 0x0d, 0xffffffffu);
        } else {
            wchar_t rcp_buf[16];
            swprintf(rcp_buf, 16, L"RCP%03u", (unsigned)d.rcp);
            draw_cl_text(c, rx + 215, ry + 3, 46, rcp_buf, 0x0a, 0x0c, 0xfff0f0d0u);
        }
    }

    if (A->frame() < g_status_until && !g_status_msg.empty()) {
        draw_cl_text(c, botX + 24, botY + 3, 262, g_status_msg.c_str(), 0x0b, 0x0d, 0xff70ffffu);
    } else {
        draw_cl_text(c, botX + 22, botY + 3, 75, L"Edit Deck", 0x0b, 0x0d, 0xffffffffu);
        draw_cl_text(c, botX + 118, botY + 3, 65, L"Restore", 0x0b, 0x0d, 0xffffffffu);
        draw_cl_text(c, botX + 206, botY + 3, 55, L"Back", 0x0b, 0x0d, 0xffffffffu);
    }

    list = call_game8(c, F_CL_END_TEXT);

    /* 2E. Native Animated Pulsing Yellow/Red Selection Cursor (FUN_00002364) around the selected Deck Row */
    const int selY = 101 + g_cursor * 22;
    g_in_cursor = true;
    list = call_game8(c, F_CL_DRAW_CURSOR, list, winX + 8, selY, 264, 20);
    g_in_cursor = false;

    /* 2F. Queue Crisp Pixel-Art Icons for Header, Rows, and Bottom Bar */
    draw_icon(721, g_ico_deck, (float)(hdrX + 4), (float)(hdrY + 1), 15.0f, 15.0f);
    if (!is_simple) {
        draw_icon(722, g_ico_btn_tri, (float)(hdrX + 209), (float)(hdrY + 2), 13.0f, 13.0f);
    }

    for (size_t i = 0; i < decks.size() && i < 6; i++) {
        const DeckEntry &d = decks[i];
        const bool sel = ((int)i == g_cursor);
        const bool edited = g_overrides.count(d.rcp) != 0;
        const int rx = winX + 8;
        const int ry = 101 + (int)i * 22;

        draw_icon(730 + (int)i, edited ? g_ico_deck_edit : g_ico_deck, (float)(rx + 2), (float)(ry + 2), 16.0f, 16.0f);
        if (edited) {
            draw_icon(740 + (int)i, g_ico_star, (float)(rx + 214), (float)(ry + 3), 12.0f, 12.0f);
        } else if (sel) {
            draw_icon(740 + (int)i, g_ico_btn_x, (float)(rx + 214), (float)(ry + 3), 13.0f, 13.0f);
        }
    }

    if (A->frame() < g_status_until && !g_status_msg.empty()) {
        draw_icon(750, g_ico_star, (float)(botX + 7), (float)(botY + 3), 13.0f, 13.0f);
    } else {
        draw_icon(751, g_ico_btn_x, (float)(botX + 6), (float)(botY + 3), 13.0f, 13.0f);
        draw_icon(752, g_ico_btn_sq, (float)(botX + 102), (float)(botY + 3), 13.0f, 13.0f);
        draw_icon(753, g_ico_btn_cir, (float)(botX + 190), (float)(botY + 3), 13.0f, 13.0f);
    }

    return list;
}

/* Hook on FUN_00002364 (DrawCursor, last call in State 2 before EndList): append State 2 badges & icons */
static int hk_cl_draw_cursor(TfCpu *c) {
    if (g_in_cursor || !in_database_duelist_list()) return 0;
    const uint16_t state = u16(LIST_STATE + 0x02);
    if (state != 2) return 0;

    g_in_cursor = true;
    A->call_original(c, F_CL_DRAW_CURSOR);
    g_in_cursor = false;

    uint32_t list = A->reg(c, 2);
    load_all_overrides();
    init_icons();

    if (g_simple_detail_open) {
        list = draw_charalist_detail_menu(c, list, true);
        A->set_reg(c, 2, list);
        return 1;
    }

    const uint32_t tex1003 = call_game(c, F_CL_GET_RES, 0x1003);
    if (!tex1003) return 1;

    const uint8_t tab = u8(LIST_STATE + 0x06) & 3;
    const uint16_t first_row = u16(LIST_STATE + 0x08 + tab * 2);
    const uint16_t total_rows = u16(LIST_STATE + 0x16 + tab * 2);
    const uint32_t list_ptr = u32(LIST_STATE + 0x1c + tab * 4);
    const bool is_db_mode = (u8(LIST_STATE) & 0x0f) == 0;

    /* 1A. Draw edited capsule highlights inside the 98x14 teal slots on each visible row */
    list = call_game8(c, F_CL_BIND_TEX, list, tex1003);
    list = call_game8(c, F_CL_SET_FILTER, list, 0, 0);

    struct RowDeckInfo { int y; int total; int edited; };
    RowDeckInfo row_info[4] = {};
    int valid_rows = 0;

    if (is_db_mode && list_ptr >= 0x08000000u && list_ptr < 0x0a000000u) {
        for (int r = 0; r < 4 && (first_row + r) < total_rows; r++) {
            const uint32_t entry = list_ptr + (first_row + r) * 0x10;
            const uint32_t cid = u16(entry) & 0x3ff;
            uint16_t rcps[8];
            uint8_t lvs[8];
            const int n = get_character_rcp_ids(cid, rcps, lvs);
            int ed = 0;
            for (int k = 0; k < n; k++) if (g_overrides.count(rcps[k])) ed++;
            const int slotY = 78 + r * 56;
            row_info[r] = {slotY, n, ed};
            valid_rows = r + 1;
            if (ed > 0) {
                list = draw_cl_capsule(c, list, 318, slotY + 1, 96, 12, 0xff286838u);
            }
        }
    }

    /* 1B. Draw native game text for the top header prompt & per-row deck badges */
    call_game8(c, F_CL_BEGIN_TEXT, list);
    draw_cl_text(c, 250, 5, 130, L"Decks / Profile", 0x0c, 0x0d, 0xffffffffu);
    draw_cl_text(c, 404, 5, 65, L"Back", 0x0c, 0x0d, 0xffffffffu);

    for (int r = 0; r < valid_rows; r++) {
        if (row_info[r].total <= 0) continue;
        wchar_t badge[32];
        if (row_info[r].edited > 0) {
            swprintf(badge, 32, L"Edited (%d/%d)", row_info[r].edited, row_info[r].total);
            draw_cl_text(c, 336, row_info[r].y + 1, 76, badge, 0x0a, 0x0c, 0xff70ffffu);
        } else {
            swprintf(badge, 32, row_info[r].total == 1 ? L"1 Deck" : L"%d Decks", row_info[r].total);
            draw_cl_text(c, 336, row_info[r].y + 1, 76, badge, 0x0b, 0x0c, 0xffffffffu);
        }
    }
    list = call_game8(c, F_CL_END_TEXT);

    /* 1C. Queue crisp pixel-art icons */
    draw_icon(700, g_ico_btn_x, 233.0f, 4.0f, 14.0f, 14.0f);
    draw_icon(701, g_ico_btn_cir, 387.0f, 4.0f, 14.0f, 14.0f);
    for (int r = 0; r < valid_rows; r++) {
        if (row_info[r].total <= 0) continue;
        const IconBuf &ico = (row_info[r].edited > 0) ? g_ico_deck_edit : g_ico_deck;
        draw_icon(710 + r, ico, 319.0f, (float)(row_info[r].y - 1), 15.0f, 15.0f);
    }

    A->set_reg(c, 2, list);
    return 1;
}

/* Hook on FUN_00003030 (DrawDetailView, last call in State 3 before EndList): suppress bio & draw Deck Menu */
static int hk_cl_draw_detail(TfCpu *c) {
    if (!in_database_duelist_list()) return 0;

    uint32_t char_id = 0;
    bool no_detail = false;
    const uint32_t entry = current_list_entry(&char_id, &no_detail);
    if (!entry || no_detail) return 0;

    uint32_t saved_bio_addr = 0;
    uint32_t saved_bio_val = 0;
    uint32_t saved_det_addr = 0;
    uint8_t saved_det_flags = 0;

    const uint32_t detail = u32(entry + 8);
    if (!g_bio_mode && detail >= 0x08000000u && detail < 0x0a000000u) {
        const uint32_t bio_ptr_addr = detail + (u8(detail) & 0x0c) + 0x14;
        saved_bio_addr = bio_ptr_addr;
        saved_bio_val  = u32(bio_ptr_addr);
        u32(bio_ptr_addr) = 0;

        saved_det_addr = detail;
        saved_det_flags = u8(detail);
        u8(detail) &= ~0x40u; /* hide bottom voice player while Deck Menu is active */
    }

    A->call_original(c, F_CL_DRAW_DETAIL);

    if (saved_bio_addr) u32(saved_bio_addr) = saved_bio_val;
    if (saved_det_addr) u8(saved_det_addr) = saved_det_flags;

    uint32_t list = A->reg(c, 2);
    list = draw_charalist_detail_menu(c, list, false);
    A->set_reg(c, 2, list);
    return 1;
}

/* Handle deck menu navigation and actions when the character detail menu is open */
static bool handle_deck_menu_input(TfCpu *c, uint32_t char_id, bool is_simple_overlay) {
    std::vector<DeckEntry> decks;
    get_character_decks(c, char_id, decks);
    if (decks.empty()) return false;
    if (g_cursor >= (int)decks.size()) g_cursor = 0;

    uint32_t &held = u32(PAD_STATE + 0x00);
    uint32_t &pressed = u32(PAD_STATE + 0x04);
    uint32_t &repeated = u32(PAD_STATE + 0x0c);

    const uint32_t cur_pad = held;
    const uint32_t edge = cur_pad & ~g_prev_pad;
    g_prev_pad = cur_pad;

    /* Triangle: toggle between Deck Menu and Character Bio (for characters with full detail view) */
    if (!is_simple_overlay && (edge & BTN_TRIANGLE)) {
        g_bio_mode = !g_bio_mode;
        call_game(c, F_SOUND, 0x621b);
        held &= ~BTN_TRIANGLE; pressed &= ~BTN_TRIANGLE; repeated &= ~BTN_TRIANGLE;
        return false;
    }

    if (g_bio_mode && !is_simple_overlay) {
        return false; /* let native bio scrolling & voice controls work normally */
    }

    if (edge & BTN_UP) {
        g_cursor = (g_cursor > 0) ? (g_cursor - 1) : ((int)decks.size() - 1);
        call_game(c, F_SOUND, 0x6212);
    } else if (edge & BTN_DOWN) {
        g_cursor = (g_cursor + 1 < (int)decks.size()) ? (g_cursor + 1) : 0;
        call_game(c, F_SOUND, 0x6212);
    }
    /* Block native bio scrolling while our deck list menu is active */
    held &= ~(BTN_UP | BTN_DOWN);
    pressed &= ~(BTN_UP | BTN_DOWN);
    repeated &= ~(BTN_UP | BTN_DOWN);

    /* Square: Restore Original Deck */
    if (edge & BTN_SQUARE) {
        const DeckEntry &d = decks[g_cursor];
        if (g_overrides.count(d.rcp)) {
            delete_override_file(d.rcp);
            call_game(c, F_SOUND, 0x6213);
            wchar_t msg[96];
            swprintf(msg, 96, L"Restored original: %ls", d.name.c_str());
            g_status_msg = msg;
            g_status_until = A->frame() + 120;
            A->log("Restored original deck for RCP%03u", (unsigned)d.rcp);
        } else {
            call_game(c, F_SOUND, 0x6215);
            g_status_msg = L"Deck is already using original recipe.";
            g_status_until = A->frame() + 90;
        }
        held &= ~BTN_SQUARE;
        pressed &= ~BTN_SQUARE;
        repeated &= ~BTN_SQUARE;
    }

    /* Cross (X): Open Native Deck Editor on the selected deck */
    if (edge & BTN_CROSS) {
        const DeckEntry &d = decks[g_cursor];
        g_editing_rcp = d.rcp;
        g_editing_char = (int)char_id;
        g_editing_char_name = read_utf16(call_game(c, F_CHAR_NAME, 0, char_id));
        g_editing_deck_name = d.name;
        g_launching_editor = true;
        g_simple_detail_open = false;

        call_game(c, F_SOUND, 0x6213);
        A->log("Opening native Deck Editor for %ls - %ls (RCP%03u)",
               g_editing_char_name.c_str(), g_editing_deck_name.c_str(), (unsigned)d.rcp);

        /* Trigger libcharalist fade-out and exit (state 4 with timer > 0x10 transitions cleanly to state 5 -> 6 -> return 1) */
        u16(LIST_STATE + 0x48) = 0x11;
        u16(LIST_STATE + 0x02) = 4;
        held &= ~BTN_CROSS; pressed &= ~BTN_CROSS; repeated &= ~BTN_CROSS;
        return true;
    }

    /* Circle (O) in simple overlay: close overlay back to list */
    if (is_simple_overlay && (pressed & BTN_CIRCLE)) {
        g_simple_detail_open = false;
        call_game(c, F_SOUND, 0x6214);
        held &= ~BTN_CIRCLE; pressed &= ~BTN_CIRCLE; repeated &= ~BTN_CIRCLE;
        return true;
    }

    return false;
}

/* Hook on libcharalist State 2 (FUN_00004264: list navigation) */
static int hk_charalist_list(TfCpu *c) {
    if (!in_database_duelist_list()) return 0;

    uint32_t char_id = 0;
    bool no_detail = false;
    if (!current_list_entry(&char_id, &no_detail) || !char_id) return 0;

    if (g_simple_detail_open) {
        handle_deck_menu_input(c, char_id, true);
        return 1;
    }

    /* Sync g_prev_pad while on the list so holding X into State 3 does not immediately trigger Cross on frame 1 */
    g_prev_pad = u32(PAD_STATE + 0x00);

    /* For Duelist 3 characters (or Yugi) that have kNoDetailFlag set, pressing X opens our native detail window */
    const uint32_t pressed = u32(PAD_STATE + 0x04);
    if (no_detail && (pressed & BTN_CROSS)) {
        g_simple_detail_open = true;
        g_cursor = 0;
        g_bio_mode = false;
        call_game(c, F_SOUND, 0x6213);
        u32(PAD_STATE + 0x04) &= ~BTN_CROSS;
        return 1;
    }

    return 0;
}

/* Hook on libcharalist State 3 (FUN_00004844: character detail screen with big portrait) */
static int hk_charalist_detail(TfCpu *c) {
    if (!in_database_duelist_list()) return 0;

    const uint16_t sub = u16(LIST_STATE + 0x04);
    if (sub == 3) {
        uint32_t char_id = 0;
        if (current_list_entry(&char_id, nullptr) && char_id) {
            if (handle_deck_menu_input(c, char_id, false))
                return 1;
        }
    } else {
        g_prev_pad = u32(PAD_STATE + 0x00);
    }
    return 0;
}

/* Hook on libtitle SwitchScreen(code, arg): suppress SwitchScreen(9, 6) when leaving CharListStep for Deck Edit */
static int hk_switch_screen(TfCpu *c) {
    if (A->module_loaded("libtitle") && g_launching_editor && A->reg(c, 4) == 9 && A->reg(c, 5) == 6) {
        u16(TITLE_STATE + 0x08) = 41; /* prevent RunScreen from drawing unloaded libcharalist on this frame */
        A->set_reg(c, 2, 41);
        return 1;
    }
    return 0;
}

/* Hook on libtitle UpdateFlow(): on the next frame's Tick, switch cleanly to screen 20 (Deck Edit) and return 41 */
static int hk_update_flow(TfCpu *c) {
    if (A->module_loaded("libtitle") && g_launching_editor && !A->module_loaded("libcharalist_08eeb400")) {
        g_launching_editor = false;
        g_return_to_charalist = true;
        u16(TITLE_STATE + 0x08) = 20; /* State::screen = 20 (kScreenDeckEdit) */
        const uint32_t ret = call_game(c, F_SWITCH_SCREEN, 20, 0);
        A->set_reg(c, 2, ret);
        A->log("Redirecting Database exit to native Deck Editor (screen 20, ret=%u)", ret);
        return 1;
    }
    return 0;
}

/* Hook on libtitle StartLogoWith(screen, arg): return to Database menu (screen 9, arg 6) and auto-enter List of Duelists */
static int hk_start_logo(TfCpu *c) {
    if (A->module_loaded("libtitle") && g_return_to_charalist && A->reg(c, 4) == 9) {
        A->set_reg(c, 4, 9);
        A->set_reg(c, 5, 6); /* SubList 6 = Database */
        A->log("Returning from Deck Editor to Database > List of Duelists");
    }
    return 0;
}

/* Hook on libtitle SubList6Input(): automatically enter List of Duelists (row 0 -> screen 0x16) after returning from Deck Editor */
static int hk_sublist6_input(TfCpu *c) {
    if (A->module_loaded("libtitle") && g_return_to_charalist) {
        g_return_to_charalist = false;
        call_game(c, F_REQ_SCREEN16, 1, 0x16, 0);
        return 1;
    }
    return 0;
}

/* Hook on libdeck init (FUN_00007c38): schedule loading the selected duelist deck on the first step after init completes */
static int hk_deck_init(TfCpu *c) {
    (void)c;
    if (!A->module_loaded("libdeck") || g_editing_rcp < 0) return 0;
    g_deck_load_pending = true;
    return 0;
}

/* Hook on libdeck per-frame step (FUN_00008b14): load the duelist's recipe after FUN_00007c38 has finished */
static int hk_deck_step(TfCpu *c) {
    if (!A->module_loaded("libdeck") || !g_deck_load_pending || g_editing_rcp < 0) return 0;
    g_deck_load_pending = false;

    if (!g_guest_deck) {
        const uint32_t buf = A->guest_alloc(1024);
        g_guest_deck = buf;
        g_guest_str  = buf ? (buf + 256) : 0;
    }
    if (!g_guest_deck) return 0;

    /* Read deck (either original or existing override) into g_guest_deck */
    call_game(c, F_READ_DECK, (uint32_t)g_editing_rcp, g_guest_deck);

    /* Write recipe name as UTF-16LE at g_guest_deck + 200 */
    const uint32_t name_addr = g_guest_deck + 200;
    size_t i = 0;
    for (; i < g_editing_deck_name.size() && i < 24; i++)
        u16(name_addr + (uint32_t)i * 2) = (uint16_t)g_editing_deck_name[i];
    u16(name_addr + (uint32_t)i * 2) = 0;

    /* Populate working Main / Extra / Side decks via native FUN_00001900 */
    call_game(c, F_DECK_LOAD_RCP, g_guest_deck, name_addr);

    /* Clear bit 0x80 (unowned card flag) on all cards in Main, Extra, and Side so duelist cards are never dimmed */
    const uint32_t panes[3] = {LIBDECK + 0xdce660, LIBDECK + 0xdce680, LIBDECK + 0xdce6a0};
    for (int p = 0; p < 3; p++) {
        const uint32_t base = u32(panes[p] + 4);
        const uint16_t cnt = u16(panes[p] + 8);
        if (base >= 0x08000000u && base < 0x0a000000u) {
            for (uint16_t k = 0; k < cnt; k++)
                u8(base + k * 6 + 4) &= 0x7f;
        }
    }

    /* Switch active slot 0 to Main Deck (pane 0), refresh tab bar and left card preview */
    u8(LIBDECK + 0xdce5c0) &= 0xcf;
    call_game(c, F_DECK_SET_PANE, 0, 0, 2);
    call_game(c, F_DECK_PREVIEW);
    A->log("Loaded duelist deck RCP%03d (%ls) into native Deck Editor", g_editing_rcp, g_editing_deck_name.c_str());
    return 0;
}

/* Hook on libdeck exit validator (FUN_00006694): bypass deck size/banlist restrictions when editing a duelist deck */
static int hk_validate(TfCpu *c) {
    if (A->module_loaded("libdeck") && (g_editing_rcp >= 0 || g_bypass_env)) {
        A->set_reg(c, 2, 0);
        return 1;
    }
    return 0;
}

static int hk_card_limit(TfCpu *c) {
    if (A->module_loaded("libdeck") && (g_editing_rcp >= 0 || g_bypass_env)) {
        A->set_reg(c, 2, 3);
        return 1;
    }
    return 0;
}

/* Hook on libdeck exit commit (FUN_00001cf0): save edited duelist deck to override file instead of overwriting player's deck */
static int hk_commit(TfCpu *c) {
    (void)c;
    if (!A->module_loaded("libdeck") || g_editing_rcp < 0) return 0;

    DeckData d;
    auto read_pane = [&](uint32_t pane_off, std::vector<uint16_t> &out) {
        const uint32_t base = u32(LIBDECK + pane_off + 4);
        const uint16_t cnt = u16(LIBDECK + pane_off + 8);
        if (base < 0x08000000u || base >= 0x0a000000u) return;
        for (uint16_t i = 0; i < cnt; i++) {
            const uint16_t cid = u16(base + i * 6) & 0x1fff;
            const uint8_t copies = u8(base + i * 6 + 4) & 0x7f;
            if (cid) {
                for (uint8_t cpy = 0; cpy < (copies ? copies : 1); cpy++)
                    out.push_back(cid);
            }
        }
    };
    read_pane(0xdce660, d.main_deck);
    read_pane(0xdce6a0, d.extra_deck);
    read_pane(0xdce680, d.side_deck);

    if (!d.main_deck.empty()) {
        save_override_file(g_editing_rcp, d);
        A->log("Saved edited duelist deck RCP%03d (%zu main, %zu extra, %zu side)",
               g_editing_rcp, d.main_deck.size(), d.extra_deck.size(), d.side_deck.size());
        wchar_t msg[96];
        swprintf(msg, 96, L"Saved %ls (RCP%03d)", g_editing_deck_name.c_str(), g_editing_rcp);
        g_status_msg = msg;
        g_status_until = A->frame() + 180;
    }
    g_editing_rcp = -1;
    return 1; /* skip writing to player's personal deck & trunk */
}

/* Hook on ReadDeckData (0x0884a694): serve edited duelist decks everywhere (Duels, Recipe Viewer, Deck Editor) */
static int hk_read_deck(TfCpu *c) {
    load_all_overrides();
    const int idx = (int)A->reg(c, 4);
    const uint32_t dst = A->reg(c, 5);
    auto it = g_overrides.find(idx);
    if (it == g_overrides.end()) return 0;

    const DeckData &d = it->second;
    uint16_t *o = (uint16_t *)A->mem(dst);
    memset(o, 0, 198);
    o[0] = (uint16_t)d.main_deck.size();
    for (size_t i = 0; i < d.main_deck.size(); i++) o[1 + i] = d.main_deck[i];
    const size_t ex_off = 1 + d.main_deck.size();
    o[ex_off] = (uint16_t)d.extra_deck.size();
    for (size_t i = 0; i < d.extra_deck.size(); i++) o[ex_off + 1 + i] = d.extra_deck[i];
    const size_t sd_off = ex_off + 1 + d.extra_deck.size();
    o[sd_off] = (uint16_t)d.side_deck.size();
    for (size_t i = 0; i < d.side_deck.size(); i++) o[sd_off + 1 + i] = d.side_deck[i];

    A->set_reg(c, 2, 1);
    return 1;
}

static int hk_frame(TfCpu *c) {
    if (!g_guest_deck) {
        const uint32_t buf = A->guest_alloc(1024);
        g_guest_deck = buf;
        g_guest_str  = buf ? (buf + 256) : 0;
    }
    (void)c;
    return 0;
}

extern "C" __declspec(dllexport) int tfmod_init(const TfModApi *api) {
    TFMOD_REQUIRE(api, 5);
    A = api;
    const char *bp = getenv("TF_DECK_BYPASS");
    g_bypass_env = bp && bp[0] && bp[0] != '0';

    A->hook(F_FRAME, hk_frame);
    A->hook(F_READ_DECK, hk_read_deck);
    A->hook(F_CHARALIST_LIST, hk_charalist_list);
    A->hook(F_CHARALIST_DET, hk_charalist_detail);
    A->hook(F_CL_DRAW_CURSOR, hk_cl_draw_cursor);
    A->hook(F_CL_DRAW_DETAIL, hk_cl_draw_detail);
    A->hook(F_SWITCH_SCREEN, hk_switch_screen);
    A->hook(F_UPDATE_FLOW, hk_update_flow);
    A->hook(F_START_LOGO, hk_start_logo);
    A->hook(F_SUBLIST6_INPUT, hk_sublist6_input);
    A->hook(F_DECK_INIT, hk_deck_init);
    A->hook(F_DECK_STEP, hk_deck_step);
    A->hook(F_VALIDATE, hk_validate);
    A->hook(F_CARD_LIMIT_A, hk_card_limit);
    A->hook(F_CARD_LIMIT_B, hk_card_limit);
    A->hook(F_COMMIT, hk_commit);

    A->log("Duelist Deck Editor active (integrated native Database UI + save overrides)");
    return 0;
}
