/* Runtime minimo para o codigo C gerado pelo recompilador (tools/recomp.py). */
#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>
#ifdef __TINYC__
/* TinyCC (jit.cpp): nomes proprios para as funcoes de math.h que o codigo gerado usa. O TinyCC as ligaria a msvcrt.dll (versoes x87 que enchem a
 * pilha x87 e passam a devolver NaN depois de poucas chamadas) e o math.h dele nem declara trunc/nearbyint; o jit.cpp fornece versoes SSE. */
double tf_trunc(double); double tf_nearbyint(double); double tf_ceil(double); double tf_floor(double); float tf_sqrtf(float);
#define trunc tf_trunc
#define nearbyint tf_nearbyint
#define ceil tf_ceil
#define floor tf_floor
#define sqrtf tf_sqrtf
#endif

typedef union { float f; uint32_t u; int32_t i; } FReg;

typedef struct CPU {
    uint32_t r[32];
    uint32_t hi, lo;
    uint32_t fcr31;
    uint32_t cc;          /* bit de condicao do FPU (fcr31 bit 23) */
    FReg     f[32];
    FReg     v[128];      /* registradores do VFPU (indice fisico mtx*4 + col + row*32) */
    uint32_t vctrl[16];   /* 0 SPFX, 1 TPFX, 2 DPFX, 3 CC, 8.. RCX */
    uint8_t *mem;         /* base da memoria virtual (1 GiB de espaco) */
} CPU;

#ifdef __cplusplus
extern "C" {
#endif
/* despacho por endereco (jalr, jr nao-ra, jal para outro modulo, stubs HLE) */
void psp_call(CPU *c, uint32_t addr);
void vfpu_exec(CPU *c, uint32_t word);
void psp_syscall(CPU *c, uint32_t pc, uint32_t code);
void psp_break(CPU *c, uint32_t pc);
void psp_unimpl(CPU *c, uint32_t pc, uint32_t word);
#ifdef __cplusplus
}
#endif

/* TinyCC (jit.cpp) liga o codigo em memoria longe do .exe: dados do runtime precisam ser acessados por indirecao (dllimport) */
#ifdef __TINYC__
#define TF_DATA __declspec(dllimport)
#else
#define TF_DATA
#endif
#ifdef PSP_TRACE
#ifdef __cplusplus
extern "C" {
#endif
extern TF_DATA uint32_t g_trace[1024]; extern TF_DATA uint32_t g_trace_i;
#ifdef __cplusplus
}
#endif
#ifdef __cplusplus
extern "C" {
#endif
extern TF_DATA uint32_t g_watch, g_watch_val;
void psp_watch_hit(uint32_t fn, uint32_t before, uint32_t after);
#ifdef __cplusplus
}
#endif
#ifdef TF_FTRACE
void tf_ftrace(uint32_t a, CPU *c);   /* depuracao: grava cada entrada de funcao com um resumo dos registradores (jit.cpp / host.cpp) */
#define TRC(a) (tf_ftrace(a, c), g_trace[g_trace_i++ & 1023] = (a), (g_watch && *(uint32_t *)(c->mem + ((g_watch) & 0x3FFFFFFFu)) != g_watch_val) ? psp_watch_hit((a), g_watch_val, *(uint32_t *)(c->mem + ((g_watch) & 0x3FFFFFFFu))) : (void)0)
#else
#define TRC(a) (g_trace[g_trace_i++ & 1023] = (a), (g_watch && *(uint32_t *)(c->mem + ((g_watch) & 0x3FFFFFFFu)) != g_watch_val) ? psp_watch_hit((a), g_watch_val, *(uint32_t *)(c->mem + ((g_watch) & 0x3FFFFFFFu))) : (void)0)
#endif
#else
#define TRC(a) ((void)0)
#endif
/* Ganchos de mods de codigo (hooks.h): a entrada de cada funcao recompilada consulta um mapa de bits (1 MB, 1 bit por palavra de
 * 0x08000000..0x09ffffff); funcao com gancho chama psp_hook, que pode executar o gancho e dispensar o corpo original. */
#ifdef __cplusplus
extern "C" {
#endif
extern TF_DATA uint8_t *g_hookbm; extern TF_DATA int g_hook_any;
int psp_hook(CPU *c, uint32_t addr);
#ifdef __cplusplus
}
#endif
#define HOOK(a) do { if (g_hook_any) { uint32_t i_ = ((a) & 0x01fffffcu) >> 2; if (((g_hookbm[i_ >> 3] >> (i_ & 7)) & 1) && psp_hook(c, (a))) return; } } while (0)
#define MA(a)   ((uint32_t)(a) & 0x3FFFFFFFu)
#define P8(a)   (c->mem + MA(a))
#define LB(a)   ((uint32_t)(int32_t)(int8_t)*P8(a))
#define LBU(a)  ((uint32_t)*P8(a))
#define LH(a)   ((uint32_t)(int32_t)*(int16_t *)P8(a))
#define LHU(a)  ((uint32_t)*(uint16_t *)P8(a))
#define LW(a)   (*(uint32_t *)P8(a))
#ifdef TF_FTRACE
void tf_wlog(uint32_t a, uint32_t v, int sz);   /* depuracao: grava as escritas na memoria (host.cpp) */
#define SB(a,v) (tf_wlog(MA(a), (uint32_t)(v), 1), *P8(a) = (uint8_t)(v))
#define SH(a,v) (tf_wlog(MA(a), (uint32_t)(v), 2), *(uint16_t *)P8(a) = (uint16_t)(v))
#define SW(a,v) (tf_wlog(MA(a), (uint32_t)(v), 4), *(uint32_t *)P8(a) = (uint32_t)(v))
#else
#define SB(a,v) (*P8(a) = (uint8_t)(v))
#define SH(a,v) (*(uint16_t *)P8(a) = (uint16_t)(v))
#define SW(a,v) (*(uint32_t *)P8(a) = (uint32_t)(v))
#endif

static inline uint32_t lwl_(CPU *c, uint32_t a, uint32_t rt) {
    uint32_t s = a & 3, m = LW(a & ~3u);
    return (rt & (0x00ffffffu >> (s * 8))) | (m << (24 - s * 8));
}
static inline uint32_t lwr_(CPU *c, uint32_t a, uint32_t rt) {
    uint32_t s = a & 3, m = LW(a & ~3u);
    return (rt & (0xffffff00u << (24 - s * 8))) | (m >> (s * 8));
}
static inline void swl_(CPU *c, uint32_t a, uint32_t rt) {
    uint32_t s = a & 3, m = LW(a & ~3u);
    SW(a & ~3u, (m & (0xffffff00u << (s * 8))) | (rt >> (24 - s * 8)));
}
static inline void swr_(CPU *c, uint32_t a, uint32_t rt) {
    uint32_t s = a & 3, m = LW(a & ~3u);
    SW(a & ~3u, (m & (0x00ffffffu >> (24 - s * 8))) | (rt << (s * 8)));
}

static inline void div_(CPU *c, int32_t a, int32_t b) {
    if (b == 0) { c->lo = a < 0 ? 1u : 0xffffffffu; c->hi = (uint32_t)a; }
    else if (a == INT32_MIN && b == -1) { c->lo = 0x80000000u; c->hi = 0; }
    else { c->lo = (uint32_t)(a / b); c->hi = (uint32_t)(a % b); }
}
static inline void divu_(CPU *c, uint32_t a, uint32_t b) {
    if (b == 0) { c->lo = 0xffffffffu; c->hi = a; }
    else { c->lo = a / b; c->hi = a % b; }
}
static inline uint32_t clz_(uint32_t x) { return x ? (uint32_t)__builtin_clz(x) : 32; }
static inline uint32_t bitrev_(uint32_t x) {
    x = ((x >> 1) & 0x55555555u) | ((x & 0x55555555u) << 1);
    x = ((x >> 2) & 0x33333333u) | ((x & 0x33333333u) << 2);
    x = ((x >> 4) & 0x0f0f0f0fu) | ((x & 0x0f0f0f0fu) << 4);
    return __builtin_bswap32(x);
}
/* conversoes float->int32 com saturacao (o PSP satura, nao da wrap) */
static inline int32_t f2i_(float v, int mode) {
    if (v != v) return 0x7fffffff;
    double d = v;
    switch (mode) { case 0: d = nearbyint(d); break; case 1: d = trunc(d); break;
                    case 2: d = ceil(d); break; default: d = floor(d); }
    if (d >= 2147483648.0) return 0x7fffffff;
    if (d <= -2147483648.0) return (int32_t)0x80000000;
    return (int32_t)d;
}

static inline void cpu_reset_vfpu(CPU *c) { c->vctrl[0] = 0xe4; c->vctrl[1] = 0xe4; c->vctrl[2] = 0; }
