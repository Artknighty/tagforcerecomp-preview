/* Mod "Decks Aprimorados" (plugin, API 3): troca os decks dos duelistas (os 351 decks RCPnnn do jogo) por decks editados, sem mexer nos arquivos do jogo.
 *
 * O jogo le cada deck com libehsys_85A292D7(indice_do_RCP, saida) (0884a694, BOOT): preenche 0x108 bytes {[0] = n_principal, [1] = n_side, [2] = n_extra,
 * principal em +0x0c, side em +0xac, extra em +0xca (u16 cada)}. Este plugin engancha essa funcao: para os indices que tem arquivo em <mod>\decks\RCPnnn.txt, devolve
 * o deck do arquivo; para os outros, roda o original. Vale para o duelo, o Free Duel, o visualizador de receitas e a historia.
 * Formato do arquivo (texto; '#' comeca comentario):
 *     principal: 4219 4219 5066 ...        (id de cada copia; ate 80)
 *     extra: 5961 ...                      (monstros de fusao; ate 30)
 *     side: ...                            (ate 15)
 * Os ids sao os do jogo (tools/decks_extrair.py gera gen/decks_db/cartas.json com id -> nome). Variavel de ambiente TF_DECKS_OFF=1 desliga o mod (arena: comparar com o original). */
#include "tfmod.h"
#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const TfModApi *A;
static const uint32_t F_LER_DECK = 0x0884a694;
struct Deck { std::vector<uint16_t> principal, extra, side; };
static std::map<int, Deck> g_decks;
static bool g_off;

static void ler_arquivo(int idx, const std::wstring &caminho) {
    FILE *f = _wfopen(caminho.c_str(), L"r"); if (!f) return;
    Deck d; char l[4096];
    while (fgets(l, sizeof l, f)) {
        char *h = strchr(l, '#'); if (h) *h = 0;
        std::vector<uint16_t> *dst = nullptr; char *p = l;
        if (!strncmp(p, "principal:", 10)) { dst = &d.principal; p += 10; }
        else if (!strncmp(p, "extra:", 6)) { dst = &d.extra; p += 6; }
        else if (!strncmp(p, "side:", 5)) { dst = &d.side; p += 5; }
        if (!dst) continue;
        for (char *q = p; *q;) { while (*q == ' ' || *q == '\t' || *q == ',') q++; if (*q < '0' || *q > '9') break; dst->push_back((uint16_t)strtoul(q, &q, 10)); }
    }
    fclose(f);
    if (d.principal.size() < 1 || d.principal.size() > 80 || d.extra.size() > 30 || d.side.size() > 15) { A->log("deck RCP%03d ignorado (tamanho invalido: %zu/%zu/%zu)", idx, d.principal.size(), d.extra.size(), d.side.size()); return; }
    g_decks[idx] = d;
}
static void carregar(void) {
    const std::wstring pasta = std::wstring(A->mod_dir) + L"\\decks";
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((pasta + L"\\RCP*.txt").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        int idx = -1; if (swscanf(fd.cFileName, L"RCP%d", &idx) == 1 && idx >= 0 && idx < 0x160) ler_arquivo(idx, pasta + L"\\" + fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static int hk_ler_deck(TfCpu *c) {   /* libehsys_85A292D7(indice, saida) */
    if (g_off) return 0;
    int idx = (int)A->reg(c, 4); const uint32_t out = A->reg(c, 5);
    { static const char *rm = getenv("TF_DECK_REMAP"); int de = -1, para = -1; if (rm && sscanf(rm, "%d:%d", &de, &para) == 2 && idx == de) idx = para; }   /* teste: reproduz duelos gravados com o deck antigo (ex.: 37:1 = Zane com o deck do Jaden) */
    auto it = g_decks.find(idx);
    A->log("[deck] leitura RCP%03d (ra=%08x, campo do indice em %08x; slot: personagem=%u especial=%u deck=%u variante=%u) -> %s", idx, A->reg(c, 31), out - 128, ((uint16_t*)A->mem(out - 132))[0], ((uint16_t*)A->mem(out - 132))[1], ((uint16_t*)A->mem(out - 132))[2], ((uint16_t*)A->mem(out - 132))[3], it == g_decks.end() ? "deck original do jogo" : "deck do mod");   /* diagnostico: qual deck cada duelista recebeu (report "Zane com deck do Jaden") */
    if (it == g_decks.end() || out < 0x08000000u || out > 0x0a000000u) return 0;
    const Deck &d = it->second;
    uint8_t *o = A->mem(out); memset(o, 0, 0x108);
    uint32_t *w = (uint32_t *)o; w[0] = (uint32_t)d.principal.size(); w[1] = (uint32_t)d.side.size(); w[2] = (uint32_t)d.extra.size();
    for (size_t i = 0; i < d.principal.size(); i++) ((uint16_t *)(o + 0x0c))[i] = d.principal[i];
    for (size_t i = 0; i < d.side.size(); i++) ((uint16_t *)(o + 0xac))[i] = d.side[i];
    for (size_t i = 0; i < d.extra.size(); i++) ((uint16_t *)(o + 0xca))[i] = d.extra[i];
    return 1;   /* tratado: o original nao roda */
}

extern "C" __declspec(dllexport) int tfmod_init(const TfModApi *api) {
    A = api; g_off = getenv("TF_DECKS_OFF") != nullptr;
    carregar();
    A->hook(F_LER_DECK, hk_ler_deck);
    A->log("Decks Aprimorados: %zu deck(s) editado(s)%s", g_decks.size(), g_off ? " (DESLIGADO por TF_DECKS_OFF)" : "");
    return 0;
}
