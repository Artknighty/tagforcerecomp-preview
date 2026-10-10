/* VFPU do PSP: interpretador em tempo de execucao para as instrucoes vetoriais (as de memoria lv/sv sao geradas inline).
 * Escrito a partir da descricao do comportamento do hardware (prefixos S/T/D, tamanhos s/p/t/q, matrizes transpostas).
 * Desvios conhecidos: quirks de hardware para prefixos "invalidos" e ordenacao exata de NaN/denormais em vmin/vmax/vcmp. */
#include "psp_rt.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
[[noreturn]] void host_fatal(int code);   /* host.cpp: grava o fim do log e sai */

#define SPFX 0
#define TPFX 1
#define DPFX 2
#define CC   3

union FB { float f[4]; uint32_t u[4]; int32_t i[4]; };

static inline uint32_t bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static inline float fbits(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

/* ---------------------------------------------------------------- registradores
 * indice fisico = mtx*4 + col + row*32 (transposto: linha e coluna trocam de papel) */
static void vregs(int n, int reg, int out[4]) {
    int mtx = (reg >> 2) & 7, col = reg & 3, row = 0, transpose = (reg >> 5) & 1;
    switch (n) {
    case 1: transpose = 0; row = (reg >> 5) & 3; break;
    case 2: row = (reg >> 5) & 2; break;
    case 3: row = (reg >> 6) & 1; break;
    default: row = (reg >> 5) & 2; break;
    }
    for (int i = 0; i < n; i++) {
        int idx = mtx * 4;
        if (transpose) idx += ((row + i) & 3) + col * 32; else idx += col + ((row + i) & 3) * 32;
        out[i] = idx;
    }
}
static void mregs(int side, int reg, int out[16]) {
    int mtx = (reg >> 2) & 7, col = reg & 3, row = 0, transpose = (reg >> 5) & 1;
    switch (side) {
    case 2: row = (reg >> 5) & 2; break;
    case 3: row = (reg >> 6) & 1; break;
    default: row = (reg >> 5) & 2; break;
    }
    for (int i = 0; i < side; i++) for (int j = 0; j < side; j++) {
        int idx = mtx * 4;
        if (transpose) idx += ((row + i) & 3) + ((col + j) & 3) * 32; else idx += ((col + j) & 3) + ((row + i) & 3) * 32;
        out[j * 4 + i] = idx;
    }
}
static void rdv(CPU *c, float *d, int n, int reg) { int r[4]; vregs(n, reg, r); for (int i = 0; i < n; i++) d[i] = c->v[r[i]].f; }
static void wrv(CPU *c, const float *d, int n, int reg) {
    int r[4]; vregs(n, reg, r); uint32_t mask = c->vctrl[DPFX];
    for (int i = 0; i < n; i++) if (!((mask >> (8 + i)) & 1)) c->v[r[i]].f = d[i];
}
static void rdm(CPU *c, float *m, int side, int reg) { int r[16]; mregs(side, reg, r); for (int i = 0; i < side; i++) for (int j = 0; j < side; j++) m[j * 4 + i] = c->v[r[j * 4 + i]].f; }
static void wrm(CPU *c, const float *m, int side, int reg) {
    int r[16]; mregs(side, reg, r); uint32_t mask = c->vctrl[DPFX];
    for (int i = 0; i < side; i++) for (int j = 0; j < side; j++)
        if (j != side - 1 || !((mask >> (8 + i)) & 1)) c->v[r[j * 4 + i]].f = m[j * 4 + i];
}

/* ---------------------------------------------------------------- prefixos */
static void pfx_st(float *r, uint32_t data, int n, float invalid) {
    if (data == 0xe4) return;
    static const float K[8] = {0.f, 1.f, 2.f, 0.5f, 3.f, 1.f / 3.f, 0.25f, 1.f / 6.f};
    float o[4] = {invalid, invalid, invalid, invalid};
    for (int i = 0; i < n; i++) o[i] = r[i];
    for (int i = 0; i < n; i++) {
        int sw = (data >> (i * 2)) & 3, ab = (data >> (8 + i)) & 1, ng = (data >> (16 + i)) & 1, cs = (data >> (12 + i)) & 1;
        if (!cs) { r[i] = o[sw]; if (ab) r[i] = fbits(bits(r[i]) & 0x7fffffffu); }
        else r[i] = K[sw + (ab << 2)];
        if (ng) r[i] = fbits(bits(r[i]) ^ 0x80000000u);
    }
}
static inline float clampf(float v, float lo, float hi) { return v <= lo ? lo : (v >= hi ? hi : v); }
static void pfx_d(CPU *c, float *v, int n, bool only_mask = false) {
    uint32_t d = c->vctrl[DPFX];
    if (!d || only_mask) return;
    for (int i = 0; i < n; i++) {
        int sat = (d >> (i * 2)) & 3;
        if (sat == 1) v[i] = clampf(v[i], 0.f, 1.f); else if (sat == 3) v[i] = clampf(v[i], -1.f, 1.f);
    }
}
static inline void S_(CPU *c, float *v, int n, float inv = 0.f) { pfx_st(v, c->vctrl[SPFX], n, inv); }
static inline void T_(CPU *c, float *v, int n, float inv = 0.f) { pfx_st(v, c->vctrl[TPFX], n, inv); }
static inline void eat(CPU *c) { c->vctrl[SPFX] = 0xe4; c->vctrl[TPFX] = 0xe4; c->vctrl[DPFX] = 0; }
/* ops "de ultimo elemento" (vdiv, vrcp...): a mascara/saturacao do D vale so para a ultima lane */
static void last_lane_d(CPU *c, int n) {
    uint32_t d = c->vctrl[DPFX];
    c->vctrl[DPFX] = ((d & (1 << 8)) << (n - 1)) | ((d & 3) << (n + n - 2));
}

/* ---------------------------------------------------------------- funcoes matematicas do VFPU */
static float v_sin(float x) {
    if (x != x || isinf(x)) return fbits(0x7f800001u);
    double a = fmod((double)x, 4.0); if (a < 0) a += 4;
    if (a == 0 || a == 2) return 0.f; if (a == 1) return 1.f; if (a == 3) return -1.f;
    return (float)sin(a * 1.5707963267948966);
}
static float v_cos(float x) {
    if (x != x || isinf(x)) return fbits(0x7f800001u);
    double a = fmod(fabs((double)x), 4.0);
    if (a == 0) return 1.f; if (a == 1 || a == 3) return 0.f; if (a == 2) return -1.f;
    return (float)cos(a * 1.5707963267948966);
}
static float v_asin(float x) { return fabsf(x) > 1.f ? fbits(0x7f800001u) : (float)(asin((double)x) * 0.6366197723675814); }
static float half2f(uint16_t h) {
    uint32_t s = (h & 0x8000u) << 16, e = (h >> 10) & 0x1f, m = h & 0x3ff;
    if (e == 0) { if (!m) return fbits(s); float f = ldexpf((float)m, -24); return s ? -f : f; }
    if (e == 31) return fbits(s | 0x7f800000u | (m << 13));
    return fbits(s | ((e + 112) << 23) | (m << 13));
}
static uint16_t f2half(float f) {
    uint32_t u = bits(f), s = (u >> 16) & 0x8000u; int e = (int)((u >> 23) & 0xff) - 127 + 15; uint32_t m = u & 0x7fffff;
    if (((u >> 23) & 0xff) == 255) return (uint16_t)(s | 0x7c00 | (m ? 0x200 : 0));
    if (e >= 31) return (uint16_t)(s | 0x7c00);
    if (e <= 0) { if (e < -10) return (uint16_t)s; m |= 0x800000; return (uint16_t)(s | (m >> (14 - e))); }
    return (uint16_t)(s | (e << 10) | (m >> 13));
}
static uint32_t rnd_next(CPU *c) { uint32_t &x = c->vctrl[8]; if (!x) x = 0x12345678u; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return x; }

static const float CST[32] = {0.f, 3.402823466e+38f, 1.41421356237f, 0.70710678118f, 1.12837916709f, 0.63661977236f, 0.31830988618f, 0.78539816339f,
    1.57079632679f, 3.14159265358f, 2.71828182845f, 1.44269504088f, 0.43429448190f, 0.69314718055f, 2.30258509299f, 6.28318530717f,
    0.52359877559f, 0.30102999566f, 3.32192809488f, 0.86602540378f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static int32_t f2i_mode(float s, float mult, int mode) {   /* 16:n 17:z 18:u 19:d */
    if (s != s) return 0x7fffffff;
    double sv = (double)s * mult;
    if (sv > 2147483647.0) return 0x7fffffff;
    if (sv <= -2147483648.0) return (int32_t)0x80000000;
    switch (mode) {
    case 16: return (int32_t)nearbyint(sv);
    case 17: return (int32_t)trunc(sv);
    case 18: return (int32_t)ceil(sv);
    default: return (int32_t)floor(sv);
    }
}

static void fatal(CPU *c, uint32_t w, const char *what) {
    (void)c; fprintf(stderr, "\nVFPU: instrucao nao suportada (%s): %08x\n", what, w); host_fatal(7);
}

/* ---------------------------------------------------------------- despacho */
extern "C" void vfpu_exec(CPU *c, uint32_t w) {
    int op = w >> 26, vd = w & 0x7f, vs = (w >> 8) & 0x7f, vt = (w >> 16) & 0x7f;
    int n = 1 + ((w >> 7) & 1) + ((w >> 14) & 2);
    float s[4] = {0, 0, 0, 0}, t[4] = {0, 0, 0, 0}, d[4] = {0, 0, 0, 0};

    switch (op) {
    case 0x12: {   /* COP2: mfv/mtv/mfvc/mtvc */
        int rs = (w >> 21) & 31, rt = (w >> 16) & 31, imm = w & 0xff;
        if (rs == 3) {
            if (rt) { if (imm < 128) c->r[rt] = c->v[imm].u; else if (imm < 128 + 16) c->r[rt] = c->vctrl[imm - 128]; }
        } else if (rs == 7) {
            if (imm < 128) c->v[imm].u = c->r[rt];
            else if (imm < 128 + 16) {
                static const uint32_t mask[16] = {0xfffff, 0xfffff, 0xfff, 0x3f, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu};
                int k = imm - 128; c->vctrl[k] = (c->r[rt] & mask[k]) | (c->vctrl[k] & ~mask[k]);
            }
        } else fatal(c, w, "cop2");
        return;
    }
    case 0x18: case 0x19: {   /* vadd vsub vdiv | vmul vdot vscl vhdp vcrs vdet */
        int sub = (w >> 23) & 7;
        if (op == 0x18 && (sub == 0 || sub == 1 || sub == 7)) {
            rdv(c, s, n, vs); rdv(c, t, n, vt);
            if (sub == 7) { S_(c, &s[n - 1], 1, -INFINITY); T_(c, &t[n - 1], 1, -INFINITY); }
            else { S_(c, s, n); T_(c, t, n); }
            for (int i = 0; i < n; i++) d[i] = sub == 0 ? s[i] + t[i] : sub == 1 ? s[i] - t[i] : s[i] / t[i];
            if (sub == 7) last_lane_d(c, n);
            pfx_d(c, d, n); wrv(c, d, n, vd); eat(c); return;
        }
        if (op == 0x19 && sub == 0) {   /* vmul */
            rdv(c, s, n, vs); rdv(c, t, n, vt); S_(c, s, n); T_(c, t, n);
            for (int i = 0; i < n; i++) d[i] = s[i] * t[i];
            pfx_d(c, d, n); wrv(c, d, n, vd); eat(c); return;
        }
        if (op == 0x19 && (sub == 1 || sub == 4)) {   /* vdot / vhdp */
            rdv(c, s, n, vs); rdv(c, t, n, vt);
            if (sub == 4) { s[n - 1] = 1.f; }
            S_(c, s, 4); T_(c, t, 4);
            float sum = 0; for (int i = 0; i < 4; i++) sum += s[i] * t[i];
            d[0] = sum; pfx_d(c, d, 1); wrv(c, d, 1, vd); eat(c); return;
        }
        if (op == 0x19 && sub == 2) {   /* vscl: vt e um escalar (o swizzle de T e forcado para replicar a lane) */
            rdv(c, s, n, vs); S_(c, s, n);
            float sc = c->v[vt].f, tt[4] = {sc, sc, sc, sc}; T_(c, tt, 4);
            for (int i = 0; i < n; i++) d[i] = s[i] * tt[i];
            pfx_d(c, d, n); wrv(c, d, n, vd); eat(c); return;
        }
        if (op == 0x19 && sub == 5) {   /* vcrs (meio produto vetorial) */
            rdv(c, s, n, vs); rdv(c, t, n, vt);
            float a[4] = {s[1], s[2], s[0], 0}, b[4] = {t[2], t[0], t[1], 0};
            for (int i = 0; i < 3; i++) d[i] = a[i] * b[i];
            pfx_d(c, d, n); wrv(c, d, n, vd); eat(c); return;
        }
        if (op == 0x19 && sub == 6) {   /* vdet */
            rdv(c, s, n, vs); S_(c, s, 4); rdv(c, t, n, vt);
            float tt[4] = {t[1], t[0], t[2], t[3]};
            d[0] = s[0] * tt[0] - s[1] * tt[1]; d[0] += s[2] * tt[2] + s[3] * tt[3];
            pfx_d(c, d, 1); wrv(c, d, 1, vd); eat(c); return;
        }
        break;
    }
    case 0x1b: {   /* vcmp vmin vmax vscmp vsge vslt */
        int sub = (w >> 23) & 7;
        rdv(c, s, n, vs); S_(c, s, n); rdv(c, t, n, vt); T_(c, t, n);
        if (sub == 0) {
            int cond = w & 15, cc = 0, orv = 0, andv = 1;
            for (int i = 0; i < n; i++) {
                int r = 0;
                switch (cond) {
                case 0: r = 0; break; case 1: r = s[i] == t[i]; break; case 2: r = s[i] < t[i]; break; case 3: r = s[i] <= t[i]; break;
                case 4: r = 1; break; case 5: r = s[i] != t[i]; break; case 6: r = s[i] >= t[i]; break; case 7: r = s[i] > t[i]; break;
                case 8: r = s[i] == 0.f; break; case 9: r = s[i] != s[i]; break; case 10: r = isinf(s[i]) != 0; break;
                case 11: r = (s[i] != s[i]) || isinf(s[i]); break; case 12: r = s[i] != 0.f; break; case 13: r = !(s[i] != s[i]); break;
                case 14: r = !isinf(s[i]); break; default: r = !((s[i] != s[i]) || isinf(s[i])); break;
                }
                cc |= r << i; orv |= r; andv &= r;
            }
            int aff = (1 << 4) | (1 << 5); for (int i = 0; i < n; i++) aff |= 1 << i;
            c->vctrl[CC] = (c->vctrl[CC] & ~aff) | ((cc | (orv << 4) | (andv << 5)) & aff);
            eat(c); return;
        }
        if (sub == 2 || sub == 3) { for (int i = 0; i < n; i++) d[i] = sub == 2 ? (t[i] < s[i] ? t[i] : s[i]) : (t[i] > s[i] ? t[i] : s[i]); }
        else if (sub == 5) { for (int i = 0; i < n; i++) { float a = s[i] - t[i]; d[i] = (float)((0.f < a) - (a < 0.f)); } }
        else if (sub == 6) { for (int i = 0; i < n; i++) d[i] = (s[i] != s[i] || t[i] != t[i]) ? 0.f : (s[i] >= t[i] ? 1.f : 0.f); }
        else if (sub == 7) { for (int i = 0; i < n; i++) d[i] = (s[i] != s[i] || t[i] != t[i]) ? 0.f : (s[i] < t[i] ? 1.f : 0.f); }
        else break;
        pfx_d(c, d, n); wrv(c, d, n, vd); eat(c); return;
    }
    case 0x34: {   /* VFPU4 */
        int A = (w >> 21) & 31, B = (w >> 16) & 31;
        if (A == 0) {   /* operacoes unarias */
            if (B == 3) {   /* vidt */
                int mask = (n == 4 || n == 3) ? 3 : 1, off = vd & mask;
                for (int i = 0; i < n; i++) d[i] = (off == (i & mask)) ? 1.f : 0.f;
                S_(c, d, n); pfx_d(c, d, n); wrv(c, d, n, vd); eat(c); return;
            }
            if (B == 6 || B == 7) {   /* vzero / vone */
                for (int i = 0; i < n; i++) d[i] = B == 7 ? 1.f : 0.f;
                pfx_d(c, d, n); wrv(c, d, n, vd); eat(c); return;
            }
            rdv(c, s, n, vs);
            if (B >= 16 && B <= 23) S_(c, &s[n - 1], 1, INFINITY);
            else if (B == 24 || B == 26) S_(c, &s[n - 1], 1, -INFINITY);
            else if (B == 28) S_(c, &s[n - 1], 1, INFINITY);
            else if (B == 1) { uint32_t sp = c->vctrl[SPFX]; c->vctrl[SPFX] = sp | 0xf00; S_(c, s, n); }        /* vabs: forca abs */
            else if (B == 2) { uint32_t sp = c->vctrl[SPFX]; c->vctrl[SPFX] = sp | 0xf0000; S_(c, s, n); }      /* vneg: forca negate */
            else S_(c, s, n);
            for (int i = 0; i < n; i++) {
                float x = s[i];
                switch (B) {
                case 0: case 1: case 2: d[i] = x; break;
                case 4: d[i] = x <= 0 ? 0.f : (x > 1.f ? 1.f : x); break;
                case 5: d[i] = x < -1.f ? -1.f : (x > 1.f ? 1.f : x); break;
                case 16: d[i] = 1.f / x; break;
                case 17: d[i] = 1.f / sqrtf(x); break;
                case 18: d[i] = v_sin(x); break;
                case 19: d[i] = v_cos(x); break;
                case 20: d[i] = exp2f(x); break;
                case 21: d[i] = log2f(x); break;
                case 22: d[i] = sqrtf(x); break;
                case 23: d[i] = v_asin(x); break;
                case 24: d[i] = -1.f / x; break;
                case 26: d[i] = -v_sin(x); break;
                case 28: d[i] = 1.f / exp2f(x); break;
                default: fatal(c, w, "vfpu4 unario");
                }
            }
            if (B >= 16) last_lane_d(c, n);
            if (B == 5) pfx_d(c, d, n, true); else pfx_d(c, d, n);
            wrv(c, d, n, vd); eat(c); return;
        }
        if (A == 1) {   /* VFPU7 (tabela por B) */
            if (B >= 1 && B <= 3) {   /* vrndi / vrndf1 / vrndf2 */
                FB o; for (int i = n - 1; i >= 0; i--) { uint32_t r = rnd_next(c); o.u[i] = B == 1 ? r : B == 2 ? (0x3f800000u | (r & 0x7fffff)) : (0x40000000u | (r & 0x7fffff)); }
                last_lane_d(c, n); pfx_d(c, o.f, n); wrv(c, o.f, n, vd); eat(c); return;
            }
            if (B == 0) { c->vctrl[8] = c->v[vd].u; eat(c); return; }   /* vrnds */
            if (B == 18) {   /* vf2h */
                rdv(c, s, n, vs); S_(c, s, 4); FB o; int on;
                if (n <= 2) { o.u[0] = f2half(s[0]) | ((uint32_t)f2half(s[1]) << 16); on = 1; }
                else { o.u[0] = f2half(s[0]) | ((uint32_t)f2half(s[1]) << 16); o.u[1] = f2half(s[2]) | ((uint32_t)f2half(s[3]) << 16); on = 2; }
                pfx_d(c, o.f, on); wrv(c, o.f, on, vd); eat(c); return;
            }
            if (B == 19) {   /* vh2f */
                FB in; rdv(c, in.f, n, vs); S_(c, in.f, n); FB o; int on;
                o.f[0] = half2f(in.u[0] & 0xffff); o.f[1] = half2f(in.u[0] >> 16); on = 2;
                if (n > 1) { o.f[2] = half2f(in.u[1] & 0xffff); o.f[3] = half2f(in.u[1] >> 16); on = 4; }
                pfx_d(c, o.f, on); wrv(c, o.f, on, vd); eat(c); return;
            }
            if (B >= 24 && B <= 27) {   /* vuc2i vc2i vus2i vs2i */
                FB in; rdv(c, in.f, n, vs); S_(c, in.f, n); FB o; memset(&o, 0, sizeof o); int on = 4;
                uint32_t v0 = in.u[0];
                switch (B & 3) {
                case 0: for (int i = 0; i < 4; i++) { o.u[i] = (uint32_t)(((v0 >> (8 * i)) & 0xff) * 0x01010101u) >> 1; } break;
                case 1: o.u[0] = (v0 & 0xff) << 24; o.u[1] = (v0 & 0xff00) << 16; o.u[2] = (v0 & 0xff0000) << 8; o.u[3] = v0 & 0xff000000u; break;
                default: {
                    int m = n >= 2 ? 2 : 1; on = m == 2 ? 4 : 2;
                    for (int i = 0; i < m; i++) {
                        uint32_t v = in.u[i];
                        if ((B & 3) == 2) { o.u[i * 2] = (v & 0xffff) << 15; o.u[i * 2 + 1] = (v & 0xffff0000u) >> 1; }
                        else { o.u[i * 2] = (v & 0xffff) << 16; o.u[i * 2 + 1] = v & 0xffff0000u; }
                    }
                } }
                pfx_d(c, o.f, on); wrv(c, o.f, on, vd); eat(c); return;
            }
            if (B >= 28 && B <= 31) {   /* vi2uc vi2c vi2us vi2s */
                FB in; rdv(c, in.f, n, vs); memset(in.i + n, 0, (4 - n) * 4); S_(c, in.f, 4);
                FB o; memset(&o, 0, sizeof o); int on = 1;
                switch (B & 3) {
                case 0: for (int i = 0; i < 4; i++) { int v = in.i[i]; if (v < 0) v = 0; o.u[0] |= ((uint32_t)(v >> 23) & 0xff) << (i * 8); } break;
                case 1: for (int i = 0; i < 4; i++) o.u[0] |= (in.u[i] >> 24) << (i * 8); break;
                default: {
                    int elems = (n + 1) / 2; on = n >= 3 ? 2 : 1;
                    for (int i = 0; i < elems; i++) {
                        if ((B & 3) == 2) { int lo = in.i[i * 2], hi = in.i[i * 2 + 1]; if (lo < 0) lo = 0; if (hi < 0) hi = 0; o.u[i] = (uint32_t)(lo >> 15) | ((uint32_t)(hi >> 15) << 16); }
                        else o.u[i] = (in.u[i * 2] >> 16) | ((in.u[i * 2 + 1] >> 16) << 16);
                    }
                } }
                pfx_d(c, o.f, on); wrv(c, o.f, on, vd); eat(c); return;
            }
            fatal(c, w, "vfpu7");
        }
        if (A == 2) {   /* VFPU9 */
            rdv(c, s, n, vs);
            switch (B) {
            case 4: {   /* vocp */
                S_(c, s, n); for (int i = 0; i < n; i++) d[i] = 1.f - s[i]; pfx_d(c, d, n); wrv(c, d, n, vd); eat(c); return; }
            case 5: {   /* vsocp: saida com o dobro do tamanho */
                int on = n == 1 ? 2 : 4; float a = clampf(s[0], 0.f, 1.f); d[0] = a; d[1] = 1.f - a;
                if (on == 4) { float b = clampf(s[1], 0.f, 1.f); d[2] = b; d[3] = 1.f - b; }
                wrv(c, d, on, vd); eat(c); return; }
            case 6: case 7: {   /* vfad / vavg */
                S_(c, s, 4); float sum = 0; for (int i = 0; i < 4; i++) sum += s[i];
                d[0] = B == 6 ? sum : sum / (float)n; pfx_d(c, d, 1); wrv(c, d, 1, vd); eat(c); return; }
            case 10: {   /* vsgn */
                S_(c, s, n); for (int i = 0; i < n; i++) d[i] = (s[i] > 0.f) ? 1.f : (s[i] < 0.f ? -1.f : 0.f); pfx_d(c, d, n); wrv(c, d, n, vd); eat(c); return; }
            case 2: {   /* vbfy1 */
                S_(c, s, n); d[0] = s[0] + s[1]; d[1] = s[0] - s[1]; d[2] = s[2] + s[3]; d[3] = s[2] - s[3]; pfx_d(c, d, n); wrv(c, d, n, vd); eat(c); return; }
            case 3: {   /* vbfy2 */
                S_(c, s, n); d[0] = s[0] + s[2]; d[1] = s[1] + s[3]; d[2] = s[0] - s[2]; d[3] = s[1] - s[3]; pfx_d(c, d, n); wrv(c, d, n, vd); eat(c); return; }
            default: fatal(c, w, "vfpu9");
            }
        }
        if (A == 3) {   /* vcst */
            for (int i = 0; i < n; i++) d[i] = CST[B]; pfx_d(c, d, n); wrv(c, d, n, vd); eat(c); return;
        }
        if (A >= 16 && A <= 19) {   /* vf2in/z/u/d */
            float mult = (float)(1u << B); rdv(c, s, n, vs); S_(c, s, n); FB o;
            for (int i = 0; i < n; i++) o.i[i] = f2i_mode(s[i], mult, A);
            pfx_d(c, o.f, n, true); wrv(c, o.f, n, vd); eat(c); return;
        }
        if (A == 20) {   /* vi2f */
            float mult = 1.f / (float)(1u << B); FB in; rdv(c, in.f, n, vs); S_(c, in.f, n);
            for (int i = 0; i < n; i++) d[i] = (float)in.i[i] * mult; pfx_d(c, d, n); wrv(c, d, n, vd); eat(c); return;
        }
        if (A == 21) {   /* vcmov */
            int tf = (w >> 19) & 1, imm3 = (w >> 16) & 7;
            rdv(c, s, n, vs); S_(c, s, n); rdv(c, d, n, vd); T_(c, d, n);
            uint32_t cc = c->vctrl[CC];
            if (imm3 < 6) { if (((cc >> imm3) & 1) == (uint32_t)!tf) for (int i = 0; i < n; i++) d[i] = s[i]; }
            else if (imm3 == 6) { for (int i = 0; i < n; i++) if (((cc >> i) & 1) == (uint32_t)!tf) d[i] = s[i]; }
            pfx_d(c, d, n); wrv(c, d, n, vd); eat(c); return;
        }
        break;
    }
    case 0x37: {   /* prefixos e imediatos */
        int k = (w >> 23) & 7;
        if (k <= 1) { c->vctrl[SPFX] = w & 0xfffff; return; }
        if (k <= 3) { c->vctrl[TPFX] = w & 0xfffff; return; }
        if (k <= 5) { c->vctrl[DPFX] = w & 0xfff; return; }
        d[0] = (k == 6) ? (float)(int16_t)(w & 0xffff) : half2f((uint16_t)(w & 0xffff));
        pfx_d(c, d, 1); wrv(c, d, 1, vt); eat(c); return;
    }
    case 0x3c: {   /* VFPU6: matrizes */
        int idx = (w >> 21) & 31;
        if (idx < 4) {   /* vmmul */
            float ms[16] = {0}, mt[16] = {0}, md[16]; rdm(c, ms, n, vs); rdm(c, mt, n, vt);
            for (int a = 0; a < n; a++) for (int b = 0; b < n; b++) {
                float sum = 0; if (a == n - 1 && b == n - 1) { S_(c, &ms[b * 4], 4); T_(c, &mt[a * 4], 4); for (int k = 0; k < 4; k++) sum += ms[b * 4 + k] * mt[a * 4 + k]; }
                else for (int k = 0; k < n; k++) sum += ms[b * 4 + k] * mt[a * 4 + k];
                md[a * 4 + b] = sum;
            }
            uint32_t dp = c->vctrl[DPFX]; c->vctrl[DPFX] = ((dp & (1 << 8)) << (n - 1)) | ((dp & 3) << (n + n - 2));
            pfx_d(c, &md[4 * (n - 1)], 4); wrm(c, md, n, vd); eat(c); return;
        }
        if (idx >= 4 && idx < 16) {   /* vtfm / vhtfm */
            int ins = (w >> 23) & 3, side = ins + 1, nv = n, tn = nv < side ? nv : side;
            float ms[16] = {0}, tt[4] = {0, 0, 0, 0}; rdm(c, ms, side, vs); rdv(c, tt, side, vt);
            FB o; memset(&o, 0, sizeof o);
            for (int i = 0; i < ins; i++) {
                o.f[i] = ms[i * 4] * tt[0];
                for (int k = 1; k < tn; k++) o.f[i] += ms[i * 4 + k] * tt[k];
                if (ins >= nv) o.f[i] += ms[i * 4 + ins];
            }
            /* ultima linha: S/T valem so nela; T fornece as constantes 0/1 do caso homogeneo */
            S_(c, &ms[ins * 4], 4);
            float t4[4] = {tt[0], tt[1], tt[2], tt[3]};
            for (int k = nv; k < 4; k++) t4[k] = 0.f;
            if (ins >= nv) t4[ins] = 1.f;
            T_(c, t4, 4);
            o.f[ins] = ms[ins * 4] * t4[0]; for (int k = 1; k < 4; k++) o.f[ins] += ms[ins * 4 + k] * t4[k];
            uint32_t dp = c->vctrl[DPFX]; c->vctrl[DPFX] = ((dp & (1 << 8)) << ins) | ((dp & 3) << (ins + ins));
            pfx_d(c, o.f, side); wrv(c, o.f, side, vd); eat(c); return;
        }
        if (idx >= 16 && idx < 20) {   /* vmscl */
            float ms[16] = {0}, md[16] = {0}, sc[4] = {0}; rdm(c, ms, n, vs); rdv(c, sc, 1, vt);
            for (int a = 0; a < n - 1; a++) for (int b = 0; b < n; b++) md[a * 4 + b] = ms[a * 4 + b] * sc[0];
            S_(c, &ms[(n - 1) * 4], 4);
            float tt[4] = {sc[0], sc[0], sc[0], sc[0]}; T_(c, tt, 4);
            for (int b = 0; b < n; b++) md[(n - 1) * 4 + b] = ms[(n - 1) * 4 + b] * tt[b];
            pfx_d(c, &md[(n - 1) * 4], 4); wrm(c, md, n, vd); eat(c); return;
        }
        if (idx >= 20 && idx < 24) {   /* vcrsp.t / vqmul.q */
            rdv(c, s, n, vs); rdv(c, t, n, vt);
            if (n == 3) { d[0] = s[1] * t[2] - s[2] * t[1]; d[1] = s[2] * t[0] - s[0] * t[2]; d[2] = s[0] * t[1] - s[1] * t[0]; }
            else if (n == 4) {
                d[0] = s[0] * t[3] + s[1] * t[2] - s[2] * t[1] + s[3] * t[0];
                d[1] = -s[0] * t[2] + s[1] * t[3] + s[2] * t[0] + s[3] * t[1];
                d[2] = s[0] * t[1] - s[1] * t[0] + s[2] * t[3] + s[3] * t[2];
                d[3] = -s[0] * t[0] - s[1] * t[1] - s[2] * t[2] + s[3] * t[3];
            } else fatal(c, w, "vcrsp tamanho");
            pfx_d(c, d, n); wrv(c, d, n, vd); eat(c); return;
        }
        if (idx == 28) {   /* vmmov / vmidt / vmzero / vmone */
            int k = (w >> 16) & 15; float m[16] = {0};
            if (k == 0) { rdm(c, m, n, vs); S_(c, &m[(n - 1) * 4], 4); pfx_d(c, &m[(n - 1) * 4], 4); wrm(c, m, n, vd); eat(c); return; }
            for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) m[j * 4 + i] = k == 3 ? (i == j ? 1.f : 0.f) : k == 7 ? 1.f : 0.f;
            if (k != 3 && k != 6 && k != 7) fatal(c, w, "vmatrix init");
            wrm(c, m, n, vd); eat(c); return;
        }
        if (idx == 29) {   /* vrot */
            int imm = (w >> 16) & 31; bool neg = (imm & 16) != 0; int sl = (imm >> 2) & 3, cl = imm & 3;
            float ang = c->v[vs].f; float sn, cs;
            if (c->vctrl[SPFX] == 0xe4) { sn = v_sin(ang); cs = v_cos(ang); }
            else { float sv[1] = {ang}; S_(c, sv, 1); sn = v_sin(sv[0]); cs = v_cos(ang); }
            if (neg) sn = -sn;
            if (sl == cl) { for (int i = 0; i < 4; i++) d[i] = sn; } else d[sl] = sn;
            d[cl] = cs;
            uint32_t rm = (3u << (cl * 2)) | (1u << (8 + cl)); c->vctrl[DPFX] &= 0xfffffu ^ rm;
            pfx_d(c, d, n); wrv(c, d, n, vd); eat(c); return;
        }
        break;
    }
    case 0x3f: {   /* vnop / vsync / vflush */
        if ((w & 0xffff0000u) != 0xffff0000u) eat(c);
        return;
    }
    }
    fatal(c, w, "desconhecida");
}
