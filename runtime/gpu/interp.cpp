/* 60 FPS por interpolacao da apresentacao (ver interp.h). */
#include "interp.h"
#include "gfx.h"
#include "util.h"
#include <vector>
#include <deque>
#include <string>
#include <array>
#include <memory>
#include <unordered_map>
#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

extern int g_ge_res;   /* resolucao interna atual (hle_ge.cpp) */

namespace {
/* Copias congeladas de render-to-texture. Um desenho que amostra a imagem de outro quadro sombra (GPU_FBTEX: reflexo do chao da loja,
 * painel da carta...) le o conteudo que ela tinha NA HORA do desenho; mas o GE ja executa o inicio do quadro seguinte antes do vblank e
 * reescreve o alvo, entao refazer o desenho no quadro intermediario leria a imagem do FUTURO (o reflexo saia um quadro adiantado e o
 * meio "pulava" para frente). Por isso, ao gravar, a imagem e copiada para um quadro sombra proprio (uma copia por alvo e versao). */
struct Frozen { int fb, w, h, res; };
static bool g_rtt_cache_on; static std::vector<std::pair<int, int>> g_rtt_cache; static std::vector<Frozen> g_rtt_held;   /* (definidos junto de g_fpool: interp_reset usa) */
std::vector<Frozen> g_fpool;                  /* copias livres para reuso */
uint32_t g_fgen = 1;                          /* geracao: interp_reset invalida as copias (os ids da GPU deixam de valer) */
std::unordered_map<int, uint32_t> g_fbver;    /* versao de cada quadro sombra (muda a cada desenho/upload nele) */
uint64_t g_st_frozen;
struct FrozenSet {
    std::vector<Frozen> f; uint32_t gen = g_fgen;
    ~FrozenSet() { if (gen == g_fgen) for (const Frozen &x : f) g_fpool.push_back(x); }
};
struct Rec;
/* copia congelada de um alvo: {alvo, versao, copia, tamanho, lista de desenhos do alvo desde o ultimo clear (para refazer o alvo interpolado)} */
struct FzEntry { uint32_t src, ver; int fb, w, h; std::shared_ptr<Rec> list; };
struct Draw { GpuState s; uint32_t off, n, otex; int cull; };   /* cull: modo do GE (a lista inclui os triangulos de costas; refeito no quadro do meio) */   /* otex: textura original (com GPU_FBTEX, o alvo; s.tex aponta para a copia) */
struct Rec {
    std::vector<Draw> d; std::vector<GpuVtx> v; std::vector<float> disp; bool taint = false, any = false;   /* disp: quanto cada desenho andou em relacao ao quadro anterior (1e9 = nao medido) */   /* any: houve desenho (mesmo descartado) desde a ultima apresentacao */
    std::vector<std::array<float, 2>> vel, vel2;   /* deslocamento tipico (mediana) de cada desenho em relacao ao quadro anterior, e o do passo antes desse (1e9 = nao medido): detecta o laco de uma rolagem */
    std::shared_ptr<FrozenSet> fz; std::vector<FzEntry> fzmap;   /* copias congeladas usadas por este quadro */
    bool rtt = false;   /* desenhos de um alvo de textura (segmento desde o ultimo clear de cor) */
    void clear() { d.clear(); v.clear(); disp.clear(); vel.clear(); vel2.clear(); fz.reset(); fzmap.clear(); taint = any = false; }
};
std::unordered_map<int, Rec> g_pend;
std::unordered_map<int, std::pair<uint32_t, std::shared_ptr<Rec>>> g_lastlist;   /* ultima lista congelada de cada alvo (por versao) */   /* desenhos de cada quadro sombra exibido desde a ultima apresentacao dele */
Rec g_prev; bool g_have_prev;          /* ultimo quadro novo apresentado */
Rec g_old; bool g_have_old;            /* 120 FPS: o quadro antes de g_prev, guardado ate o ultimo intermediario do par (g_old, g_prev) */
uint64_t g_last_fresh, g_old_vb;
int g_scratch = -1, g_scratch_res;
/* 120 FPS com a thread de apresentacao: Q1, Q2 e Q3 desenhados de uma vez, cada um no seu buffer (interp_use_scratch troca o g_scratch ativo) */
static int g_scr[6] = {-1, -1, -1, -1, -1, -1}, g_scr_res[6], g_scr_k;   /* dois jogos de 3 (display.cpp alterna a cada quadro novo) */
void scratch_use(int k) { g_scr[g_scr_k] = g_scratch; g_scr_res[g_scr_k] = g_scratch_res; g_scr_k = k; g_scratch = g_scr[k]; g_scratch_res = g_scr_res[k]; }
bool g_mid_ready;                      /* interp_prepare desenhou um intermediario em g_scratch que ainda nao foi mostrado */
/* par parado: o quadro novo e igual ao anterior (mesmos desenhos, estados, texturas e vertices; sem alvos de textura): todo intermediario seria a
 * propria imagem real, entao ela e apresentada no lugar (nenhum desenho a mais na GPU; menus e telas de texto paradas). -1 = par em movimento */
int g_static_fb = -1; uint64_t g_st_static;
bool static_pair(const Rec &a, const Rec &b) {
    static const bool off = getenv("TF_INTERP_SEM_PARADO") != nullptr;   /* A/B: desenha o intermediario sempre */
    if (off || a.taint || b.taint || a.d.size() != b.d.size() || a.v.size() != b.v.size() || a.d.empty() || !a.fzmap.empty() || !b.fzmap.empty()) return false;
    for (size_t i = 0; i < a.d.size(); i++) {
        const Draw &x = a.d[i], &y = b.d[i];
        if (x.off != y.off || x.n != y.n || x.otex != y.otex || x.cull != y.cull || (x.s.tex & GPU_FBTEX)) return false;
        GpuState u = x.s, w = y.s; u.fb = w.fb = 0; if (memcmp(&u, &w, sizeof u)) return false;   /* o quadro de destino muda com o buffer duplo */
    }
    return !memcmp(a.v.data(), b.v.data(), a.v.size() * sizeof(GpuVtx));
}
uint64_t g_st_mid, g_st_skip, g_st_tris, g_st_tris_ok, g_st_nomatch, g_st_loop; double g_st_us;   /* g_st_us: tempo de CPU montando os quadros do meio (TF_INTERP_DEBUG); g_st_loop: triangulos no laco de uma rolagem (extrapolados) */
const bool g_dbg = getenv("TF_INTERP_DEBUG") != nullptr;
const long long g_dump_vb = getenv("TF_INTERP_DUMP") ? atoll(getenv("TF_INTERP_DUMP")) : -1;   /* "a-b" lista o pareamento de todos os quadros de a ate b */
const long long g_dump_hi = (getenv("TF_INTERP_DUMP") && strchr(getenv("TF_INTERP_DUMP"), '-')) ? atoll(strchr(getenv("TF_INTERP_DUMP"), '-') + 1) : g_dump_vb;
static inline bool dump_vb_now(uint64_t vb) { return g_dump_vb >= 0 && (long long)vb >= g_dump_vb && (long long)vb <= g_dump_hi; }   /* quadro cujo pareamento e listado (TF_INTERP_DUMP) */
const bool g_v1 = getenv("TF_INTERP_V1") != nullptr;   /* A/B: regras de antes das correcoes da loja (sem copia congelada, par 'que menos andou', sem isencao rigida, limite 12 px) */
static const unsigned g_v1mask = getenv("TF_INTERP_BISECT") ? (unsigned)atoi(getenv("TF_INTERP_BISECT")) : 0u;   /* depuracao: bit n liga a regra ANTIGA so no ponto n (ver v1b) */
static inline bool v1b(int bit) { return g_v1 || ((g_v1mask >> bit) & 1u); }
const size_t MAX_DRAWS = 60000, MAX_VERTS = 3000000;
}

bool interp_enabled(void) {
    static const char *e = getenv("TF_INTERP");
    return e ? atoi(e) != 0 : g_gfx.fps != GFX_FPS_30;
}

void interp_fb_drawn(int fb) { if (interp_enabled()) g_fbver[fb]++; }
uint32_t interp_fb_version(int fb) { auto it = g_fbver.find(fb); return it == g_fbver.end() ? 0 : it->second; }
/* copia congelada da imagem atual do quadro sombra `src` (nw x nh nativos) para o quadro `r`; -1 = sem quadro livre na GPU */
static bool g_froze;   /* houve desenho com alvo de textura (copia congelada) desde o ultimo quadro novo: o intermediario le o que o quadro real copiou */
static int freeze_fbtex(Rec &r, int src, int nw, int nh) {
    g_froze = true;
    const uint32_t ver = g_fbver[src];
    for (const auto &e : r.fzmap) if ((int)e.src == src && e.ver == ver) return e.fb;
    static const bool off = getenv("TF_INTERP_NOFREEZE") != nullptr;   /* depuracao: amostra o alvo vivo (comportamento antigo) */
    if (off || v1b(0)) return src;
    Frozen fz{-1, nw, nh, g_ge_res};
    for (size_t i = 0; i < g_fpool.size(); i++)
        if (g_fpool[i].w == nw && g_fpool[i].h == nh && g_fpool[i].res == g_ge_res) { fz = g_fpool[i]; g_fpool.erase(g_fpool.begin() + i); break; }
    if (fz.fb < 0) {
        while (g_fpool.size() > 4) { gpu_fb_destroy(g_fpool.front().fb); g_fpool.erase(g_fpool.begin()); }   /* tamanhos que sairam de uso */
        fz.fb = gpu_fb_create(nw, nh, g_ge_res); if (fz.fb < 0) return -1;
    }
    if (!r.fz) r.fz = std::make_shared<FrozenSet>();
    r.fz->f.push_back(fz);
    GpuState c; memset(&c, 0, sizeof c);   /* copia 1:1 (sem filtro, substitui cor e alfa) */
    c.fb = fz.fb; c.tex = (uint32_t)GPU_FBTEX | (uint32_t)src; c.tw = (uint32_t)nw; c.th = (uint32_t)nh; c.tfn = 3; c.talpha = 1; c.clampu = c.clampv = 1;
    c.sx2 = nw - 1; c.sy2 = nh - 1; c.cmask = 15;
    const float W = (float)nw, H = (float)nh;
    const GpuVtx q[6] = {{0, 0, 0, 0, 0, 1, 0xffffffffu}, {W, 0, 0, W, 0, 1, 0xffffffffu}, {W, H, 0, W, H, 1, 0xffffffffu},
                         {0, 0, 0, 0, 0, 1, 0xffffffffu}, {W, H, 0, W, H, 1, 0xffffffffu}, {0, H, 0, 0, H, 1, 0xffffffffu}};
    gpu_draw(c, q, 6);
    /* a lista de desenhos do alvo (se ele foi refeito do zero, a partir de um clear de cor) permite refaze-lo interpolado no quadro do meio */
    std::shared_ptr<Rec> list;
    auto lit = g_lastlist.find(src);
    if (lit != g_lastlist.end() && lit->second.first == ver) list = lit->second.second;
    else {
        auto pit = g_pend.find(src);
        if (pit != g_pend.end() && pit->second.rtt && !pit->second.taint && !pit->second.d.empty() && (pit->second.d[0].s.clear & 0x100)) list = std::make_shared<Rec>(pit->second);
        if (g_dbg) { static int nd; if (nd++ < 20) { const Rec *pr = pit != g_pend.end() ? &pit->second : nullptr;
            fprintf(stderr, "[interp] alvo %d congelado: lista %s (rtt=%d taint=%d desenhos=%zu clear0=%x)\n", src, list ? "ok" : "NAO", pr ? (int)pr->rtt : -1, pr ? (int)pr->taint : -1, pr ? pr->d.size() : (size_t)0, pr && !pr->d.empty() ? pr->d[0].s.clear : 0u); } }
        g_lastlist[src] = {ver, list};
    }
    r.fzmap.push_back({(uint32_t)src, ver, fz.fb, nw, nh, list}); g_st_frozen++;
    return fz.fb;
}
void interp_record(const GpuState &s, const GpuVtx *v, int n, bool rtt, int cull) {
    if (n <= 0 || !interp_enabled()) return;
    Rec &r = g_pend[s.fb];
    if (rtt && (s.clear & 0x100)) r.clear();   /* alvo de textura: clear de cor = comeca um segmento novo (a lista guardada e so o segmento atual) */
    r.rtt = rtt; r.any = true;
    if (r.taint) return;   /* ja descartado ate a proxima apresentacao */
    if (r.d.size() >= MAX_DRAWS || r.v.size() + n > MAX_VERTS) { r.clear(); r.any = r.taint = true; return; }
    Draw d{s, (uint32_t)r.v.size(), (uint32_t)n, s.tex, cull};
    if ((s.tex & GPU_FBTEX) && (int)(s.tex & 0xff) != s.fb) {
        int fz = freeze_fbtex(r, (int)(s.tex & 0xff), (int)s.tw, (int)s.th);
        if (fz < 0) { r.clear(); r.any = r.taint = true; return; }
        d.s.tex = (uint32_t)GPU_FBTEX | (uint32_t)fz;
    }
    r.d.push_back(d);
    r.v.insert(r.v.end(), v, v + n);
}
void interp_taint(int fb) { g_fbver[fb]++; Rec &r = g_pend[fb]; const bool a = r.any; r.clear(); r.any = a; r.taint = true; }
/* texturas liberadas durante o quadro: os desenhos gravados ainda apontam para elas. g_dead[0] = desde a ultima apresentacao de quadro novo,
 * [1] e [2] = as duas anteriores. Uma textura liberada enquanto o jogo desenhava o quadro N pode estar nas listas de N, que ainda servem de
 * "quadro anterior" no par (N, N+1); no 120 FPS o ultimo intermediario desse par (t = 0,75) so e desenhado no vblank DEPOIS do quadro N+1
 * chegar, por isso ela vive 3 apresentacoes (com 2, o par (N, N+1) do 60 FPS ja terminou; uma a mais nao muda nada la) */
static std::vector<uint32_t> g_dead[3];
bool interp_defer_release(uint32_t h) { static const bool nd = getenv("TF_NO_DEFER") != nullptr; if (nd || !interp_enabled() || g_dead[0].size() > 4000) return false; for (uint32_t d : g_dead[0]) if (d == h) { fprintf(stderr, "[interp] AVISO: textura %u liberada duas vezes\n", h); return true; }   /* ja adiada: guardar de novo a liberaria duas vezes */
    g_dead[0].push_back(h); return true; }
static void free_dead(bool all = false) {   /* libera as de 3 apresentacoes atras (as mais novas ainda podem estar nas listas gravadas); all = tudo */
    for (uint32_t h : g_dead[2]) gpu_tex_release_now(h);
    g_dead[2].clear(); g_dead[2].swap(g_dead[1]); g_dead[1].swap(g_dead[0]);
    if (all) for (int i = 1; i < 3; i++) { for (uint32_t h : g_dead[i]) gpu_tex_release_now(h); g_dead[i].clear(); }
}
/* modo adiado (display.cpp): o quadro real F so aparece no inicio da chamada do quadro F+1, e o jogo pode ter redesenhado o MESMO quadro sombra
 * (mapa: um buffer so) - apresentar o sombra mostrava F+1 antes do intermediario (F, F+1) e a imagem ia e voltava 2-4 px a cada quadro. Uma copia
 * na GPU (dois quadros alternados) guarda F. */
static int g_rcopy[2] = {-1, -1}, g_rcopy_res[2], g_rcopy_k;
int interp_real_copy(int src) {
    const int k = g_rcopy_k ^= 1;
    if (g_rcopy[k] >= 0 && g_rcopy_res[k] != g_ge_res) { gpu_fb_destroy(g_rcopy[k]); g_rcopy[k] = -1; }
    if (g_rcopy[k] < 0) { g_rcopy[k] = gpu_fb_create(480, 272, g_ge_res); g_rcopy_res[k] = g_ge_res; if (g_rcopy[k] < 0) return -1; }
    GpuState c; memset(&c, 0, sizeof c);   /* copia 1:1, como freeze_fbtex */
    c.fb = g_rcopy[k]; c.tex = (uint32_t)GPU_FBTEX | (uint32_t)src; c.tw = 480; c.th = 272; c.tfn = 3; c.talpha = 1; c.clampu = c.clampv = 1;
    c.sx2 = 479; c.sy2 = 271; c.cmask = 15;
    const float W = 480.f, H = 272.f;
    const GpuVtx q[6] = {{0, 0, 0, 0, 0, 1, 0xffffffffu}, {W, 0, 0, W, 0, 1, 0xffffffffu}, {W, H, 0, W, H, 1, 0xffffffffu},
                         {0, 0, 0, 0, 0, 1, 0xffffffffu}, {W, H, 0, W, H, 1, 0xffffffffu}, {0, H, 0, 0, H, 1, 0xffffffffu}};
    gpu_draw(c, q, 6); gpu_kick();
    return g_rcopy[k];
}
void interp_reset(void) {
    for (int &f : g_rcopy) { if (f >= 0) gpu_fb_destroy(f); f = -1; }
    free_dead(true);
    for (const Frozen &f : g_rtt_held) g_fpool.push_back(f); g_rtt_held.clear(); g_rtt_cache.clear(); g_rtt_cache_on = false;
    g_pend.clear(); g_prev.clear(); g_have_prev = false; g_old.clear(); g_have_old = false; g_mid_ready = false; g_last_fresh = 0;
    g_lastlist.clear();
    g_fgen++; for (const Frozen &x : g_fpool) gpu_fb_destroy(x.fb); g_fpool.clear(); g_fbver.clear();   /* copias ainda presas (autoteste) nao voltam ao pool */
    for (int k = 0; k < 6; k++) { if (k != g_scr_k && g_scr[k] >= 0) gpu_fb_destroy(g_scr[k]); g_scr[k] = -1; }
    if (g_scratch >= 0) gpu_fb_destroy(g_scratch);
    g_scratch = -1;
}

namespace {
/* mesma "forma" de desenho nos dois quadros (estado e textura iguais, exceto o quadro de destino e o scissor) */
bool same_kind(const Draw &a, const Draw &b) {
    if (a.n != b.n || a.cull != b.cull) return false;
    GpuState x = a.s, y = b.s; x.fb = y.fb = 0; x.tex = a.otex; y.tex = b.otex; x.sx1 = y.sx1 = x.sy1 = y.sy1 = x.sx2 = y.sx2 = x.sy2 = y.sy2 = 0; x.uvclamp = y.uvclamp = 0; x.cu0 = y.cu0 = x.cv0 = y.cv0 = x.cu1 = y.cu1 = x.cv1 = y.cv1 = 0; /* a textura tem que ser a mesma: sprites de personagens diferentes usam o mesmo recorte de UV em texturas distintas */
    return !memcmp(&x, &y, sizeof x);
}
/* igual a same_kind, mas aceita textura (pagina) diferente: o boneco do mapa troca de pagina de textura ao mudar de pose */
bool same_kind_anytex(const Draw &a, const Draw &b) {
    if (a.n != b.n || a.cull != b.cull) return false;
    GpuState x = a.s, y = b.s; x.fb = y.fb = 0; x.tex = y.tex = 0; x.sx1 = y.sx1 = x.sy1 = y.sy1 = x.sx2 = y.sx2 = x.sy2 = y.sy2 = 0; x.uvclamp = y.uvclamp = 0; x.cu0 = y.cu0 = x.cv0 = y.cv0 = x.cu1 = y.cu1 = x.cv1 = y.cv1 = 0; x.tw = y.tw = 0; x.th = y.th = 0;
    return !memcmp(&x, &y, sizeof x);
}
inline uint32_t avg_col(uint32_t a, uint32_t b) { return ((a & 0xfefefefeu) >> 1) + ((b & 0xfefefefeu) >> 1) + (a & b & 0x01010101u); }

uint64_t g_cur_vb; float g_T_mul = 1.f;   /* g_T_mul = 2 no autoteste (F0->F2 anda o dobro) */
/* fracao do caminho entre o quadro anterior (0) e o novo (1) que o quadro intermediario mostra: 0,5 no 60 FPS; 0,25 / 0,5 / 0,75 no 120 FPS.
 * Em 0,5 as contas sao as de sempre, bit a bit (o 60 FPS nao muda). */
float g_t = 0.5f;
inline float mix_f(float a, float b) { return g_t == 0.5f ? (a + b) * 0.5f : a + (b - a) * g_t; }
inline int32_t mix_i(int32_t a, int32_t b) { return g_t == 0.5f ? (b + a) / 2 : a + (int32_t)lroundf((float)(b - a) * g_t); }
inline uint32_t mix_col(uint32_t a, uint32_t b) {
    if (g_t == 0.5f) return avg_col(a, b);
    const uint32_t w = (uint32_t)lroundf(g_t * 256.f); uint32_t r = 0;
    for (int s = 0; s < 32; s += 8) { const uint32_t x = (a >> s) & 255, y = (b >> s) & 255; r |= ((x * (256 - w) + y * w) >> 8) << s; }
    return r;
}
std::vector<GpuVtx> g_outs[2];
/* posicao final de cada vertice do quadro novo, pela chave da posicao: enderecamento aberto, reaproveitado entre quadros (o unordered_map
 * fazia uma alocacao por vertice a cada quadro do meio). A "marca" de cada quadro dispensa limpar a tabela. */
struct VMap {
    std::vector<uint64_t> key; std::vector<uint32_t> mark; std::vector<std::array<float, 3>> val; uint32_t cur = 0, mask = 0;
    void reset(size_t n) {
        size_t cap = 64; while (cap < n * 2) cap <<= 1;
        if (cap > key.size()) { key.assign(cap, 0); mark.assign(cap, 0); val.resize(cap); cur = 0; }
        mask = (uint32_t)key.size() - 1;
        if (++cur == 0) { std::fill(mark.begin(), mark.end(), 0u); cur = 1; }
    }
    /* devolve a posicao guardada (e *novo = false) ou cria a entrada com p (e *novo = true) */
    std::array<float, 3> &get(uint64_t k, const std::array<float, 3> &p, bool *novo) {
        for (uint32_t i = (uint32_t)(k ^ (k >> 29)) & mask;; i = (i + 1) & mask) {
            if (mark[i] != cur) { mark[i] = cur; key[i] = k; val[i] = p; *novo = true; return val[i]; }
            if (key[i] == k) { *novo = false; return val[i]; }
        }
    }
    const std::array<float, 3> *find(uint64_t k) const {
        for (uint32_t i = (uint32_t)(k ^ (k >> 29)) & mask;; i = (i + 1) & mask) { if (mark[i] != cur) return nullptr; if (key[i] == k) return &val[i]; }
    }
};
VMap g_vmap[2];
/* movimento afim (2x3) de prev -> cur por minimos quadrados nos pares de pontos; uma passada robusta descarta os pares que destoam (par
 * errado). false = poucos pontos ou degenerado (fica a translacao que ja estava em m) */
bool fit_affine(const std::vector<float> &P, float m[6]) {   /* P: ax, ay, bx, by, ... */
    const size_t n = P.size() / 4; if (n < 6) return false;
    static std::vector<char> use; static std::vector<float> res, sr; use.assign(n, 1);   /* reaproveitados (todo quadro do meio, cada desenho) */
    for (int pass = 0; pass < 2; pass++) {
        double S[3][3] = {}, X[3] = {}, Y[3] = {}; size_t cnt = 0;
        for (size_t i = 0; i < n; i++) {
            if (!use[i]) continue;
            const double v[3] = {P[i * 4], P[i * 4 + 1], 1.0}; cnt++;
            for (int r = 0; r < 3; r++) { for (int c2 = 0; c2 < 3; c2++) S[r][c2] += v[r] * v[c2]; X[r] += v[r] * P[i * 4 + 2]; Y[r] += v[r] * P[i * 4 + 3]; }
        }
        if (cnt < 6) return false;
        const double det = S[0][0] * (S[1][1] * S[2][2] - S[1][2] * S[2][1]) - S[0][1] * (S[1][0] * S[2][2] - S[1][2] * S[2][0]) + S[0][2] * (S[1][0] * S[2][1] - S[1][1] * S[2][0]);
        if (fabs(det) < 1e-6 * (S[2][2] * S[2][2] * S[2][2] + 1.0)) return false;   /* pontos colineares */
        auto solve = [&](const double *B, float *o) {   /* Cramer */
            for (int k = 0; k < 3; k++) {
                double M[3][3]; for (int r = 0; r < 3; r++) for (int c2 = 0; c2 < 3; c2++) M[r][c2] = c2 == k ? B[r] : S[r][c2];
                o[k] = (float)((M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1]) - M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0]) + M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0])) / det);
            }
        };
        solve(X, m); solve(Y, m + 3);
        if (pass == 1) break;
        res.resize(n); for (size_t i = 0; i < n; i++) res[i] = fabsf(m[0] * P[i * 4] + m[1] * P[i * 4 + 1] + m[2] - P[i * 4 + 2]) + fabsf(m[3] * P[i * 4] + m[4] * P[i * 4 + 1] + m[5] - P[i * 4 + 3]);
        sr = res; std::nth_element(sr.begin(), sr.begin() + n / 2, sr.end()); const float lim = std::max(2.f, 3.f * sr[n / 2]);
        for (size_t i = 0; i < n; i++) use[i] = res[i] <= lim;
    }
    return true;
}
int mid_rtt(const FzEntry &ec, const FzEntry &ep, std::vector<Frozen> &tmp);
/* pareia as listas de desenho do quadro anterior (pv) e do novo (cur) e desenha o quadro do meio em `target`. depth 1 = lista de um alvo de textura */
/* 120 FPS: alvos refeitos do 1o intermediario do par, guardados ate interp_extra_done (nao voltam ao pool antes: outro alvo refeito os sobrescreveria entre os intermediarios) */
struct PreC { bool valid = false, solid = false, velset = false; float ex = 0, ey = 0, af[6] = {}; };
static std::vector<PreC> g_pcache;
static std::vector<int8_t> g_bcache; static uint64_t g_bcache_vb = ~0ull; static size_t g_bcache_nv, g_bcache_pv;
static double g_pf_calc, g_pf_rtt, g_pf_draw; static int g_pf_n;
struct RingFrame { uint64_t vb = 0; std::string text; };
static std::deque<RingFrame> g_ring;
bool interp_lists(const Rec &pv, Rec &cur, int target, int depth, std::vector<Frozen> &tmp) {
    const double tcalc0 = gpu_now_us();
    std::vector<GpuVtx> &g_out = g_outs[depth];
    static const float T0 = getenv("TF_INTERP_MAX") ? (float)atof(getenv("TF_INTERP_MAX")) : 96.f; const float T = T0 * g_T_mul;   /* deslocamento maximo (pixels nativos) para interpolar um triangulo */
    /* pareia cada desenho novo com o desenho anterior de mesma forma que menos andou (janela em torno do ultimo pareado; ordem de desenho
     * pode mudar, ex.: o botao destacado do menu) */
    const size_t pn = pv.d.size(), cn = cur.d.size();
    std::vector<int> match(cn, -1); std::vector<char> used(pn, 0); size_t hint = 0, nm = 0;   std::vector<char> uvchg(cn, 0);   /* uvchg: sprite 3D (boneco do mapa) que trocou de quadro de animacao (outro recorte da mesma textura) mas e do mesmo tamanho e esta perto: par com UV novo */
    /* a sliding text line: this glyph pair moved by the same offset as a nearby pair with the same texture rectangle (see the glyph rule below) */
    static const bool no_slide = getenv("TF_INTERP_NOSLIDE") != nullptr;
    auto slide_pair = [&](size_t j, size_t k) -> bool {
        if (no_slide) return false;
        const GpuVtx &b0 = cur.v[cur.d[j].off], &a0 = pv.v[pv.d[k].off];
        /* most of the glyphs around it (not just one: a text swap lines up by chance on a few repeated letters, a slide moves the whole line) */
        int avail = 0, ok = 0;
        for (int dn = -3; dn <= 3; dn++) {
            const int jn = (int)j + dn, kn = (int)k + dn;
            if (!dn || jn < 0 || jn >= (int)cn || (cur.d[jn].n != 6 && cur.d[jn].n != 12) || cur.v[cur.d[jn].off].z >= 1.f) continue;
            avail++;
            if (kn < 0 || kn >= (int)pn || !same_kind(pv.d[kn], cur.d[jn])) continue;
            const GpuVtx &nb = cur.v[cur.d[jn].off], &na = pv.v[pv.d[kn].off];
            if (fabsf(nb.u - na.u) > 2.f || fabsf(nb.v - na.v) > 2.f) continue;
            if (fabsf((nb.x - na.x) - (b0.x - a0.x)) <= 2.f && fabsf((nb.y - na.y) - (b0.y - a0.y)) <= 2.f) ok++;
        }
        return ok >= 2 && ok * 2 >= avail;
    };
    for (size_t j = 0; j < cn; j++) {
        const Draw &c = cur.d[j]; int best = -1; float bestc = 1e30f;
        size_t lo = hint > 6 ? hint - 6 : 0, hi = std::min(pn, hint + 14);
        /* janela em torno do ultimo par; sem par nela, uma 2a passada olha o resto da lista. Um efeito que entra no meio da lista (a espiral do corte
         * de invocacao: ~20 desenhos novos antes do resto) desloca os indices alem da janela e deixava TODO o resto sem par (cena "trocou": o corte
         * 3D ficava a 30 FPS; sessao 20261023_213431, quadros 3779-3793). TF_INTERP_NOWIDE=1 desliga (A/B) */
        static const bool no_wide = getenv("TF_INTERP_NOWIDE") != nullptr;
        /* a poor pair inside the window (a sprite that would travel > 12 px) does not stop the search: the real partner can be outside it when the
         * draw order changed (the hand: the raised card moves to the end of the list, the window then held only OTHER cards' identical frames and each
         * card slid to the next slot in the middle frames; session 20261024_141125, F8 'P' marks). TF_INTERP_NOWIDEPOOR=1 disables */
        static const bool no_widepoor = getenv("TF_INTERP_NOWIDEPOOR") != nullptr;
        for (int pass = 0; pass < (no_wide ? 1 : 2) && (best < 0 || (pass == 1 && !no_widepoor && c.n <= 12 && bestc > 12.f * g_T_mul)); pass++)
        for (size_t k = pass ? 0 : lo, kend = pass ? pn : hi; k < kend; k++) {
            if (pass && k >= lo && k < hi) continue;
            if (used[k] || !same_kind(pv.d[k], c)) continue;
            const GpuVtx *a = &pv.v[pv.d[k].off], *b = &cur.v[c.off]; float cost = 0; const uint32_t step = std::max<uint32_t>(1, c.n / 48);
            bool uvok = true;
            for (uint32_t q = 0; q < c.n && uvok; q += step) { cost += fabsf(b[q].x - a[q].x) + fabsf(b[q].y - a[q].y); if (fabsf(b[q].u - a[q].u) > 2.f || fabsf(b[q].v - a[q].v) > 2.f) uvok = false; }
            if (!uvok) continue;   /* outro recorte da textura: nao e o mesmo objeto */
            cost /= (float)((c.n + step - 1) / step);
            /* identical sprites of different sizes far apart (opponent's small hand card vs the player's big one, same card-back texture, drawn
             * in a different order than the previous frame): the pair made the middle frame squash and slide a card across the table (flicker
             * of the leftmost hand card, session 20261024_130045/131952, found by the [iaudit] log). A real zoom keeps the displacement small
             * relative to the sprite. TF_INTERP_NOSIZEGUARD=1 disables */
            static const bool no_sizeguard = getenv("TF_INTERP_NOSIZEGUARD") != nullptr;
            if (!no_sizeguard && c.n == 6 && c.s.tex != 0) {
                float cx0 = 1e9f, cx1 = -1e9f, cy0 = 1e9f, cy1 = -1e9f, px0 = 1e9f, px1 = -1e9f, py0 = 1e9f, py1 = -1e9f;
                for (int q = 0; q < 6; q++) { cx0 = std::min(cx0, b[q].x); cx1 = std::max(cx1, b[q].x); cy0 = std::min(cy0, b[q].y); cy1 = std::max(cy1, b[q].y); px0 = std::min(px0, a[q].x); px1 = std::max(px1, a[q].x); py0 = std::min(py0, a[q].y); py1 = std::max(py1, a[q].y); }
                const float cw = cx1 - cx0, ch = cy1 - cy0, pw = px1 - px0, ph = py1 - py0, big = std::max(std::max(cw, ch), std::max(pw, ph));
                const bool resized = fabsf(cw - pw) > 0.3f * std::max(cw, pw) + 2 || fabsf(ch - ph) > 0.3f * std::max(ch, ph) + 2;
                if (resized && cost > 0.75f * big) continue;
            }
            /* letra de texto 2D (um desenho de 1 quadrado, ou 2 com a sombra, no atlas da fonte): quando a mensagem do dialogo troca, o MESMO
             * recorte aparece em outra palavra e o pareamento por UV o fazia deslizar ate la ("Nic to eet you" por ~50 ms no 120 FPS; sessao
             * 20261018_121134). Texto nao anda mais que alguns px por quadro: acima disso e troca de conteudo, sem par (aparece como no quadro
             * novo). TF_INTERP_BISECT bit 8 desliga */
            if (!v1b(8) && (c.n == 6 || c.n == 12) && cur.v[c.off].z < 1.f) {
                float gx0 = 1e9f, gx1 = -1e9f, gy0 = 1e9f, gy1 = -1e9f;
                for (uint32_t q = 0; q < c.n; q++) { const GpuVtx &w = cur.v[c.off + q]; gx0 = std::min(gx0, w.x); gx1 = std::max(gx1, w.x); gy0 = std::min(gy0, w.y); gy1 = std::max(gy1, w.y); }
                if (gx1 - gx0 <= 40.f && gy1 - gy0 <= 40.f && cost > 4.f * g_T_mul) {
                    /* a whole line of text sliding in (Free Duel opponent panel: name, deck, portrait labels fly in at ~70 px per game frame, the
                     * glyphs were left unpaired = 30 FPS): a big move is real when the NEIGHBOURING glyph (next or previous draw) has the same texture
                     * rectangle and moved by the same offset. A content swap does not line up like that. TF_INTERP_NOSLIDE=1 disables */
                    const bool slide = slide_pair(j, k);
                    if (!slide) continue;
                }
            }
            /* objetos identicos em fila (pacotes da pilha da loja, cartas da mao): o vizinho pode estar mais perto na tela que o proprio objeto
             * quando ele anda (um pacote subindo), e o par saia deslocado em cadeia. O jogo desenha na mesma ordem quase sempre: o candidato
             * que continua a sequencia do ultimo par (ou de mesmo indice) leva desconto, e so perde para um bem mais perto (g_v1 desliga) */
            /* o desconto so vale para um candidato que andou o que uma camera anda por quadro (<= 20 px): sem esse limite, quando a ordem de desenho
             * mudava entre dois quadros (indice igual = OUTRO objeto) o vizinho distante (NPC do lado, a ~45 px) ganhava do proprio objeto: a cabeca do NPC
             * era pareada com a do vizinho e o quadro do meio a mostrava no caminho entre os dois, solta do corpo (sessao 20261018_003821, Duel Field;
             * achado por TF_INTERP_BISECT=2) */
            if (!v1b(1) && (k == hint || k == j) && cost <= 20.f * g_T_mul) cost = cost * 0.25f - 4.f;
            if (cost < bestc) { bestc = cost; best = (int)k; }
        }
        /* contador de digitos (LP caindo): cada digito e um desenho com o recorte do glifo; quando o valor muda, o MESMO glifo aparece no slot
         * vizinho (9975 -> 9950: o '5' passa do slot 4 ao 3) e o pareamento por UV o fazia deslizar meio slot por cima do digito vizinho.
         * Se o desenho andou ~1 largura dele e o lugar onde chegou estava ocupado no quadro anterior por OUTRO recorte (mesma forma, mesma
         * posicao), e troca de conteudo e nao movimento: sem par (aparece como no quadro novo). TF_INTERP_NOSLOT=1 desliga */
        static const bool no_slot = getenv("TF_INTERP_NOSLOT") != nullptr;
        if (best >= 0 && !no_slot) {
            const GpuVtx *a = &pv.v[pv.d[best].off], *b = &cur.v[c.off]; const uint32_t step = std::max<uint32_t>(1, c.n / 48);
            float mdx = 0, x0 = 1e9f, x1 = -1e9f; uint32_t cnt = 0;
            for (uint32_t q = 0; q < c.n; q += step) { mdx += fabsf(b[q].x - a[q].x); x0 = std::min(x0, b[q].x); x1 = std::max(x1, b[q].x); cnt++; }
            mdx /= (float)cnt;
            if (mdx > 1.5f && x1 - x0 < 80.f && mdx >= 0.5f * (x1 - x0) && !((c.n == 6 || c.n == 12) && slide_pair(j, (size_t)best))) {
                for (size_t k = lo; k < hi; k++) {
                    if ((int)k == best || !same_kind(pv.d[k], c)) continue;
                    const GpuVtx *o = &pv.v[pv.d[k].off]; float pos = 0; bool uvdiff = false;
                    for (uint32_t q = 0; q < c.n; q += step) { pos += fabsf(b[q].x - o[q].x) + fabsf(b[q].y - o[q].y); if (fabsf(b[q].u - o[q].u) > 2.f || fabsf(b[q].v - o[q].v) > 2.f) uvdiff = true; }
                    if (uvdiff && pos / (float)cnt <= 1.f) { best = -1; break; }
                }
            }
        }
        /* boneco do mapa que troca de quadro de animacao (parado -> andando, passo): o recorte da textura muda, o pareamento por UV recusava e o
         * sprite aparecia no lugar novo ja no 1o quadro do meio, adiantado em relacao ao cenario, e depois ficava parado ate o quadro real
         * ("pula e congela"; sessao 20261018_143032, NPC ao lado do usuario no Slifer Red Dorm). Mesma textura, mesmo tamanho (+-2 px) e a no maximo
         * 20 px (o que a camera anda por quadro; a 10 px falhava com a camera em 15 px): e o mesmo boneco; interpola so a posicao e usa o UV do quadro novo. TF_INTERP_NOSPRITEUV=1 desliga */
        static const bool no_spriteuv = getenv("TF_INTERP_NOSPRITEUV") != nullptr;
        if (best < 0 && !no_spriteuv && c.n == 6 && cur.v[c.off].z >= 2.f && c.s.tex != 0 && !(c.s.tex & GPU_FBTEX)) {
            auto bb = [](const GpuVtx *v, float *cx, float *cy, float *w, float *h) { float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f; for (int q = 0; q < 6; q++) { x0 = std::min(x0, v[q].x); x1 = std::max(x1, v[q].x); y0 = std::min(y0, v[q].y); y1 = std::max(y1, v[q].y); } *cx = (x0 + x1) * .5f; *cy = (y0 + y1) * .5f; *w = x1 - x0; *h = y1 - y0; };
            float bcx, bcy, bw, bh; bb(&cur.v[c.off], &bcx, &bcy, &bw, &bh); float bd = 20.f * g_T_mul;
            for (size_t k = lo; k < hi; k++) {
                if (used[k] || pv.d[k].n != 6 || pv.d[k].s.tex == 0 || (pv.d[k].s.tex & GPU_FBTEX) || !same_kind_anytex(pv.d[k], c)) continue;
                float acx, acy, aw, ah; bb(&pv.v[pv.d[k].off], &acx, &acy, &aw, &ah);
                const float d = hypotf(bcx - acx, bcy - acy);
                if (fabsf(aw - bw) <= 2.f && fabsf(ah - bh) <= 2.f && d <= bd) { bd = d; best = (int)k; uvchg[j] = 1; }
            }
        }
        if (best >= 0) { match[j] = best; used[best] = 1; hint = best + 1; nm++; }
    }
    /* Always on (TF_INTERP_NOAUDIT=1 disables): sanity check of every accepted pair, no images needed. A sprite quad whose size or texture rectangle
     * changes a lot between the two real frames is the signature of a wrong pairing (the middle frame shows a squashed or swapped card). */
    static const bool utrace = getenv("TF_INTERP_TRACE") != nullptr;
    if (utrace && depth == 0) {   /* debug: every draw that found no partner (it shows up in the middle frame only as the new one, i.e. at 30 FPS) */
        int nu = 0;
        for (size_t j = 0; j < cn; j++) if (match[j] < 0) nu++;
        fprintf(stderr, "[iunm] vb=%llu unmatched %d of %zu\n", (unsigned long long)g_cur_vb, nu, cn);
        for (size_t j = 0, shown = 0; j < cn && shown < 40; j++) {
            if (match[j] >= 0) continue; const Draw &c = cur.d[j]; shown++;
            float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f; for (uint32_t q = 0; q < c.n; q++) { const GpuVtx &w = cur.v[c.off + q]; x0 = std::min(x0, w.x); x1 = std::max(x1, w.x); y0 = std::min(y0, w.y); y1 = std::max(y1, w.y); }
            fprintf(stderr, "[iunm]   %zu n=%u tex=%x %dx%d box %.0f,%.0f %.0fx%.0f\n", j, c.n, c.s.tex, c.s.tw, c.s.th, x0, y0, x1 - x0, y1 - y0);
        }
    }
    static const bool audit = getenv("TF_INTERP_NOAUDIT") == nullptr;
    if (audit && depth == 0) {
        int bad = 0;
        for (size_t j = 0; j < cn; j++) {
            if (match[j] < 0 || cur.d[j].n != 6 || cur.d[j].s.tex == 0) continue;
            const Draw &c = cur.d[j], &p = pv.d[match[j]];
            float cx0 = 1e9f, cx1 = -1e9f, cy0 = 1e9f, cy1 = -1e9f, px0 = 1e9f, px1 = -1e9f, py0 = 1e9f, py1 = -1e9f;
            for (int q = 0; q < 6; q++) { const GpuVtx &a = cur.v[c.off + q], &b = pv.v[p.off + q]; cx0 = std::min(cx0, a.x); cx1 = std::max(cx1, a.x); cy0 = std::min(cy0, a.y); cy1 = std::max(cy1, a.y); px0 = std::min(px0, b.x); px1 = std::max(px1, b.x); py0 = std::min(py0, b.y); py1 = std::max(py1, b.y); }
            const float cw = cx1 - cx0, pw = px1 - px0, ch = cy1 - cy0, ph = py1 - py0;
            const GpuVtx &cv = cur.v[c.off], &pvv = pv.v[p.off];
            const bool size = fabsf(cw - pw) > 0.3f * std::max(cw, pw) + 2 || fabsf(ch - ph) > 0.3f * std::max(ch, ph) + 2;
            const bool uv = fabsf(cv.u - pvv.u) > 2 || fabsf(cv.v - pvv.v) > 2;
            if (!size && !uv) continue;
            bad++;
            if (bad <= 6) fprintf(stderr, "[iaudit] vb=%llu cur %zu tex=%x %dx%d %s%s w %.0f->%.0f h %.0f->%.0f uv %.0f,%.0f->%.0f,%.0f at %.0f,%.0f\n", (unsigned long long)g_cur_vb, j, c.s.tex, c.s.tw, c.s.th, size ? "SIZE " : "", uv ? "UV" : "", pw, cw, ph, ch, pvv.u, pvv.v, cv.u, cv.v, cx0, cy0);
        }
        if (bad) fprintf(stderr, "[iaudit] vb=%llu flagged=%d of %zu draws\n", (unsigned long long)g_cur_vb, bad, cn);
        /* black box for the F8 mark: the pairing of the last frames (every small sprite: what it was paired with, or UNMATCHED), so the
         * cause of a flicker can be read from the session log without a replay (interp_audit_flush) */
        if (g_ring.empty() || g_ring.back().vb != g_cur_vb) {
            if (g_ring.size() >= 8) g_ring.pop_front();
            g_ring.push_back(RingFrame()); g_ring.back().vb = g_cur_vb;
            std::string &o = g_ring.back().text; char buf[220];
            for (size_t j = 0; j < cn; j++) {
                const Draw &c = cur.d[j]; if ((c.n != 6 && c.n != 12) || c.s.tex == 0) continue;
                float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f; for (uint32_t q = 0; q < c.n; q++) { const GpuVtx &w = cur.v[c.off + q]; x0 = std::min(x0, w.x); x1 = std::max(x1, w.x); y0 = std::min(y0, w.y); y1 = std::max(y1, w.y); }
                if (x1 - x0 > 140 || y1 - y0 > 140 || x1 < 0 || x0 > 480) continue;
                if (match[j] < 0) { snprintf(buf, sizeof buf, "  %zu tex=%x %dx%d u=%.0f,%.0f box %.0f,%.0f %.0fx%.0f UNMATCHED\n", j, c.s.tex, c.s.tw, c.s.th, cur.v[c.off].u, cur.v[c.off].v, x0, y0, x1 - x0, y1 - y0); o += buf; continue; }
                const Draw &p = pv.d[match[j]]; float px0 = 1e9f, px1 = -1e9f, py0 = 1e9f, py1 = -1e9f; for (uint32_t q = 0; q < p.n; q++) { const GpuVtx &w = pv.v[p.off + q]; px0 = std::min(px0, w.x); px1 = std::max(px1, w.x); py0 = std::min(py0, w.y); py1 = std::max(py1, w.y); }
                snprintf(buf, sizeof buf, "  %zu tex=%x %dx%d u=%.0f,%.0f box %.0f,%.0f %.0fx%.0f <- prev %d box %.0f,%.0f %.0fx%.0f\n", j, c.s.tex, c.s.tw, c.s.th, cur.v[c.off].u, cur.v[c.off].v, x0, y0, x1 - x0, y1 - y0, match[j], px0, py0, px1 - px0, py1 - py0); o += buf;
            }
        }
    }
    if (depth == 0 && g_dump_vb >= 0) { if (dump_vb_now(g_cur_vb)) {
        for (size_t j = 0; j < cn; j++) { const Draw &c = cur.d[j]; const GpuVtx &v0 = cur.v[c.off];
            float x0=1e9,y0=1e9,x1=-1e9,y1=-1e9; for (uint32_t q = 0; q < c.n; q++) { const GpuVtx &w = cur.v[c.off + q]; x0 = std::min(x0, w.x); x1 = std::max(x1, w.x); y0 = std::min(y0, w.y); y1 = std::max(y1, w.y); }
            fprintf(stderr, "[idump] bbox %.0f,%.0f-%.0f,%.0f z=%.0f ", x0, y0, x1, y1, v0.z); fprintf(stderr, "cur %zu n=%u tex=%x %dx%d x=%.1f y=%.1f u=%.1f v=%.1f col=%08x clr=%u blend=%u -> prev %d", j, c.n, c.s.tex, c.s.tw, c.s.th, v0.x, v0.y, v0.u, v0.v, v0.col, c.s.clear, c.s.blend, match[j]);
            if (match[j] < 0 && c.n == 6 && c.s.tex != 0) { for (size_t k = 0; k < pn; k++) { if (pv.d[k].n != 6 || pv.d[k].otex != c.otex) continue; float bx0 = 1e9f, bx1 = -1e9f, by0 = 1e9f, by1 = -1e9f; for (int q = 0; q < 6; q++) { const GpuVtx &w = pv.v[pv.d[k].off + q]; bx0 = std::min(bx0, w.x); bx1 = std::max(bx1, w.x); by0 = std::min(by0, w.y); by1 = std::max(by1, w.y); } fprintf(stderr, " [cand %zu%s bbox %.0f,%.0f-%.0f,%.0f u=%.0f v=%.0f same=%d]", k, used[k] ? " usado" : "", bx0, by0, bx1, by1, pv.v[pv.d[k].off].u, pv.v[pv.d[k].off].v, (int)same_kind(pv.d[k], c)); } }
            if (match[j] >= 0) { const Draw &pd = pv.d[match[j]]; const GpuVtx &p0 = pv.v[pd.off]; float px0 = 1e9f, px1 = -1e9f; for (uint32_t q = 0; q < pd.n; q++) { px0 = std::min(px0, pv.v[pd.off + q].x); px1 = std::max(px1, pv.v[pd.off + q].x); } fprintf(stderr, " (x=%.1f y=%.1f, prev x %.0f..%.0f)", p0.x, p0.y, px0, px1); } fputc(10, stderr); } } }
    if (nm * 2 < cn || cn < 2) { g_st_nomatch++; return false; }   /* cena trocou: nao ha o que interpolar */
    g_out.assign(cur.v.begin(), cur.v.end());
    /* cull no quadro do meio: 0 = decide pela orientacao interpolada, 1 = desenha, 2 = descarta. Um triangulo de costas (ou de frente) nos
     * DOIS quadros reais fica assim no meio: num giro grande, a orientacao interpolada linearmente pode virar e mostrar o lado de dentro
     * do modelo (cacos pretos no cabelo do professor) */
    static std::vector<uint8_t> g_face[2]; std::vector<uint8_t> &face = g_face[depth]; face.assign(cur.v.size() / 3, 0);
    auto front = [](const GpuVtx *t, int cm) { const float ar = (t[1].x - t[0].x) * (t[2].y - t[0].y) - (t[1].y - t[0].y) * (t[2].x - t[0].x); return fabsf(ar) >= 1e-6f && (ar > 0) != (cm == 1); };
    /* rolagem do cenario: deslocamento comum dos desenhos 3D (z grande) pareados, se quase todos andaram igual */
    float scx = 0, scy = 0; bool scroll = false;
    if (!v1b(2)) {   /* usada para aceitar a partida da camera (todos os desenhos saltam juntos); mover os desenhos sem par junto (TF_INTERP_SCROLL) piorou */
        std::vector<float> dxs, dys;
        for (size_t j = 0; j < cn; j++) {
            if (match[j] < 0 || cur.d[j].s.clear) continue;
            const GpuVtx &b = cur.v[cur.d[j].off], &a = pv.v[pv.d[match[j]].off];
            if (b.z < 1000.f) continue;
            dxs.push_back(b.x - a.x); dys.push_back(b.y - a.y);
        }
        if (dxs.size() >= 8) {
            std::vector<float> sx = dxs, sy = dys; std::nth_element(sx.begin(), sx.begin() + sx.size() / 2, sx.end()); std::nth_element(sy.begin(), sy.begin() + sy.size() / 2, sy.end());
            float mx = sx[sx.size() / 2], my = sy[sy.size() / 2]; size_t agree = 0;
            for (size_t i = 0; i < dxs.size(); i++) if (fabsf(dxs[i] - mx) <= 1.5f && fabsf(dys[i] - my) <= 1.5f) agree++;
            if ((fabsf(mx) > 0.5f || fabsf(my) > 0.5f) && fabsf(mx) <= T && fabsf(my) <= T && agree * 10 >= dxs.size() * 7) { scx = mx; scy = my; scroll = true; }
        }
    }
    cur.disp.assign(cn, 1e9f); cur.vel.assign(cn, std::array<float, 2>{1e9f, 1e9f}); cur.vel2.assign(cn, std::array<float, 2>{1e9f, 1e9f});
    const bool dump_now = depth == 0 && g_dump_vb >= 0 && dump_vb_now(g_cur_vb);
    static const bool no_loop = getenv("TF_INTERP_NOLOOP") != nullptr;   /* A/B: sem a regra do laco de rolagem (comportamento antigo: o meio do salto) */
    static const bool no_bcache = getenv("TF_INTERP_NOBCACHE") != nullptr, selftest_on = getenv("TF_INTERP_SELFTEST") != nullptr;
    bool bcache_on = false;
    if (depth == 0 && !no_bcache && !selftest_on) {
        const size_t nt = cur.v.size() / 3 + 1;
        if (!(g_bcache_vb == g_cur_vb && g_bcache_nv == cur.v.size() && g_bcache_pv == pv.v.size() && g_bcache.size() == nt)) { g_bcache.assign(nt, -2); g_pcache.assign(cn, PreC()); g_bcache_vb = g_cur_vb; g_bcache_nv = cur.v.size(); g_bcache_pv = pv.v.size(); }
        bcache_on = true;
    }
    VMap &shared = g_vmap[depth]; shared.reset(cur.v.size());   /* posicao final de cada vertice do quadro novo (em todos os desenhos): vertices compartilhados nao podem se separar (rachaduras entre poligonos vizinhos) */
    /* triangulos nao interpolados so SEGUEM os vizinhos interpolados depois de todos processados: se definissem a posicao do vertice (por virem
     * antes na lista), o vizinho interpolado herdaria um vertice no lugar novo e esticaria (pacotes da vitrine da loja tortos no quadro do meio) */
    static std::vector<uint32_t> pinned; pinned.clear();
    for (size_t j = 0; j < cn; j++) {
        if (match[j] < 0 || cur.d[j].s.clear) continue;
        const Draw &c = cur.d[j], &p = pv.d[match[j]];
        const float prev_disp = (size_t)match[j] < pv.disp.size() ? pv.disp[match[j]] : 1e9f; float dmax = 0; bool dany = false;
        auto commit = [&](const GpuVtx *b, GpuVtx *o) {
            for (int q = 0; q < 3; q++) {
                uint32_t xb, yb, zb; memcpy(&xb, &b[q].x, 4); memcpy(&yb, &b[q].y, 4); memcpy(&zb, &b[q].z, 4);
                uint64_t key = ((uint64_t)xb << 32 | yb) * 0x9E3779B97F4A7C15ull ^ zb;
                bool novo; const std::array<float, 3> &v = shared.get(key, std::array<float, 3>{o[q].x, o[q].y, o[q].z}, &novo);
                if (!novo) { o[q].x = v[0]; o[q].y = v[1]; o[q].z = v[2]; }
            }
        };
        /* movimento esperado do desenho: mediana do deslocamento dos triangulos alinhados por indice (com o mesmo UV). Escolher o par "que menos
         * andou" falha em objetos repetidos: na vitrine da loja as cartas ficam a ~13 px umas das outras e a camera anda ~13 px por quadro, entao
         * a carta VIZINHA (mesmo UV) parecia o par mais perto e as cartas saiam rasgadas no quadro do meio */
        /* movimento afim do desenho (translacao + rotacao + escala): objeto que gira ou vem para a camera (pacote voando na loja, carta virando)
         * tem deslocamentos diferentes em cada ponto; com so a translacao, os triangulos pequenos dele caiam na regra de 'particula' e o objeto
         * rasgava no quadro do meio. Os pares saem dos triangulos alinhados por indice (mesmo UV); quando a ordem muda (muitos pares errados),
         * os minimos quadrados entortam e a mediana da translacao e mais robusta: vale o modelo que explica mais pontos (<= 2 px). */
        float ex = 0, ey = 0, af[6] = {1, 0, 0, 0, 1, 0}; bool solid = false;   /* solid: o desenho andou como um bloco (>= 90% dos vertices com o deslocamento tipico) */
        /* 120 FPS: movimento do desenho (mediana, ajuste afim, solid) nao depende de t: o 1o intermediario do par guarda, os outros leem (fit_affine +
         * medianas eram ~30% do tempo do interp_lists) */
        PreC *pc = bcache_on && j < g_pcache.size() ? &g_pcache[j] : nullptr;
        if (pc && pc->valid) { ex = pc->ex; ey = pc->ey; memcpy(af, pc->af, sizeof af); solid = pc->solid; if (pc->velset) cur.vel[j] = {ex, ey}; }
        else if (!v1b(3)) {
            static std::vector<float> mx, my, P; mx.clear(); my.clear(); P.clear();
            for (uint32_t k = 0; k + 2 < c.n && k + 2 < p.n; k += 3) {
                const GpuVtx *b = &cur.v[c.off + k], *a = &pv.v[p.off + k]; bool ok = true;
                for (int q = 0; q < 3 && ok; q++) if (fabsf(b[q].u - a[q].u) > 2.f || fabsf(b[q].v - a[q].v) > 2.f) ok = false;
                if (!ok) continue;
                mx.push_back((b[0].x + b[1].x + b[2].x - a[0].x - a[1].x - a[2].x) / 3.f); my.push_back((b[0].y + b[1].y + b[2].y - a[0].y - a[1].y - a[2].y) / 3.f);
                for (int q = 0; q < 3; q++) { P.push_back(a[q].x); P.push_back(a[q].y); P.push_back(b[q].x); P.push_back(b[q].y); }
            }
            if (!mx.empty()) { std::nth_element(mx.begin(), mx.begin() + mx.size() / 2, mx.end()); std::nth_element(my.begin(), my.begin() + my.size() / 2, my.end()); ex = mx[mx.size() / 2]; ey = my[my.size() / 2]; cur.vel[j] = {ex, ey}; }
            af[2] = ex; af[5] = ey;
            if (!P.empty()) { size_t in = 0; const size_t np = P.size() / 4;
                for (size_t i = 0; i < np; i++) in += fabsf(P[i * 4 + 2] - P[i * 4] - ex) + fabsf(P[i * 4 + 3] - P[i * 4 + 1] - ey) <= 1.5f * g_T_mul;
                solid = in * 10 >= np * 9; }
            float m[6];
            if (fit_affine(P, m)) {
                size_t in_t = 0, in_a = 0; const size_t np = P.size() / 4;
                for (size_t i = 0; i < np; i++) {
                    const float ax = P[i * 4], ay = P[i * 4 + 1], bx = P[i * 4 + 2], by = P[i * 4 + 3];
                    in_t += fabsf(ax + ex - bx) + fabsf(ay + ey - by) <= 2.f;
                    in_a += fabsf(m[0] * ax + m[1] * ay + m[2] - bx) + fabsf(m[3] * ax + m[4] * ay + m[5] - by) <= 2.f;
                }
                if (in_a > in_t) memcpy(af, m, sizeof af);
            }
            if (pc) { pc->valid = true; pc->ex = ex; pc->ey = ey; memcpy(pc->af, af, sizeof af); pc->solid = solid; pc->velset = !mx.empty(); }
        }
        auto dev = [&](const GpuVtx &a, const GpuVtx &b) { return fabsf(af[0] * a.x + af[1] * a.y + af[2] - b.x) + fabsf(af[3] * a.x + af[4] * a.y + af[5] - b.y); };   /* distancia ao previsto */
        /* LACO DE ROLAGEM: fundos que rolam sem fim (xadrez do Duel Result: +1 px por quadro e, a cada 32, volta 31 px; o desenho com textura
         * periodica fica igual) e letreiros que recomecam do inicio saltam PARA TRAS de uma vez. O meio desse salto e uma posicao que nao
         * existe: no xadrez, meio periodo = casas claras e escuras trocadas por um quadro (piscada a cada ~1 s, reported by a player). Triangulo
         * que inverte o sentido do movimento do quadro anterior com um salto bem maior que aquele passo = laco: o meio continua o movimento
         * anterior por meio passo (no fundo periodico fica perfeito; num objeto que recomeca, ele so anda mais um pouco antes de reaparecer).
         * Assinatura exigida (sem ela, cursor que pula e pedacos de modelo 3D nos cortes de camera dos cut-ins eram 'extrapolados' e saiam
         * separados/quebrados): desenho 2D plano (1/w = 1 em todos os vertices), que vinha rolando com velocidade CONSTANTE (os dois passos
         * anteriores iguais), que agora salta como um bloco (>= 90% dos vertices com o mesmo deslocamento) para tras, mais de 4x o passo
         * (+4 px), e o salto cabe dentro do proprio desenho (o ladrilho volta para cobrir o lugar de onde saiu; um objeto pequeno que pula
         * longe e outra coisa: teletransporte, cursor). Os triangulos so seguem a regra se andarem junto com o bloco, sem deformar. */
        if (match[j] < (int)pv.vel.size()) cur.vel2[j] = pv.vel[match[j]];
        float pvx = 0, pvy = 0; bool pvel = false, pring = false;   /* pring: a volta do anel (o par nao e vizinho dos pares dos desenhos ao lado) */
        if (!v1b(4) && !no_loop && solid && (size_t)match[j] < pv.vel.size() && pv.vel[match[j]][0] < 1e8f && pv.vel2[match[j]][0] < 1e8f) {
            pvx = pv.vel[match[j]][0]; pvy = pv.vel[match[j]][1];
            const float pl = hypotf(pvx, pvy), dl = hypotf(ex, ey);
            const bool steady = fabsf(pvx - pv.vel2[match[j]][0]) + fabsf(pvy - pv.vel2[match[j]][1]) <= 0.25f + 0.1f * pl;
            if (steady && pl >= 0.25f && ex * pvx + ey * pvy < 0.f && dl > 4.f * pl + 4.f * g_T_mul) {
                bool flat = true; float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
                for (uint32_t q = 0; q < c.n; q++) { const GpuVtx &w = cur.v[c.off + q]; if (w.rw != 1.f) { flat = false; break; } x0 = std::min(x0, w.x); x1 = std::max(x1, w.x); y0 = std::min(y0, w.y); y1 = std::max(y1, w.y); }
                /* o fundo da animacao e um anel de ladrilhos iguais (3 quadrados de 480 px que rolam e dao a volta: o que sai de um lado reaparece no
                 * outro, a 1440 px): o salto do ladrilho que da a volta e maior que ele mesmo, mas cabe no anel. A extensao vale entao a dos desenhos
                 * da mesma forma nos dois quadros; sem isso o ladrilho que cobria a borda esquerda nao andava no quadro do meio e sobrava uma faixa vazia */
                float ext = std::max(x1 - x0, y1 - y0);
                if (flat && dl > ext) {
                    float gx0 = x0, gy0 = y0, gx1 = x1, gy1 = y1;
                    auto grow = [&](const Rec &r) { for (const Draw &o : r.d) if (same_kind(o, c)) for (uint32_t q = 0; q < o.n; q++) { const GpuVtx &w = r.v[o.off + q]; gx0 = std::min(gx0, w.x); gx1 = std::max(gx1, w.x); gy0 = std::min(gy0, w.y); gy1 = std::max(gy1, w.y); } };
                    grow(cur); grow(pv); ext = std::max(gx1 - gx0, gy1 - gy0); pring = true;
                }
                pvel = flat && dl <= ext;
            }
        }
        const bool glyph2d = !v1b(9) && (c.n == 6 || c.n == 12) && cur.v[c.off].z < 1.f && c.s.tex != 0;
        for (uint32_t k = 0; k + 2 < c.n; k += 3) {
            const GpuVtx *b = &cur.v[c.off + k]; GpuVtx *o = &g_out[c.off + k];
            if (uvchg[j]) {   /* sprite que trocou de quadro de animacao: posicao interpolada, UV e cor do quadro novo */
                const GpuVtx *a = &pv.v[p.off + k]; float md = 0;
                for (int q = 0; q < 3; q++) md = std::max(md, hypotf(b[q].x - a[q].x, b[q].y - a[q].y));
                g_st_tris++;
                if (md > 24.f * g_T_mul) { pinned.push_back(c.off + k); continue; }
                g_st_tris_ok++; dmax = std::max(dmax, md); dany = true;
                for (int q = 0; q < 3; q++) { o[q] = b[q]; o[q].x = mix_f(a[q].x, b[q].x); o[q].y = mix_f(a[q].y, b[q].y); o[q].z = mix_f(a[q].z, b[q].z); }
                commit(b, o);
                continue;
            }
            /* triangulo anterior equivalente: mesmo UV e deslocamento mais parecido com o do desenho, numa janela em volta (o alinhamento por
             * indice falha quando um desenho junta varios objetos e a ordem deles muda, ex.: o botao destacado do menu) */
            const GpuVtx *best = nullptr; float bestc = 1e30f; int bestmi = -1;
            /* 120 FPS: os tres intermediarios do par (t = 0,25 / 0,5 / 0,75) repetem esta busca com o mesmo resultado (so a mistura depende de t):
             * o 1o a guarda por triangulo e os outros leem (era ~2 ms de CPU por intermediario no mapa, 6 ms por quadro novo; o 120 FPS caia
             * para ~52 quadros/s reais). TF_INTERP_NOBCACHE=1 desliga (A/B) */
            const size_t bci = (c.off + k) / 3; bool hit = false;
            if (bcache_on) { const int8_t cv = g_bcache[bci]; if (cv != -2) { hit = true; bestmi = cv; if (cv >= 0) { const int64_t kk = (int64_t)k + (int64_t)((cv & 1) ? -((cv + 1) / 2) : (cv / 2)) * 3; best = &pv.v[p.off + kk]; } } }
            if (!hit) for (int mi = 0; mi <= 24; mi++) {
                int64_t kk = (int64_t)k + (int64_t)((mi & 1) ? -((mi + 1) / 2) : (mi / 2)) * 3;   /* 0, -1, +1, -2, +2... triangulos */
                if (kk < 0 || kk + 2 >= (int64_t)p.n) continue;
                const GpuVtx *a = &pv.v[p.off + kk]; bool ok = true; float cost = 0;
                for (int q = 0; q < 3 && ok; q++) {
                    if (fabsf(b[q].u - a[q].u) > 2.f || fabsf(b[q].v - a[q].v) > 2.f) ok = false;
                    else cost += dev(a[q], b[q]);
                }
                if (ok && cost < bestc) { bestc = cost; best = a; bestmi = mi; }
                if (bestc < 0.75f) break;   /* ja bate com o previsto (< 0,25 px por vertice): os outros candidatos so custariam tempo */
            }
            if (bcache_on && !hit) g_bcache[bci] = (int8_t)bestmi;
            if (dump_now && bestmi != 0) fprintf(stderr, "[idump2] cur %zu tri %u: E=(%.1f,%.1f) par %d custo %.1f (b0 %.1f,%.1f)\n", j, k / 3, ex, ey, bestmi, bestc, b[0].x, b[0].y);
            g_st_tris++;
            bool ok = best != nullptr;
            const GpuVtx *a = best;
            if (best && pvel) {   /* laco de rolagem: o desenho saltou para tras (> 4x o passo anterior + 4 px) e este triangulo foi junto, sem deformar */
                const float tdx = (b[0].x + b[1].x + b[2].x - a[0].x - a[1].x - a[2].x) / 3.f, tdy = (b[0].y + b[1].y + b[2].y - a[0].y - a[1].y - a[2].y) / 3.f;
                float defo = 0; for (int q = 0; q < 3; q++) defo = std::max(defo, fabsf(b[q].x - a[q].x - tdx) + fabsf(b[q].y - a[q].y - tdy));
                if (defo <= 1.f * g_T_mul && fabsf(tdx - ex) + fabsf(tdy - ey) <= 2.f * g_T_mul + 0.05f * hypotf(ex, ey)) {
                    const float h = g_t * g_T_mul;   /* no autoteste o quadro do meio fica um passo inteiro a frente de F0 */
                    for (int q = 0; q < 3; q++) { o[q] = a[q]; o[q].x = a[q].x + pvx * h; o[q].y = a[q].y + pvy * h; o[q].col = mix_col(a[q].col, b[q].col); }
                    if (c.cull) face[(c.off + k) / 3] = front(a, c.cull) ? 1 : 2;
                    if (dump_now) fprintf(stderr, "[idump2] cur %zu tri %u: laco de rolagem (salto %.1f,%.1f contra o passo anterior %.1f,%.1f): meio passo extrapolado%c", j, k / 3, tdx, tdy, pvx, pvy, 10);
                    g_st_tris_ok++; g_st_loop++;
                    if (!pring) commit(b, o);   /* na volta do anel os vertices compartilhados com o ladrilho ao lado (no quadro novo) nao se juntam: um esticaria o outro por cima do fundo */
                    continue;
                }
            }
            for (int q = 0; q < 3 && ok; q++) if (fabsf(b[q].x - a[q].x) > T || fabsf(b[q].y - a[q].y) > T) ok = false;
            float md = 0, e = 0;
            if (best) {
                for (int q = 0; q < 3; q++) { int q2 = (q + 1) % 3; e = std::max(e, hypotf(b[q].x - b[q2].x, b[q].y - b[q2].y)); md = std::max(md, hypotf(b[q].x - a[q].x, b[q].y - a[q].y)); }
                dmax = std::max(dmax, md); dany = true;
            }
            /* peca pequena que andou muito para o tamanho dela = outra particula (respingo que renasce em outro lugar), nao movimento; mas nao se ela
             * andou junto com o resto do desenho (frisos finos do cenario com a camera andando: 350 triangulos recusados num quadro da loja) */
            bool rigid = false;
            if (best) { float dv = 0; for (int q = 0; q < 3; q++) dv = std::max(dv, dev(a[q], b[q])); rigid = !v1b(5) && dv <= 2.f * g_T_mul; }
            if (ok && !rigid && md > (2.f * e + 6.f) * g_T_mul) ok = false;   /* g_T_mul: no autoteste o passo e o dobro */
            /* estava parada no quadro anterior e agora salta: passo de animacao por quadros (ex.: o tile da cachoeira troca de posicao a cada ~10 vblanks);
             * o movimento suave nunca comeca com um salto grande, e o meio do salto seria um estagio inexistente (um quadro de "flash"). Limite 20 px:
             * a cachoeira salta ~42 px; a camera do campo parte do repouso com 12,8 px por quadro (com 12 px, todo inicio de caminhada dava um tranco) */
            /* ... exceto quando o salto e a rolagem rigida do cenario inteiro (a camera comecando a andar: todos os desenhos estavam parados e saltam juntos) */
            if (ok && prev_disp < 1.f && md > (v1b(6) ? 12.f : 20.f) * g_T_mul && !(scroll && fabsf((b[0].x - a[0].x) - scx) <= 1.5f && fabsf((b[0].y - a[0].y) - scy) <= 1.5f)) ok = false;
            /* faixa 2D da interface (um retangulo, n=6, ate 48 px de altura) que anda em passos de slot de 12+ px (o corpo da barra do item do menu desce 16 px por
             * passo enquanto a ponta ao lado fica parada): o meio do passo (8 px) desalinhava as duas pecas e a barra aparecia torta por 1-2 subquadros, estagio
             * que o original nao tem; aparece no lugar novo, como no original. Rolagem rigida do cenario fica de fora. TF_INTERP_BISECT bit 1024 desliga
             * A strip that was ALREADY moving the same way (same velocity as in the previous pair) is a slide (Free Duel opponent panel flying in at ~36 px per game frame,
             * name/deck boxes and their text were pinned = 30 FPS), not a slot step: it is interpolated. TF_INTERP_NOSLIDE=1 disables */
            if (ok && !v1b(10) && c.n == 6 && b[0].rw == 1.f && b[1].rw == 1.f && b[2].rw == 1.f && b[0].z < 2.f && md > 12.f * g_T_mul && !scroll && !(!no_slide && (pvel ? hypotf(ex - pvx, ey - pvy) <= 0.3f * hypotf(ex, ey) + 2.f : fabsf(prev_disp - md) <= 0.15f * md + 1.f))) {
                float gy0 = 1e9f, gy1 = -1e9f; for (uint32_t q = 0; q < 6; q++) { gy0 = std::min(gy0, cur.v[c.off + q].y); gy1 = std::max(gy1, cur.v[c.off + q].y); }
                if (gy1 - gy0 <= 48.f) ok = false;
            }
            if (c.cull) face[(c.off + k) / 3] = (ok && best && front(best, c.cull) != front(b, c.cull)) ? 0 : (front(b, c.cull) ? 1 : 2);
            if (!ok) { if (v1b(7)) commit(b, o); else pinned.push_back(c.off + k); if (dump_now) fprintf(stderr, "[idump2] cur %zu tri %u: NAO interpolado (par %d, md %.1f, e %.1f, prev_disp %.1f)%c", j, k / 3, bestmi, md, e, prev_disp, 10); continue; }
            g_st_tris_ok++;
            for (int q = 0; q < 3; q++) {
                o[q].x = mix_f(a[q].x, b[q].x); o[q].y = mix_f(a[q].y, b[q].y); o[q].z = mix_f(a[q].z, b[q].z);
                o[q].u = mix_f(a[q].u, b[q].u); o[q].v = mix_f(a[q].v, b[q].v); o[q].rw = mix_f(a[q].rw, b[q].rw);
                o[q].col = glyph2d ? b[q].col : mix_col(a[q].col, b[q].col);   /* letra de texto: cor do quadro novo (misturar fazia a letra nascer cinza por 2 subquadros no 'very fast') */
            }
            commit(b, o);
        }
        if (dany) cur.disp[j] = dmax;
    }
    auto follow = [&](const GpuVtx &b, GpuVtx &o) {
        uint32_t xb, yb, zb; memcpy(&xb, &b.x, 4); memcpy(&yb, &b.y, 4); memcpy(&zb, &b.z, 4);
        if (const std::array<float, 3> *v = shared.find(((uint64_t)xb << 32 | yb) * 0x9E3779B97F4A7C15ull ^ zb)) { o.x = (*v)[0]; o.y = (*v)[1]; o.z = (*v)[2]; }
    };
    for (uint32_t i : pinned) for (int q = 0; q < 3; q++) follow(cur.v[i + q], g_out[i + q]);
    /* desenhos sem par (ex.: poligonos cujo numero de triangulos mudou por recorte) ficam no lugar novo, mas os vertices que dividem posicao com um
     * desenho interpolado vao junto: senao abre uma rachadura entre os dois e o fundo aparece */
    for (size_t j = 0; j < cn; j++) {
        if (match[j] >= 0 || cur.d[j].s.clear) continue;
        static const bool scroll_unmatched = getenv("TF_INTERP_SCROLL") != nullptr;   /* EXPERIMENTAL: piorou a medida de pixels inventados na loja (47 mil -> 58 mil) */
        /* a glyph of a sliding text line that has no previous partner (it was still outside the screen in the previous frame): it
         * follows the displacement of its matched neighbours instead of appearing, at the new place, next to its own sliding word
         * (a ghost "Nova" + "va"). TF_INTERP_NOSLIDE=1 disables */
        float sdx = 0, sdy = 0;
        { static const bool no_slide2 = getenv("TF_INTERP_NOSLIDE") != nullptr;
          const Draw &cj = cur.d[j];
          if (!no_slide2 && (cj.n == 6 || cj.n == 12) && cur.v[cj.off].z < 1.f) {
              float ddx[6], ddy[6]; int nd = 0;
              for (int dn = -3; dn <= 3; dn++) {
                  const int jn = (int)j + dn; if (!dn || jn < 0 || jn >= (int)cn || match[jn] < 0 || cur.d[jn].n != cj.n) continue;
                  const GpuVtx &nb = cur.v[cur.d[jn].off], &na = pv.v[pv.d[match[jn]].off];
                  if (fabsf(nb.x - na.x) + fabsf(nb.y - na.y) > 4.f) { ddx[nd] = nb.x - na.x; ddy[nd] = nb.y - na.y; nd++; } }
              for (int i = 0; i < nd && sdx == 0.f && sdy == 0.f; i++) {   /* two matched neighbours moved the same way */
                  int same = 0; for (int q = 0; q < nd; q++) if (fabsf(ddx[q] - ddx[i]) <= 2.f && fabsf(ddy[q] - ddy[i]) <= 2.f) same++;
                  if (same >= 2) { sdx = ddx[i]; sdy = ddy[i]; } } } }
        bool world = scroll_unmatched && scroll && cur.v[cur.d[j].off].z >= 1000.f;   /* desenho 3D sem par (recortado na borda: o numero de vertices mudou): anda a metade da rolagem, como o resto do cenario */
        for (uint32_t q = 0; q < cur.d[j].n; q++) {
            const GpuVtx &b = cur.v[cur.d[j].off + q]; GpuVtx &o = g_out[cur.d[j].off + q];
            if (world) { o.x = b.x - (1.f - g_t) * scx; o.y = b.y - (1.f - g_t) * scy; }
            else if (sdx != 0.f || sdy != 0.f) { o.x = b.x - (1.f - g_t) * sdx; o.y = b.y - (1.f - g_t) * sdy; continue; }
            follow(b, o);
        }
    }
    /* cull refeito com as posicoes do quadro do meio (triangulo degenerado = nao desenha) */
    for (size_t j = 0; j < cn; j++) {
        const int cm = cur.d[j].cull; if (!cm) continue;
        for (uint32_t k = 0; k + 2 < cur.d[j].n; k += 3) {
            GpuVtx *o = &g_out[cur.d[j].off + k];
            const float area = (o[1].x - o[0].x) * (o[2].y - o[0].y) - (o[1].y - o[0].y) * (o[2].x - o[0].x);
            const uint8_t fc = face.empty() ? 0 : face[(cur.d[j].off + k) / 3];
            const bool hide = fc == 2 || fabsf(area) < 1e-6f || (fc == 0 && (area > 0) == (cm == 1));
            if (hide) { o[1].x = o[2].x = o[0].x; o[1].y = o[2].y = o[0].y; }
        }
    }
    const double tcalc1 = gpu_now_us(); double trtt = 0, tdraw = 0;
    std::vector<std::pair<int, int>> rtt_mid;   /* copia congelada -> alvo refeito interpolado neste quadro do meio */
    for (size_t j = 0; j < cn; j++) { GpuState s = cur.d[j].s; s.fb = target;
        if (match[j] >= 0) { const GpuState &ps = pv.d[match[j]].s; s.sx1 = mix_i(ps.sx1, s.sx1); s.sy1 = mix_i(ps.sy1, s.sy1); s.sx2 = mix_i(ps.sx2, s.sx2); s.sy2 = mix_i(ps.sy2, s.sy2); }   /* o recorte (scissor) tambem anda */
        /* textura = alvo congelado (ex.: reflexo do chao da loja): refaz o alvo a partir das listas dos dois quadros, interpolado, senao o reflexo
         * ficaria meio passo atras do resto do quadro do meio */
        if (depth == 0 && match[j] >= 0 && (s.tex & GPU_FBTEX) && s.tex != cur.d[j].otex) {
            const int fc = (int)(s.tex & 0xff), fp = (int)(pv.d[match[j]].s.tex & 0xff); int got = -2;
            for (auto &m : rtt_mid) if (m.first == fc) got = m.second;
            if (got == -2) {
                const FzEntry *ec = nullptr, *ep = nullptr;
                for (const auto &e : cur.fzmap) if (e.fb == fc) ec = &e;
                for (const auto &e : pv.fzmap) if (e.fb == fp) ep = &e;
                const double tr0 = gpu_now_us();
                /* 120 FPS: o alvo refeito do 1o intermediario do par (t = 0,25) serve aos outros dois (o reflexo erra 1/4 de quadro: invisivel; refazer custava
                 * 2,4 ms de CPU por intermediario no mapa). TF_INTERP_RTT_POR_T=1 refaz em cada um */
                bool reused = false;
                if (g_rtt_cache_on && depth == 0) for (const auto &r : g_rtt_cache) if (r.first == fc) { got = r.second; reused = true; }
                if (!reused) { got = ec && ep ? mid_rtt(*ec, *ep, tmp) : -1; if (g_rtt_cache_on && depth == 0) g_rtt_cache.push_back({fc, got}); }
                trtt += gpu_now_us() - tr0;
                rtt_mid.push_back({fc, got});
            }
            if (got >= 0) s.tex = (uint32_t)GPU_FBTEX | (uint32_t)got;
        }
        const double td0 = gpu_now_us(); gpu_draw(s, &g_out[cur.d[j].off], (int)cur.d[j].n); tdraw += gpu_now_us() - td0; }
    if (depth == 0) { g_pf_calc += tcalc1 - tcalc0; g_pf_rtt += trtt; g_pf_draw += tdraw; if (++g_pf_n >= 300) { if (g_dbg) fprintf(stderr, "[interp] CPU por intermediario: pareamento+vertices %.2f ms, alvo refeito %.2f, gpu_draw %.2f (ms; medios por chamada)%c", g_pf_calc / 1000.0 / g_pf_n, g_pf_rtt / 1000.0 / g_pf_n, g_pf_draw / 1000.0 / g_pf_n, 10); g_pf_calc = g_pf_rtt = g_pf_draw = 0; g_pf_n = 0; } }
    return true;
}
/* alvo de textura do quadro do meio: as listas dos dois quadros (cada uma comeca num clear de cor) pareadas e desenhadas num alvo temporario */
int mid_rtt(const FzEntry &ec, const FzEntry &ep, std::vector<Frozen> &tmp) {
    static const bool off = getenv("TF_INTERP_NORTT") != nullptr;   /* depuracao: usa a copia congelada do quadro novo */
    if (off || !ec.list || !ep.list || ec.w != ep.w || ec.h != ep.h || ec.src != ep.src) return -1;
    Frozen t{-1, ec.w, ec.h, g_ge_res};
    for (size_t i = 0; i < g_fpool.size(); i++)
        if (g_fpool[i].w == t.w && g_fpool[i].h == t.h && g_fpool[i].res == t.res) { t = g_fpool[i]; g_fpool.erase(g_fpool.begin() + i); break; }
    if (t.fb < 0) { t.fb = gpu_fb_create(t.w, t.h, t.res); if (t.fb < 0) return -1; }
    tmp.push_back(t);   /* volta ao pool depois do quadro do meio (os desenhos seguintes no pool vem depois na fila da GPU) */
    const bool ok = interp_lists(*ep.list, *ec.list, t.fb, 1, tmp);
    if (g_dbg) { static int nd; if (nd++ < 10) fprintf(stderr, "[interp] alvo %u refeito no quadro do meio: %s (%zu/%zu desenhos)\n", ec.src, ok ? "ok" : "sem par", ep.list->d.size(), ec.list->d.size()); }
    return ok ? t.fb : -1;
}
/* um quadro so de rascunho basta: cada intermediario e desenhado depois que o anterior ja foi apresentado (a fila da GPU respeita a ordem) */
bool render_mid(const Rec &pv, Rec &cur, int res) {
    if (g_scratch >= 0 && g_scratch_res != res) { gpu_fb_destroy(g_scratch); g_scratch = -1; }
    if (g_scratch < 0) { g_scratch = gpu_fb_create(480, 272, res); g_scratch_res = res; if (g_scratch < 0) return false; }
    std::vector<Frozen> tmp;
    bool ok = interp_lists(pv, cur, g_scratch, 0, tmp);
    for (const Frozen &f : tmp) (g_rtt_cache_on ? g_rtt_held : g_fpool).push_back(f);
    return ok;
}

void dump_bmp(int fb, uint64_t vb) {   /* t = 0,5: gen/interp_<vb>.bmp (como sempre); 120 FPS: gen/interp_<vb>_25.bmp e _75.bmp */
    std::vector<uint32_t> px; int w, h;
    if (!gpu_fb_readback(fb, px, &w, &h)) return;
    char fn[64];
    if (g_t == 0.5f) snprintf(fn, sizeof fn, "gen/interp_%llu.bmp", (unsigned long long)vb);
    else snprintf(fn, sizeof fn, "gen/interp_%llu_%d.bmp", (unsigned long long)vb, (int)lroundf(g_t * 100.f));
    const bool ok = write_bmp24(fn, w, h, [&](int y, uint8_t *row) {
        for (int x = 0; x < w; x++) { uint32_t p = px[(size_t)y * w + x]; row[x * 3] = (uint8_t)(p >> 16); row[x * 3 + 1] = (uint8_t)(p >> 8); row[x * 3 + 2] = (uint8_t)p; }
    });
    if (ok) fprintf(stderr, "[interp] quadro intermediario %llu gravado\n", (unsigned long long)vb);
}
}

namespace {
/* AUTOTESTE (TF_INTERP_SELFTEST=1): verdade de referencia sem precisar de um jogo a 60 fps. O jogo gera quadros reais F0, F1, F2 a 30 fps;
 * interpola F0->F2 (o dobro do movimento, entao e um teste mais duro) e compara com o F1 REAL, que e exatamente o quadro do meio. Mede o erro por
 * pixel em qualquer cena/movimento, aponta ONDE esta (grade 8x4) e, com TF_INTERP_SNAP, grava gen/selftest_<vblank>.bmp (intermediario com os pixels
 * errados em magenta). "hold" = erro de simplesmente mostrar F2 no lugar do meio (o que o 30 fps faria); a nota boa e erro bem abaixo do hold. */
Rec g_prev2; bool g_have_prev2; uint64_t g_gap_prev; std::vector<uint32_t> g_real1; int g_r1w, g_r1h;
uint64_t g_stt_n, g_stt_bad_i, g_stt_bad_h, g_stt_px;
void selftest_dump(const std::vector<uint32_t> &m, const std::vector<char> &bad, int w, int h, uint64_t vb) {
    char fn[64]; snprintf(fn, sizeof fn, "gen/selftest_%llu.bmp", (unsigned long long)vb);
    write_bmp24(fn, w, h, [&](int y, uint8_t *row) {
        for (int x = 0; x < w; x++) { uint32_t p = m[(size_t)y * w + x]; uint8_t r = p & 255, g = p >> 8, b = p >> 16;
            if (bad[(size_t)y * w + x]) { r = 255; g = 0; b = 255; } else { r = r * 2 / 5; g = g * 2 / 5; b = b * 2 / 5; }
            row[x * 3] = b; row[x * 3 + 1] = g; row[x * 3 + 2] = r; }
    });
}
void selftest_run(Rec &cur, int gfb, uint64_t vb, int res) {   /* chamado ao chegar F2 (cur), com g_prev = F1 e g_prev2 = F0 */
    g_T_mul = 2.f; g_t = 0.5f;
    bool ok = render_mid(g_prev2, cur, res); g_T_mul = 1.f;
    if (!ok || g_real1.empty()) return;
    std::vector<uint32_t> m, h2; int w, h, w2, hh2;
    if (!gpu_fb_readback(g_scratch, m, &w, &h) || !gpu_fb_readback(gfb, h2, &w2, &hh2) || w != g_r1w || h != g_r1h || w2 != w) return;
    std::vector<char> bad((size_t)w * h); uint64_t bi = 0, bh = 0; int cell[8][4] = {};
    auto differs = [](uint32_t a, uint32_t b) { int d = abs((int)(a & 255) - (int)(b & 255)) + abs((int)((a >> 8) & 255) - (int)((b >> 8) & 255)) + abs((int)((a >> 16) & 255) - (int)((b >> 16) & 255)); return d > 120; };
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        size_t i = (size_t)y * w + x; bool e = differs(m[i], g_real1[i]); bad[i] = e; bi += e; bh += differs(h2[i], g_real1[i]);
        if (e) cell[x * 8 / w][y * 4 / h]++;
    }
    g_stt_n++; g_stt_bad_i += bi; g_stt_bad_h += bh; g_stt_px += (uint64_t)w * h;
    if (getenv("TF_INTERP_SELFTEST_V")) {
        fprintf(stderr, "[selftest] vb %llu: erro %.2f%% (hold %.2f%%) grade:", (unsigned long long)vb, 100.0 * bi / ((double)w * h), 100.0 * bh / ((double)w * h));
        for (int cy = 0; cy < 4; cy++) { fputc(' ', stderr); for (int cx = 0; cx < 8; cx++) fprintf(stderr, "%d%s", (int)(100.0 * cell[cx][cy] * 32 / ((double)w * h) * 1.0), cx < 7 ? "," : ""); }
        fputc(10, stderr);
    }
    if (quadro_na_lista(getenv("TF_INTERP_SNAP"), vb)) selftest_dump(m, bad, w, h, vb);
    if (g_stt_n % 100 == 0) fprintf(stderr, "[selftest] %llu quadros: erro medio %.3f%%, hold %.3f%% (razao %.2f)\n", (unsigned long long)g_stt_n, 100.0 * g_stt_bad_i / g_stt_px, 100.0 * g_stt_bad_h / g_stt_px, (double)g_stt_bad_i / std::max<uint64_t>(1, g_stt_bad_h));
}
}

/* ha desenho novo no quadro exibido desde a ultima apresentacao? */
bool interp_has_new(int gfb) { auto it = g_pend.find(gfb); return it != g_pend.end() && it->second.any; }

/* o quadro novo ja esta pronto e vai ganhar um quadro do meio? (display_present pode apresentar sem esperar a grade de vblanks) */
bool interp_mid_ready(int gfb, uint64_t vb, bool allow) {
    if (!allow || !interp_enabled() || !g_have_prev || vb - g_last_fresh != 2) return false;
    auto it = g_pend.find(gfb);
    return it != g_pend.end() && it->second.any && !it->second.taint && !g_prev.taint && !it->second.d.empty();
}

bool interp_prepare(int gfb, uint64_t vb, bool allow, int res, float t) {
    g_mid_ready = false; g_static_fb = -1;
    if (!interp_enabled()) { free_dead(true); if (g_have_prev || !g_pend.empty()) interp_reset(); return false; }
    auto it = g_pend.find(gfb);
    if (it == g_pend.end() || !it->second.any) return false;   /* nada novo: o chamador mostra o quadro real (o 2o vblank do quadro) */
    Rec cur = std::move(it->second); it->second = Rec();
    g_old.clear(); g_have_old = false;   /* intermediarios do par anterior que nao sairam (troca de modo, quadro novo antes da hora) */
    const uint64_t gap = vb - g_last_fresh; g_last_fresh = vb;
    bool done = false;
    static const bool trace = getenv("TF_INTERP_TRACE") != nullptr;
    if (trace) {   /* depuracao: um resumo por quadro novo (fb, intervalo, nº de desenhos e a posicao do primeiro desenho 3D) */
        float x = 0, y = 0; for (const Draw &d : cur.d) if (d.n >= 30 && cur.v[d.off].z >= 1000.f) { x = cur.v[d.off].x; y = cur.v[d.off].y; break; }
        fprintf(stderr, "[itrace] vb %llu fb %d gap %llu desenhos %zu taint %d 3d=(%.1f,%.1f)\n", (unsigned long long)vb, gfb, (unsigned long long)gap, cur.d.size(), (int)cur.taint, x, y);
    }
    static const bool selftest = getenv("TF_INTERP_SELFTEST") != nullptr;
    if (selftest && allow && gap == 2 && g_gap_prev == 2 && g_have_prev && g_have_prev2 && !cur.taint && !g_prev.taint && !g_prev2.taint && !cur.d.empty()) { g_cur_vb = vb; selftest_run(cur, gfb, vb, res); }
    if (allow && gap == 2 && g_have_prev && !cur.taint && !g_prev.taint && !cur.d.empty()) {   /* jogo a 30 fps: o vblank do meio ganha o quadro intermediario */
        static const bool rtt_per_t = getenv("TF_INTERP_RTT_POR_T") != nullptr; for (const Frozen &f : g_rtt_held) g_fpool.push_back(f); g_rtt_held.clear(); g_rtt_cache.clear(); g_rtt_cache_on = t != 0.5f && !rtt_per_t;
        g_cur_vb = vb; g_t = t; const double t0 = gpu_now_us(); const uint64_t loops0 = g_st_loop; const bool parado = static_pair(g_prev, cur);
        if (parado) { g_static_fb = gfb; done = true; g_st_static++; gpu_kick(); /* solta o quadro real retido (gpu_hold): e ele que vai aparecer */ } else { g_static_fb = -1; gpu_mid_scope(true); done = render_mid(g_prev, cur, res); if (done) { g_gpu_prof_tag = 'M'; gpu_kick(); } gpu_mid_scope(false); }
        g_st_us += gpu_now_us() - t0;
        if (trace && g_st_loop != loops0) fprintf(stderr, "[itrace] vb %llu: %llu triangulo(s) no laco de rolagem (meio passo extrapolado)\n", (unsigned long long)vb, (unsigned long long)(g_st_loop - loops0));
        if (done) {
            if (quadro_na_lista(getenv("TF_INTERP_SNAP"), vb)) dump_bmp(parado ? gfb : g_scratch, vb);
            /* (o intermediario ja foi enviado a GPU acima, na frente do quadro real retido: ela o desenha enquanto o chamador espera a hora de mostrar) */
            g_mid_ready = true; g_st_mid++;
        } else g_st_skip++;
    } else g_st_skip++;
    if (g_dbg && (g_st_mid + g_st_skip) % 300 == 0)
        fprintf(stderr, "[interp] quadros novos %llu: intermediarios %llu (%llu parados: a imagem real no lugar), sem interpolar %llu (cena trocou %llu); triangulos interpolados %llu de %llu (laco de rolagem %llu); CPU %.2f ms por quadro do meio\n", (unsigned long long)(g_st_mid + g_st_skip), (unsigned long long)g_st_mid, (unsigned long long)g_st_static, (unsigned long long)g_st_skip, (unsigned long long)g_st_nomatch, (unsigned long long)g_st_tris_ok, (unsigned long long)g_st_tris, (unsigned long long)g_st_loop, g_st_mid ? g_st_us / 1000.0 / (double)g_st_mid : 0.0);
    free_dead();
    if (selftest) { g_prev2 = g_prev; g_have_prev2 = g_have_prev; g_gap_prev = gap; g_real1.clear(); if (!gpu_fb_readback(gfb, g_real1, &g_r1w, &g_r1h)) g_real1.clear(); }
    g_froze = false;
    if (done && t != 0.5f) { g_old = std::move(g_prev); g_have_old = true; g_old_vb = vb; }   /* 120 FPS: os outros intermediarios do par saem de interp_extra */
    g_prev = std::move(cur); g_have_prev = true;
    return done;
}
void interp_show(void) {
    if (!g_mid_ready) return;
    g_mid_ready = false; gpu_seq_tag(1); gpu_present_fb(g_static_fb >= 0 ? g_static_fb : g_scratch);
}
bool interp_mid_static(void) { return g_static_fb >= 0; }
int interp_take_fb(void) { if (!g_mid_ready) return -1; g_mid_ready = false; return g_static_fb >= 0 ? g_static_fb : g_scratch; }   /* o intermediario pronto (sem apresentar) */
bool interp_hold_ok(void) { return !g_froze; }
bool interp_present(int gfb, uint64_t vb, bool allow, int res) {
    if (!interp_prepare(gfb, vb, allow, res, 0.5f)) return false;
    interp_show(); return true;
}
/* 120 FPS: mais um intermediario do par (g_old, g_prev) na fracao t, desenhado e enviado a GPU agora; aparece com interp_show. Os contadores do
 * TF_INTERP_DEBUG ficam como os do primeiro intermediario (o pareamento e o mesmo; contar de novo so inflaria os numeros) */
bool interp_extra(float t, int res) {
    g_mid_ready = false;
    if (!g_have_old || !g_have_prev || !interp_enabled()) return false;
    const uint64_t s[6] = {g_st_tris, g_st_tris_ok, g_st_nomatch, g_st_loop, g_st_mid, g_st_skip};
    if (g_static_fb >= 0) { g_mid_ready = true; return true; }   /* par parado (interp_prepare): a imagem real de novo */
    g_t = t; g_cur_vb = g_old_vb;
    const bool ok = render_mid(g_old, g_prev, res);
    g_st_tris = s[0]; g_st_tris_ok = s[1]; g_st_nomatch = s[2]; g_st_loop = s[3]; g_st_mid = s[4]; g_st_skip = s[5];
    if (!ok) return false;
    if (quadro_na_lista(getenv("TF_INTERP_SNAP"), g_old_vb)) dump_bmp(g_scratch, g_old_vb);
    g_gpu_prof_tag = 'M'; gpu_kick(); g_mid_ready = true;
    return true;
}
void interp_extra_done(void) { g_old.clear(); g_have_old = false; for (const Frozen &f : g_rtt_held) g_fpool.push_back(f); g_rtt_held.clear(); g_rtt_cache.clear(); g_rtt_cache_on = false; }   /* devolve as copias congeladas de g_old ao pool */
void interp_audit_flush(unsigned mark) {   /* F8: the pairing ring kept by the audit in interp_lists */
    for (const RingFrame &r : g_ring) fprintf(stderr, "[iring] mark %u vb=%llu\n%s", mark, (unsigned long long)r.vb, r.text.c_str());
}
void interp_use_scratch(int k) { scratch_use(k); }   /* (scratch_use fica no namespace anonimo, junto do g_scratch) */
