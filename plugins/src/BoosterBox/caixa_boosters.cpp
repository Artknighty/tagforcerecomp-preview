/* Mod "Booster box" (plugin, API 2 in plugins/sdk/tfmod.h). In the card shop, pressing R on the pack selection makes the game's own message
 * window ask "Buy all N packs for X DP?" (YES/NO); YES buys ALL the remaining packs of that booster at once (limited by DP), instead of
 * one at a time. With DP for only one pack, the same window says so (OK).
 *
 * Animation: reuses the game's own. The first pack does the full original flight (rises, flies to the camera, stops, leaves sideways)
 * at 3x and each following pack repeats the same flight at 8x to 16x, chained and without the pause in front of the camera. What takes
 * the pack out of stock and adds it to "packs to open" is the game's own substate 2, one per flight; the DP is charged at once at the start.
 * Afterwards the normal flow opens the bought packs in sequence.
 *
 * How the game keeps the shop (libshop, fixed addresses at 0x09820xxx):
 *   09820de0  shop state (2 = stacks rising, 3 = pack selection)
 *   09820de4  purchase substate (0 starts and charges the DP, 1 pack rises, 2 leaves stock and becomes the flying pack,
 *             3 flies to the camera (10 frames), 4 pause (13), 5 leaves sideways, 6 end)
 *   09820e4d  current booster          09820e5c  selected pack: bit 7 = stack (0/1), bits 0-6 = index in the stack
 *   09820eb0  stock per booster: low nibble = stack 0, high = stack 1
 *   09820e60  pack heights: float[2][10]        09820ee0  bit 0 = flying pack visible     09820fb8  counter of states 3/4
 *   09820de8  total spent (u32)                        091e073c  booster table (12 bytes; +0 price u16; -4 state byte)
 *   091c72d4  shop_purchase_animate_pack (once per logical frame during the purchase)
 *   091ca5a4  shop_dp_animate_change(delta in r4)     091c6550  shop entry (every logical frame)
 *   091da478  libshop stub for player_dp(0) */
#include "../../sdk/tfmod.h"
#include <algorithm>
#include <stdio.h>

static const TfModApi *A;
static const uint32_t A_SUB = 0x09820de4, A_PACK = 0x09820e4d, A_SEL = 0x09820e5c, A_STOCK = 0x09820eb0, A_HEIGHT = 0x09820e60,
                      A_SPENT = 0x09820de8, A_PRICE = 0x091e073c, A_DP_STUB = 0x091da478, A_STATE = 0x09820de0, A_FLY = 0x09820ee0,
                      A_TIMER = 0x09820fb8, F_COMPRA = 0x091c72d4, F_DP = 0x091ca5a4, F_LOJA = 0x091c6550;
static const uint32_t BTN_R = 0x200;

static bool g_act, g_armed;   /* g_armed: YES na pergunta; a proxima compra vira a compra em lote */
static int g_dpmul, g_left, g_iter; static uint32_t g_price;

static uint8_t rd8(uint32_t a) { return *A->mem(a); }
static uint16_t rd16(uint32_t a) { return *(uint16_t *)A->mem(a); }
static uint32_t &u32(uint32_t a) { return *(uint32_t *)A->mem(a); }
static void wr8(uint32_t a, uint8_t v) { *A->mem(a) = v; }
static float &height(int row, int idx) { return *(float *)A->mem(A_HEIGHT + row * 0x28 + idx * 4); }
static bool in_shop(void) { return A->module_loaded("libshop") != 0; }
static uint32_t chamar(TfCpu *c, uint32_t fn, uint32_t a0, uint32_t a1);
static int dp_now(TfCpu *c) { return (int)chamar(c, A_DP_STUB, 0, 0); }   /* jogador_dp(0) pelo stub do libshop */
static void stop(const char *why) {
    if (g_act) A->log("%s (%d voo(s) extra(s))", why, g_iter);
    g_act = false; g_left = 0; g_iter = 0; g_dpmul = 0;
}

static int hk_compra(TfCpu *c) {   /* loja_compra_animar_pacote */
    if (!in_shop()) { g_act = false; g_armed = false; return 0; }
    const uint32_t sub = u32(A_SUB);
    if (!g_act) {
        if (sub != 0 || !g_armed) return 0;   /* compra confirmada com YES na pergunta (hk_janela) */
        g_armed = false;
        const int pack = rd8(A_PACK); const uint8_t st = rd8(A_STOCK + pack); const int n[2] = {st & 15, st >> 4}, row = rd8(A_SEL) >> 7;
        if (pack >= 0x30 || n[row] == 0) return 0;   /* mesmas condicoes do original (pilha vazia: ele mesmo sai) */
        const uint32_t price = rd16(A_PRICE + pack * 12); if (!price) return 0;
        const int dp = dp_now(c), K = std::min(n[0] + n[1], (int)((uint32_t)std::max(dp, 0) / price));
        if (K < 2) return 0;
        g_act = true; g_left = K - 1; g_iter = 0; g_price = price; g_dpmul = K;
        u32(A_SPENT) += (uint32_t)(K - 1) * price;   /* o original soma o preco de 1 no estado 0 */
        A->log("booster %d, %d pacote(s) de %u DP (DP atual %d)", pack, K, price, dp);
        return 0;
    }
    if (sub == 0) { stop("compra nova durante o lote (interrompido)"); return 0; }
    /* fim de um voo com pacotes ainda a voar: esconde o voador e volta a 'subir' com o pacote que o jogo ja selecionou */
    auto next = [](void) -> bool {
        if (g_left <= 0 || !rd8(A_STOCK + rd8(A_PACK))) return false;
        wr8(A_FLY, (uint8_t)(rd8(A_FLY) & 0xfe));
        u32(A_SUB) = 1; g_left--; g_iter++;
        const uint8_t sel = rd8(A_SEL); height(sel >> 7, sel & 0x7f) = 10.f;   /* comeca ja quase levantado */
        return true;
    };
    if (sub == 6 && !next()) { stop("fim"); return 0; }   /* o ultimo voo termina pelo original (volta a loja) */
    /* Velocidade (passos da animacao original por quadro): o primeiro pacote em 3x, com a pausa na frente da camera; os seguintes de 8x
     * a 16x, sem a pausa, e um voo emenda no outro no mesmo quadro (a compra de 17 pacotes inteira leva ~1,3 s). Requirement: at least double da
     * versao anterior (1x e 3x..6x com um quadro parado entre os voos). */
    const int speed = g_iter == 0 ? 3 : std::min(8 + g_iter / 2, 16);
    for (int i = 0; i < speed; i++) {
        if (g_iter > 0 && u32(A_SUB) == 4 && u32(A_TIMER) < 13) u32(A_TIMER) = 13;   /* sem a pausa: o estado 4 so avanca com o contador > 12 */
        A->call_original(c, F_COMPRA);
        const uint32_t s = u32(A_SUB);
        if (s == 0) break;
        if (s == 6) { if (!next()) break; }   /* emenda o proximo voo; o ultimo termina no proximo quadro */
    }
    return 1;
}
static int hk_dp(TfCpu *c) {   /* loja_dp_animar_alteracao(delta): a compra em lote desconta todos os pacotes de uma vez */
    if (g_act && g_dpmul > 1 && (int32_t)A->reg(c, 4) == -(int32_t)g_price) { A->set_reg(c, 4, (uint32_t)(-(int32_t)(g_price * (uint32_t)g_dpmul))); g_dpmul = 0; }
    return 0;
}
/* ---- confirmacao pela janela de mensagens do PROPRIO jogo (a mesma do "Exiting Pack purchase... Is this OK?" com YES/NO) ----
 * FUN_091d52cc(tipo) monta a janela em 0998f6bc (tipo 1 = pergunta YES/NO com o texto 0x19 da tabela da loja, tipo 2 = aviso
 * com o texto 0x1a) e guarda o tipo no nibble baixo de 09820e4c; com o nibble != 0 a loja nao trata botoes e chama FUN_091d5520,
 * que roda a janela (libehsys_39ADDA9F: -1 aberta, 2 = YES), fecha (libehsys_E1139F1A) e zera o nibble. O texto vem do leitor
 * de strings libehsys_E58C0FDC(tabela, indice): durante a montagem ele devolve o nosso texto (UTF-16 na memoria do PSP). */
static const uint32_t F_JANELA = 0x091d52cc, F_JANELA_RODAR = 0x091d5520, F_STR = 0x08841664, F_JANELA_ATUALIZAR = 0x088493cc,
                      F_JANELA_FECHAR = 0x08849ad8, F_SOM = 0x08863294, F_REACAO = 0x091c8fb8, F_PREPARA_COMPRA = 0x091c5eb8,
                      A_JANELA = 0x0998f6bc, A_FLAGS = 0x09820e4c, A_COMPRA_FLAG = 0x09820fa0, A_BOTOES_NOVOS = 0x08903db4 + 0xc;
static const uint8_t JANELA_MOD = 4;   /* tipo da nossa pergunta no nibble (o jogo so usa 1, 2 e 3) */
static uint32_t g_txt;                 /* texto UTF-16 na memoria do PSP (guest_alloc) */
static int g_str_idx = -1;             /* indice da tabela que o leitor de strings troca pelo nosso texto (so durante a montagem) */

/* chama uma funcao do jogo preservando TODOS os registradores do chamador (a0-a3 e t0-t3 podem ser argumentos da funcao enganchada, v0 o
 * resultado; k0/k1 nao mudam na mesma thread) */
static uint32_t chamar(TfCpu *c, uint32_t fn, uint32_t a0, uint32_t a1) {
    uint32_t s[32]; for (int i = 1; i < 32; i++) s[i] = A->reg(c, i);
    A->set_reg(c, 4, a0); A->set_reg(c, 5, a1); A->call(c, fn); const uint32_t v = A->reg(c, 2);
    for (int i = 1; i < 32; i++) if (i != 26 && i != 27) A->set_reg(c, i, s[i]);
    return v;
}
static void texto(const char *s) {   /* ASCII -> UTF-16 no buffer do PSP ('\n' quebra a linha, como nos textos do jogo) */
    uint16_t *d = (uint16_t *)A->mem(g_txt); int i = 0;
    for (; s[i] && i < 255; i++) d[i] = (uint8_t)s[i];
    d[i] = 0;
}
static void abrir(TfCpu *c, int tipo, int idx) {
    g_str_idx = idx; chamar(c, F_JANELA, (uint32_t)tipo, 0); g_str_idx = -1;
}
static int hk_str(TfCpu *c) {   /* leitor de strings: na montagem da nossa janela, devolve o nosso texto */
    if (g_str_idx < 0 || (int)A->reg(c, 5) != g_str_idx) return 0;
    A->set_reg(c, 2, g_txt); return 1;
}
static int hk_loja(TfCpu *c) {   /* entrada da loja (todo quadro logico): R na escolha do pacote abre a pergunta */
    if (g_act || g_armed || !in_shop() || u32(A_STATE) != 3 || (rd8(A_FLAGS) & 15) != 0) return 0;
    if (!(u32(A_BOTOES_NOVOS) & BTN_R)) return 0;   /* botao apertado neste quadro (mesma leitura que o jogo usa) */
    const int pack = rd8(A_PACK); if (pack >= 0x30) return 0;
    const uint8_t st = rd8(A_STOCK + pack); const int total = (st & 15) + (st >> 4);
    const uint32_t price = rd16(A_PRICE + pack * 12); if (!price || total == 0) return 0;
    const int dp = dp_now(c), K = std::min(total, (int)((uint32_t)std::max(dp, 0) / price));
    char b[256];
    if (K >= 2) {
        snprintf(b, sizeof b, "Buy all %d packs for %u DP?\nDP: %d -> %d", K, (unsigned)K * price, dp, dp - K * (int)price);
        texto(b); abrir(c, 1, 0x19); wr8(A_FLAGS, (uint8_t)((rd8(A_FLAGS) & 0xf0) | JANELA_MOD));
        chamar(c, F_SOM, 0x621a, 0);
    } else {   /* aviso com OK: sem DP nem para um pacote, o proprio texto do jogo ("You don't have enough DP.") */
        if (K == 1) { snprintf(b, sizeof b, "You only have enough DP\nfor one pack."); texto(b); }
        abrir(c, 2, K == 1 ? 0x1a : -1); chamar(c, F_REACAO, 4, 0);
    }
    return 0;
}
static int hk_janela(TfCpu *c) {   /* FUN_091d5520 (roda a janela aberta): a nossa pergunta e tratada aqui */
    if ((rd8(A_FLAGS) & 15) != JANELA_MOD) return 0;
    const int r = (int)chamar(c, F_JANELA_ATUALIZAR, A_JANELA, 0);
    if (r == -1) return 1;
    if (r == 2 && in_shop() && u32(A_STATE) == 3) {   /* YES: o mesmo caminho do jogo ao confirmar a compra de um pacote */
        if ((rd8(A_COMPRA_FLAG) & 15) == 0) { wr8(A_COMPRA_FLAG, (uint8_t)((rd8(A_COMPRA_FLAG) & 0xf0) | 1)); chamar(c, F_PREPARA_COMPRA, 0, 0); }
        u32(A_STATE) = 4; u32(A_SUB) = 0; g_armed = true;
    }
    chamar(c, F_JANELA_FECHAR, A_JANELA, 0);
    wr8(A_FLAGS, (uint8_t)(rd8(A_FLAGS) & 0xf0));
    return 1;
}

extern "C" int tfmod_init(const TfModApi *api) {
    if (!api || api->version < 2) return 1;   /* precisa de guest_alloc (API 2) */
    A = api;
    g_txt = A->guest_alloc(512); if (!g_txt) return 2;
    A->hook(F_COMPRA, hk_compra); A->hook(F_DP, hk_dp); A->hook(F_LOJA, hk_loja); A->hook(F_JANELA_RODAR, hk_janela); A->hook(F_STR, hk_str);
    return 0;
}
