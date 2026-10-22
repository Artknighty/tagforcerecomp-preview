/* Mod "Better AI" (plugin, API 3): makes the duel CPU as hard as possible. A single mode (on/off in the Mods menu).
 *
 * Map of the game's AI (libduel_eng; details in docs/AI.md):
 *   - player p at 0x09f79cec + p*0xfac (LP +0, hand at +0x120 + i*4, hand counter at 0x09f79cf8); slots (0x14 bytes) at 0x09f79d1c + p*0xfac + i*0x14
 *     (0..4 monsters, 5..9 spell/trap, 10 field): +0 id&0x1fff, +6 position, +8 flag, +0xc value, +0x10 bits (0x80 = face up, 0x08 = defense).
 *   - control: 0x09f77888[p] == 1 -> the CPU plays that side.
 *   - 09e76a14 picks the monster to summon (original: the LAST eligible one in hand), 09e75258 is the attack planner, 09e6cff0 decides the position
 *     (attack/defense) of a monster, 09e87760 plays a spell from hand using the rule table (09f6a478/09f6b470), 09e82bd4 is the Dark Hole rule.
 * What this mod changes:
 *   1. Adaptive posture (postura()): looks at LP, monsters, hand and the opponent's face-down cards and picks DEFENSIVE / NEUTRAL / AGGRESSIVE; the decisions below use it.
 *   2. Monster to summon: among the cards the game itself would accept, it keeps the best one (not the last), depending on the posture.
 *   3. Dark Hole: only when it pays off (several opposing monsters or a favorable board balance); it holds the card when ahead.
 *   4. Holding the line: with 1 monster, an empty hand and the opponent full of face-down cards, it does not attack (unless lethal) and goes defensive.
 *   5. Extra rules for cards the game never plays (Lightning Vortex, Brain Control): g_regras[].
 * Environment variables for research: TF_IA_OFF=1 (turns everything off), TF_IA_LADO=0/1 (improves only that player: A/B in the arena), TF_IA_SONDA=1 (logs the
 * original decisions), TF_IA_FORCAR_REGRAS / TF_IA_DAR_CARTA=<id> (test of the extra rules). */
#include "tfmod.h"
#include "decisor.h"
#include <windows.h>
#include <map>
#include <vector>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <string>
#include <wchar.h>

static const TfModApi *A;
static const uint32_t JOG = 0x09f79cec, PASSO = 0xfac, CASAS = 0x09f79d1c;
static const uint32_t F_STATS = 0x09f56da8, F_ATAQUE = 0x09e74f60, F_MAO = 0x09e762e0, F_MAGIA = 0x09e76a14, F_JOGAR = 0x09e6bad4, F_POSICAO = 0x09e6c4a0,
                      F_PLANO = 0x09e75258, F_POSDEC = 0x09e6cff0, F_EXEC = 0x09e87760, F_PRED_SIM = 0x09e8133c, F_PRED_DARKHOLE = 0x09e82bd4,
                      F_CARTA_ATK = 0x0884b7ec, F_CARTA_DEF = 0x0884b830, F_CARTA_NIVEL = 0x0884b77c, TAB0 = 0x09f6a478, TAB0_N = 0x3d;
static uint32_t g_buf;      /* area do PSP para as chamadas */
static bool g_off;          /* TF_IA_OFF */
static int g_lado = -1;     /* -1: os dois lados; 0/1: so esse jogador */
static bool g_sonda;

static uint32_t u32(uint32_t a) { return *(uint32_t *)A->mem(a); }
static uint16_t u16(uint32_t a) { return *(uint16_t *)A->mem(a); }
static void u32w(uint32_t a, uint32_t v) { *(uint32_t *)A->mem(a) = v; }
static void u16w(uint32_t a, uint16_t v) { *(uint16_t *)A->mem(a) = v; }
static bool cpu(uint32_t jog) { return u32(0x09f77888 + (jog & 1) * 4) == 1; }
static bool melhora(uint32_t jog) { return !g_off && cpu(jog) && (g_lado < 0 || g_lado == (int)(jog & 1)); }
/* Interruptores de PESQUISA (A/B de cada regra): TF_IA_SEM=lista (invocar,posicao,segurar,monstro,darkhole,extras) desliga essas regras (volta o comportamento original do jogo) e
 * TF_IA_SEM_LADO=0/1 so para esse jogador (o outro mantem o plugin completo). Serve para medir na arena se cada regra ajuda ou atrapalha. */
enum { SEM_INVOCAR = 1, SEM_POSICAO = 2, SEM_SEGURAR = 4, SEM_MONSTRO = 8, SEM_DARKHOLE = 16, SEM_EXTRAS = 32, SEM_ORDEM = 64 };
static uint32_t g_sem; static int g_sem_lado = -1;
static bool melhora_r(uint32_t jog, uint32_t bit) { return melhora(jog) && !((g_sem & bit) && (g_sem_lado < 0 || g_sem_lado == (int)(jog & 1))); }

/* chama uma funcao do jogo (ate 5 argumentos: a0-a3, t0) preservando TODOS os registradores do gancho: a0-a3/t0-t3 podem ser argumentos da funcao
 * enganchada e r2 o resultado do original (que o gancho devolve); k0/k1 nao mudam na mesma thread */
static uint32_t chama5(TfCpu *c, uint32_t fn, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4) {
    uint32_t s[32]; for (int i = 1; i < 32; i++) s[i] = A->reg(c, i);
    A->set_reg(c, 4, a0); A->set_reg(c, 5, a1); A->set_reg(c, 6, a2); A->set_reg(c, 7, a3); A->set_reg(c, 8, a4); A->call(c, fn);
    const uint32_t r = A->reg(c, 2);
    for (int i = 1; i < 32; i++) if (i != 26 && i != 27) A->set_reg(c, i, s[i]);
    return r;
}
static uint32_t chama(TfCpu *c, uint32_t fn, uint32_t a0, uint32_t a1 = 0, uint32_t a2 = 0) { return chama5(c, fn, a0, a1, a2, 0, 0); }

/* ------------------------------------------------------------------ leitura do tabuleiro */
struct Mon { uint32_t id; bool vis, defesa; int atk, def; };
struct Lado {
    int lp, mao, n, nvis, nback, nback_fd;      /* monstros / cartas de magia-armadilha no campo / viradas para baixo */
    int melhor_atk, soma_atk, melhor_def;        /* so monstros conhecidos (ou os proprios) */
    int atk_ameaca;                              /* maior ATK entre os monstros virados para cima em posicao de ataque (podem atacar no proximo turno) */
    Mon m[5];
};
static const int DESCONHECIDO = 1200;           /* valor suposto de um monstro virado do oponente (a IA nao "ve" a carta) */
static void ler_lado(TfCpu *c, int p, bool oculto, Lado &L) {
    memset(&L, 0, sizeof L);
    L.lp = (int)u32(JOG + p * PASSO); L.mao = (int)u32(0x09f79cf8 + p * PASSO);
    for (int s = 0; s < 5; s++) {
        const uint32_t cs = CASAS + p * PASSO + s * 0x14, id = u16(cs) & 0x1fff, f = u32(cs + 0x10);
        Mon &m = L.m[s]; m.id = id; if (!id) continue;
        L.n++; m.vis = (f & 0x80) != 0 || !oculto; m.defesa = (f & 0x08) != 0 || !(f & 0x80);
        if (m.vis) {
            chama(c, F_STATS, p, s, g_buf); m.atk = (int)u32(g_buf + 20); m.def = (int)u32(g_buf + 24); L.nvis++;
            if (m.atk > L.melhor_atk) L.melhor_atk = m.atk;
            if (!m.defesa) { L.soma_atk += m.atk; if (m.atk > L.atk_ameaca) L.atk_ameaca = m.atk; }
            if (m.def > L.melhor_def) L.melhor_def = m.def;
        } else { m.atk = m.def = DESCONHECIDO; if (DESCONHECIDO > L.melhor_atk) L.melhor_atk = DESCONHECIDO; }
    }
    for (int s = 5; s < 10; s++) {
        const uint32_t cs = CASAS + p * PASSO + s * 0x14;
        if (!(u16(cs) & 0x1fff)) continue;
        L.nback++; if (!(u32(cs + 0x10) & 0x80)) L.nback_fd++;
    }
}
static int valor_mon(const Mon &m) { const int d = m.def * 7 / 10; return m.atk > d ? m.atk : d; }

enum Postura { NEUTRA, DEFENSIVA, AGRESSIVA };
static Postura postura(TfCpu *c, uint32_t jog, Lado *me_out = nullptr, Lado *opp_out = nullptr) {
    Lado me, opp; ler_lado(c, jog & 1, false, me); ler_lado(c, 1 - (jog & 1), true, opp);
    if (me_out) *me_out = me; if (opp_out) *opp_out = opp;
    if (opp.n == 0 && me.soma_atk >= opp.lp) return AGRESSIVA;                       /* letal no campo */
    if (me.n <= 1 && me.mao <= 1 && opp.nback_fd >= 2) return DEFENSIVA;             /* sem recursos contra um campo cheio de cartas viradas */
    if (opp.n >= 2 && me.n == 0 && me.mao == 0) return DEFENSIVA;
    if (me.lp + 2500 < opp.lp && opp.soma_atk > me.soma_atk + 800) return DEFENSIVA; /* atras no placar e no campo */
    if (me.soma_atk > opp.soma_atk + 1500 && me.lp >= opp.lp) return AGRESSIVA;      /* na frente */
    return NEUTRA;
}
static const char *nome_postura(Postura p) { return p == DEFENSIVA ? "defensiva" : p == AGRESSIVA ? "agressiva" : "neutra"; }

/* limita o log: a mesma decisao e consultada varias vezes por quadro (simulacoes); so registra se a chave mudou ou passou 5 s */
static bool log_novo(int canal, uint32_t chave) {
    static uint32_t ult[8]; static uint64_t quando[8];
    if (ult[canal] == chave && A->frame() < quando[canal] + 300) return false;
    ult[canal] = chave; quando[canal] = A->frame(); return true;
}
static int stat_carta(TfCpu *c, uint32_t fn, uint32_t id) { return (int)chama(c, fn, id); }

/* ------------------------------------------------------------------ texto da situacao (para o decisor por modelo) */
static const uint32_t F_CARTA_NOME = 0x0884b46c, F_CARTA_DESC = 0x0884b4b8, F_E_MONSTRO_ = 0x0884c0d8;
static std::string utf16_guest(uint32_t addr, size_t maxc) {   /* texto UTF-16 da memoria do PSP -> UTF-8 (quebras de linha viram espaco) */
    std::string o;
    for (size_t i = 0; i < maxc; i++) {
        const uint16_t ch = u16(addr + (uint32_t)i * 2); if (!ch) break;
        if (ch < 0x80) o += (ch == '\n' || ch == '\r') ? ' ' : (char)ch;
        else { char b[8]; const wchar_t w = (wchar_t)ch; const int n = WideCharToMultiByte(CP_UTF8, 0, &w, 1, b, sizeof b, nullptr, nullptr); if (n > 0) o.append(b, (size_t)n); }
    }
    return o;
}
static std::string nome_carta(TfCpu *c, uint32_t id) {
    static std::map<uint32_t, std::string> cache; auto it = cache.find(id); if (it != cache.end()) return it->second;
    const uint32_t p = chama(c, F_CARTA_NOME, id); return cache[id] = p ? utf16_guest(p, 60) : std::string("?");
}
static std::string desc_carta(TfCpu *c, uint32_t id) {
    static std::map<uint32_t, std::string> cache; auto it = cache.find(id); if (it != cache.end()) return it->second;
    const uint32_t p = chama(c, F_CARTA_DESC, id); return cache[id] = p ? utf16_guest(p, 400) : std::string();
}
static std::string carta_texto(TfCpu *c, uint32_t id, bool com_efeito) {   /* "Nome (monster, ATK 1600/DEF 1400, level 4)" ou "Nome (spell/trap)" + efeito */
    char b[120]; std::string t = nome_carta(c, id);
    if (chama(c, F_E_MONSTRO_, id)) { snprintf(b, sizeof b, " (monster, ATK %d/DEF %d, level %d)", stat_carta(c, F_CARTA_ATK, id), stat_carta(c, F_CARTA_DEF, id), stat_carta(c, F_CARTA_NIVEL, id)); t += b; }
    else t += " (spell/trap)";
    if (com_efeito) { const std::string d = desc_carta(c, id); if (!d.empty()) t += ": " + d; }
    return t;
}
static std::string situacao_texto(TfCpu *c, uint32_t jog, const Lado &me, const Lado &opp) {
    const int p = jog & 1, q = 1 - p; char b[200]; std::string t;
    snprintf(b, sizeof b, "You have %d LP and the opponent has %d LP. Cards left in decks: yours %u, opponent's %u.\n", me.lp, opp.lp, u32(0x09f79cfc + p * PASSO), u32(0x09f79cfc + q * PASSO)); t += b;
    t += "Your hand:"; for (int i = 0; i < me.mao && i < 8; i++) t += (i ? "; " : " ") + carta_texto(c, u16(JOG + p * PASSO + 0x120 + i * 4) & 0x1fff, false);
    t += "\nYour monsters:";
    int k = 0; for (int i = 0; i < 5; i++) if (me.m[i].id) { snprintf(b, sizeof b, " ATK %d/DEF %d, %s", me.m[i].atk, me.m[i].def, me.m[i].defesa ? "defense position" : "attack position"); t += (k++ ? "; " : " ") + nome_carta(c, me.m[i].id) + b; }
    if (!k) t += " none";
    snprintf(b, sizeof b, "\nYour spell/trap zone: %d card(s), %d face-down.", me.nback, me.nback_fd); t += b;
    t += "\nOpponent's monsters:"; k = 0; int ocultos = 0;
    for (int i = 0; i < 5; i++) if (opp.m[i].id) {
        if (!opp.m[i].vis) { ocultos++; continue; }
        snprintf(b, sizeof b, " ATK %d/DEF %d, %s", opp.m[i].atk, opp.m[i].def, opp.m[i].defesa ? "defense position" : "attack position"); t += (k++ ? "; " : " ") + nome_carta(c, opp.m[i].id) + b;
    }
    if (ocultos) { snprintf(b, sizeof b, "%s%d face-down monster(s) of unknown strength", k ? "; " : " ", ocultos); t += b; k++; }
    if (!k) t += " none";
    snprintf(b, sizeof b, "\nOpponent's spell/trap zone: %d card(s), %d face-down. Opponent's hand: %d card(s).\n", opp.nback, opp.nback_fd, opp.mao); t += b;
    const uint32_t campo = u16(CASAS + q * PASSO + 10 * 0x14) & 0x1fff; if (campo) t += "Field spell on the opponent's side: " + nome_carta(c, campo) + ".\n";
    const uint32_t meucampo = u16(CASAS + p * PASSO + 10 * 0x14) & 0x1fff; if (meucampo) t += "Field spell on your side: " + nome_carta(c, meucampo) + ".\n";
    return t;
}
/* consequencias de um monstro (atk/def) ficar em ataque ou em defesa virada contra os atacantes do oponente que ja estao em campo */
static std::string nota_combate(TfCpu *c, int atk, int def, bool ataque, const Lado &opp) {
    std::string t; char b[220]; int n = 0;
    for (int i = 0; i < 5; i++) {
        const Mon &o = opp.m[i]; if (!o.id || !o.vis || o.defesa) continue;
        const std::string nm = nome_carta(c, o.id);
        if (ataque) { if (o.atk > atk) snprintf(b, sizeof b, "%s%s (ATK %d) could attack it: it would be destroyed and you would lose %d LP.", n ? " " : "", nm.c_str(), o.atk, o.atk - atk);
                      else if (o.atk == atk) snprintf(b, sizeof b, "%s%s (ATK %d) attacking it: both are destroyed.", n ? " " : "", nm.c_str(), o.atk);
                      else snprintf(b, sizeof b, "%s%s (ATK %d) could not attack it profitably.", n ? " " : "", nm.c_str(), o.atk); }
        else { if (o.atk > def) snprintf(b, sizeof b, "%s%s (ATK %d) attacking it would destroy it (no LP loss).", n ? " " : "", nm.c_str(), o.atk);
               else snprintf(b, sizeof b, "%s%s (ATK %d) attacking it would fail.", n ? " " : "", nm.c_str(), o.atk); }
        t += b; n++;
    }
    if (!n) t = "The opponent has no face-up attacker, so it is safe from battle until the opponent summons one.";
    if (ataque && opp.n > opp.nvis) t += " The opponent also has face-down monsters of unknown strength.";
    return t;
}
static std::string efeitos_texto(TfCpu *c, const std::vector<uint32_t> &ids) {   /* efeito de cada carta citada (uma vez so) */
    std::string t; std::vector<uint32_t> vistos;
    for (uint32_t id : ids) {
        bool ja = false; for (uint32_t v : vistos) if (v == id) ja = true; if (ja || !id) continue; vistos.push_back(id);
        const std::string d = desc_carta(c, id); if (!d.empty()) t += "- " + nome_carta(c, id) + ": " + d + "\n";
    }
    return t.empty() ? t : "Card texts:\n" + t;
}

/* ------------------------------------------------------------------ 2. monstro a baixar (09e76a14) */
/* O original percorre a mao e SOBRESCREVE a escolha a cada carta que serve: vence a ULTIMA elegivel, nao a melhor. Aqui: descobrimos quais cartas o proprio
 * jogo aceitaria (trocamos cada carta para a ultima posicao da mao e perguntamos) e ficamos com a de maior valor. Elegibilidade, tributos e o modo
 * (0 = ofensivo/Invocar, 1 = ATK<=DEF/baixar em defesa) continuam sendo os do jogo. */
static int hk_magia(TfCpu *c) {
    const uint32_t jog = A->reg(c, 4), pm = A->reg(c, 5);
    if (!melhora_r(jog, SEM_MONSTRO) || !pm) return 0;
    const uint32_t mao = JOG + (jog & 1) * PASSO + 0x120, n = u32(0x09f79cf8 + (jog & 1) * PASSO);
    if (n < 2 || n > 8) return 0;
    Lado me, opp; const Postura po = postura(c, jog, &me, &opp);
    struct Cand { int idx; uint32_t id, modo; int score; int atk, def; } cand[8]; int nc = 0;
    for (uint32_t k = 0; k < n; k++) {
        const uint32_t ultimo = mao + (n - 1) * 4, atual = mao + k * 4;
        if (k != n - 1) { const uint32_t t = u32(atual); u32w(atual, u32(ultimo)); u32w(ultimo, t); }
        const uint32_t id = u16(ultimo) & 0x1fff;
        A->set_reg(c, 4, jog); A->set_reg(c, 5, pm);
        A->call_original(c, F_MAGIA);
        const int32_t r = (int32_t)A->reg(c, 2); const uint32_t modo = u32(pm);
        if (k != n - 1) { const uint32_t t = u32(atual); u32w(atual, u32(ultimo)); u32w(ultimo, t); }   /* desfaz a troca */
        if (r == (int32_t)(n - 1) && id) {
            const int atk = stat_carta(c, F_CARTA_ATK, id), def = stat_carta(c, F_CARTA_DEF, id), nivel = stat_carta(c, F_CARTA_NIVEL, id);
            const int trib = nivel >= 7 ? 2 : nivel >= 5 ? 1 : 0;
            const uint32_t modo_ia = atk > opp.atk_ameaca ? modo : 1u;   /* so em ataque se supera o maior atacante do oponente; senao baixa em defesa */
            int v = modo_ia ? def : atk;
            if (v > opp.melhor_atk) v += 1500;                    /* aguenta / vence o maior ataque visivel do oponente */
            if (po == DEFENSIVA) v += def / 3; else if (po == AGRESSIVA) v += atk / 3;
            cand[nc++] = Cand{(int)k, id, modo_ia, v - 800 * trib, atk, def};
        }
    }
    A->set_reg(c, 4, jog); A->set_reg(c, 5, pm);
    A->call_original(c, F_MAGIA);                               /* escolha original (deixa o estado como o jogo espera) */
    const int32_t orig = (int32_t)A->reg(c, 2);
    int best = 0; for (int i = 1; i < nc; i++) if (cand[i].score > cand[best].score) best = i;
    if (nc >= 1 && decisor_quer()) {   /* o modelo escolhe entre as melhores candidatas do jogo, em ataque ou em defesa virada, ou nenhuma */
        std::vector<Opcao> ops; std::vector<std::pair<int, uint32_t>> mapa;
        std::vector<int> ord; for (int i = 0; i < nc; i++) ord.push_back(i);
        for (int i = 0; i < nc; i++) for (int j = i + 1; j < nc; j++) if (cand[ord[j]].score > cand[ord[i]].score) std::swap(ord[i], ord[j]);
        std::vector<uint32_t> ids;
        for (int k = 0; k < nc && ids.size() < 4; k++) { bool dup = false; for (uint32_t v : ids) if (v == cand[ord[k]].id) dup = true; if (!dup) ids.push_back(cand[ord[k]].id); }
        const std::string sit = situacao_texto(c, jog, me, opp) + efeitos_texto(c, ids);
        for (int k = 0; k < nc; k++) {
            const int i = ord[k]; bool top = false; for (uint32_t v : ids) if (v == cand[i].id) top = true;
            if (!top) continue;
            for (uint32_t md = 0; md < 2; md++) {
                bool dup = false; for (size_t j = 0; j < mapa.size(); j++) if (cand[mapa[j].first].id == cand[i].id && mapa[j].second == md) dup = true;
                if (dup) continue;   /* a mesma carta duas vezes na mao: uma opcao so */
                ops.push_back({std::string(md ? "Set " : "Summon ") + carta_texto(c, cand[i].id, false) + (md ? " face-down in DEFENSE position. " : " face-up in ATTACK position. ") + nota_combate(c, cand[i].atk, cand[i].def, md == 0, opp)});
                mapa.push_back({i, md});
            }
        }
        ops.push_back({"Do not summon or set any monster this turn (keep the monsters in hand)."});
        int pad = (int)mapa.size(); for (size_t j = 0; j < mapa.size(); j++) if (cand[mapa[j].first].id == cand[best].id && mapa[j].second == cand[best].modo) pad = (int)j;
        const int d = decisor_decidir("which monster to summon or set this turn", (int)(jog & 1), sit, ops, pad);
        if (d >= (int)mapa.size()) { A->log("[ia] decisor: nenhum monstro neste turno"); A->set_reg(c, 2, 0xffffffffu); return 1; }
        u32w(pm, mapa[d].second); A->set_reg(c, 2, (uint32_t)cand[mapa[d].first].idx); return 1;
    }
    if (nc < 2) return 1;
    if (cand[best].idx != orig) {
        A->log("[ia] baixar monstro (jog %u, postura %s): jogo escolheria o indice %d; IA escolhe o indice %d (carta %u, modo %u, valor %d, %d candidatas)", jog, nome_postura(po), orig, cand[best].idx, cand[best].id, cand[best].modo, cand[best].score, nc);
        u32w(pm, cand[best].modo); A->set_reg(c, 2, (uint32_t)cand[best].idx);
    }
    return 1;
}

/* ------------------------------------------------------------------ 3. Dark Hole so quando compensa (regra 09e82bd4) */
/* O original joga com 1 monstro do oponente (a regra so exige que ele tenha algum) e nao guarda a carta para quando esta atras. Aqui: so joga se o saldo de
 * tabuleiro compensa; com 1 monstro so, apenas se for uma ameaca grande e eu estiver sem monstros e sem mao. */
static bool dark_hole_compensa(Lado &me, Lado &opp) {
    if (opp.n == 0) return false;
    int vo = 0, vm = 0; for (int i = 0; i < 5; i++) { if (opp.m[i].id) vo += valor_mon(opp.m[i]); if (me.m[i].id) vm += valor_mon(me.m[i]); }
    const int saldo = vo - vm;
    if (opp.n >= 2 && saldo >= 800) return true;
    if (opp.n >= 3 && saldo >= 0) return true;
    if (opp.n >= 2 && me.n == 0) return true;
    if (opp.n == 1 && me.n == 0 && opp.melhor_atk >= 1800 && (me.lp <= 3000 || me.mao <= 1)) return true;   /* ameaca grande e nada para responder */
    return false;
}
static int cartas_na_mao_(uint32_t p) { return (int)u32(0x09f79cf8 + (p & 1) * PASSO); }
static bool ctx_ok(uint32_t a) { return a >= 0x08000000u && a < 0x0a000000u; }
/* decide se joga uma carta agora: o modelo (se ligado e respondeu) ou a regra do plugin */
static bool decidir_ativar(TfCpu *c, uint32_t jog, uint32_t id, bool regra) {
    if (!decisor_quer()) return regra;
    Lado me, opp; postura(c, jog, &me, &opp);
    const std::string sit = situacao_texto(c, jog, me, opp);
    std::vector<Opcao> ops;
    ops.push_back({"Activate " + carta_texto(c, id, true) + "."});
    ops.push_back({"Do not activate it now; keep it for a better moment."});
    const std::string tipo = "activate " + nome_carta(c, id) + " now?";
    return decisor_decidir(tipo.c_str(), (int)(jog & 1), sit, ops, regra ? 0 : 1) == 0;
}
static int hk_dark_hole(TfCpu *c) {
    const uint32_t ctx = A->reg(c, 4);
    if (g_off || !ctx_ok(ctx) || (u16(ctx) & 0x1fff) != 4342) return 0;
    const uint32_t jog = A->mem(ctx + 2)[0] & 1;
    if (!melhora_r(jog, SEM_DARKHOLE)) return 0;
    A->call_original(c, F_PRED_DARKHOLE);
    if (!A->reg(c, 2)) return 1;                                  /* o jogo ja nao quer */
    Lado me, opp; postura(c, jog, &me, &opp);
    const bool ok = decidir_ativar(c, jog, 4342, dark_hole_compensa(me, opp));
    if (log_novo(3, (uint32_t)(ok * 1000 + opp.n * 100 + me.n * 10 + me.mao))) A->log("[ia] Dark Hole (jog %u): %s (oponente %d monstros, eu %d, mao %d)", jog, ok ? "JOGA" : "guarda", opp.n, me.n, me.mao);
    A->set_reg(c, 2, ok ? 1 : 0);
    return 1;
}

/* ------------------------------------------------------------------ 4. segurar o jogo: nao atacar as cegas e ir para a defesa */
/* texto e consequencias de um ataque planejado pelo jogo: atacante (casa atq) contra o monstro alvo (casa alvo) ou direto */
static std::string texto_ataque(TfCpu *c, const Lado &me, const Lado &opp, int p, int atq, int alvo, bool direto, bool *ganha_seguro) {
    Mon m = me.m[atq]; char b[320]; std::string t = "Attack with " + nome_carta(c, m.id) + " (ATK " + std::to_string(m.atk) + ") ";
    *ganha_seguro = false;
    if (direto || alvo < 0 || alvo > 4 || !opp.m[alvo].id) {
        snprintf(b, sizeof b, "directly: the opponent (%d LP) would lose %d LP%s.", opp.lp, m.atk, m.atk >= opp.lp ? " - that is LETHAL, you win the duel" : "");
        *ganha_seguro = true; return t + b;
    }
    const Mon &o = opp.m[alvo];
    if (!o.vis) {
                  snprintf(b, sizeof b, "into a face-down monster of unknown strength: it may be a weak monster that gets destroyed, or a strong one that survives and hurts you."); return t + b; }
    const std::string nm = nome_carta(c, o.id);
    std::string extra;
    /* Skyscraper (6399) no meu campo: um Elemental HERO que ataca um monstro de ATK maior ganha +1000 de ATK no calculo de dano (o texto/ATK da carta nao mostra isso) */
    if ((u16(CASAS + (p & 1) * PASSO + 10 * 0x14) & 0x1fff) == 6399 && o.atk > m.atk && nome_carta(c, m.id).compare(0, 14, "Elemental Hero") == 0) {
        m.atk += 1000; snprintf(b, sizeof b, "(Skyscraper is on your side: an Elemental HERO attacking a stronger monster gets +1000 ATK, so it fights with ATK %d) ", m.atk); extra = b;
    }
    t += extra;
    if (!o.defesa) {
        if (m.atk > o.atk) { snprintf(b, sizeof b, "into %s (ATK %d, attack position): it is destroyed and the opponent loses %d LP; yours survives.", nm.c_str(), o.atk, m.atk - o.atk); *ganha_seguro = true; }
        else if (m.atk == o.atk) snprintf(b, sizeof b, "into %s (ATK %d, attack position): both monsters are destroyed.", nm.c_str(), o.atk);
        else snprintf(b, sizeof b, "into %s (ATK %d, attack position): yours is destroyed and you lose %d LP.", nm.c_str(), o.atk, o.atk - m.atk);
    } else {
        if (m.atk > o.def) { snprintf(b, sizeof b, "into %s (DEF %d, defense position): it is destroyed, no LP loss for anyone; yours survives.", nm.c_str(), o.def); *ganha_seguro = true; }
        else if (m.atk == o.def) snprintf(b, sizeof b, "into %s (DEF %d, defense position): nothing is destroyed.", nm.c_str(), o.def);
        else snprintf(b, sizeof b, "into %s (DEF %d, defense position): nothing is destroyed and you lose %d LP.", nm.c_str(), o.def, o.def - m.atk);
    }
    return t + b;
}
/* Ordem dos ataques: o planejador original ordena os atacantes do MAIS FRACO para o mais forte (selecao por chave crescente) e fica com o primeiro que
 * tem um plano. Aqui o mais forte vai primeiro (design decision, 2026-10-06): mais dano garantido se algo interromper a fase de batalha e o monstro maior
 * resolve a ameaca maior. Reaproveita a avaliacao do jogo (F_ATAQUE por atacante, a mesma chamada do original): se o mais forte tem plano, ele vira o plano. */
static const uint32_t F_PODE_ATACAR = 0x09f5060c;   /* libduel_eng_43595669(jog, casa, 1): o monstro pode atacar agora (filtro do planejador) */
static void ataque_mais_forte_primeiro(TfCpu *c, uint32_t jog) {
    Lado me; ler_lado(c, (int)(jog & 1), false, me);
    int ord[5], n = 0;
    for (int i = 0; i < 5; i++) if (me.m[i].id && chama(c, F_PODE_ATACAR, jog, i, 1)) ord[n++] = i;
    for (int a = 1; a < n; a++) { const int v = ord[a]; int b = a - 1; while (b >= 0 && me.m[ord[b]].atk < me.m[v].atk) { ord[b + 1] = ord[b]; b--; } ord[b + 1] = v; }   /* ATK decrescente, estavel */
    const int atual = (int)u32(0x09f77d88);
    if (atual < 0 || atual > 4) return;
    const uint32_t buf = g_buf + 128;
    for (int k = 0; k < n; k++) {
        const int i = ord[k];
        if (i == atual || me.m[i].atk <= me.m[atual].atk) return;   /* o plano atual ja e do mais forte (ou de um empate) */
        if (chama5(c, F_ATAQUE, jog, (uint32_t)i, 1, buf, 0)) {
            memcpy(A->mem(0x09f77d80), A->mem(buf), 0x20);
            if (log_novo(4, (uint32_t)(i * 10 + atual))) A->log("[ia] ordem de ataque: casa %d (ATK %d) antes da casa %d (ATK %d)", i, me.m[i].atk, atual, me.m[atual].atk);
            return;
        }
    }
}
static int hk_plano(TfCpu *c) {   /* 09e75258(jog, flag): planejador de ataque; flag < 0 = simulacao da escolha da mao (nao mexer) */
    const uint32_t jog = A->reg(c, 4); const int32_t flag = (int32_t)A->reg(c, 5);
    const bool segurar = melhora_r(jog, SEM_SEGURAR), ordem = melhora_r(jog, SEM_ORDEM);
    if ((!segurar && !ordem) || flag < 0) return 0;
    A->call_original(c, F_PLANO);
    const uint32_t r = A->reg(c, 2);
    if (!r) return 1;
    if (ordem) ataque_mais_forte_primeiro(c, jog);
    if (!segurar) return 1;
    Lado me, opp; const Postura po = postura(c, jog, &me, &opp);
    const int danoCausa = (int)u32(0x09f77d9c), direto = (int)u32(0x09f77d80), atq = (int)u32(0x09f77d88), alvo = (int)u32(0x09f77d8c);
    const bool letal = direto && danoCausa >= opp.lp;
    bool seguro = false; std::string txt;
    const bool ok_atq = atq >= 0 && atq <= 4 && me.m[atq].id;
    if (ok_atq) txt = texto_ataque(c, me, opp, (int)(jog & 1), atq, alvo, direto != 0, &seguro);
    /* regra: segura o jogo (postura defensiva sem recursos contra varias cartas viradas) a menos que o ataque seja letal ou claramente vencedor contra monstro visivel */
    bool vetar = !letal && po == DEFENSIVA && me.n <= 1 && me.mao <= 1 && opp.nback_fd >= 2 && !(seguro && !direto);
    if (ok_atq && decisor_quer()) {
        std::vector<Opcao> ops;
        ops.push_back({txt + (opp.nback_fd ? " Beware: the opponent has " + std::to_string(opp.nback_fd) + " face-down spell/trap card(s) that may punish an attack." : "")});
        ops.push_back({"Do not attack: end the battle phase and keep the monster(s) as they are."});
        std::string sit = situacao_texto(c, jog, me, opp);
        if (opp.atk_ameaca > 0) sit += "The opponent's strongest face-up attacker has ATK " + std::to_string(opp.atk_ameaca) + ".\n";
        vetar = decisor_decidir("attack with this monster now?", (int)(jog & 1), sit, ops, vetar ? 1 : 0) == 1;
    }
    if (vetar) {
        if (log_novo(2, (uint32_t)(me.n * 100 + me.mao * 10 + opp.nback_fd))) A->log("[ia] ataque vetado (postura %s: eu %d monstro(s), mao %d, oponente com %d cartas viradas): segurando o jogo", nome_postura(po), me.n, me.mao, opp.nback_fd);
        A->set_reg(c, 2, 0); return 1;
    }
    return 1;
}

/* decisao de posicao (09e6cff0(jog, casa) -> 0 ataque / 1 defesa / -1): na postura defensiva, monstro que nao supera o maior ataque do oponente fica em defesa */
static int hk_posicao_dec(TfCpu *c) {
    const uint32_t jog = A->reg(c, 4), casa = A->reg(c, 5);
    if (!melhora_r(jog, SEM_POSICAO) || casa > 4) return 0;
    A->call_original(c, F_POSDEC);
    const int32_t r = (int32_t)A->reg(c, 2);
    if (r != 0) return 1;                                         /* o jogo ja quer defesa (ou nao decidiu) */
    Lado me, opp; const Postura po = postura(c, jog, &me, &opp);
    if (po == DEFENSIVA && me.m[casa].atk <= opp.melhor_atk) {
        if (log_novo(1, casa * 100003u + (uint32_t)me.m[casa].atk)) A->log("[ia] monstro da casa %u (ATK %d) vai para a defesa (postura defensiva, maior ATK do oponente %d)", casa, me.m[casa].atk, opp.melhor_atk);
        A->set_reg(c, 2, 1);
    }
    return 1;
}

/* ------------------------------------------------------------------ 5. regras extras de cartas (ensina a CPU a jogar cartas que o jogo nunca joga) */
/* O jogo joga magias da mao por uma TABELA de regras {id da carta, predicado(ctx,0)}: 09e87760(jog, entrada) acha a carta, confere a legalidade e o predicado
 * e joga. Carta fora da tabela a CPU nunca joga. Aqui: depois da ULTIMA entrada da tabela 0, tentamos as entradas sinteticas do plugin {id, 09e8133c}; o
 * predicado "sempre verdadeiro" 09e8133c e enganchado e, para os ids do plugin, decide pela regra em C++ (quando). Para ensinar uma carta nova: uma linha em
 * g_regras. Alvos e custos continuam sendo escolhidos pelo jogo. ctx: u16 +0 = id; byte +2: bit0 = dono. */
static bool quando_lightning_vortex(TfCpu *c, uint32_t jog) {   /* destroi os monstros virados para cima do oponente; custo: descartar 1 carta */
    Lado me, opp; postura(c, jog, &me, &opp);
    if (me.mao < 2 || opp.nvis == 0) return false;
    int vo = 0; for (int i = 0; i < 5; i++) if (opp.m[i].id && opp.m[i].vis) vo += valor_mon(opp.m[i]);
    if (opp.nvis >= 2 && vo >= 2400) return true;
    if (opp.melhor_atk >= 1800 && (me.n >= 1 || me.mao >= 3)) return true;
    return me.n >= 1 && vo >= 1500;                               /* abre caminho para os meus atacarem */
}
static bool quando_brain_control(TfCpu *c, uint32_t jog) {      /* custo 800 PV; toma 1 monstro do oponente ate o fim do turno */
    Lado me, opp; postura(c, jog, &me, &opp);
    if (me.lp <= 1500) return false;
    return opp.nvis >= 1 && opp.melhor_atk >= 1600 && opp.melhor_atk > me.melhor_atk;
}
/* cemiterio do jogador p: contador em +0x14, ids (u16) em +0x710 + i*4 */
static const uint32_t F_E_MONSTRO = 0x0884c0d8;   /* libehsys_E5738C32(id): a carta e um monstro? */
static int cem_n(uint32_t p) { return (int)u32(JOG + (p & 1) * PASSO + 0x14); }
static uint32_t cem_id(uint32_t p, int i) { return u16(JOG + (p & 1) * PASSO + 0x710 + i * 4) & 0x1fff; }
static int melhor_atk_cemiterio(TfCpu *c, uint32_t p, int *n_monstros = nullptr) {   /* maior ATK entre os monstros do cemiterio */
    int best = 0, nm = 0;
    for (int i = 0; i < cem_n(p) && i < 40; i++) {
        const uint32_t id = cem_id(p, i);
        if (!id || !chama(c, F_E_MONSTRO, id)) continue;
        nm++; const int a = stat_carta(c, F_CARTA_ATK, id); if (a > best) best = a;
    }
    if (n_monstros) *n_monstros = nm;
    return best;
}
static bool quando_premature_burial(TfCpu *c, uint32_t jog) {   /* custo 800 PV: revive um monstro do proprio cemiterio, equipado */
    Lado me, opp; postura(c, jog, &me, &opp);
    if (me.lp <= 2000 || me.n >= 5) return false;
    const int best = melhor_atk_cemiterio(c, jog);
    return best >= 1400 && (me.n == 0 || best > me.melhor_atk);
}
static bool quando_monster_reborn(TfCpu *c, uint32_t jog) {   /* revive o melhor monstro dos dois cemiterios */
    Lado me, opp; postura(c, jog, &me, &opp);
    if (me.n >= 5) return false;
    const int best = melhor_atk_cemiterio(c, jog) > melhor_atk_cemiterio(c, 1 - (jog & 1)) ? melhor_atk_cemiterio(c, jog) : melhor_atk_cemiterio(c, 1 - (jog & 1));
    return best >= 1200 && (me.n == 0 || best > me.melhor_atk);
}
static bool quando_call_of_the_haunted(TfCpu *c, uint32_t jog) {   /* armadilha: revive um monstro do proprio cemiterio */
    Lado me, opp; postura(c, jog, &me, &opp);
    if (me.n >= 5) return false;
    const int best = melhor_atk_cemiterio(c, jog);
    return best >= 1000 && (me.n == 0 || best > me.melhor_atk);
}
static bool quando_dark_factory(TfCpu *c, uint32_t jog) {   /* devolve 2 monstros normais do cemiterio para a mao */
    int nm = 0; melhor_atk_cemiterio(c, jog, &nm);
    return nm >= 2 && cartas_na_mao_(jog) <= 5;
}
static bool quando_pot_of_avarice(TfCpu *c, uint32_t jog) {   /* 5 monstros do cemiterio voltam ao deck, compra 1 */
    int nm = 0; melhor_atk_cemiterio(c, jog, &nm);
    return nm >= 5;
}
static bool quando_equipamento(TfCpu *c, uint32_t jog) {   /* equipamento: so com um monstro meu virado para cima para receber */
    Lado me, opp; postura(c, jog, &me, &opp);
    return me.nvis >= 1;
}
struct Regra { uint32_t id; const char *nome; bool (*quando)(TfCpu *, uint32_t jog); };
static const Regra g_regras[] = {
    {4803, "Brain Control", quando_brain_control},
    {5217, "Lightning Vortex", quando_lightning_vortex},
    {4966, "Premature Burial", quando_premature_burial},
    {4842, "Monster Reborn", quando_monster_reborn},
    {4989, "Call of the Haunted", quando_call_of_the_haunted},
    {6129, "Dark Factory of Mass Production", quando_dark_factory},
    {6511, "Pot of Avarice", quando_pot_of_avarice},
    {4334, "Power of Kaishin", quando_equipamento},
    {4315, "Steel Shell", quando_equipamento},
    {4890, "Stim-Pack", quando_equipamento},
};
static const int N_REGRAS = (int)(sizeof g_regras / sizeof g_regras[0]);
static uint32_t g_extra;   /* tabela sintetica no PSP: {id, F_PRED_SIM} */
static int hk_pred_sim(TfCpu *c) {
    const uint32_t ctx = A->reg(c, 4);
    if (g_off || !ctx_ok(ctx)) return 0;
    const uint32_t id = u16(ctx) & 0x1fff, jog = A->mem(ctx + 2)[0] & 1;
    for (int i = 0; i < N_REGRAS; i++) if (g_regras[i].id == id) {
        const bool ok = melhora_r(jog, SEM_EXTRAS) && (getenv("TF_IA_FORCAR_REGRAS") || decidir_ativar(c, jog, id, g_regras[i].quando(c, jog)));
        A->log("[ia] regra extra %s (jog %u): %s", g_regras[i].nome, jog, ok ? "JOGA" : "nao");
        A->set_reg(c, 2, ok ? 1 : 0); return 1;
    }
    return 0;
}
static int hk_exec(TfCpu *c) {   /* 09e87760(jog, entrada): depois da ultima entrada da tabela 0, tenta as do plugin */
    const uint32_t jog = A->reg(c, 4), ent = A->reg(c, 5);
    if (ent != TAB0 + (TAB0_N - 1) * 8 || !melhora_r(jog, SEM_EXTRAS) || !g_extra) return 0;
    if (const char *e = getenv("TF_IA_DAR_CARTA")) {              /* teste: troca a 1a carta da mao da CPU por esta (uma vez, quando o oponente tem monstro visivel) */
        static bool dado; Lado opp; ler_lado(c, 1 - (jog & 1), true, opp);
        if (!dado && (getenv("TF_IA_DAR_GY") ? cem_n(jog) >= 1 : opp.nvis >= 1)) { dado = true; const uint32_t h = JOG + (jog & 1) * PASSO + 0x120; u16w(h, (u16(h) & 0xe000) | (atoi(e) & 0x1fff)); A->log("TESTE: carta %s dada a CPU", e); }
    }
    A->call_original(c, F_EXEC);
    if (A->reg(c, 2)) return 1;
    for (int i = 0; i < N_REGRAS; i++) {
        A->set_reg(c, 4, jog); A->set_reg(c, 5, g_extra + i * 8);
        A->call_original(c, F_EXEC);
        if (A->reg(c, 2)) { A->log("[ia] regra extra: %s jogada (jog %u)", g_regras[i].nome, jog); return 1; }
    }
    A->set_reg(c, 2, 0); return 1;
}

/* ------------------------------------------------------------------ sonda (pesquisa): registra as decisoes originais da CPU */
static void tabuleiro(TfCpu *c, const char *rotulo) {
    A->log("== %s quadro %llu", rotulo, (unsigned long long)A->frame());
    for (int p = 0; p < 2; p++) {
        const uint32_t base = JOG + p * PASSO;
        char linha[900]; int n = snprintf(linha, sizeof linha, "J%d LP=%u mao=%u(", p, u32(base), u32(0x09f79cf8 + p * PASSO));
        for (uint32_t h = 0; h < u32(0x09f79cf8 + p * PASSO) && h < 8; h++) n += snprintf(linha + n, sizeof linha - n, "%s%u", h ? "," : "", u16(JOG + p * PASSO + 0x120 + h * 4) & 0x1fff);
        n += snprintf(linha + n, sizeof linha - n, ") |");
        for (int s = 0; s < 10; s++) {
            const uint32_t cs = CASAS + p * PASSO + s * 0x14; const unsigned id = u16(cs) & 0x1fff;
            if (!id) continue;
            if (s < 5) { chama(c, F_STATS, p, s, g_buf); n += snprintf(linha + n, sizeof linha - n, " [%d:%u atk=%d def=%d f=%08x]", s, id, (int)u32(g_buf + 20), (int)u32(g_buf + 24), u32(cs + 0x10)); }
            else n += snprintf(linha + n, sizeof linha - n, " [ST%d:%u f=%08x]", s, id, u32(cs + 0x10));
        }
        A->log("%s", linha);
        { char g[500]; int m = snprintf(g, sizeof g, "J%d cemiterio(%u):", p, u32(base + 0x14)); for (uint32_t i = 0; i < u32(base + 0x14) && i < 30; i++) m += snprintf(g + m, sizeof g - m, " %u", u16(base + 0x710 + i * 4) & 0x1fff); A->log("%s", g); }
    }
}
/* 09e762e0(jog, modo): escolhe, pela simulacao do ataque, um monstro da mao para INVOCAR em posicao de ataque. A simulacao so olha o que ele consegue atacar e ignora a resposta do
 * oponente: o Jaden baixava o Sparkman 1600 em ataque diante de um Amphibian Beast 2400 e perdia o monstro no turno seguinte. Aqui: se o monstro escolhido nao supera o maior atacante
 * do oponente, a escolha e vetada (-1) e o jogo cai no caminho de baixar o monstro em defesa (hk_magia). */
static int hk_mao(TfCpu *c) {
    const uint32_t jog = A->reg(c, 4);
    if (!melhora_r(jog, SEM_INVOCAR)) return 0;
    A->call_original(c, F_MAO);
    const int32_t r = (int32_t)A->reg(c, 2);
    if (r < 0 || r > 8) return 1;
    const uint32_t id = u16(JOG + (jog & 1) * PASSO + 0x120 + r * 4) & 0x1fff;
    if (!id) return 1;
    Lado opp; ler_lado(c, 1 - (jog & 1), true, opp);
    const int atk = stat_carta(c, F_CARTA_ATK, id);
    bool vetar = opp.atk_ameaca > 0 && atk <= opp.atk_ameaca;
    if (decisor_quer()) {   /* o modelo decide: invocar esta carta em ataque ou nao */
        Lado me; ler_lado(c, jog & 1, false, me);
        std::vector<Opcao> ops;
        ops.push_back({"Summon " + carta_texto(c, id, false) + " face-up in ATTACK position (the game's attack simulation likes this). " + nota_combate(c, atk, stat_carta(c, F_CARTA_DEF, id), true, opp)});
        ops.push_back({"Do not summon it in attack position (the game will then set a monster face-down in defense, or do something else)."});
        const std::string sit = situacao_texto(c, jog, me, opp) + efeitos_texto(c, std::vector<uint32_t>{id});
        vetar = decisor_decidir("summon this monster in attack position?", (int)(jog & 1), sit, ops, vetar ? 1 : 0) == 1;
    }
    if (vetar) {
        if (log_novo(4, id * 31u + (uint32_t)opp.atk_ameaca)) A->log("[ia] invocar em ataque vetado: carta %u (ATK %d), maior atacante do oponente ATK %d", id, atk, opp.atk_ameaca);
        A->set_reg(c, 2, 0xffffffffu);
    }
    return 1;
}
static int hk_sonda_mao(TfCpu *c) {
    const uint32_t jog = A->reg(c, 4); tabuleiro(c, "escolher carta da mao");
    A->call_original(c, F_MAO);
    const int32_t r = (int32_t)A->reg(c, 2);
    A->log("ia_mao(jog=%u) -> indice %d (carta %u)", jog, r, r >= 0 ? (u16(JOG + (jog & 1) * PASSO + 0x120 + r * 4) & 0x1fff) : 0);
    return 1;
}
static int hk_sonda_jogar(TfCpu *c) {
    const uint32_t jog = A->reg(c, 4), idx = A->reg(c, 5), modo = A->reg(c, 6);
    const unsigned id = idx < 12 ? (u16(JOG + (jog & 1) * PASSO + 0x120 + idx * 4) & 0x1fff) : 0;   /* idx sem sinal: negativo vira enorme e cai fora */
    A->call_original(c, F_JOGAR);
    A->log("jogar_da_mao(jog=%u indice=%d carta=%u modo=%u) -> %d quadro %llu", jog, (int)idx, id, modo, (int)A->reg(c, 2), (unsigned long long)A->frame());
    return 1;
}
static int hk_sonda_plano(TfCpu *c) {
    const uint32_t jog = A->reg(c, 4); const int32_t flag = (int32_t)A->reg(c, 5);
    if (flag >= 0) tabuleiro(c, "plano de ataque");
    A->call_original(c, F_PLANO);
    if (flag >= 0) A->log("plano_ataque(jog=%u flag=%d) -> %u  dir=%u atacar=%u atq=%u alvo=%u danoSofre=%d danoCausa=%d", jog, flag, A->reg(c, 2), u32(0x09f77d80), u32(0x09f77d84), u32(0x09f77d88), u32(0x09f77d8c), (int)u32(0x09f77d98), (int)u32(0x09f77d9c));
    return 1;
}

/* ------------------------------------------------------------------ funcao de VALOR aprendida (tools/ai/ia_valor): "quem esta ganhando" no lugar da avaliacao original do jogo */
/* O jogo escolhe o monstro a invocar em ataque (09e762e0) comparando uma NOTA do tabuleiro hipotetico (09e75cc0 = dano total que o ataque causaria, dentro de 09e7604c) com a nota do
 * tabuleiro atual: so invoca se a nota sobe. Aqui a nota passa a ser a probabilidade de vencer prevista por uma MLP treinada com ~700 duelos da arena (AUC 0,84 em duelos novos),
 * *10000. Mesmos 35 numeros de entrada de tools/ai/ia_valor/dataset.py (mantenha as duas versoes iguais). TF_IA_VALOR=1 liga; TF_IA_VALOR_LADO=0/1 so esse jogador (A/B). */
#include "valor_pesos.h"
static bool g_valor; static int g_valor_lado = -1;
static float valor_prever(const float *x) {
    float h1[VALOR_N1], h2[VALOR_N2];
    for (int i = 0; i < VALOR_N1; i++) { float a = VALOR_B1[i]; for (int j = 0; j < VALOR_NIN; j++) a += VALOR_W1[i * VALOR_NIN + j] * x[j]; h1[i] = tanhf(a); }
    for (int i = 0; i < VALOR_N2; i++) { float a = VALOR_B2[i]; for (int j = 0; j < VALOR_N1; j++) a += VALOR_W2[i * VALOR_N1 + j] * h1[j]; h2[i] = tanhf(a); }
    float z = VALOR_B3[0]; for (int j = 0; j < VALOR_N2; j++) z += VALOR_W3[j] * h2[j];
    return 1.0f / (1.0f + expf(-z));
}
struct StatCarta { bool monstro; int atk, def; bool armadilha; };
static const StatCarta &stat_cache(TfCpu *c, uint32_t id) {   /* ATK/DEF/tipo BASE da carta (igual ao banco de cartas do Python), em cache */
    static std::map<uint32_t, StatCarta> cache; auto it = cache.find(id); if (it != cache.end()) return it->second;
    StatCarta s; s.monstro = chama(c, 0x0884c0d8, id) != 0; s.atk = s.monstro ? (int)chama(c, F_CARTA_ATK, id) : 0; s.def = s.monstro ? (int)chama(c, F_CARTA_DEF, id) : 0;
    if (s.atk < 0 || s.atk > 20000) s.atk = 0; if (s.def < 0 || s.def > 20000) s.def = 0;   /* "?" = 0x1ff*10 */
    s.armadilha = !s.monstro && chama(c, 0x0884b754, id) == 9;
    return cache[id] = s;
}
static int extrair_lado(TfCpu *c, int p, bool conhecido, float *v) {
    const uint32_t base = JOG + p * PASSO;
    const int lp = (int)u32(base), hand_n = (int)u32(0x09f79cf8 + p * PASSO), deck_n = (int)u32(0x09f79cfc + p * PASSO);
    int up_atk = 0, up_def = 0, down = 0, st = 0, st_down = 0, campo = 0, soma_atk = 0, soma_def = 0, max_atk = 0, max_def = 0; float poder = 0;
    for (int sl = 0; sl < 5; sl++) {
        const uint32_t cs = CASAS + p * PASSO + sl * 0x14, id = u16(cs) & 0x1fff; if (!id) continue;
        const uint32_t fl = u32(cs + 0x10); const bool vis = (fl & 0x80) != 0, defesa = (fl & 0x08) != 0 || !vis; const StatCarta &sc = stat_cache(c, id);
        if (vis) {
            if (defesa) { up_def++; soma_def += sc.def; if (sc.def > max_def) max_def = sc.def; } else { up_atk++; soma_atk += sc.atk; if (sc.atk > max_atk) max_atk = sc.atk; }
            const float d7 = 0.7f * sc.def; poder += sc.atk > d7 ? (float)sc.atk : d7;
        } else { down++; poder += 1200; if (1200 > max_def) max_def = 1200; }
    }
    for (int sl = 5; sl < 10; sl++) { const uint32_t cs = CASAS + p * PASSO + sl * 0x14; if (!(u16(cs) & 0x1fff)) continue; st++; if (!(u32(cs + 0x10) & 0x80)) st_down++; }
    if (u16(CASAS + p * PASSO + 10 * 0x14) & 0x1fff) campo = 1;
    int n = 0;
    v[n++] = lp / 8000.0f; v[n++] = hand_n / 8.0f; v[n++] = deck_n / 40.0f; v[n++] = up_atk / 5.0f; v[n++] = up_def / 5.0f; v[n++] = down / 5.0f; v[n++] = st / 5.0f; v[n++] = st_down / 5.0f;
    v[n++] = (float)campo; v[n++] = soma_atk / 10000.0f; v[n++] = max_atk / 4000.0f; v[n++] = soma_def / 10000.0f; v[n++] = max_def / 4000.0f; v[n++] = poder / 10000.0f;
    if (conhecido) {
        int mm = 0, sm = 0, tm = 0, msoma = 0, mmax = 0;
        for (int i = 0; i < hand_n && i < 12; i++) {
            const uint32_t id = u16(base + 0x120 + i * 4) & 0x1fff; if (!id) continue; const StatCarta &sc = stat_cache(c, id);
            if (sc.monstro) { mm++; msoma += sc.atk; if (sc.atk > mmax) mmax = sc.atk; } else if (sc.armadilha) tm++; else sm++;
        }
        v[n++] = mm / 6.0f; v[n++] = sm / 6.0f; v[n++] = tm / 6.0f; v[n++] = mmax / 4000.0f; v[n++] = msoma / 10000.0f;
    }
    return n;
}
static float valor_do_tabuleiro(TfCpu *c, int p) {   /* probabilidade de o jogador p vencer, do ponto de vista dele, na sua vez */
    float f[VALOR_NIN]; int n = extrair_lado(c, p, true, f); n += extrair_lado(c, 1 - p, false, f + n);
    f[n++] = 1.0f; f[n++] = ((int)u32(JOG + p * PASSO) - (int)u32(JOG + (1 - p) * PASSO)) / 8000.0f;
    return n == VALOR_NIN ? valor_prever(f) : 0.5f;
}
static int hk_avalia(TfCpu *c) {   /* FUN_09e75cc0(jogador): nota do tabuleiro (original: dano total do ataque simulado) */
    const uint32_t jog = A->reg(c, 4);
    if (g_off || !g_valor || !cpu(jog) || (g_valor_lado >= 0 && g_valor_lado != (int)(jog & 1))) return 0;
    const float v = valor_do_tabuleiro(c, (int)(jog & 1));
    static uint64_t ult; if (A->frame() > ult + 600) { ult = A->frame(); A->log("[valor] J%u tabuleiro hipotetico: P(vencer) = %.3f", jog & 1, v); }
    A->set_reg(c, 2, (uint32_t)(int)(v * 10000.0f)); return 1;
}

extern "C" __declspec(dllexport) int tfmod_init(const TfModApi *api) {
    A = api; if (A->version < 3) { A->log("IA Aprimorada precisa da API 3 (runtime mais novo)"); return 1; }
    g_buf = A->guest_alloc(256); if (!g_buf) return 2;   /* sem a area do PSP as consultas ao jogo escreveriam no endereco 0 */
    decisor_init(A->mod_dir, A->log);
    g_off = getenv("TF_IA_OFF") != nullptr; g_sonda = getenv("TF_IA_SONDA") != nullptr;
    if (const char *e = getenv("TF_IA_LADO")) g_lado = atoi(e);
    if (const char *e = getenv("TF_IA_SEM")) { std::string t = e; struct { const char *n; uint32_t b; } tb[] = {{"invocar", SEM_INVOCAR}, {"posicao", SEM_POSICAO}, {"segurar", SEM_SEGURAR}, {"monstro", SEM_MONSTRO}, {"darkhole", SEM_DARKHOLE}, {"extras", SEM_EXTRAS}, {"ordem", SEM_ORDEM}}; for (auto &x : tb) if (t.find(x.n) != std::string::npos) g_sem |= x.b; A->log("[ia] regras desligadas (pesquisa): %s (lado %d)", e, getenv("TF_IA_SEM_LADO") ? atoi(getenv("TF_IA_SEM_LADO")) : -1); }
    if (const char *e = getenv("TF_IA_SEM_LADO")) g_sem_lado = atoi(e);
    g_valor = getenv("TF_IA_VALOR") != nullptr && atoi(getenv("TF_IA_VALOR")) != 0; if (const char *e = getenv("TF_IA_VALOR_LADO")) g_valor_lado = atoi(e);
    if (g_valor) A->hook(0x09e75cc0, hk_avalia);
    g_extra = A->guest_alloc(8 * N_REGRAS);
    for (int i = 0; i < N_REGRAS; i++) { u32w(g_extra + i * 8, g_regras[i].id); u32w(g_extra + i * 8 + 4, F_PRED_SIM); }
    A->hook(F_MAGIA, hk_magia); if (!g_sonda) A->hook(F_MAO, hk_mao); A->hook(F_PLANO, g_sonda ? hk_sonda_plano : hk_plano); A->hook(F_POSDEC, hk_posicao_dec);
    A->hook(F_PRED_DARKHOLE, hk_dark_hole); A->hook(F_PRED_SIM, hk_pred_sim); A->hook(F_EXEC, hk_exec);
    if (g_sonda) { A->hook(F_MAO, hk_sonda_mao); A->hook(F_JOGAR, hk_sonda_jogar); }
    A->log("IA Aprimorada carregada%s", g_off ? " (DESLIGADA por TF_IA_OFF)" : "");
    return 0;
}

/* F7 / L3+R3 no jogo ("a CPU errou"): despeja as ultimas decisoes do plugin (o que a CPU via e escolheu) */
extern "C" __declspec(dllexport) void tfmod_marca(int n) { decisor_despejar(n); }
