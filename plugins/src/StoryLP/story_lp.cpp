/* Mod "Story Duel LP" (plugin). The duel engine start function libduel_eng_89914605 (09eb14f4) stores a fixed 8000 in the LP of both duelists
 * (0x09f79cec + player * 0xfac) and reports it to the duel manager with libduel_eng_f_09f62d88(13, player, LP, 0). We wrap it: while it runs, that
 * callback gets our LP, and afterwards the duel state is rewritten. Rules chosen in Free Duel are applied later by the game, so they win. */
#include "tfmod.h"
#include <string>
#include <stdio.h>
#include <stdlib.h>

static const TfModApi *A;
static const uint32_t F_INICIO = 0x09eb14f4, F_AVISO = 0x09f62d88, JOG = 0x09f79cec, PASSO = 0xfac;
static int g_lp = 4000, g_dentro;

static std::wstring cfg_path(void) { return std::wstring(A->mod_dir) + L"\\config.ini"; }
static void cfg_load(void) {
    if (FILE *f = _wfopen(cfg_path().c_str(), L"r")) { int v; char l[64]; while (fgets(l, sizeof l, f)) if (sscanf(l, "lp=%d", &v) == 1 && v >= 100 && v <= 99999) g_lp = v; fclose(f); }
    if (const char *e = getenv("TF_STORY_LP")) { const int v = atoi(e); if (v >= 100 && v <= 99999) g_lp = v; }
}

static int hk_aviso(TfCpu *c) {   /* callback do motor para o gerenciador: evento 13 = LP do jogador r5 */
    if (g_dentro && A->reg(c, 4) == 13 && A->reg(c, 6) == 8000) A->set_reg(c, 6, (uint32_t)g_lp);
    return 0;
}
static int hk_inicio(TfCpu *c) {
    g_dentro = 1; A->call_original(c, F_INICIO); g_dentro = 0;
    for (int p = 0; p < 2; p++) *(uint32_t *)A->mem(JOG + p * PASSO) = (uint32_t)g_lp;
    A->log("duel start: LP %d for both duelists", g_lp);
    A->set_reg(c, 2, 1);
    return 1;
}

extern "C" __declspec(dllexport) int tfmod_init(const TfModApi *api) {
    A = api; cfg_load();
    A->hook(F_INICIO, hk_inicio); A->hook(F_AVISO, hk_aviso);
    A->log("Story Duel LP: %d", g_lp);
    return 0;
}
