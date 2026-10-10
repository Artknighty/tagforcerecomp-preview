#include "mouse_ui.h"
#include "hle.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <algorithm>

namespace {
struct Spr { int x0, y0, x1, y1; float u0, v0, u1, v1; uint32_t tex, col; };
struct Run { int x0, y0, x1, y1; };   /* varios sprites num PRIM so (uma linha de texto) */
struct Bar { int x0, y0, x1, y1; uint64_t sig; size_t last; };

std::map<uint32_t, std::vector<Spr>> g_cur;   /* sprites desenhados em cada buffer desde que ele foi exibido pela ultima vez */
std::map<uint32_t, std::vector<Run>> g_cur_runs; std::vector<Run> g_runs;   /* linhas de texto (por buffer / do quadro exibido) */
std::vector<Spr> g_snap;                      /* sprites do quadro exibido */
std::vector<Bar> g_list;                      /* barras da lista de menu do quadro exibido (mesmo tamanho, >= 2) */
std::set<uint64_t> g_known;                   /* assinaturas ja vistas como 'selecionada' em listas com 3 ou mais barras (valem tambem para listas de 2) */
std::set<uint64_t> g_normal;                  /* assinaturas ja vistas em 2 ou mais barras de uma mesma lista (estilo 'nao selecionada') */
int g_sel = -1;                               /* indice em g_list da barra selecionada (-1 = nao deu para saber) */
const bool g_mtrace = getenv("TF_MOUSE_TRACE") != nullptr;

uint64_t mix(uint64_t h, uint64_t v) { return (h ^ v) * 0x100000001b3ull; }

void trace_list() {
    if (!g_mtrace) return;
    static std::string last; char buf[4096]; int n = snprintf(buf, sizeof buf, "sel=%d known=%zu:", g_sel, g_known.size());
    for (const Bar &b : g_list) n += snprintf(buf + n, sizeof buf - n, " [%d,%d %dx%d %04x]", b.x0, b.y0, b.x1 - b.x0, b.y1 - b.y0, (unsigned)(b.sig & 0xffff));
    if (last != buf) { last = buf; fprintf(stderr, "[mouse] lista: %s\n", buf); }
}

/* Lista de texto (mapa, menus claros): linhas de texto de mesmo x e passo constante; a selecionada e a que tem um realce (moldura 9 pedacos de uma cor so) atras. */
void analyze_text() {
    std::vector<Run> r;   /* junta o texto e a sombra dele (mesmo lugar, +-2 px) */
    for (const Run &a : g_runs) {
        if (a.y1 - a.y0 < 8 || a.y1 - a.y0 > 40) continue;
        bool merged = false;
        for (Run &b : r) if (std::abs(a.x0 - b.x0) <= 2 && std::abs(a.y0 - b.y0) <= 2) { b.x0 = std::min(b.x0, a.x0); b.y0 = std::min(b.y0, a.y0); b.x1 = std::max(b.x1, a.x1); b.y1 = std::max(b.y1, a.y1); merged = true; break; }
        if (!merged) r.push_back(a);
    }
    std::sort(r.begin(), r.end(), [](const Run &a, const Run &b) { return a.y0 < b.y0; });
    std::vector<Run> best;
    for (size_t i = 0; i < r.size(); i++) {
        std::vector<Run> ch = {r[i]};
        int pitch = 0;
        for (size_t j = i + 1; j < r.size(); j++) {
            if (std::abs(r[j].x0 - r[i].x0) > 2) continue;
            const int d = r[j].y0 - ch.back().y0;
            if (!pitch) { if (d >= r[i].y1 - r[i].y0 && d <= 60) { pitch = d; ch.push_back(r[j]); } }
            else if (std::abs(d - pitch) <= 1) ch.push_back(r[j]);
        }
        if (ch.size() > best.size()) best = ch;
    }
    if (best.size() < 3) return;
    const int pitch = best[1].y0 - best[0].y0;
    int left = best[0].x0, right = best[0].x1;
    for (const Run &b : best) { left = std::min(left, b.x0); right = std::max(right, b.x1); }
    /* realces: componentes de sprites de mesma textura e cor que se tocam */
    std::vector<int> comp(g_snap.size()); for (size_t i = 0; i < comp.size(); i++) comp[i] = (int)i;
    auto find = [&](int x) { while (comp[x] != x) x = comp[x] = comp[comp[x]]; return x; };
    for (size_t i = 0; i < g_snap.size(); i++) for (size_t j = i + 1; j < g_snap.size(); j++) {
        const Spr &a = g_snap[i], &b = g_snap[j];
        if (a.tex == b.tex && a.col == b.col && a.x0 <= b.x1 + 1 && b.x0 <= a.x1 + 1 && a.y0 <= b.y1 + 1 && b.y0 <= a.y1 + 1) comp[find((int)i)] = find((int)j);
    }
    std::map<int, std::tuple<int, int, int, int, int>> bb;   /* raiz -> x0,y0,x1,y1,n */
    for (size_t i = 0; i < g_snap.size(); i++) {
        const Spr &a = g_snap[i]; auto it = bb.find(find((int)i));
        if (it == bb.end()) bb[find((int)i)] = std::make_tuple(a.x0, a.y0, a.x1, a.y1, 1);
        else { auto &t = it->second; std::get<0>(t) = std::min(std::get<0>(t), a.x0); std::get<1>(t) = std::min(std::get<1>(t), a.y0); std::get<2>(t) = std::max(std::get<2>(t), a.x1); std::get<3>(t) = std::max(std::get<3>(t), a.y1); std::get<4>(t)++; }
    }
    int hx0 = 0, hx1 = 0, hn = 0; std::vector<std::pair<int, int>> hl;   /* realces plausiveis (y0,y1) */
    for (auto &kv : bb) {
        const int x0 = std::get<0>(kv.second), y0 = std::get<1>(kv.second), x1 = std::get<2>(kv.second), y1 = std::get<3>(kv.second), n = std::get<4>(kv.second);
        if (n >= 4 && y1 - y0 >= 14 && y1 - y0 <= pitch + 8 && x1 - x0 >= 40 && x1 - x0 <= 420 && x0 <= left + 2 && x1 >= left) { hl.push_back({y0, y1}); hx0 = x0; hx1 = x1; hn++; }
    }
    if (hn) right = std::max(right, hx1);
    const int rx0 = std::max(0, left - 6), rx1 = right + 4, top = best[0].y0 - (pitch - (best[0].y1 - best[0].y0)) / 2;
    for (size_t i = 0; i < best.size(); i++) g_list.push_back({rx0, top + (int)i * pitch, rx1, top + (int)(i + 1) * pitch, 0, 0});
    int n = 0;
    for (size_t i = 0; i < g_list.size(); i++) for (auto &h : hl) {
        const int ov = std::min(h.second, g_list[i].y1) - std::max(h.first, g_list[i].y0);
        if (ov * 10 >= (h.second - h.first) * 6) { g_sel = (int)i; n++; break; }
    }
    if (n != 1) g_sel = -1;
    (void)hx0;
}

/* barras = sequencias contiguas na horizontal de sprites de mesma altura (ponta, meio, ponta) com um meio largo */
void analyze() {
    g_list.clear(); g_sel = -1;
    std::vector<Spr> u;   /* sem repeticoes de retangulo, para achar as cadeias */
    for (const Spr &s : g_snap) {
        const int h = s.y1 - s.y0, w = s.x1 - s.x0;
        if (h < 14 || h > 48 || w < 2) continue;
        bool dup = false; for (const Spr &t : u) if (t.x0 == s.x0 && t.y0 == s.y0 && t.x1 == s.x1 && t.y1 == s.y1) { dup = true; break; }
        if (!dup) u.push_back(s);
    }
    std::sort(u.begin(), u.end(), [](const Spr &a, const Spr &b) { return a.y0 != b.y0 ? a.y0 < b.y0 : a.y1 != b.y1 ? a.y1 < b.y1 : a.x0 < b.x0; });
    std::vector<Bar> bars;
    for (size_t i = 0; i < u.size();) {
        size_t j = i + 1; int right = u[i].x1, widest = u[i].x1 - u[i].x0;
        while (j < u.size() && u[j].y0 == u[i].y0 && u[j].y1 == u[i].y1 && u[j].x0 >= right - 1 && u[j].x0 <= right + 1) { right = u[j].x1; widest = std::max(widest, u[j].x1 - u[j].x0); j++; }
        if (j - i >= 3 && widest >= 40 && right - u[i].x0 >= 80) {
            Bar b = {u[i].x0, u[i].y0, right, u[i].y1, 0xcbf29ce484222325ull, 0};
            for (size_t si = 0; si < g_snap.size(); si++) {   /* assinatura: so os sprites da altura inteira da barra, na ordem do desenho (o selecionado e desenhado de outro jeito) */
                const Spr &s = g_snap[si];
                if (s.y0 == b.y0 && s.y1 == b.y1 && s.x0 >= b.x0 && s.x1 <= b.x1) {
                    b.last = si;
                    b.sig = mix(b.sig, s.tex); b.sig = mix(b.sig, (uint64_t)(s.u0 * 16)); b.sig = mix(b.sig, (uint64_t)(s.v0 * 16)); b.sig = mix(b.sig, (uint64_t)(s.u1 * 16)); b.sig = mix(b.sig, (uint64_t)(s.v1 * 16));
                }
            }
            bars.push_back(b);
        }
        i = j;
    }
    /* a lista = o grupo de barras de mesmo tamanho e mesma coluna desenhado por ultimo (o que esta por cima: um submenu cobre o menu de tras) */
    std::map<std::tuple<int, int, int>, std::vector<int>> grp;
    for (size_t i = 0; i < bars.size(); i++) grp[std::make_tuple(bars[i].x1 - bars[i].x0, bars[i].y1 - bars[i].y0, bars[i].x0)].push_back((int)i);
    const std::vector<int> *best = nullptr; size_t best_last = 0;
    for (auto &g : grp) { size_t l = 0; for (int i : g.second) l = std::max(l, bars[i].last); if (g.second.size() >= 2 && (!best || l > best_last)) { best = &g.second; best_last = l; } }
    if (!best) { analyze_text(); trace_list(); return; }
    for (int i : *best) g_list.push_back(bars[i]);
    std::sort(g_list.begin(), g_list.end(), [](const Bar &a, const Bar &b) { return a.y0 < b.y0; });
    /* selecionada: (1) lista de 3+ com so duas assinaturas e uma barra isolada = ela (e as outras sao o estilo 'normal'); (2) uma assinatura ja vista como
     * selecionada; (3) a unica barra cuja assinatura nao e de estilo normal (listas de 2: 'Deck Edit' / 'Recipe Viewer') */
    std::map<uint64_t, int> cnt; for (const Bar &b : g_list) cnt[b.sig]++;
    for (auto &c : cnt) if (c.second >= 2) g_normal.insert(c.first);
    if (cnt.size() == 2 && g_list.size() >= 3) {
        uint64_t odd = 0; int oc = 1 << 30;
        for (auto &c : cnt) if (c.second < oc) { oc = c.second; odd = c.first; }
        if (oc == 1) { for (size_t i = 0; i < g_list.size(); i++) if (g_list[i].sig == odd) g_sel = (int)i; g_known.insert(odd); }
    } else if (cnt.size() >= 2) {
        int n = 0; for (size_t i = 0; i < g_list.size(); i++) if (g_known.count(g_list[i].sig)) { g_sel = (int)i; n++; }
        if (n != 1) {
            n = 0; g_sel = -1;
            for (size_t i = 0; i < g_list.size(); i++) if (!g_normal.count(g_list[i].sig)) { g_sel = (int)i; n++; }
            if (n != 1) g_sel = -1; else g_known.insert(g_list[g_sel].sig);
        }
    }
    trace_list();
}

/* estado do controle */
int g_tkx = -1, g_tky = -1;          /* barra alvo (canto superior esquerdo), -1 = nenhuma */
bool g_pending;                       /* clique esperando a selecao chegar na barra */
uint64_t g_pending_until, g_next_tap, g_pulse_end; uint32_t g_pulse; int g_taps;
int g_lmx = -1, g_lmy = -1;
int g_burst_n, g_burst_dir; uint64_t g_burst_start, g_burst_end;
const int g_period = getenv("TF_MOUSE_PERIOD") ? atoi(getenv("TF_MOUSE_PERIOD")) : 4;   /* quadros por passo (metade apertado, metade solto) */
}

void mui_prim(uint32_t fb, int x0, int y0, int x1, int y1, float u0, float v0, float u1, float v1, uint32_t tex, uint32_t col) {
    if (x0 > x1) { std::swap(x0, x1); std::swap(u0, u1); }
    if (y0 > y1) { std::swap(y0, y1); std::swap(v0, v1); }
    if (x1 <= 0 || y1 <= 0 || x0 >= 480 || y0 >= 272) return;
    std::vector<Spr> &v = g_cur[fb & 0x1ffff0u];
    if (v.size() < 6000) v.push_back({x0, y0, x1, y1, u0, v0, u1, v1, tex, col});
}

void mui_run(uint32_t fb, int x0, int y0, int x1, int y1) {
    if (x1 <= 0 || y1 <= 0 || x0 >= 480 || y0 >= 272) return;
    std::vector<Run> &v = g_cur_runs[fb & 0x1ffff0u];
    if (v.size() < 400) v.push_back({x0, y0, x1, y1});
}

void mui_set_frame(uint32_t fb) {
    std::vector<Spr> &v = g_cur[fb & 0x1ffff0u];
    g_snap.swap(v); v.clear();
    std::vector<Run> &r = g_cur_runs[fb & 0x1ffff0u]; g_runs.swap(r); r.clear();
    analyze();
}

namespace {
/* Pulo direto: o jogo guarda o indice do cursor de cada lista numa variavel (byte baixo). Ela e descoberta em tempo de execucao: quando a selecao
 * muda de a para b enquanto este modulo aperta o direcional, os enderecos que valiam a antes (copia da RAM tirada antes do aperto) e b depois sao
 * candidatos; a proxima mudanca filtra (valor atual == indice selecionado). Com um candidato so, as proximas trocas escrevem o indice direto,
 * sem passar por cada item. Cada uso confere que a variavel ainda vale a selecao exibida; se nao, esquece e reaprende. */
const uint32_t RAM0 = 0x08800000, RAMN = 0x1800000;
std::vector<uint8_t> g_ram0;                  /* RAM antes do aperto (so enquanto aprende) */
std::vector<uint32_t> g_cands; bool g_cands_init; int g_obs;
std::map<uint64_t, uint32_t> g_addr;          /* chave da lista -> endereco do cursor */
uint64_t g_key; int g_last_sel = -1, g_ram0_sel = -1, g_stab_sel = -2, g_stab_n; int g_tap_from = -1; uint64_t g_tap_at;
uint64_t list_key() {
    uint64_t h = mix(0x9e3779b97f4a7c15ull, g_list.size()); const Bar &b = g_list[0];
    h = mix(h, b.x0); h = mix(h, b.y0); h = mix(h, b.x1 - b.x0); h = mix(h, b.y1 - b.y0); return h;
}
inline uint8_t *ram(uint32_t a) { return mem_ptr(a); }
/* a cada quadro: quanto tempo a selecao ficou igual (o desenho atrasa em relacao ao aperto) */
void stab_update() {
    if (g_sel != g_stab_sel) { g_stab_sel = g_sel; g_stab_n = 0; } else g_stab_n++;
}
/* (nao usado: o pulo direto escrevendo o cursor na memoria nao toca o som do jogo; ficou so como referencia) a cada quadro com selecao conhecida: aprende com as mudancas */
void learn() {
    if (g_sel < 0) return;
    if (g_sel != g_stab_sel) { g_stab_sel = g_sel; g_stab_n = 0; return; }   /* so aprende com a selecao estavel: o desenho atrasa um pouco em relacao a variavel */
    if (++g_stab_n < 3) return;
    const uint64_t key = list_key();
    if (key != g_key) { g_key = key; g_cands.clear(); g_cands_init = false; g_obs = 0; g_ram0.clear(); g_last_sel = -1; }
    if (g_last_sel >= 0 && g_sel != g_last_sel && !g_addr.count(key)) {
        if (!g_cands_init && !g_ram0.empty() && g_ram0_sel == g_last_sel) {
            for (uint32_t o = 0; o < RAMN; o++) if (g_ram0[o] == (uint8_t)g_last_sel && *ram(RAM0 + o) == (uint8_t)g_sel) g_cands.push_back(RAM0 + o);
            g_cands_init = true; g_obs = 1;
        } else if (g_cands_init) {
            std::vector<uint32_t> k; for (uint32_t a : g_cands) if (*ram(a) == (uint8_t)g_sel) k.push_back(a);
            g_cands.swap(k); g_obs++;
        }
        if (g_mtrace && g_cands_init && g_cands.size() <= 600) { fprintf(stderr, "[mouse]   sel %d->%d:", g_last_sel, g_sel); for (size_t i = 0; i < g_cands.size() && i < 600; i++) fprintf(stderr, " %x", g_cands[i]); fprintf(stderr, "\n"); }
        if (g_mtrace && g_cands_init) fprintf(stderr, "[mouse] cursor: %zu candidato(s) apos %d mudanca(s)\n", g_cands.size(), g_obs);
        if (g_cands_init && g_obs >= 2 && g_cands.size() == 1) { g_addr[key] = g_cands[0]; if (g_mtrace) fprintf(stderr, "[mouse] cursor da lista em %08x\n", g_cands[0]); }
        g_ram0.clear();
    }
    g_last_sel = g_sel;
}
/* antes de apertar o direcional: guarda a RAM para aprender com a mudanca (so se ainda precisa) */
void learn_snapshot() {
    if (g_sel < 0 || g_addr.count(g_key) || (g_cands_init && g_cands.size() <= 1 && g_obs >= 2)) return;
    if (g_cands_init) return;   /* ja tem candidatos: o filtro usa so a memoria atual */
    g_ram0.assign(ram(RAM0), ram(RAM0) + RAMN); g_ram0_sel = g_sel;
}
/* escreve o indice direto; false = nao sabe o endereco (ou ele nao vale mais) */
bool jump_to(int idx) {
    auto it = g_addr.find(g_key); if (it == g_addr.end()) return false;
    if (*ram(it->second) != (uint8_t)g_sel) { g_addr.erase(it); g_cands.clear(); g_cands_init = false; g_obs = 0; return false; }
    *ram(it->second) = (uint8_t)idx;
    return true;
}
}

uint32_t mui_update(uint64_t frame, bool inside, int mx, int my, bool l_edge, bool l_down, uint32_t user_dpad, bool *eat_cross) {
    *eat_cross = false;
    const bool moved = inside && (mx != g_lmx || my != g_lmy);
    if (inside) { g_lmx = mx; g_lmy = my; }
    uint32_t out = frame < g_pulse_end ? g_pulse : 0;
    if (g_burst_n && frame >= g_burst_start && frame < g_burst_start + (uint64_t)g_burst_n * g_period && (frame - g_burst_start) % g_period < g_period / 2) out |= g_burst_dir;
    if (g_list.empty()) { g_tkx = g_tky = -1; g_pending = false; return out; }
    stab_update();
    int hover = -1;
    if (inside) for (size_t i = 0; i < g_list.size(); i++) if (mx >= g_list[i].x0 && mx < g_list[i].x1 && my >= g_list[i].y0 && my < g_list[i].y1) hover = (int)i;
    if (user_dpad) { g_tkx = g_tky = -1; g_pending = false; return out; }   /* o jogador voltou ao teclado/controle */
    if ((moved || l_edge) && hover >= 0) { g_tkx = g_list[hover].x0; g_tky = g_list[hover].y0; g_taps = 0; g_burst_n = 0; }
    if (l_edge && hover >= 0) { g_pending = true; g_pending_until = frame + 120; }
    if (l_down && hover >= 0 && g_sel >= 0) *eat_cross = true;   /* quem confirma e este modulo, quando a selecao chegar */
    if (g_pending && frame > g_pending_until) g_pending = false;
    int tgt = -1; for (size_t i = 0; i < g_list.size(); i++) if (g_list[i].x0 == g_tkx && g_list[i].y0 == g_tky) tgt = (int)i;
    if (tgt < 0) { g_pending = false; return out; }
    if (g_burst_n && frame < g_burst_start + (uint64_t)g_burst_n * g_period) return out;   /* rajada em andamento */
    if (g_burst_n) { g_burst_end = g_burst_start + (uint64_t)g_burst_n * g_period; g_burst_n = 0; }
    if (g_sel < 0 || frame < g_pulse_end + 2 || frame < g_next_tap) return out;
    if (g_stab_n < 2) return out;   /* espera o desenho refletir o ultimo aperto */
    if (tgt == g_sel) {
        if (g_pending) { g_pending = false; g_pulse = 0x4000; g_pulse_end = frame + 3; g_next_tap = frame + 10; out = g_pulse; if (g_mtrace) fprintf(stderr, "[mouse] confirma barra %d\n", tgt); }
        g_tkx = g_tky = -1;
        return out;
    }
    if (frame < g_burst_end + 12) return out;   /* o realce atrasa alguns quadros em relacao ao aperto: so corrige uma rajada que nao chegou depois de esperar */
    if (g_taps >= 6) { g_tkx = g_tky = -1; g_pending = false; return out; }   /* nao esta chegando (deteccao errada): desiste */
    /* rajada: um aperto por item no ritmo mais rapido que o jogo aceita (o jogo le o controle a cada 2 quadros); cada passo toca o som e o realce do jogo */
    const Bar &t = g_list[tgt], &s = g_list[g_sel];
    g_burst_dir = t.y0 != s.y0 ? (t.y0 < s.y0 ? 0x10 : 0x40) : (t.x0 < s.x0 ? 0x80 : 0x20);
    g_burst_n = std::abs(tgt - g_sel); g_burst_start = frame; g_taps++;
    if (g_mtrace) fprintf(stderr, "[mouse] quadro %llu: selecionada %d -> alvo %d, rajada de %d (%02x)\n", (unsigned long long)frame, g_sel, tgt, g_burst_n, g_burst_dir);
    return g_burst_dir;
}
