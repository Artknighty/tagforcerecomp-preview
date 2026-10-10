/* Mod "CPU contra CPU" (plugin, API 3). O motor de duelo guarda o tipo de controle de cada jogador em 0x09f77888 + 4*jogador
 * (0 = humano local, 1 = CPU, 2 = outro, 3 = ?), gravado pelo gerenciador de duelo com libduel_eng_5A41E0A6(jogador, tipo) (09f626dc). Com o mod ligado, quando o
 * jogo pede "jogador 0 = humano" o plugin troca por "CPU": os dois lados jogam com a IA do jogo (e com o mod IA Avancada, se estiver ligado).
 * Ajuste: menu Mods -> "CPU contra CPU" (0 = desligado, 1 = ligado) ou config.ini (cpu_contra_cpu=1). Serve para estudar e comparar a IA. */
#include "tfmod.h"
#include <string>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

static const TfModApi *A;
static const uint32_t F_TIPO = 0x09f626dc;
static int g_on = 0;   /* padrao: desligado (ligue no menu Mods ou com TF_CPUCPU=1) */

static std::wstring cfg_path(void) { return std::wstring(A->mod_dir) + L"\\config.ini"; }   /* (antes "\c" sem escapar: gravava em mods\CPUvsCPUconfig.ini, fora da pasta do mod) */
static void cfg_load(void) {
    if (FILE *f = _wfopen(cfg_path().c_str(), L"r")) { int v; char l[64]; while (fgets(l, sizeof l, f)) if (sscanf(l, "cpu_vs_cpu=%d", &v) == 1 || sscanf(l, "cpu_contra_cpu=%d", &v) == 1) g_on = v != 0; fclose(f); }
    if (const char *e = getenv("TF_CPUCPU")) g_on = atoi(e) != 0;
}
static void cfg_save(void) { if (FILE *f = _wfopen(cfg_path().c_str(), L"w")) { fprintf(f, "cpu_vs_cpu=%d\n", g_on); fclose(f); } }
static int slider_get(void) { return g_on; }
static void slider_set(int v) { g_on = v != 0; cfg_save(); A->log("CPU contra CPU = %d", g_on); }

static uint32_t u32(uint32_t a) { return *(uint32_t *)A->mem(a); }
static bool g_fim, g_viu_lp; static uint64_t g_fim_quadro;
static uint32_t g_turno_jog = 9, g_turno_n; static int g_lp_ant[2] = {-1, -1};
static int hk_inicio_duelo(TfCpu *c) {   /* libduel_eng_89914605 (09eb14f4): inicio do duelo (LP 8000, monta e embaralha os decks com rand(), semente em 09f77880, quem comeca em 09f77890) */
    (void)c;
    if (const char *e = getenv("TF_RNG_SEED")) { *(uint32_t *)A->mem(0x09f77880) = (uint32_t)strtoul(e, nullptr, 0) * 2654435761u + 12345u; A->log("[arena] semente %s", e); }
    if (const char *e = getenv("TF_ESPELHO")) {   /* arena: os dois jogadores usam o deck do jogador indicado (duelo espelho: sem vantagem de baralho) */
        const uint32_t de = atoi(e) & 1, A0 = 0x09f77894, A1 = 0x09f7799c, n = 0x108;
        memcpy(A->mem(de ? A0 : A1), A->mem(de ? A1 : A0), n); A->log("[arena] duelo espelho: deck do jogador %u nos dois lados", de);
    }
    if (const char *e = getenv("TF_PRIMEIRO")) { *(uint32_t *)A->mem(0x09f77890) = (uint32_t)atoi(e) & 1; A->log("[arena] comeca o jogador %d", atoi(e) & 1); }
    return 0;
}
/* arena: TF_DECK_J0 / TF_DECK_J1 = numero do deck do jogo (RCPnnn) de cada lado. Tem de ser feito ANTES do jogo carregar o duelo (libehsys_F11F0D9A @0883f4f8, recebe a
 * estrutura de montagem com 0x18c bytes por duelista; +0x124 = indice do RCP): trocar o deck depois (no inicio do motor) derruba o jogo porque os recursos (modelos, texturas)
 * das cartas ja foram carregados para os decks originais. O deck sai do leitor do jogo (0884a694), entao vale tambem o mod Decks Aprimorados. */
static int hk_montar_duelo(TfCpu *c) {
    const uint32_t base = A->reg(c, 4); if (base < 0x08000000u || base > 0x0a000000u) return 0;
    for (int j = 0; j < 2; j++) {
        const char *e = getenv(j ? "TF_DECK_J1" : "TF_DECK_J0"); if (!e) continue;
        uint8_t *m = A->mem(base + (uint32_t)j * 0x18c);
        *(uint16_t *)(m + 0x124) = (uint16_t)atoi(e); *(uint16_t *)(m + 0x126) = 0; A->log("[arena] deck do jogador %d = RCP%03d", j, atoi(e));
    }
    return 0;
}
static int hk_tipo(TfCpu *c) {   /* libduel_eng_5A41E0A6(jogador, tipo) */
    if (A->reg(c, 4) == 0) { g_fim = false; g_viu_lp = false; g_turno_jog = 9; g_turno_n = 0; g_lp_ant[0] = g_lp_ant[1] = -1; }   /* inicio de um duelo */
    if (g_on && A->reg(c, 4) == 0 && A->reg(c, 5) == 0) { A->set_reg(c, 5, 1); A->log("jogador 0 passou a ser controlado pela CPU"); }
    return 0;
}

/* ---- monitor: registra no log da sessao todas as jogadas dos dois lados ([mon] ...). tools/ia_monitor.py le e mostra com nomes de cartas ---- */
static const uint32_t JOG = 0x09f79cec, PASSO = 0xfac, CASAS = 0x09f79d1c;
static uint16_t u16(uint32_t a) { return *(uint16_t *)A->mem(a); }
static unsigned carta_da_mao(uint32_t jog, int idx) { return idx >= 0 && idx < 12 ? (u16(JOG + (jog & 1) * PASSO + 0x120 + idx * 4) & 0x1fff) : 0; }
static void log_tabuleiro(const char *quando) {
    for (int p = 0; p < 2; p++) {
        char l[800]; int n = snprintf(l, sizeof l, "[mon] %s J%d LP=%d raw[cf0..d08]=%u,%u,%u,%u,%u,%u mao=%u(", quando, p, (int)u32(JOG + p * PASSO), u32(0x09f79cf0 + p * PASSO), u32(0x09f79cf4 + p * PASSO), u32(0x09f79cf8 + p * PASSO), u32(0x09f79cfc + p * PASSO), u32(0x09f79d00 + p * PASSO), u32(0x09f79d04 + p * PASSO), u32(0x09f79cf8 + p * PASSO));
        for (uint32_t h = 0; h < u32(0x09f79cf8 + p * PASSO) && h < 10; h++) n += snprintf(l + n, sizeof l - n, "%s%u", h ? "," : "", carta_da_mao(p, (int)h));
        n += snprintf(l + n, sizeof l - n, ") campo:");
        for (int s2 = 0; s2 < 11; s2++) { const uint32_t cs = CASAS + p * PASSO + s2 * 0x14; const unsigned id = u16(cs) & 0x1fff; if (id) n += snprintf(l + n, sizeof l - n, " %s%d=%u/%x", s2 < 5 ? "M" : s2 < 10 ? "ST" : "C", s2 < 5 ? s2 : s2 - 5, id, u32(cs + 0x10) & 0xff); }
        A->log("%s", l);
    }
}
static int hk_mon_jogar(TfCpu *c) {   /* 09e6bad4(jogador, indice da mao, modo): a CPU joga uma carta da mao */
    const uint32_t jog = A->reg(c, 4), idx = A->reg(c, 5);
    A->log("[mon] J%u joga da mao: carta %u (indice %d, modo %u) quadro %llu", jog & 1, carta_da_mao(jog, (int)idx), (int)idx, A->reg(c, 6), (unsigned long long)A->frame());
    return 0;
}
static int hk_mon_sel(TfCpu *c) {   /* libduel_eng_F51DDF37(jogador, zona, idx, tipo) (09e9ccb8): selecao de carta (ativacao de magia, alvo...) */
    const uint32_t jog = A->reg(c, 4), zona = A->reg(c, 5), idx = A->reg(c, 6), tipo = A->reg(c, 7);
    const unsigned id = zona == 0xb ? carta_da_mao(jog, (int)idx) : zona < 11 ? (u16(CASAS + (jog & 1) * PASSO + zona * 0x14) & 0x1fff) : 0;
    A->log("[mon] J%u seleciona carta %u (zona %u, idx %u, tipo %u) quadro %llu", jog & 1, id, zona, idx, tipo, (unsigned long long)A->frame());
    return 0;
}
static int g_ult_lp[2];
static int hk_quadro(TfCpu *) {   /* ehsys_fim_de_quadro: registra o fim do duelo (arena) e o monitor */
    if (!A->module_loaded("libduel_eng")) {
        if (g_viu_lp && !g_fim) {   /* o motor descarregou: fim do duelo (o golpe final nao aparece no LP amostrado; quem joga o turno e quem vence, salvo LP <= 0) */
            g_fim = true; g_fim_quadro = A->frame();
            int venc = (int)g_turno_jog;
            if (g_ult_lp[0] <= 0) venc = 1; else if (g_ult_lp[1] <= 0) venc = 0;
            A->log("[arena] FIM quadro %llu (motor descarregado): ultimo LP J0=%d J1=%d, turno %u (vez de J%u) -> venceu o jogador %d", (unsigned long long)A->frame(), g_ult_lp[0], g_ult_lp[1], g_turno_n, g_turno_jog, venc);
        }
        if (g_fim && getenv("TF_ARENA_SAIR") && A->frame() > g_fim_quadro + 30) { fflush(NULL); exit(0); }
        return 0;
    }
    if (g_viu_lp) {
        { const int a = (int)u32(JOG), b = (int)u32(JOG + PASSO); if (a > 0 || b > 0) { g_ult_lp[0] = a; g_ult_lp[1] = b; } }
        const uint32_t tj = u32(0x09f7d460) & 1;
        if (tj != g_turno_jog) { g_turno_jog = tj; g_turno_n++; char t[40]; snprintf(t, sizeof t, "turno %u (vez de J%u)", g_turno_n, tj); log_tabuleiro(t); }
        for (int p = 0; p < 2; p++) { const int lp = (int)u32(JOG + p * PASSO); if (g_lp_ant[p] != lp) { if (g_lp_ant[p] >= 0) A->log("[mon] LP J%d: %d -> %d (quadro %llu)", p, g_lp_ant[p], lp, (unsigned long long)A->frame()); g_lp_ant[p] = lp; } }
    }
    const int32_t lp0 = (int32_t)u32(0x09f79cec), lp1 = (int32_t)u32(0x09f79cec + 0xfac);
    if (lp0 > 0 && lp1 > 0) g_viu_lp = true;
    if (g_viu_lp && !g_fim && (lp0 <= 0 || lp1 <= 0) && lp0 > -1000000 && lp1 > -1000000) {   /* fim por pontos de vida (o duelo real pode ficar numa tela de resultado que espera tecla) */
        g_fim = true; g_fim_quadro = A->frame();
        A->log("[arena] FIM quadro %llu (LP): J0=%d J1=%d, turno %u -> venceu o jogador %d", (unsigned long long)A->frame(), lp0, lp1, g_turno_n, lp0 <= 0 ? 1 : 0);
    }
    if (g_fim && getenv("TF_ARENA_SAIR") && A->frame() > g_fim_quadro + 30) { fflush(NULL); exit(0); }
    if (A->frame() % 600 == 0) A->log("[arena] quadro %llu LP %d x %d", (unsigned long long)A->frame(), lp0, lp1);
    return 0;
}

extern "C" __declspec(dllexport) int tfmod_init(const TfModApi *api) {
    A = api; cfg_load();
    if (A->version >= 3) A->add_slider(L"CPU vs CPU (0 = off, 1 = on)", L"", 0, 1, 0, slider_get, slider_set);
    A->hook(F_TIPO, hk_tipo); A->hook(0x09eb14f4, hk_inicio_duelo); A->hook(0x09e6bad4, hk_mon_jogar); A->hook(0x09e9ccb8, hk_mon_sel); A->hook(0x08833fc4, hk_quadro); A->hook(0x0883f4f8, hk_montar_duelo);
    A->log("CPU contra CPU carregado: %s", g_on ? "ligado" : "desligado");
    return 0;
}

/* F7 / L3+R3 no jogo ("a CPU errou"): despeja o tabuleiro completo dos dois lados no log; tools/marcas_cpu.py le isto e as jogadas anteriores */
extern "C" __declspec(dllexport) void tfmod_marca(int n) { char t[48]; snprintf(t, sizeof t, "MARCA #%d", n); A->log("[marca-cpu] estado do duelo na marca #%d:", n); log_tabuleiro(t); }
