/* Mod "Card Randomizer" (plugin, API 3): every deck the game reads for a duel is filled with random cards from the whole card database.
 *
 * The game reads each deck with libehsys_85A292D7(rcp_index, out) (0884a694, BOOT): it fills 0x108 bytes {[0] = n_main, [1] = n_side, [2] = n_extra,
 * main at +0x0c, side at +0xac, extra at +0xca (u16 card ids)}. The hook lets the original run (so the sizes and the layout are the game's own) and
 * then overwrites the ids (the opponent; the player's saved deck is replaced at the start of the duel engine). The card pool is built once from the game's card database (card_prop 0884b4f8: id -> 8 bytes, layout in tools/game/cartas.py),
 * so no game data is stored in the mod. Settings: config.ini in the mod folder. */
#include "tfmod.h"
#include <windows.h>
#include <vector>
#include <random>
#include <algorithm>
#include <map>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const TfModApi *A;
static const uint32_t F_READ_DECK = 0x0884a694, F_PROP = 0x0884b4f8, F_PREPARE_DUEL = 0x0883f4f8;
enum { K_MONSTER, K_SPELL, K_TRAP, K_FUSION, K_N };
static std::vector<uint16_t> g_pool[K_N];
static bool g_pool_ready, g_off;
static int g_player = 1, g_opponent = 1, g_balance = 0, g_max_copies = 3;
static uint32_t g_seed;
static std::mt19937 g_rng;

static std::wstring cfg_path(void) { return std::wstring(A->mod_dir) + L"\\config.ini"; }
static void cfg_load(void) {
    if (FILE *f = _wfopen(cfg_path().c_str(), L"r")) {
        char l[128]; int v;
        while (fgets(l, sizeof l, f)) {
            if (sscanf(l, "randomize_player=%d", &v) == 1) g_player = v != 0;
            else if (sscanf(l, "randomize_opponent=%d", &v) == 1) g_opponent = v != 0;
            else if (sscanf(l, "keep_balance=%d", &v) == 1) g_balance = v != 0;
            else if (sscanf(l, "max_copies=%d", &v) == 1) g_max_copies = std::max(1, std::min(3, v));
            else if (sscanf(l, "seed=%d", &v) == 1) g_seed = (uint32_t)v;
        }
        fclose(f);
    }
    if (const char *e = getenv("TF_CARDRAND_SEED")) g_seed = (uint32_t)strtoul(e, nullptr, 0);
}

static uint32_t chama(TfCpu *c, uint32_t fn, uint32_t a0) {   /* call a game function keeping every register of the hooked one */
    uint32_t s[32]; for (int i = 1; i < 32; i++) s[i] = A->reg(c, i);
    A->set_reg(c, 4, a0); A->call(c, fn); const uint32_t r = A->reg(c, 2);
    for (int i = 1; i < 32; i++) if (i != 26 && i != 27) A->set_reg(c, i, s[i]);
    return r;
}
static int kind_of(uint32_t cat) {
    if (cat == 2 || cat == 3) return K_FUSION;
    if (cat == 13) return K_SPELL;
    if (cat == 14) return K_TRAP;
    if (cat < 9) return K_MONSTER;   /* 9 = token: never in a deck */
    return -1;
}
static void build_pool(TfCpu *c) {
    g_pool_ready = true;
    for (uint32_t id = 4000; id < 8192; id++) {
        const uint32_t p = chama(c, F_PROP, id);
        if (p < 0x08800000 || p >= 0x0a000000) continue;
        const uint32_t w0 = *(uint32_t *)A->mem(p), w1 = *(uint32_t *)A->mem(p + 4);
        if ((w0 & 0x1fff) != id) continue;   /* not the entry of this id: no such card */
        const int k = kind_of(w1 & 15); if (k >= 0) g_pool[k].push_back((uint16_t)id);
    }
    A->log("Card Randomizer: pool = %zu monsters, %zu spells, %zu traps, %zu fusion monsters", g_pool[K_MONSTER].size(), g_pool[K_SPELL].size(), g_pool[K_TRAP].size(), g_pool[K_FUSION].size());
}
static int kind_of_id(TfCpu *c, uint16_t id) {
    const uint32_t p = chama(c, F_PROP, id);
    if (p < 0x08800000 || p >= 0x0a000000) return K_MONSTER;
    return std::max(0, kind_of(*(uint32_t *)A->mem(p + 4) & 15));
}

/* fills `n` slots at `ids` with random cards; kinds[i] (when balancing) is the kind the slot must keep */
static void fill(uint16_t *ids, int n, int fixed_kind, const int *kinds) {
    std::map<uint16_t, int> copies;
    for (int i = 0; i < n; i++) {
        const int k = fixed_kind >= 0 ? fixed_kind : (kinds ? kinds[i] : -1);
        for (int tries = 0; tries < 64; tries++) {
            int kind = k;
            if (k < 0) {   /* fully random: pick uniformly among all non-fusion cards */
                const size_t m = g_pool[K_MONSTER].size(), s = g_pool[K_SPELL].size(), t = g_pool[K_TRAP].size(), r = g_rng() % (m + s + t);
                kind = r < m ? K_MONSTER : r < m + s ? K_SPELL : K_TRAP;
            }
            const std::vector<uint16_t> &pool = g_pool[kind]; if (pool.empty()) break;
            const uint16_t id = pool[g_rng() % pool.size()];
            if (copies[id] >= g_max_copies && tries < 63) continue;
            copies[id]++; ids[i] = id; break;
        }
    }
}

/* randomizes the deck buffer at PSP address `out` in place (same layout the deck reader fills) */
static void randomize_buffer(TfCpu *c, uint32_t out, uint32_t seed_salt) {
    if (!g_pool_ready) build_pool(c);
    if (g_pool[K_MONSTER].empty() || g_pool[K_SPELL].empty() || g_pool[K_TRAP].empty()) return;
    uint8_t *o = A->mem(out);
    const uint32_t nm = ((uint32_t *)o)[0], ns = ((uint32_t *)o)[1], ne = ((uint32_t *)o)[2];
    if (nm > 80 || ns > 15 || ne > 30) return;
    g_rng.seed(g_seed ? g_seed * 2654435761u + seed_salt : std::random_device{}() ^ (uint32_t)GetTickCount64());
    uint16_t *main = (uint16_t *)(o + 0x0c), *side = (uint16_t *)(o + 0xac), *extra = (uint16_t *)(o + 0xca);
    int kinds_main[80], kinds_side[15];
    if (g_balance) {
        for (uint32_t i = 0; i < nm; i++) kinds_main[i] = kind_of_id(c, main[i]);
        for (uint32_t i = 0; i < ns; i++) kinds_side[i] = kind_of_id(c, side[i]);
    }
    fill(main, (int)nm, -1, g_balance ? kinds_main : nullptr);
    fill(side, (int)ns, -1, g_balance ? kinds_side : nullptr);
    if (!g_pool[K_FUSION].empty()) fill(extra, (int)ne, K_FUSION, nullptr);
    A->log("Card Randomizer: deck %u randomized (%u main, %u side, %u extra)", seed_salt, nm, ns, ne);
}

/* libehsys_F11F0D9A (0883f4f8): commits the duel setup before the duel is loaded (a0 = setup block, 0x18c bytes per duelist). The deck reader above only
 * runs for the opponent (and the extra slots): duelist 0 is the player, whose saved deck the setup copies into the block (deck buffer at base + 0x1a4,
 * 0x108 bytes, layout of the reader's output; a second copy at the start of the block) without going through the reader. The copy exists by the time
 * the first deck is read, so the player's deck is randomized then (hk_read_deck, via g_setup_pending). Doing it later (duel engine start) crashes the game:
 * the card resources are already loaded for the old deck. The save itself is not touched. */
static bool g_setup_pending;
static int hk_prepare_duel(TfCpu *c) {
    (void)c;
    g_setup_pending = !g_off && g_player;   /* a0 is the request block in the story and the slot array in Free Duel: the slots are found from the reader's output instead */
    return 0;
}
static void randomize_player_slot(TfCpu *c, uint32_t out) {
    if (!g_setup_pending) return;   /* stays pending until a slot validates: the first reads can come before the other slots are filled (Tag Duel) */
    /* Tag Duel: the player's saved deck (slot deck index +0x124 >= 0x15f) sits in any of the four slots, not only slot 0; `out` is one of the slots'
     * buffers (the one being read), so every block alignment is tried. The copy at the start of the block equals the player's buffer. */
    for (int k = 0; k < 4; k++) {
        const uint32_t base = out - 0x1a4 - (uint32_t)k * 0x18c;
        if (base < 0x08000000u || base > 0x0a000000u - 0x4000) continue;
        for (int m = 0; m < 4; m++) {
            const uint32_t slot = base + (uint32_t)m * 0x18c;
            if (*(uint16_t *)A->mem(slot + 0x124) < 0x15f) continue;
            const uint32_t *h0 = (const uint32_t *)A->mem(base), *h1 = (const uint32_t *)A->mem(slot + 0x1a4);
            if (h1[0] < 1 || h1[0] > 80 || memcmp(h0, h1, 12)) continue;
            g_setup_pending = false;
            A->log("Card Randomizer: player's deck found at %08x (slot %d, %u cards)", slot + 0x1a4, m, h1[0]);
            randomize_buffer(c, slot + 0x1a4, 0x1000);
            memcpy(A->mem(base), A->mem(slot + 0x1a4), 0x108);
            return;
        }
    }
    for (int j = 1; j <= 4; j++) {   /* `out` is slot j's deck buffer (base + j * 0x18c + 0x1a4); the player's is slot 0 */
        const uint32_t base = out - 0x1a4 - (uint32_t)(j % 4) * 0x18c;
        if (base < 0x08000000u || base > 0x0a000000u - 0x300) continue;
        const uint32_t *h0 = (const uint32_t *)A->mem(base), *h1 = (const uint32_t *)A->mem(base + 0x1a4);
        if (h0[0] < 1 || h0[0] > 80 || memcmp(h0, h1, 12)) continue;
        g_setup_pending = false;
        A->log("Card Randomizer: player's deck found at %08x (%u cards)", base + 0x1a4, h1[0]);
        randomize_buffer(c, base + 0x1a4, 0x1000);
        memcpy(A->mem(base), A->mem(base + 0x1a4), 0x108);   /* the second copy at the start of the block stays equal */
        return;
    }
}

static int hk_read_deck(TfCpu *c) {   /* libehsys_85A292D7(rcp_index, out): the opponent's deck (the player's deck is handled by randomize_player_slot) */
    if (g_off) return 0;
    const uint32_t idx = A->reg(c, 4), out = A->reg(c, 5);
    A->call_original(c, F_READ_DECK);
    randomize_player_slot(c, out);
    if (out < 0x08000000u || out > 0x0a000000u - 0x108) return 1;
    const bool is_player = idx == 0 || idx >= 0x15f;
    if (is_player ? !g_player : !g_opponent) return 1;
    randomize_buffer(c, out, idx);
    return 1;
}

extern "C" __declspec(dllexport) int tfmod_init(const TfModApi *api) {
    A = api; g_off = getenv("TF_CARDRAND_OFF") != nullptr;
    cfg_load();
    A->hook(F_READ_DECK, hk_read_deck);
    A->hook(F_PREPARE_DUEL, hk_prepare_duel);
    A->log("Card Randomizer: player=%d opponent=%d balance=%d max_copies=%d seed=%u%s", g_player, g_opponent, g_balance, g_max_copies, g_seed, g_off ? " (OFF by TF_CARDRAND_OFF)" : "");
    return 0;
}
