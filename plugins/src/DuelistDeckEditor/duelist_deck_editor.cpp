/* Duelist Deck Editor (work in progress): edit the decks of the duelists with the game's own deck editor.
 * Findings (docs/internal/registro/duelo-decks-marcas.md, 2026-11-05):
 *   - libdeck (base 08efca00) exit validator 08f03094: returns 0 ok, 1..4 size errors, 5 = a card above its limit.
 *   - Load Recipe (Deck & Recipe Menu) already loads a duelist deck into the working deck.
 * Stage 0 (this file): hook the validator; with DDE_BYPASS=1 in the environment it always answers "ok" so the exit path can be traced. */
#include "tfmod.h"
#include <stdlib.h>
#include <stdio.h>

static const TfModApi *A;
static const uint32_t F_VALIDATE = 0x08f03094;
static const uint32_t F_COMMIT = 0x08efe6f0, LIBDECK = 0x08efca00;
static bool g_bypass;

static uint32_t u32(uint32_t a) { return *(uint32_t *)A->mem(a); }
static uint16_t u16(uint32_t a) { return *(uint16_t *)A->mem(a); }

/* reads the editor's working lists (ids, one per copy): main, side, extra */
static void read_lists(uint16_t *main_, int &nm, uint16_t *side, int &ns, uint16_t *extra, int &ne) {
    uint32_t arr = u32(LIBDECK + 0xdce664); nm = u16(LIBDECK + 0xdce668); if (nm > 80) nm = 80;
    for (int i = 0; i < nm; i++) main_[i] = u16(arr + 6 * i);
    arr = u32(LIBDECK + 0xdce6a4); ns = u16(LIBDECK + 0xdce6a8); if (ns > 15) ns = 15;
    for (int i = 0; i < ns; i++) side[i] = u16(arr + 6 * i);
    arr = u32(LIBDECK + 0xdce684); uint32_t idx = u32(LIBDECK + 0xdce690); ne = u16(LIBDECK + 0xdce688); if (ne > 30) ne = 30;
    for (int i = 0; i < ne; i++) extra[i] = u16(arr + 6 * u16(idx + 2 * i));
}

static const uint32_t F_READ_DECK = 0x0884a694;
static int hk_read(TfCpu *c) {   /* libehsys_85A292D7(recipe, out) */
    if (A->module_loaded("libdeck") && g_bypass)
        A->log("reader: recipe %u out %08x ra %08x frame %llu", A->reg(c, 4), A->reg(c, 5), A->reg(c, 31), (unsigned long long)A->frame());
    return 0;
}

static int hk_commit(TfCpu *c) {
    (void)c;
    if (!A->module_loaded("libdeck") || !g_bypass) return 0;
    uint16_t m[80], s[15], e[30]; int nm, ns, ne;
    read_lists(m, nm, s, ns, e, ne);
    char line[1024]; int k = 0;
    for (int i = 0; i < nm && k < 900; i++) k += snprintf(line + k, sizeof line - k, "%u ", m[i]);
    A->log("commit: main %d, side %d, extra %d; main = %s", nm, ns, ne, line);
    return 1;   /* skip the original: the player's saved deck stays untouched */
}

static int hk_validate(TfCpu *c) {
    if (!A->module_loaded("libdeck") || !g_bypass) return 0;
    A->set_reg(c, 2, 0);   /* 0 = the deck is valid */
    return 1;
}

extern "C" __declspec(dllexport) int tfmod_init(const TfModApi *api) {
    A = api;
    TFMOD_REQUIRE(api, 6);
    g_bypass = getenv("DDE_BYPASS") != nullptr;
    A->hook(F_VALIDATE, hk_validate);
    A->hook(F_COMMIT, hk_commit);
    A->hook(F_READ_DECK, hk_read);
    A->log("Duelist Deck Editor stage 0 loaded (bypass %d)", g_bypass ? 1 : 0);
    return 0;
}
extern "C" __declspec(dllexport) const char *tfmod_game_ids = "ULES-00600";
