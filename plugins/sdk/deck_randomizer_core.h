/* Shared core of the "Player Deck Randomizer" and "Opponent Deck Randomizer" mods (plugins, API 3): the deck a duelist brings to the duel is swapped for a
 * random whole recipe (RCPnnn) of the game, with its name. Each mod's .cpp defines DECKRAND_PLAYER / DECKRAND_OPPONENT (1 or 0) and DECKRAND_NAME, then
 * includes this header.
 *
 * The game fills a duelist slot with libehsys_85A292D7(rcp_index, out) (0884a694, BOOT): out = slot + 0x1a4 gets the deck (0x108 bytes), the 0x40-byte
 * UTF-16 deck name was copied just before to slot + 0x162 (from libehsys_E1677D35(rcp_index), 0884a5cc, the name table) and slot + 0x124 holds the rcp
 * index. The hook runs on ENTRY (returns 0, so the original still runs and the other mod's hook too): it writes the random recipe's name into the slot
 * and replaces the index argument, so the reader loads the game's own deck. The player's slot is not read through the reader (the setup copies the saved
 * deck), so it is found from the first slot the reader fills and replaced in place. The recipe pool (indices with a real deck and a name, not the
 * placeholder) is built once from the game, so no game data is stored in the mod. Settings: config.ini (seed) in the mod folder. */
#pragma once
#include "tfmod.h"
#include <windows.h>
#include <vector>
#include <random>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const TfModApi *A;
static const uint32_t F_READ_DECK = 0x0884a694, F_DECK_NAME = 0x0884a5cc, F_PREPARE_DUEL = 0x0883f4f8;
static const int N_RECIPES = 0x15f, PLACEHOLDER = 51;
static std::vector<int> g_pool;
static bool g_pool_ready, g_off;
#if DECKRAND_PLAYER
static bool g_setup_pending;
#endif
static uint32_t g_seed, g_scratch;
static std::mt19937 g_rng;

static void cfg_load(void) {
    wchar_t path[MAX_PATH]; swprintf(path, MAX_PATH, L"%ls\\config.ini", A->mod_dir);
    if (FILE *f = _wfopen(path, L"r")) {
        char l[128]; int v;
        while (fgets(l, sizeof l, f)) if (sscanf(l, "seed=%d", &v) == 1) g_seed = (uint32_t)v;
        fclose(f);
    }
    if (const char *e = getenv("TF_DECKRAND_SEED")) g_seed = (uint32_t)strtoul(e, nullptr, 0);
}

/* runs a game function (or the hooked original) with a0/a1, keeping every register of the hooked one; returns v0 */
static uint32_t run(TfCpu *c, uint32_t fn, bool original, uint32_t a0, uint32_t a1) {
    uint32_t s[32]; for (int i = 1; i < 32; i++) s[i] = A->reg(c, i);
    A->set_reg(c, 4, a0); A->set_reg(c, 5, a1);
    if (original) A->call_original(c, fn); else A->call(c, fn);
    const uint32_t r = A->reg(c, 2);
    for (int i = 1; i < 32; i++) if (i != 26 && i != 27) A->set_reg(c, i, s[i]);
    return r;
}
static bool in_ram(uint32_t p, uint32_t n) { return p >= 0x08800000u && p < 0x0a000000u - n; }
static uint32_t name_of(TfCpu *c, int idx) {
    const uint32_t p = run(c, F_DECK_NAME, false, (uint32_t)idx, 0);
    return in_ram(p, 0x40) && *(uint16_t *)A->mem(p) ? p : 0;
}
static bool read_deck(TfCpu *c, int idx, uint32_t out) { run(c, F_READ_DECK, true, (uint32_t)idx, out); return *(uint32_t *)A->mem(out) != 0; }

static void build_pool(TfCpu *c) {
    g_pool_ready = true;
    uint8_t ph[0x108]; if (!read_deck(c, PLACEHOLDER, g_scratch)) return;
    memcpy(ph, A->mem(g_scratch), sizeof ph);
    for (int i = 1; i < N_RECIPES; i++) {
        if (!read_deck(c, i, g_scratch) || !name_of(c, i)) continue;
        if (!memcmp(A->mem(g_scratch), ph, sizeof ph)) continue;   /* the placeholder fills every unused slot */
        g_pool.push_back(i);
    }
    A->log(DECKRAND_NAME ": %zu recipes in the pool", g_pool.size());
}

/* picks a random recipe and writes its name at `name` (the slot's deck name); returns the recipe index or -1 */
static int pick_random(TfCpu *c, uint32_t name, uint32_t salt) {
    if (!g_pool_ready) build_pool(c);
    if (g_pool.empty()) return -1;
    g_rng.seed(g_seed ? g_seed * 2654435761u + salt : std::random_device{}() ^ (uint32_t)GetTickCount64());
    const int idx = g_pool[g_rng() % g_pool.size()];
    const uint32_t nm = name_of(c, idx);
    if (!nm) return -1;
    memcpy(A->mem(name), A->mem(nm), 0x40);
    return idx;
}

#if DECKRAND_PLAYER
/* libehsys_F11F0D9A (0883f4f8): commits the duel setup: the player's slot copies the saved deck without the reader, so it is found from the first deck the
 * reader fills (slot j's buffer = base + j * 0x18c + 0x1a4; the player's is slot 0) and replaced right then. */
static int hk_prepare_duel(TfCpu *c) { (void)c; g_setup_pending = !g_off; return 0; }
static void replace_player_slot(TfCpu *c, uint32_t out) {
    if (!g_setup_pending) return;   /* stays pending until a slot validates: the other mod's pool scan also goes through this hook (its buffer is no slot) */
    for (int j = 1; j <= 4; j++) {
        const uint32_t base = out - 0x1a4 - (uint32_t)(j % 4) * 0x18c;
        if (base < 0x08800000u || base > 0x0a000000u - 0x300) continue;
        const uint32_t *h0 = (const uint32_t *)A->mem(base), *h1 = (const uint32_t *)A->mem(base + 0x1a4);
        if (h0[0] < 1 || h0[0] > 80 || memcmp(h0, h1, 12)) continue;
        g_setup_pending = false;
        const int r = pick_random(c, base + 0x162, 0x1000);
        if (r >= 0 && read_deck(c, r, base + 0x1a4)) memcpy(A->mem(base), A->mem(base + 0x1a4), 0x108);   /* the second copy at the start of the block stays equal */
        A->log(DECKRAND_NAME ": player's deck -> RCP%03d (slot at %08x)", r, base);
        return;
    }
}
#endif

static int hk_read_deck(TfCpu *c) {   /* libehsys_85A292D7(rcp_index, out) */
    if (g_off) return 0;
    const uint32_t idx = A->reg(c, 4), out = A->reg(c, 5);
#if DECKRAND_PLAYER
    replace_player_slot(c, out);
#endif
    /* only a duelist slot is replaced (the recipe viewer and the title menu read decks into other buffers): slot + 0x124 holds this index */
    if (!in_ram(out, 0x108) || out < 0x08800000u + 0x1a4 || *(uint16_t *)A->mem(out - 0x1a4 + 0x124) != (uint16_t)idx) return 0;
    const bool is_player = idx == 0 || idx >= 0x15f;
    if (is_player ? !DECKRAND_PLAYER : !DECKRAND_OPPONENT) return 0;
    const int r = pick_random(c, out - 0x1a4 + 0x162, idx);
    if (r < 0) return 0;
    A->set_reg(c, 4, (uint32_t)r);   /* the original reads the random recipe instead */
    A->log(DECKRAND_NAME ": RCP%03d -> RCP%03d", idx, r);
    return 0;
}

extern "C" __declspec(dllexport) int tfmod_init(const TfModApi *api) {
    A = api; g_off = getenv("TF_DECKRAND_OFF") != nullptr;
    cfg_load();
    g_scratch = A->guest_alloc(0x110);
    A->hook(F_READ_DECK, hk_read_deck);
#if DECKRAND_PLAYER
    A->hook(F_PREPARE_DUEL, hk_prepare_duel);
#endif
    A->log(DECKRAND_NAME ": seed=%u%s", g_seed, g_off ? " (OFF by TF_DECKRAND_OFF)" : "");
    return 0;
}
