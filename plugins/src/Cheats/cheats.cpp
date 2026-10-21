/* Mod "Cheats" (plugin, API 4 in plugins/sdk/tfmod.h): optional cheats, for testing the game (e.g. watching what the CPU holds) and for
 * players who want them. Each cheat is switched on/off in the window's Cheats menu (some also by key) and the state is kept in config.ini in the mod folder.
 * To add a cheat: a section like the ones below (state + cfg + hook/frame) and its registration in tfmod_init.
 *
 * Cheats:
 *   Everything lives in the menu bar's Cheats menu (use_menu, API 4), separate from the Mods menu.
 *   1. See the opponent's cards (F6): a corner panel with the CPU's hand and its FACE-DOWN cards on the field, one row per card
 *      in the frame color (normal, effect, fusion, ritual, spell, trap), with attribute, level and ATK/DEF (or the spell/trap type).
 *      With "CPU vs CPU" it shows both sides. It only reads memory (the duel is not changed): replays stay identical.
 *      Memory map (see docs/AI.md): player p at 0x09f79cec + p*0xfac (LP +0, hand counter at +0x0c, hand at +0x120 + i*4 (id&0x1fff));
 *      slots of 0x14 bytes at 0x09f79d1c + p*0xfac + i*0x14 (0..4 monsters, 5..9 spell/trap, 10 field): +0 id, +0x10 bits (0x80 = face
 *      up, 0x08 = defense); 0x09f77888[p] == 1 = the CPU plays that side. BOOT: card_name 0884b46c, card_prop 0884b4f8 (address of the
 *      card's 8 bytes: ATK/DEF, category, attribute, level, icon) - pure queries; the result is cached per card.
 *   2. DP multiplier (1x to 10x; it used to be the "DP multiplier" mod, migrated here with the saved value): multiplies the DP earned at the end of
 *      each duel. Set it in the Cheats menu or, in game, by holding L + R and pressing UP / DOWN.
 *      libduel_mgr 08f08534 duel_reward_calculate: lets the original run, measures the gain (player_dp before/after, BOOT 0883d908), adds
 *      gain x (N-1) with player_dp_add (0883d938, respects the cap) and multiplies the 4 parts of the context (09a151a4: +08..+14) that the
 *      result screen draws; the detail screens draw with number_draw_decimal (08f0d7f4), multiplied only on the DP rows (by the return address).
 *   3. Fast-forward (1x to 8x; API 6 set_speed): the game runs faster, the music is time-stretched in the runtime (same pitch). Set it in the Cheats menu
 *      or, in game, by holding L + R and pressing LEFT / RIGHT. It always starts at 1x (it is not saved, so a forgotten setting never surprises).
 *   4. Unlock everything (Cheats menu, one-shot): edits the save the game keeps in RAM (BOOT save_estado 0883d698, the same 0xFCA8-byte block
 *      the game writes to DATA0 on its next save). It adds every card to the collection (3 copies, marked as seen), makes every card pack
 *      available in the shop and gives every duelist all of his real decks in the Free Duel opponent list. Nothing else (story progress, DP,
 *      level and the save flags) is touched. Save layout (BOOT functions save_*, carta_*, pacote_*, and decomp/libtitle):
 *        +0x0068 + character*0x12: duelist record (u16 state, u16 count, u16 deck[6] unlocked decks, u16 ...); characters 2..151
 *                                  (the Free Duel opponent list shows the characters whose count is not zero: libcharalist BuildLists)
 *        +0x0EC4 + character*8:    duel record against that duelist (u16 wins, u16 losses, u16 draws, u16 pad); not touched
 *        +0x155C + card_index*4:   card entry (byte0 bit0 flag, bits 1-2 status 0 unseen / 1 new / 2 seen, byte1 bit7 flag + bits 0-6 copies)
 *        +0x8FF4 + pack*0x2A:      pack (byte0 shop value, byte1 bits 0-1 state: 0 locked, 1 seen, 2 available), packs 0..47
 *   BOOT 08833fc4 ehsys_end_of_frame (every logical frame): panel, L + R + UP/DOWN and the pending unlock. */
#include "tfmod.h"
#include "../../../decomp/common/duelists.h"
#include <windows.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

static const TfModApi *A;
static const uint32_t F_QUADRO = 0x08833fc4;
static uint32_t &u32(uint32_t a) { return *(uint32_t *)A->mem(a); }
static int32_t &i32(uint32_t a) { return *(int32_t *)A->mem(a); }
static uint16_t u16(uint32_t a) { return *(uint16_t *)A->mem(a); }

/* calls a game function preserving ALL the registers of whatever was hooked (a0-a3 and t0-t3 may be its arguments; v0 the original's
 * result, when the hook wraps it; the other temporaries are cheap to save). k0/k1 do not change (same thread). */
static uint32_t chama(TfCpu *c, uint32_t fn, uint32_t a0) {
    uint32_t s[32]; for (int i = 1; i < 32; i++) s[i] = A->reg(c, i);
    A->set_reg(c, 4, a0); A->call(c, fn); const uint32_t r = A->reg(c, 2);
    for (int i = 1; i < 32; i++) if (i != 26 && i != 27) A->set_reg(c, i, s[i]);
    return r;
}

/* ------------------------------------------------------------------ configuration (config.ini in the mod folder) */
static int g_ver = 0;        /* 1. see the opponent's cards */
static int g_canto = 1;      /*    panel corner: 0 top left, 1 top right, 2 bottom left, 3 bottom right */
static int g_mult = 1;       /* 2. DP multiplier */
static int g_unlock_auto = 0; /* 3. unlock everything by itself once per start, when the save is loaded (set in the launcher) */
static const int MULT_MAX = 10;
static int g_speed = 1;      /* 3. fast-forward (not saved) */
static const int SPEED_MAX = 8;
static std::wstring cfg_path(void) { return std::wstring(A->mod_dir) + L"\\config.ini"; }
static void cfg_save(void) {
    if (FILE *f = _wfopen(cfg_path().c_str(), L"w")) { fprintf(f, "show_opponent_cards=%d\npanel_corner=%d\ndp_multiplier=%d\nunlock_on_start=%d\n", g_ver, g_canto, g_mult, g_unlock_auto); fclose(f); }
}
static void cfg_load(void) {
    bool tem_mult = false;
    if (FILE *f = _wfopen(cfg_path().c_str(), L"r")) {
        char l[96]; int v;
        while (fgets(l, sizeof l, f)) {
            /* (the old Portuguese key names are still read, so existing config files keep working) */
            if (sscanf(l, "show_opponent_cards=%d", &v) == 1 || sscanf(l, "ver_cartas_adversario=%d", &v) == 1) g_ver = v != 0;
            else if (sscanf(l, "unlock_on_start=%d", &v) == 1) g_unlock_auto = v != 0;
            else if (sscanf(l, "panel_corner=%d", &v) == 1 || sscanf(l, "canto_painel=%d", &v) == 1) g_canto = std::max(0, std::min(3, v));
            else if (sscanf(l, "dp_multiplier=%d", &v) == 1 || sscanf(l, "multiplicador_dp=%d", &v) == 1) { g_mult = std::max(1, std::min(MULT_MAX, v)); tem_mult = true; }
        }
        fclose(f);
    }
    if (!tem_mult) {   /* migration from the old "DP multiplier" mod (mods\DPMultiplicador\config.ini: multiplicador=N) */
        if (FILE *f = _wfopen((std::wstring(A->mod_dir) + L"\\..\\DPMultiplicador\\config.ini").c_str(), L"r")) {
            char l[64]; int v; while (fgets(l, sizeof l, f)) if (sscanf(l, "multiplicador=%d", &v) == 1) g_mult = std::max(1, std::min(MULT_MAX, v));
            fclose(f); A->log("multiplicador de DP trazido do mod antigo DPMultiplicador: %dx", g_mult); cfg_save();
        }
    }
    if (const char *e = getenv("TF_CHEAT_VER")) g_ver = atoi(e) != 0;                              /* tests: override the file */
    if (const char *e = getenv("TF_DPMUL")) g_mult = std::max(1, std::min(MULT_MAX, atoi(e)));
}

/* ------------------------------------------------------------------ 1. see the opponent's cards */
static const uint32_t JOG = 0x09f79cec, PASSO = 0xfac, CASAS = 0x09f79d1c;
static const uint32_t F_NOME = 0x0884b46c, F_PROP = 0x0884b4f8;
/* F_PROP: id -> endereco dos 8 bytes da carta em CARD_Prop (layout em tools/game/cartas.py): w0 bits 13-21 ATK/10 e 22-30 DEF/10 (0x1ff = "?");
 * w1 bits 0-3 categoria (0 normal, 1 efeito, 2/3 fusao, 4/5 ritual, 6-8 toon/espirito/uniao, 9 ficha, 13 magia, 14 armadilha),
 * 4-7 atributo (1 luz, 2 trevas, 3 agua, 4 fogo, 5 terra, 6 vento, 8 magia, 9 armadilha), 8-11 nivel, 12-14 icone de magia/armadilha */
struct InfoCarta { std::wstring nome; int cat = 0, atrib = 0, nivel = 0, icone = 0, atk = -1, def = -1; };   /* atk/def -1 = "?" */
static const InfoCarta &info(TfCpu *c, uint32_t id) {   /* consulta ao banco de cartas do jogo, uma vez por carta */
    static std::map<uint32_t, InfoCarta> cache;
    auto it = cache.find(id); if (it != cache.end()) return it->second;
    InfoCarta in;
    if (const uint32_t p = chama(c, F_NOME, id)) for (int i = 0; i < 48; i++) { const uint16_t ch = u16(p + 2 * i); if (!ch) break; in.nome += (ch == '\n' || ch == '\r') ? L' ' : (wchar_t)ch; }
    if (in.nome.empty()) { wchar_t b[16]; swprintf(b, 16, L"#%u", id); in.nome = b; }
    const uint32_t p = chama(c, F_PROP, id);
    if (p >= 0x08800000 && p < 0x0a000000) {
        const uint32_t w0 = u32(p), w1 = u32(p + 4), a = (w0 >> 13) & 0x1ff, d = (w0 >> 22) & 0x1ff;
        in.cat = w1 & 15; in.atrib = (w1 >> 4) & 15; in.nivel = (w1 >> 8) & 15; in.icone = (w1 >> 12) & 7;
        in.atk = a == 0x1ff ? -1 : (int)a * 10; in.def = d == 0x1ff ? -1 : (int)d * 10;
    }
    return cache[id] = in;
}
/* linha de carta na marcacao do painel (plugins/sdk/tfmod.h): "@" moldura atributo {selo} nome \t colunas */
static std::wstring linha_carta(TfCpu *c, uint32_t id, const wchar_t *selo) {
    const InfoCarta &in = info(c, id);
    const bool magia = in.cat == 13 || in.atrib == 8, armadilha = in.cat == 14 || in.atrib == 9;
    std::wstring s = L"@";
    if (magia || armadilha) s += magia ? L"ss" : L"tt";
    else {
        static const wchar_t MOLD[] = L"neffrreeek", ATR[] = L"-ldwfevg";
        s += in.cat < 10 ? MOLD[in.cat] : L'e'; s += in.atrib < 8 ? ATR[in.atrib] : L'-';
    }
    if (selo) { s += L'{'; s += selo; s += L'}'; }
    s += in.nome;
    if (magia || armadilha) {
        static const wchar_t *const ICONE[8] = {L"", L"Counter ", L"Field ", L"Equip ", L"Continuous ", L"Quick-Play ", L"Ritual ", L""};
        s += L'\t'; s += ICONE[in.icone]; s += magia ? L"Spell" : L"Trap";
    } else {
        wchar_t b[64], a[8], d[8];
        if (in.atk < 0) wcscpy(a, L"?"); else swprintf(a, 8, L"%d", in.atk);
        if (in.def < 0) wcscpy(d, L"?"); else swprintf(d, 8, L"%d", in.def);
        swprintf(b, 64, L"\t*%d\tATK|%ls\tDEF|%ls", in.nivel, a, d); s += b;
    }
    return s;
}
static void painel_lado(TfCpu *c, int p, int canto, const wchar_t *quem) {
    std::vector<std::wstring> l, viradas; wchar_t b[64];
    const uint32_t base = JOG + (uint32_t)p * PASSO, nmao = std::min<uint32_t>(u32(base + 0x0c), 12);
    for (int s = 0; s < 10; s++) {   /* viradas para baixo no campo (as viradas para cima o jogo ja mostra) */
        const uint32_t cs = CASAS + (uint32_t)p * PASSO + (uint32_t)s * 0x14, id = u16(cs) & 0x1fff;
        if (!id || (u32(cs + 0x10) & 0x80)) continue;
        swprintf(b, 64, L"%ls%d", s < 5 ? L"M" : L"S", s < 5 ? s + 1 : s - 4); viradas.push_back(linha_carta(c, id, b));
    }
    swprintf(b, 64, L"#Hand\t%u", nmao); l.push_back(b);
    for (uint32_t i = 0; i < nmao; i++) { const uint32_t id = u16(base + 0x120 + i * 4) & 0x1fff; if (id) l.push_back(linha_carta(c, id, nullptr)); }
    swprintf(b, 64, L"#Face-down\t%u", (unsigned)viradas.size()); l.push_back(b);
    l.insert(l.end(), viradas.begin(), viradas.end());
    std::vector<const wchar_t *> lp; for (auto &x : l) lp.push_back(x.c_str());
    swprintf(b, 64, L"@e%ls\tLP %d", quem, (int)i32(base));
    A->panel(canto, b, lp.data(), (int)lp.size());
}
static void ver_cartas_quadro(TfCpu *c) {
    if (!g_ver || !A->module_loaded("libduel_eng")) return;
    const int32_t lp0 = i32(JOG), lp1 = i32(JOG + PASSO);
    if (lp0 <= 0 && lp1 <= 0) return;   /* fora de um duelo em andamento */
    const bool cpu0 = u32(0x09f77888) == 1, cpu1 = u32(0x09f77888 + 4) == 1;
    if (cpu0 && cpu1) { painel_lado(c, 0, (g_canto & 1) ? g_canto - 1 : g_canto, L"CPU 1"); painel_lado(c, 1, g_canto | 1, L"CPU 2"); }   /* CPU contra CPU: os dois lados */
    else if (cpu1) painel_lado(c, 1, g_canto, L"Opponent");
    else if (cpu0) painel_lado(c, 0, g_canto, L"Opponent");
}
static int ver_get(void) { return g_ver; }
static void ver_set(int v) { g_ver = v != 0; cfg_save(); A->log("ver as cartas do adversario: %s", g_ver ? "LIGADO" : "desligado"); }
static int canto_get(void) { return g_canto; }
static void canto_set(int v) { g_canto = std::max(0, std::min(3, v)); cfg_save(); }

/* ------------------------------------------------------------------ 2. DP multiplier */
static const uint32_t F_RECOMPENSA = 0x08f08534, F_DP = 0x0883d908, F_DP_SOMAR = 0x0883d938, P_CTX = 0x09a151a4, F_NUMERO = 0x08f0d7f4;
static const uint32_t BTN_UP = 0x10, BTN_DOWN = 0x40, BTN_L = 0x100, BTN_R = 0x200, BTN_LEFT = 0x80, BTN_RIGHT = 0x20;
static uint64_t g_mult_show_until; static uint32_t g_prev_btn;
static int mult_get(void) { return g_mult; }
static void mult_set(int v) { g_mult = std::max(1, std::min(MULT_MAX, v)); cfg_save(); A->log("multiplicador de DP = %dx (menu Mods)", g_mult); }
static int hk_recompensa(TfCpu *c) {   /* duelo_recompensa_calcular */
    if (g_mult <= 1 || !A->module_loaded("libduel_mgr")) return 0;
    const int32_t antes = (int32_t)chama(c, F_DP, 0);
    A->call_original(c, F_RECOMPENSA);
    const int32_t ganho = (int32_t)chama(c, F_DP, 0) - antes;
    if (ganho <= 0) { A->log("duelo sem ganho de DP (%d): nada a multiplicar", ganho); return 1; }
    const int64_t extra = (int64_t)ganho * (g_mult - 1);
    chama(c, F_DP_SOMAR, (uint32_t)std::min<int64_t>(extra, 999999999));
    if (const uint32_t ctx = u32(P_CTX)) for (int i = 0; i < 4; i++) {   /* linhas e total da tela de resultado leem estes campos */
        int32_t &f = i32(ctx + 8 + 4 * i);
        f = (int32_t)std::max<int64_t>(-999999999, std::min<int64_t>(999999999, (int64_t)f * g_mult));
    }
    A->log("DP do duelo: +%d x%d = +%lld DP", ganho, g_mult, (long long)ganho * g_mult);
    return 1;
}
static int hk_numero(TfCpu *c) {   /* linhas de DP das telas de detalhe do resultado */
    if (g_mult <= 1 || !A->module_loaded("libduel_mgr")) return 0;
    const uint32_t ra = A->reg(c, 31), v = A->reg(c, 7), t0 = A->reg(c, 8), dig = A->reg(c, 9);
    bool escala = false;
    if (ra >= 0x08f0ba90 && ra < 0x08f0bd0c) escala = true;                                                   /* FUN_08f0ba90: linhas da tabela de bonus (valor variavel) */
    else if (ra >= 0x08f0c50c && ra < 0x08f0c764) escala = v == 0x1e;                                         /* FUN_08f0c50c: 30 DP por item */
    else if (ra >= 0x08f0bf20 && ra < 0x08f0c350) escala = v == 0x14 && t0 == 9 && dig == 3;                  /* FUN_08f0bf20: 20 DP por item (as outras chamadas dela mostram contagens) */
    if (!escala) return 0;
    const uint32_t novo = std::min<uint32_t>(9999, v * (uint32_t)g_mult), preciso = novo > 999 ? 4 : novo > 99 ? 3 : 2;
    A->set_reg(c, 7, novo); A->set_reg(c, 9, std::max(dig, preciso));
    return 0;
}
static void mult_quadro(void) {   /* L + R + CIMA/BAIXO em qualquer tela */
    const uint32_t b = A->buttons(), novo = b & ~g_prev_btn; g_prev_btn = b;
    if ((b & (BTN_L | BTN_R)) == (BTN_L | BTN_R) && (novo & (BTN_UP | BTN_DOWN))) {
        const int antes = g_mult;
        g_mult = std::max(1, std::min(MULT_MAX, g_mult + ((novo & BTN_UP) ? 1 : -1)));
        if (g_mult != antes) { cfg_save(); A->log("multiplicador de DP = %dx", g_mult); }
        g_mult_show_until = A->frame() + 150;
    }
    if (A->frame() < g_mult_show_until) {
        wchar_t l1[64], l2[16];
        if (g_mult == 1) swprintf(l1, 64, L"DP earned in duels: 1x (default)");
        else swprintf(l1, 64, L"DP earned in duels: %dx", g_mult);
        swprintf(l2, 16, L"=%d/%d", g_mult, MULT_MAX);   /* barra de N de M (marcacao da caixa) */
        const wchar_t *lines[2] = {l1, l2};
        A->message_box(L"DP multiplier", lines, 2, L"L + R + Up / Down adjusts");
    }
}

/* ------------------------------------------------------------------ 3. Fast-forward */
static uint64_t g_speed_show_until;
static int speed_get(void) { return g_speed; }
static void speed_set(int v) { g_speed = std::max(1, std::min(SPEED_MAX, v)); A->set_speed(g_speed); A->log("fast-forward = %dx", g_speed); }
static void speed_quadro(void) {   /* L + R + LEFT/RIGHT on any screen */
    static uint32_t prev;
    const uint32_t b = A->buttons(), novo = b & ~prev; prev = b;
    if ((b & (BTN_L | BTN_R)) == (BTN_L | BTN_R) && (novo & (BTN_LEFT | BTN_RIGHT))) {
        speed_set(g_speed + ((novo & BTN_RIGHT) ? 1 : -1));
        g_speed_show_until = A->frame() + 150;
    }
    if (A->frame() < g_speed_show_until) {
        wchar_t l1[64], l2[16];
        if (g_speed == 1) swprintf(l1, 64, L"Game speed: 1x (normal)"); else swprintf(l1, 64, L"Game speed: %dx", g_speed);
        swprintf(l2, 16, L"=%d/%d", g_speed, SPEED_MAX);
        const wchar_t *lines[2] = {l1, l2};
        A->message_box(L"Fast-forward", lines, 2, L"L + R + Left / Right adjusts");
    }
}

/* ------------------------------------------------------------------ 4. Unlock everything */
static const uint32_t SAVE = 0x08a65418, SAVE_VERSION = 0x10002, F_COLECAO_ENTRADA = 0x0883de3c;
static const uint32_t SAVE_DUELIST_RECORDS = 0x68, DUELIST_RECORD_SIZE = 0x12, DUELIST_RECORD_DECKS = 6;
static const uint32_t SAVE_PACKS = 0x8ff4, PACK_SIZE = 0x2a, PACK_COUNT = 0x30, PACK_AVAILABLE = 2;
static const uint32_t CARD_ID_LAST = 0x2000, CARD_COPIES = 3, CARD_STATUS_SEEN = 2;
static bool g_unlock_pending;
static bool save_loaded(void) { return u32(SAVE) == SAVE_VERSION; }
static int unlock_cards(TfCpu *c) {
    int n = 0;
    for (uint32_t id = 1; id < CARD_ID_LAST; id++) {
        const uint32_t entry = chama(c, F_COLECAO_ENTRADA, id);   /* 0 for ids that are not collectable cards */
        if (!entry) continue;
        uint8_t &flags = *A->mem(entry), &copies = *A->mem(entry + 1);
        flags = (uint8_t)((flags & 0xf9) | (CARD_STATUS_SEEN << 1));
        if ((copies & 0x7f) < CARD_COPIES) copies = (uint8_t)((copies & 0x80) | CARD_COPIES);
        n++;
    }
    return n;
}
static int unlock_packs(void) {
    int n = 0;
    for (uint32_t i = 0; i < PACK_COUNT; i++) {
        uint8_t &state = *A->mem(SAVE + SAVE_PACKS + i * PACK_SIZE + 1);
        if ((state & 3) == 0) { state = (uint8_t)((state & 0xfc) | PACK_AVAILABLE); n++; }
    }
    return n;
}
/* the decks of a duelist that the story really uses (the others are the placeholder deck): the game lists these in the Free Duel */
static int duelist_real_decks(uint32_t character, uint16_t *decks) {
    int n = 0;
    if (duelist::HasLadder(character)) {
        for (uint32_t level = 1; level <= duelist::kDecksPerLadder; level++)
            if (duelist::LadderUsedMask(character) & (1u << (level - 1))) decks[n++] = (uint16_t)duelist::LadderDeck(character, level);
    } else if (duelist::HasSingleDeck(character) && !duelist::IsPlaceholder(duelist::SingleDeck(character))) decks[n++] = (uint16_t)duelist::SingleDeck(character);
    return n;
}
static int unlock_duelists(void) {
    int n = 0;
    for (uint32_t ch = duelist::kFirstCharacterId; ch <= duelist::kLastCharacterId; ch++) {
        uint16_t decks[duelist::kDecksPerLadder];
        const int count = duelist_real_decks(ch, decks);
        if (!count) continue;
        const uint32_t rec = SAVE + SAVE_DUELIST_RECORDS + ch * DUELIST_RECORD_SIZE;
        *(uint16_t *)A->mem(rec + 2) = (uint16_t)count;
        for (uint32_t i = 0; i < DUELIST_RECORD_DECKS; i++) *(uint16_t *)A->mem(rec + 4 + 2 * i) = i < (uint32_t)count ? decks[i] : 0;
        n++;
    }
    return n;
}
static void unlock_all(TfCpu *c) {
    const int cards = unlock_cards(c), packs = unlock_packs(), duelists = unlock_duelists();
    A->log("Unlock everything: %d cards, %d packs newly available, %d duelists", cards, packs, duelists);
    wchar_t l1[64], l2[64], l3[64]; swprintf(l1, 64, L"Cards in the collection: %d (3 copies each)", cards);
    swprintf(l2, 64, L"Packs made available: %d", packs); swprintf(l3, 64, L"Duelists with all their decks: %d", duelists);
    const wchar_t *lines[3] = {l1, l2, l3};
    A->message_box(L"Everything unlocked", lines, 3, L"Save the game to keep it");
}
static int unlock_get(void) { return 0; }   /* a one-shot menu item: always shown unchecked */
static void unlock_set(int v) { if (v) g_unlock_pending = true; }
static void unlock_quadro(TfCpu *c) {
    if (const char *e = getenv("TF_CHEAT_UNLOCK")) { static bool done; const int from = atoi(e); if (!done && from && save_loaded() && A->frame() > (uint64_t)(from > 1 ? from : 600)) { done = true; g_unlock_pending = true; } }   /* tests: the frame to apply at */
    if (g_unlock_auto) { static bool auto_done; if (!auto_done && save_loaded()) { auto_done = true; g_unlock_pending = true; } }   /* "Unlock on start" (launcher) */
    if (!g_unlock_pending) return;
    if (!save_loaded()) { A->log("Unlock everything: no save is loaded yet"); g_unlock_pending = false; return; }
    g_unlock_pending = false; unlock_all(c);
}

/* ------------------------------------------------------------------ */
static int hk_quadro(TfCpu *c) { mult_quadro(); speed_quadro(); ver_cartas_quadro(c); unlock_quadro(c); return 0; }

extern "C" __declspec(dllexport) int tfmod_init(const TfModApi *api) {
    A = api;
    if (A->version < 6) { A->log("Cheats precisa da API 6 (runtime mais novo)"); return 1; }
    cfg_load();
    A->use_menu(TFMOD_MENU_CHEATS);   /* everything in this mod goes in the bar's Cheats menu (separate from the mods) */
    A->add_toggle(L"&See the opponent's cards", VK_F6, ver_get, ver_set);
    A->add_slider(L"Card panel position (0 top left, 1 top right, 2 bottom left, 3 bottom right)", L"", 0, 3, 1, canto_get, canto_set);
    A->add_slider(L"&DP multiplier in duels", L"x", 1, MULT_MAX, 1, mult_get, mult_set);
    A->add_slider(L"&Fast-forward (game speed)", L"x", 1, SPEED_MAX, 1, speed_get, speed_set);
    A->add_toggle(L"&Unlock everything (cards, packs, duelists)", VK_F2, unlock_get, unlock_set);
    A->hook(F_QUADRO, hk_quadro);
    A->hook(F_RECOMPENSA, hk_recompensa);
    A->hook(F_NUMERO, hk_numero);
    A->log("Cheats carregado: ver cartas do adversario %s (F6), multiplicador de DP %dx (L + R + Cima/Baixo)", g_ver ? "LIGADO" : "desligado", g_mult);
    return 0;
}
