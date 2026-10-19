/* Codificador BC7 (ver bc7.h). Cada bloco 4x4 vira 128 bits num de 8 modos; aqui usamos 5:
 *   6: um grupo, RGBA juntos, extremos 7 bits + bit p, 16 niveis      (blocos lisos/gradientes, com ou sem alfa)
 *   1: dois grupos (64 particoes), RGB 6 bits + p comum, 8 niveis      (blocos opacos com duas cores: texto, bordas)
 *   3: dois grupos, RGB 7 bits + p, 4 niveis                           (idem, cores mais exatas)
 *   5: RGB e alfa separados (7 / 8 bits, 4 niveis cada)                (borda de transparencia sobre cor)
 *   7: dois grupos, RGBA 5 bits + p, 4 niveis                          (borda de transparencia entre duas cores)
 * O modo 6 sai sempre; os outros so sao tentados quando o erro do 6 passa do limite (a maioria dos blocos para ali).
 * Em cada grupo: eixo principal (covariancia) -> extremos -> indices -> minimos quadrados, testando os bits p. */
#include "bc7.h"
#include <math.h>
#include <string.h>
#include <algorithm>

size_t bc7_level_bytes(int w, int h) { return (size_t)((w + 3) / 4) * ((h + 3) / 4) * 16; }
size_t bc7_chain_bytes(int w, int h, int *levels) {
    size_t t = bc7_level_bytes(w, h); int n = 1;
    while (w > 1 || h > 1) { w = std::max(1, w / 2); h = std::max(1, h / 2); t += bc7_level_bytes(w, h); n++; }
    if (levels) *levels = n;
    return t;
}

static const int WT2[4] = {0, 21, 43, 64}, WT3[8] = {0, 9, 18, 27, 37, 46, 55, 64}, WT4[16] = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};
static const int *weights(int ib) { return ib == 2 ? WT2 : ib == 3 ? WT3 : WT4; }
/* particoes de 2 grupos (bit i = grupo do texel i) e o texel ancora do grupo 1 (o do grupo 0 e sempre o texel 0) */
static const uint16_t PART2[64] = {
    0xCCCC, 0x8888, 0xEEEE, 0xECC8, 0xC880, 0xFEEC, 0xFEC8, 0xEC80, 0xC800, 0xFFEC, 0xFE80, 0xE800, 0xFFE8, 0xFF00, 0xFFF0, 0xF000,
    0xF710, 0x008E, 0x7100, 0x08CE, 0x008C, 0x7310, 0x3100, 0x8CCE, 0x088C, 0x3110, 0x6666, 0x366C, 0x17E8, 0x0FF0, 0x718E, 0x399C,
    0xAAAA, 0xF0F0, 0x5A5A, 0x33CC, 0x3C3C, 0x55AA, 0x9696, 0xA55A, 0x73CE, 0x13C8, 0x324C, 0x3BDC, 0x6996, 0xC33C, 0x9966, 0x0660,
    0x0272, 0x04E4, 0x4E40, 0x2720, 0xC936, 0x936C, 0x39C6, 0x639C, 0x9336, 0x9CC6, 0x817E, 0xE718, 0xCCF0, 0x0FCC, 0x7744, 0xEE22};
static const uint8_t ANCHOR2[64] = {
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 2, 8, 2, 2, 8, 8, 15, 2, 8, 2, 2, 8, 8, 2, 2,
    15, 15, 6, 8, 2, 8, 15, 15, 2, 8, 2, 2, 2, 15, 15, 6, 6, 2, 6, 8, 15, 15, 2, 2, 15, 15, 15, 15, 15, 2, 2, 15};

int g_bc7_rounds = 2;
static inline int lerp6(int a, int b, int w) { return ((64 - w) * a + w * b + 32) >> 6; }
static inline int unq(int x, int b) { return b >= 8 ? x : (x << (8 - b)) | (x >> (2 * b - 8)); }   /* extremo de b bits (com o p no bit 0) -> 8 bits */

struct Cfg { int c0, nch, cb, pm, ib; };   /* canais [c0, c0+nch); bits de cor; p: 0 nenhum, 1 comum aos 2 extremos, 2 um por extremo; bits de indice */
struct Fit { int x[2][4]; uint8_t idx[16]; uint32_t err; };   /* x: extremos quantizados (b bits com o p no bit 0) */

static int quant1(float v, int cb, int p, int hasp) {   /* valor quantizado mais proximo de v com o bit p dado */
    const int b = cb + hasp, top = (1 << cb) - 1; int q = (int)floorf(v / 255.0f * top + 0.5f), best = 0, be = 1 << 30;
    for (int k = std::max(0, q - 1); k <= std::min(top, q + 1); k++) { const int x = hasp ? (k << 1 | p) : k; const int e = abs(unq(x, b) - (int)(v + 0.5f)); if (e < be) { be = e; best = x; } }
    return best;
}
static uint32_t assign(const int (*p)[4], const uint8_t *sel, int n, const Cfg &c, const int *e0, const int *e1, uint8_t *idx) {
    const int *W = weights(c.ib), ni = 1 << c.ib; int pal[16][4];
    for (int i = 0; i < ni; i++) for (int k = 0; k < c.nch; k++) pal[i][k] = lerp6(e0[k], e1[k], W[i]);
    int d[4] = {0, 0, 0, 0}, dd = 0; for (int k = 0; k < c.nch; k++) { d[k] = e1[k] - e0[k]; dd += d[k] * d[k]; }
    uint32_t tot = 0;
    for (int j = 0; j < n; j++) {
        const int *q = p[sel[j]] + c.c0; int g = 0;
        if (dd > 0) {
            int dp = 0; for (int k = 0; k < c.nch; k++) dp += (q[k] - e0[k]) * d[k];
            const int w = std::max(0, std::min(64, (dp * 64 + dd / 2) / dd));
            while (g < ni - 1 && W[g + 1] <= w) g++;
            if (g < ni - 1 && W[g + 1] - w < w - W[g]) g++;
        }
        uint32_t be = ~0u; int bi = g;
        for (int i = std::max(0, g - 1); i <= std::min(ni - 1, g + 1); i++) {
            uint32_t e = 0; for (int k = 0; k < c.nch; k++) { const int t = q[k] - pal[i][k]; e += (uint32_t)(t * t); }
            if (e < be) { be = e; bi = i; }
        }
        idx[j] = (uint8_t)bi; tot += be;
    }
    return tot;
}
/* ajusta um grupo (texels sel[0..n)) na configuracao c */
static Fit fit(const int (*p)[4], const uint8_t *sel, int n, const Cfg &c) {
    float m[4] = {0, 0, 0, 0}; const int nc = c.nch;
    for (int j = 0; j < n; j++) for (int k = 0; k < nc; k++) m[k] += p[sel[j]][c.c0 + k];
    for (int k = 0; k < nc; k++) m[k] /= n;
    float cov[4][4] = {}; int lo[4] = {255, 255, 255, 255}, hi[4] = {0, 0, 0, 0};
    for (int j = 0; j < n; j++) { float d[4]; for (int k = 0; k < nc; k++) { const int v = p[sel[j]][c.c0 + k]; d[k] = v - m[k]; lo[k] = std::min(lo[k], v); hi[k] = std::max(hi[k], v); }
        for (int a = 0; a < nc; a++) for (int b = 0; b < nc; b++) cov[a][b] += d[a] * d[b]; }
    float ax[4] = {0, 0, 0, 0}; for (int k = 0; k < nc; k++) ax[k] = (float)(hi[k] - lo[k]) + 0.001f * (k + 1);
    for (int it = 0; it < 6; it++) {
        float t[4] = {0, 0, 0, 0}; for (int a = 0; a < nc; a++) for (int b = 0; b < nc; b++) t[a] += cov[a][b] * ax[b];
        float l = 0; for (int a = 0; a < nc; a++) l += t[a] * t[a]; l = sqrtf(l); if (l < 1e-6f) break;
        for (int a = 0; a < nc; a++) ax[a] = t[a] / l;
    }
    { float l = 0; for (int a = 0; a < nc; a++) l += ax[a] * ax[a]; l = sqrtf(l); for (int a = 0; a < nc; a++) ax[a] = l > 1e-6f ? ax[a] / l : 0.0f; }
    float tmin = 1e9f, tmax = -1e9f;
    for (int j = 0; j < n; j++) { float s = 0; for (int k = 0; k < nc; k++) s += (p[sel[j]][c.c0 + k] - m[k]) * ax[k]; tmin = std::min(tmin, s); tmax = std::max(tmax, s); }
    float E[2][4]; for (int k = 0; k < nc; k++) { E[0][k] = m[k] + ax[k] * tmin; E[1][k] = m[k] + ax[k] * tmax; }
    const int hasp = c.pm ? 1 : 0, b = c.cb + hasp, ncomb = c.pm == 0 ? 1 : c.pm == 1 ? 2 : 4; const int *W = weights(c.ib);
    Fit best; best.err = ~0u;
    for (int round = 0; round < g_bc7_rounds; round++) {
        /* bits p: na ultima rodada testa todas as combinacoes; antes, o melhor de cada extremo isolado (quantizacao mais proxima) */
        int hp0 = 0, hp1 = 0;
        const bool full = round == g_bc7_rounds - 1;
        if (!full && c.pm) {
            int d[2][2] = {{0, 0}, {0, 0}};
            for (int e = 0; e < 2; e++) for (int pb = 0; pb < 2; pb++) for (int k = 0; k < nc; k++) { const float v = std::max(0.0f, std::min(255.0f, E[e][k])); d[e][pb] += abs(unq(quant1(v, c.cb, pb, 1), b) - (int)(v + 0.5f)); }
            if (c.pm == 1) hp0 = hp1 = d[0][1] + d[1][1] < d[0][0] + d[1][0];
            else { hp0 = d[0][1] < d[0][0]; hp1 = d[1][1] < d[1][0]; }
        }
        for (int pc = 0; pc < (full ? ncomb : 1); pc++) {
            const int p0 = !full ? hp0 : c.pm == 1 ? pc : pc & 1, p1 = !full ? hp1 : c.pm == 1 ? pc : pc >> 1;
            Fit f; int e0[4], e1[4];
            for (int k = 0; k < nc; k++) { f.x[0][k] = quant1(std::max(0.0f, std::min(255.0f, E[0][k])), c.cb, p0, hasp); f.x[1][k] = quant1(std::max(0.0f, std::min(255.0f, E[1][k])), c.cb, p1, hasp); e0[k] = unq(f.x[0][k], b); e1[k] = unq(f.x[1][k], b); }
            f.err = assign(p, sel, n, c, e0, e1, f.idx);
            if (f.err < best.err) best = f;
        }
        if (best.err == 0) break;
        float A = 0, B = 0, C = 0, r0[4] = {}, r1[4] = {};
        for (int j = 0; j < n; j++) { const float w = W[best.idx[j]] / 64.0f, u = 1.0f - w; A += u * u; B += u * w; C += w * w; for (int k = 0; k < nc; k++) { r0[k] += u * p[sel[j]][c.c0 + k]; r1[k] += w * p[sel[j]][c.c0 + k]; } }
        const float det = A * C - B * B; if (fabsf(det) < 1e-6f) break;
        for (int k = 0; k < nc; k++) { E[0][k] = (C * r0[k] - B * r1[k]) / det; E[1][k] = (A * r1[k] - B * r0[k]) / det; }
    }
    return best;
}
/* o indice do texel ancora tem o bit alto implicito 0: se precisar, troca os extremos do grupo (com seus bits p) e inverte os indices */
static void fix_anchor(Fit &f, int n, int apos, const Cfg &c) {
    const int ni = 1 << c.ib; if (!(f.idx[apos] & (ni >> 1))) return;
    for (int k = 0; k < 4; k++) std::swap(f.x[0][k], f.x[1][k]);
    for (int j = 0; j < n; j++) f.idx[j] = (uint8_t)(ni - 1 - f.idx[j]);
}
struct Bits { uint64_t lo = 0, hi = 0; int pos = 0;
    void put(uint32_t v, int n) { for (int i = 0; i < n; i++, pos++) { const uint64_t b = (v >> i) & 1; if (pos < 64) lo |= b << pos; else hi |= b << (pos - 64); } }
    void out(uint8_t *o) const { memcpy(o, &lo, 8); memcpy(o + 8, &hi, 8); } };

/* modo 6 */
static uint32_t mode6(const int (*p)[4], uint8_t *out) {
    static const uint8_t all[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    const Cfg c = {0, 4, 7, 2, 4}; Fit f = fit(p, all, 16, c); fix_anchor(f, 16, 0, c);
    Bits b; b.put(1u << 6, 7);
    for (int k = 0; k < 4; k++) { b.put((uint32_t)f.x[0][k] >> 1, 7); b.put((uint32_t)f.x[1][k] >> 1, 7); }
    b.put((uint32_t)f.x[0][0] & 1, 1); b.put((uint32_t)f.x[1][0] & 1, 1);
    for (int t = 0; t < 16; t++) b.put(f.idx[t], t ? 4 : 3);
    b.out(out); return f.err;
}
/* modo 5: RGB (7 bits, 2 bits de indice) e alfa (8 bits, 2 bits) separados, sem rotacao */
static uint32_t mode5(const int (*p)[4], uint8_t *out) {
    static const uint8_t all[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    const Cfg cc = {0, 3, 7, 0, 2}, ca = {3, 1, 8, 0, 2};
    Fit f = fit(p, all, 16, cc), a = fit(p, all, 16, ca); fix_anchor(f, 16, 0, cc); fix_anchor(a, 16, 0, ca);
    Bits b; b.put(1u << 5, 6); b.put(0, 2);
    for (int k = 0; k < 3; k++) { b.put((uint32_t)f.x[0][k], 7); b.put((uint32_t)f.x[1][k], 7); }
    b.put((uint32_t)a.x[0][0], 8); b.put((uint32_t)a.x[1][0], 8);
    for (int t = 0; t < 16; t++) b.put(f.idx[t], t ? 2 : 1);
    for (int t = 0; t < 16; t++) b.put(a.idx[t], t ? 2 : 1);
    b.out(out); return f.err + a.err;
}
/* modos de 2 grupos: 1 (RGB 6+p comum, 3 bits), 3 (RGB 7+p, 2 bits), 7 (RGBA 5+p, 2 bits) */
static uint32_t mode2s(const int (*p)[4], int mode, int part, uint8_t *out) {
    const Cfg c = mode == 1 ? Cfg{0, 3, 6, 1, 3} : mode == 3 ? Cfg{0, 3, 7, 2, 2} : Cfg{0, 4, 5, 2, 2};
    uint8_t sel[2][16]; int n[2] = {0, 0}, pos[16];
    for (int t = 0; t < 16; t++) { const int s = (PART2[part] >> t) & 1; pos[t] = n[s]; sel[s][n[s]++] = (uint8_t)t; }
    if (!n[0] || !n[1]) return ~0u;
    Fit f[2] = {fit(p, sel[0], n[0], c), fit(p, sel[1], n[1], c)};
    fix_anchor(f[0], n[0], pos[0], c); fix_anchor(f[1], n[1], pos[ANCHOR2[part]], c);
    Bits b; b.put(1u << mode, mode + 1); b.put((uint32_t)part, 6);
    const int hasp = 1;
    for (int k = 0; k < c.nch; k++) for (int s = 0; s < 2; s++) for (int e = 0; e < 2; e++) b.put((uint32_t)f[s].x[e][k] >> hasp, c.cb);
    if (c.pm == 1) { b.put((uint32_t)f[0].x[0][0] & 1, 1); b.put((uint32_t)f[1].x[0][0] & 1, 1); }
    else for (int s = 0; s < 2; s++) for (int e = 0; e < 2; e++) b.put((uint32_t)f[s].x[e][0] & 1, 1);
    for (int t = 0; t < 16; t++) { const int s = (PART2[part] >> t) & 1; const bool anchor = t == 0 || t == ANCHOR2[part]; b.put(f[s].idx[pos[t]], c.ib - (anchor ? 1 : 0)); }
    b.out(out); return f[0].err + f[1].err;
}
/* particoes candidatas: erro de reta (variancia fora do eixo principal) de cada grupo, as 2 melhores */
int g_bc7_np = 2, g_bc7_thr = 16 * 12;
enum { NP_MAX = 8 };
static void best_parts(const int (*p)[4], int nch, int *out, int np) {
    /* momentos de cada texel (soma e produtos dos canais): a soma de um grupo sai pelos bits da particao, a do outro por diferenca */
    int m1[16][4], m2[16][10], T1[4] = {0, 0, 0, 0}, T2[10] = {};
    for (int t = 0; t < 16; t++) { int q = 0; for (int a = 0; a < nch; a++) { m1[t][a] = p[t][a]; T1[a] += p[t][a]; for (int b = a; b < nch; b++, q++) { m2[t][q] = p[t][a] * p[t][b]; T2[q] += m2[t][q]; } } }
    const int nq = nch * (nch + 1) / 2;
    float best[NP_MAX]; for (int i = 0; i < np; i++) { best[i] = 1e30f; out[i] = 0; }
    for (int part = 0; part < 64; part++) {
        int S1[2][4] = {}, S2[2][10] = {}, n1 = 0;
        for (uint32_t bits = PART2[part]; bits; bits &= bits - 1) { const int t = __builtin_ctz(bits); n1++; for (int a = 0; a < nch; a++) S1[1][a] += m1[t][a]; for (int q = 0; q < nq; q++) S2[1][q] += m2[t][q]; }
        for (int a = 0; a < nch; a++) S1[0][a] = T1[a] - S1[1][a];
        for (int q = 0; q < nq; q++) S2[0][q] = T2[q] - S2[1][q];
        float tot = 0;
        for (int s = 0; s < 2; s++) {
            const int n = s ? n1 : 16 - n1; if (n <= 1) continue;
            float cov[4][4], tr = 0; int q = 0;
            for (int a = 0; a < nch; a++) for (int b = a; b < nch; b++, q++) { const float v = S2[s][q] - (float)S1[s][a] * S1[s][b] / n; cov[a][b] = cov[b][a] = v; if (a == b) tr += v; }
            float ax[4] = {1, 1, 1, 1}, lam = 0;
            for (int it = 0; it < 4; it++) { float t2[4] = {0, 0, 0, 0}; for (int a = 0; a < nch; a++) for (int b = 0; b < nch; b++) t2[a] += cov[a][b] * ax[b];
                float l = 0; for (int a = 0; a < nch; a++) l += t2[a] * t2[a]; l = sqrtf(l); if (l < 1e-6f) { lam = 0; break; } lam = l; for (int a = 0; a < nch; a++) ax[a] = t2[a] / l; }
            tot += tr - lam;
        }
        for (int i = 0; i < np; i++) if (tot < best[i]) { for (int j = np - 1; j > i; j--) { best[j] = best[j - 1]; out[j] = out[j - 1]; } best[i] = tot; out[i] = part; break; }
    }
}

static void encode_block(const int (*p)[4], uint8_t *out) {
    uint32_t err = mode6(p, out);
    if (err <= (uint32_t)g_bc7_thr) return;   /* ~3 por canal em media: bom o bastante */
    bool opaque = true; for (int t = 0; t < 16; t++) if (p[t][3] != 255) opaque = false;
    uint8_t tmp[16]; int parts[NP_MAX]; const int np = g_bc7_np;
    if (opaque) {
        best_parts(p, 3, parts, np);
        for (int i = 0; i < np && err > (uint32_t)g_bc7_thr; i++) for (int m : {1, 3}) { const uint32_t e = mode2s(p, m, parts[i], tmp); if (e < err) { err = e; memcpy(out, tmp, 16); } }
    } else {
        { const uint32_t e = mode5(p, tmp); if (e < err) { err = e; memcpy(out, tmp, 16); } }
        best_parts(p, 4, parts, np);
        for (int i = 0; i < np; i++) { const uint32_t e = mode2s(p, 7, parts[i], tmp); if (e < err) { err = e; memcpy(out, tmp, 16); } }
    }
}

void bc7_encode(const uint32_t *px, int w, int h, uint8_t *out) {
    for (int by = 0; by < h; by += 4) for (int bx = 0; bx < w; bx += 4) {
        int p[16][4];
        for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {   /* bordas fora da imagem repetem o ultimo texel */
            const uint32_t v = px[(size_t)std::min(by + y, h - 1) * w + std::min(bx + x, w - 1)];
            for (int c = 0; c < 4; c++) p[y * 4 + x][c] = (int)((v >> (8 * c)) & 255);
        }
        encode_block(p, out); out += 16;
    }
}
