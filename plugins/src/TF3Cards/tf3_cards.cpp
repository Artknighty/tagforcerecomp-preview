/* Mod "TF3 Cards" (plugin, API 3): cards ported from Yu-Gi-Oh! GX Tag Force 3.
 *
 * The card data (name, text, stats) comes from the mod's own PSP_GAME/USRDIR/duelsys/cardinfo_eng.ehp (made by tools/mods/tf3_porte_carta.py);
 * this plugin holds the behavior: ported cards borrow the effect record of an equivalent game card (ported.txt), plus a test aid: `force_copies` of card `force_card` replace the first cards of the
 * player's main deck, so the card shows up in the opening hand (the duel shuffles the deck, so use many copies). Settings: config.ini in the
 * mod folder (force_card=7234, force_copies=0 = off, force_deck_size=0 = keep the size; a deck of 8 cards with 3 copies always has the card in hand: more than 3
 * copies of one card hang the duel start); env TF3_FORCE_COPIES / TF3_FORCE_CARD / TF3_FORCE_DECK override them.
 *
 * Deck handling follows the Card Randomizer mod: the deck reader (0884a694) fills a 0x108 byte buffer {[0] = n_main, [1] = n_side,
 * [2] = n_extra, main at +0x0c (u16 ids)}; the player's deck is copied into the duel setup block (prepare duel, 0883f4f8) without going
 * through the reader, so it is found from the reader's output (see randomize_player_slot there). */
#include "tfmod.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

static const TfModApi *A;
static uint32_t g_pos;      /* TF3_FORCE_POS: first deck position of the forced cards (the duel start runs without shuffling when the deck is kept whole) */
static bool g_in_start;
static const uint32_t F_DUEL_START = 0x09eb14f4, F_RANDOM = 0x09eb30b8;
static const uint32_t F_READ_DECK = 0x0884a694, F_PREPARE_DUEL = 0x0883f4f8;
/* libduel_eng 09eb4574: finds the effect record (6 words {id, handlers...}, one per card with special rules) of the card whose key is at a0
 * (u16 id at +0, the record kind in the low 6 bits of the byte at +3); binary search in tables sorted by id, returns the record or 0. */
static const uint32_t F_EFFECT_RECORD = 0x09eb4574;
/* The ported cards (ported.txt in the mod folder, written by tools/mods/tf3_porte_carta.py): each plays under the id of a TF1 card (slot); `like` is the
 * TF1 card whose effect record it borrows (0 = no effect yet). */
struct Ported { uint16_t slot, tf3, like; std::string name; };
static std::vector<Ported> PORTED;
/* collection records, copy limits and the save format are per card index: see tf3_porte_carta.py */
static const uint32_t F_LP_AMOUNTS = 0x09ee3efc, F_FRAME = 0x08833fc4, F_COLLECTION_ENTRY = 0x0883de3c, SAVE = 0x08a65418, SAVE_VERSION = 0x10002;
static std::vector<int> g_cards;   /* force_cards=a,b,c: several cards, force_copies copies of each */
static int g_card = 7234, g_copies, g_deck_size, g_grant = 3, g_opponent;   /* g_card: a TF1 slot id or a TF3 id of ported.txt */
static bool g_setup_pending;

/* Cards beyond the 2480 indices of the game (tools/mods/tf3_porte_ext.py, ids from 6958 on): the save has no room for their collection records, so the
 * plugin keeps them in PSP memory (4 bytes per card like the game's: byte 0 bits 1-2 = status, byte 1 bits 0-6 = copies, byte 2 bits 0-1 = copy limit),
 * hands one out when the game asks for the record of such an id (BOOT 0883de3c) and keeps them in collection_ext.bin in the mod folder. */
static const uint32_t EXT_FIRST_ID = 6958;
static uint32_t g_ext, g_ext_n;
static std::vector<uint8_t> g_ext_saved;

static std::wstring ext_path(void) { return std::wstring(A->mod_dir) + L"\\collection_ext.bin"; }

static void ext_init(void) {
    for (const auto &p : PORTED)
        if (p.slot >= EXT_FIRST_ID && p.slot - EXT_FIRST_ID + 1 > g_ext_n) g_ext_n = p.slot - EXT_FIRST_ID + 1;
    if (!g_ext_n) return;
    g_ext = A->guest_alloc(4 * g_ext_n);
    uint8_t *r = A->mem(g_ext);
    for (uint32_t i = 0; i < g_ext_n; i++) { r[i * 4] = 0x02; r[i * 4 + 1] = 3; r[i * 4 + 2] = 3; r[i * 4 + 3] = 0; }   /* new, 3 copies, limit 3 */
    if (FILE *f = _wfopen(ext_path().c_str(), L"rb")) {
        std::vector<uint8_t> b(4 * g_ext_n);
        const size_t n = fread(b.data(), 1, b.size(), f);
        fclose(f);
        if (n == b.size()) memcpy(r, b.data(), b.size());
    }
    g_ext_saved.assign(r, r + 4 * g_ext_n);
    A->log("TF3 Cards: %u cards beyond the game's table (ids %u..%u), collection kept in collection_ext.bin", g_ext_n, EXT_FIRST_ID, EXT_FIRST_ID + g_ext_n - 1);
}

static void ext_save(void) {
    if (!g_ext_n) return;
    const uint8_t *r = A->mem(g_ext);
    if (memcmp(r, g_ext_saved.data(), g_ext_saved.size()) == 0) return;
    g_ext_saved.assign(r, r + 4 * g_ext_n);
    if (FILE *f = _wfopen(ext_path().c_str(), L"wb")) { fwrite(r, 1, 4 * g_ext_n, f); fclose(f); }
}

static int hk_collection_entry(TfCpu *c) {
    const uint32_t id = A->reg(c, 4) & 0xffff;
    if (id < EXT_FIRST_ID || id - EXT_FIRST_ID >= g_ext_n) return 0;
    A->set_reg(c, 2, g_ext + (id - EXT_FIRST_ID) * 4);
    return 1;
}

/* Deck editor trunk list (libdeck 08efcb68(list)): the game walks the ids 4007..6957 and writes one 6-byte record per owned card
 * {id, sort key, copies} plus an index array at +0x10; it returns the count. After it, add the same records for the ids beyond the table. */
static const uint32_t F_TRUNK_LIST = 0x08efcb68, F_DK_COPIES = 0x08f18d84, F_DK_VISIBLE = 0x08f1926c, F_DK_NEW = 0x08f18c4c,
                      F_DK_SORT_TABLE = 0x08f06498, F_DK_SORT_KEY = 0x08f0dd90, NEW_COUNTER = 0x09ccb168;

static int hk_trunk_list(TfCpu *c) {
    const uint32_t list = A->reg(c, 4), ra = A->reg(c, 31);
    A->call_original(c, F_TRUNK_LIST);
    uint32_t n = A->reg(c, 2) & 0xffff;
    for (uint32_t id = EXT_FIRST_ID; id < EXT_FIRST_ID + g_ext_n; id++) {
        A->set_reg(c, 4, id); A->call(c, F_DK_COPIES);
        const int32_t copies = (int32_t)A->reg(c, 2);
        A->set_reg(c, 4, id); A->call(c, F_DK_VISIBLE);
        if (A->reg(c, 2) && copies > 0) {
            A->set_reg(c, 4, 0x8031); A->call(c, F_DK_SORT_TABLE);
            A->set_reg(c, 5, A->reg(c, 2)); A->set_reg(c, 4, id); A->call(c, F_DK_SORT_KEY);
            const uint32_t key = A->reg(c, 2) & 0xffff;
            uint8_t *rec = A->mem(*(uint32_t *)A->mem(list + 4) + n * 6);
            *(uint16_t *)rec = (uint16_t)id; *(uint16_t *)(rec + 2) = (uint16_t)key;
            rec[4] = (uint8_t)(copies & 0x7f); rec[5] &= 0xfe;
            *(uint16_t *)A->mem(*(uint32_t *)A->mem(list + 16) + n * 2) = (uint16_t)n;
            n++;
        }
        A->set_reg(c, 4, id); A->call(c, F_DK_NEW);
        if ((int32_t)A->reg(c, 2) > 0) (*(uint16_t *)A->mem(NEW_COUNTER))++;
    }
    A->set_reg(c, 31, ra); A->set_reg(c, 2, n);
    return 1;
}

static void ported_load(void) {
    const std::wstring p = std::wstring(A->mod_dir) + L"\\ported.txt";
    if (FILE *f = _wfopen(p.c_str(), L"r")) {
        char l[256]; unsigned slot, tf3, like; int off;
        while (fgets(l, sizeof l, f)) {
            if (sscanf(l, "%u %u %u %n", &slot, &tf3, &like, &off) < 3) continue;
            std::string name = l + off;
            while (!name.empty() && (name.back() == '\n' || name.back() == '\r')) name.pop_back();
            PORTED.push_back({(uint16_t)slot, (uint16_t)tf3, (uint16_t)like, name});
        }
        fclose(f);
    }
}

static void cfg_load(void) {
    const std::wstring p = std::wstring(A->mod_dir) + L"\\config.ini";
    if (FILE *f = _wfopen(p.c_str(), L"r")) {
        char l[128]; int v;
        while (fgets(l, sizeof l, f)) {
            if (sscanf(l, "force_card=%d", &v) == 1) g_card = v;
            else if (sscanf(l, "force_copies=%d", &v) == 1) g_copies = v;
            else if (sscanf(l, "force_deck_size=%d", &v) == 1) g_deck_size = v;
            else if (sscanf(l, "grant_copies=%d", &v) == 1) g_grant = v;
            else if (strncmp(l, "force_cards=", 12) == 0) {
                g_cards.clear();
                for (const char *q = l + 12; *q;) { char *e; const long id = strtol(q, &e, 10); if (e == q) break; g_cards.push_back((int)id); q = *e ? e + 1 : e; }
            }
        }
        fclose(f);
    }
    if (const char *e = getenv("TF3_FORCE_COPIES")) g_copies = atoi(e);
    if (const char *e = getenv("TF3_FORCE_CARD")) g_card = atoi(e);
    if (const char *e = getenv("TF3_FORCE_OPP")) g_opponent = atoi(e);   /* 1: force the copies into the opponent's deck instead of yours */
    for (const auto &a : PORTED) if (a.tf3 == g_card) g_card = a.slot;   /* the TF3 id of a ported card is its slot's id in the deck */
    if (const char *e = getenv("TF3_FORCE_CARDS")) {
        g_cards.clear();
        for (const char *q = e; *q;) { char *x; const long id = strtol(q, &x, 10); if (x == q) break; g_cards.push_back((int)id); q = *x ? x + 1 : x; }
    }
    if (g_cards.empty()) g_cards.push_back(g_card);
    for (auto &id : g_cards) for (const auto &a : PORTED) if (a.tf3 == id) { id = a.slot; break; }
    if (const char *e = getenv("TF3_FORCE_POS")) g_pos = (uint32_t)atoi(e);
    if (const char *e = getenv("TF3_FORCE_DECK")) g_deck_size = atoi(e);
}

static void force_cards(uint32_t out) {
    uint8_t *o = A->mem(out);
    uint32_t nm = ((uint32_t *)o)[0];
    if (nm < 1 || nm > 80) return;
    uint16_t *main = (uint16_t *)(o + 0x0c);
    if (g_deck_size > 0 && (uint32_t)g_deck_size < nm) ((uint32_t *)o)[0] = nm = (uint32_t)g_deck_size;   /* a tiny deck puts the copies in the opening hand */
    uint32_t n = 0;
    for (const int id : g_cards)
        for (int k = 0; k < g_copies && n < nm; k++) main[n++] = (uint16_t)id;
    A->log("TF3 Cards: %u forced cards (%u kinds, %d copies each) put in the deck at %08x (%u cards)", n, (unsigned)g_cards.size(), g_copies, out, nm);
}

/* A whole deck with the forced cards in it: the duel start shuffles with the game random, so make that random return 0 while it runs (the order is
 * then fixed and the forced cards are placed where the opening hand takes them). The first-turn draw is the same call, so it also becomes fixed. */
static int hk_duel_start(TfCpu *c) {
    if (g_copies <= 0 || g_deck_size > 0) return 0;
    g_in_start = true; A->call_original(c, F_DUEL_START); g_in_start = false;
    return 1;
}
static int hk_random(TfCpu *c) {
    if (!g_in_start) return 0;
    A->set_reg(c, 2, 0);
    return 1;
}

static int hk_prepare_duel(TfCpu *c) {
    (void)c;
    g_setup_pending = g_copies > 0;
    return 0;
}

/* the player's deck sits in one of the setup block's slots (deck index at slot + 0x124 >= 0x15f): try every block alignment */
static void force_player_slot(uint32_t out) {
    if (!g_setup_pending) return;
    for (int k = 0; k < 4; k++) {
        const uint32_t base = out - 0x1a4 - (uint32_t)k * 0x18c;
        if (base < 0x08000000u || base > 0x0a000000u - 0x4000) continue;
        for (int m = 0; m < 4; m++) {
            const uint32_t slot = base + (uint32_t)m * 0x18c;
            if (*(uint16_t *)A->mem(slot + 0x124) < 0x15f) continue;
            const uint32_t *h0 = (const uint32_t *)A->mem(base), *h1 = (const uint32_t *)A->mem(slot + 0x1a4);
            if (h1[0] < 1 || h1[0] > 80 || memcmp(h0, h1, 12)) continue;
            g_setup_pending = false;
            force_cards(slot + 0x1a4);
            memcpy(A->mem(base), A->mem(slot + 0x1a4), 0x108);   /* the copy at the start of the block stays equal */
            return;
        }
    }
    for (int j = 1; j <= 4; j++) {   /* `out` is slot j's deck buffer; the player's is slot 0 */
        const uint32_t base = out - 0x1a4 - (uint32_t)(j % 4) * 0x18c;
        if (base < 0x08000000u || base > 0x0a000000u - 0x300) continue;
        const uint32_t *h0 = (const uint32_t *)A->mem(base), *h1 = (const uint32_t *)A->mem(base + 0x1a4);
        if (h0[0] < 1 || h0[0] > 80 || memcmp(h0, h1, 12)) continue;
        g_setup_pending = false;
        force_cards(base + 0x1a4);
        memcpy(A->mem(base), A->mem(base + 0x1a4), 0x108);
        return;
    }
}

static int hk_read_deck(TfCpu *c) {   /* libehsys_85A292D7(rcp_index, out): observe only, the original still runs */
    if (g_copies <= 0) return 0;
    const uint32_t out = A->reg(c, 5);
    A->call_original(c, F_READ_DECK);
    if (g_opponent) force_cards(out);   /* test aid: the decks the reader fills belong to the opponent */
    else force_player_slot(out);
    return 1;
}

/* collection record of a card id (BOOT 0883de3c: SAVE + 0x155c + index * 4; byte 0 bits 1-2 = status 0 unseen / 1 new / 2 seen, byte 1 bits 0-6 = copies) */
static uint32_t collection_entry(TfCpu *c, uint32_t id) {
    uint32_t s[32]; for (int i = 1; i < 32; i++) s[i] = A->reg(c, i);
    A->set_reg(c, 4, id); A->call(c, F_COLLECTION_ENTRY); const uint32_t r = A->reg(c, 2);
    for (int i = 1; i < 32; i++) if (i != 26 && i != 27) A->set_reg(c, i, s[i]);
    return r;
}

/* grant_copies: a ported card the player does not own yet is added to the collection (marked "new") so it shows up in the trunk. The game reloads
 * the save when Continue is pressed, so this runs every half second once a save is in memory; it only touches cards with 0 copies. */
static int hk_frame(TfCpu *c) {
    static uint64_t next, ext_next;
    if (g_ext_n && A->frame() >= ext_next) { ext_next = A->frame() + 60; ext_save(); }
    if (g_grant <= 0 || A->frame() < next) return 0;
    next = A->frame() + 30;
    if (*(uint32_t *)A->mem(SAVE) != SAVE_VERSION) return 0;
    for (const auto &pc : PORTED) {
        const uint16_t id = pc.slot;
        if (id >= EXT_FIRST_ID) continue;                  /* the plugin owns those records */
        const uint32_t e = collection_entry(c, id);
        if (!e) continue;
        uint8_t &flags = *A->mem(e), &copies = *A->mem(e + 1);
        if (copies & 0x7f) continue;
        copies = (uint8_t)((copies & 0x80) | (g_grant > 3 ? 3 : g_grant));
        if (((flags >> 1) & 3) == 0) flags = (uint8_t)((flags & 0xf9) | (1 << 1));
        A->log("TF3 Cards: granted %d copies of card %u", g_grant, id);
    }
    return 0;
}

/* libduel_eng 09ee3efc: the life point change of the life point cards, by card id. a0 = the effect object (u16 id at +0, byte +2 bit 0 = side, byte +3 bit 1 = swapped
 * side), a1 = out int[2] (LP delta per player index; the caller damages the negative ones and heals the positive ones). Ookazi writes -800 to the opponent's index. */
static const uint16_t TF3_GIFT_CARD = 7234;
static int hk_lp_amounts(TfCpu *c) {
    const uint32_t obj = A->reg(c, 4), out = A->reg(c, 5);
    if (obj < 0x08000000u || obj > 0x0a000000u - 0x20 || out < 0x08000000u || out > 0x0a000000u - 8) return 0;
    const uint8_t *o = A->mem(obj);
    static const bool debug = getenv("TF3_DEBUG_LP") != nullptr;   /* research: shows how the game fills the amounts for its own cards */
    if (debug) {
        const uint16_t id = *(const uint16_t *)o;
        A->call_original(c, F_LP_AMOUNTS);
        const int32_t *d = (const int32_t *)A->mem(out);
        A->log("LP amounts: card %u byte2 %02x byte3 %02x -> player0 %d, player1 %d", id, o[2], o[3], d[0], d[1]);
        return 1;
    }
    for (const auto &p : PORTED) {
        if (p.tf3 != TF3_GIFT_CARD || *(const uint16_t *)o != p.slot) continue;
        const uint32_t side = (uint32_t)((o[2] & 1) ^ ((o[3] >> 6) & 1));
        int32_t *d = (int32_t *)A->mem(out);
        d[0] = d[1] = 0;
        d[1 - side] = 3000;                                  /* Gift Card: the opponent gains 3000 Life Points */
        return 1;
    }
    return 0;
}

/* Suit of Sword X (TF3 3734): "toss a coin; heads: destroy all monsters your opponent controls; tails: destroy all monsters you control".
 * Built from what the game already has: it borrows the record of Dark Hole (driver 09ee3b7c walks every monster zone and asks the target filter 09ebbbe8 of the
 * record whether the monster is hit, then destroys the chosen ones). The driver hook tosses the coin with the game's own random (09eb30b8) and shows the coin
 * (animation command 09e998a0, as Fiend Comedian does); the filter hook then narrows Dark Hole's "every monster" to one side while that effect resolves. */
static const uint16_t TF3_SUIT_OF_SWORD_X = 3734;
static const uint32_t F_EFFECT_DRIVER = 0x09ee3b7c, F_TARGET_ALL = 0x09ebbbe8, F_COIN_ANIM = 0x09e998a0;
static int g_coin = -1;                                       /* -1 = not resolving; 1 = heads (opponent's side), 0 = tails (own side) */

static uint16_t slot_of(uint16_t tf3) { for (const auto &p : PORTED) if (p.tf3 == tf3) return p.slot; return 0; }

static uint32_t call_game(TfCpu *c, uint32_t fn, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) {   /* keeps every caller register */
    uint32_t s[32]; for (int i = 1; i < 32; i++) s[i] = A->reg(c, i);
    A->set_reg(c, 4, a0); A->set_reg(c, 5, a1); A->set_reg(c, 6, a2); A->set_reg(c, 7, a3); A->call(c, fn);
    const uint32_t r = A->reg(c, 2);
    for (int i = 1; i < 32; i++) if (i != 26 && i != 27) A->set_reg(c, i, s[i]);
    return r;
}

static bool is_card(uint32_t obj, uint16_t tf3) {
    if (obj < 0x08000000u || obj > 0x0a000000u - 0x20) return false;
    const uint16_t slot = slot_of(tf3);
    return slot && *(const uint16_t *)A->mem(obj) == slot;
}

static int hk_effect_driver(TfCpu *c) {
    const uint32_t obj = A->reg(c, 4);
    if (!is_card(obj, TF3_SUIT_OF_SWORD_X)) return 0;
    const uint8_t b2 = A->mem(obj)[2];
    static const char *force = getenv("TF3_FORCE_COIN");   /* test aid: 0 = tails, 1 = heads */
    g_coin = force ? atoi(force) & 1 : (int)(call_game(c, F_RANDOM, 2, 0, 0, 0) & 1);
    call_game(c, F_COIN_ANIM, b2 & 1, (b2 >> 1) & 31, 1, (uint32_t)g_coin);
    A->log("Suit of Sword X: coin %s", g_coin ? "heads" : "tails");
    A->call_original(c, F_EFFECT_DRIVER);
    g_coin = -1;
    return 1;
}

static int hk_target_all(TfCpu *c) {   /* a0 = effect object, a1 = side, a2 = zone; false for the monsters the coin spares */
    if (!is_card(A->reg(c, 4), TF3_SUIT_OF_SWORD_X)) return 0;
    if (g_coin < 0) { A->set_reg(c, 2, 1); return 1; }         /* the activation check: the card can always be played, the coin decides afterwards */
    const uint32_t own = A->mem(A->reg(c, 4))[2] & 1, side = A->reg(c, 5) & 1;
    if ((g_coin == 0) != (side == own)) { A->set_reg(c, 2, 0); return 1; }
    return 0;
}

static int hk_effect_record(TfCpu *c) {   /* looks the record up as the card it plays like, then puts the key back */
    const uint32_t p = A->reg(c, 4);
    if (p < 0x08000000u || p > 0x0a000000u - 8) return 0;
    uint16_t *key = (uint16_t *)A->mem(p);
    for (const auto &a : PORTED) {
        if (*key != a.slot || !a.like) continue;
        *key = a.like;
        A->call_original(c, F_EFFECT_RECORD);
        *key = a.slot;
        return 1;
    }
    return 0;
}

/* research aid (TF3_DEBUG_STREAM=1): dumps the card-art stream object when the game queues a read far beyond the file (BOOT 0882ead0:
 * a1 = stream object, a3 = file position; the object keeps the .cip header at +172, the index entries {unit, state} follow at +0x30) */
static int hk_debug_stream(TfCpu *c) {
    static int n;
    const uint32_t obj = A->reg(c, 5), pos = A->reg(c, 7);
    if (n >= 1 || obj < 0x08000000u || obj > 0x0a000000u - 0x8000) return 0;
    const uint32_t magic = *(const uint32_t *)A->mem(obj + 172);
    if ((magic != 0x1a4d5043u && magic != 0x1a504943u) || pos < 0x10000000u) return 0;   /* "CPM" / "CIP", garbage position */
    n++;
    A->log("stream dump: obj %08x pos %08x", obj, pos);
    const uint32_t *w = (const uint32_t *)A->mem(obj);
    for (int i = 0; i < 56; i += 4) A->log("  obj+%03x: %08x %08x %08x %08x", i * 4, w[i], w[i + 1], w[i + 2], w[i + 3]);
    const uint32_t *h = (const uint32_t *)A->mem(obj + 172);
    A->log("  header: size=%08x end_index=%08x lo=%u hi=%u idx_off=%08x chunk=%u", h[1], h[4], h[5], h[6], h[7], h[8]);
    const uint32_t *e = (const uint32_t *)A->mem(obj + 172 + h[7]);
    const uint32_t end = (h[4] - h[7]) / 8 + 40;
    for (uint32_t i = 0; i < end; i++) {
        if (e[i * 2 + 1] != 0 || e[i * 2] == 0xffffffffu || i > (h[4] - h[7]) / 8 - 6)
            A->log("  entry %u (+%x): unit %08x state %08x", i, h[7] + i * 8, e[i * 2], e[i * 2 + 1]);
    }
    return 0;
}

/* research aid (TF3_DEBUG_REQ=1): logs every card-art read request (stream object, magic of its .cip header, file position, size) */
static int hk_debug_req(TfCpu *c) {
    static int n;
    const uint32_t obj = A->reg(c, 5), pos = A->reg(c, 7);
    if (n >= 600 || obj < 0x08000000u || obj > 0x0a000000u - 0x8000) return 0;
    const uint32_t magic = *(const uint32_t *)A->mem(obj + 172);
    if (magic != 0x1a4d5043u && magic != 0x1a504943u) return 0;
    n++;
    A->log("art request: %s pos %08x (unit %u) a4=%08x a6=%08x", magic == 0x1a4d5043u ? "CPM" : "CIP", pos, pos / 0x800, A->reg(c, 8), A->reg(c, 6));
    return 0;
}

extern "C" __declspec(dllexport) int tfmod_init(const TfModApi *api) {
    A = api;
    ported_load();
    ext_init();
    cfg_load();
    A->hook(F_READ_DECK, hk_read_deck);
    A->hook(F_PREPARE_DUEL, hk_prepare_duel);
    A->hook(F_EFFECT_RECORD, hk_effect_record);
    A->hook(F_FRAME, hk_frame);
    A->hook(F_COLLECTION_ENTRY, hk_collection_entry);
    A->hook(F_DUEL_START, hk_duel_start);
    A->hook(F_RANDOM, hk_random);
    if (g_ext_n) A->hook(F_TRUNK_LIST, hk_trunk_list);
    A->hook(F_LP_AMOUNTS, hk_lp_amounts);
    A->hook(F_EFFECT_DRIVER, hk_effect_driver);
    A->hook(F_TARGET_ALL, hk_target_all);
    if (getenv("TF3_DEBUG_STREAM")) A->hook(0x0882ead0, hk_debug_stream);
    if (getenv("TF3_DEBUG_REQ")) A->hook(0x0882ead0, hk_debug_req);
    for (const auto &a : PORTED) A->log("TF3 Cards: card %u (%s) plays as id %u%s", a.tf3, a.name.c_str(), a.slot, a.like ? ", effect of another card" : "");
    A->log("TF3 Cards: force_card=%d force_copies=%d", g_card, g_copies);
    return 0;
}
