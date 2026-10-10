/* Compilacao dos modulos traduzidos em tempo de execucao, em memoria, com o TinyCC (libtcc.dll, LGPL): o codigo C do jogo (gen/<modulo>.c, gerado na
 * maquina do usuario a partir da copia dele) NAO precisa ser compilado antes nem vir pronto numa distribuicao: o runtime o compila ao iniciar
 * (~3 s para os 29 modulos). A tabela de modulos (GenModule) e montada a partir dos simbolos de cada modulo (NOME_fns, NOME_gp...) e da lista
 * gen/modulos.tsv (nome <tab> caminho no disco). Sem TF_JIT ou sem libtcc.dll o runtime usa os modulos ligados estaticamente (build normal). */
#include "hle.h"
#include "util.h"
#include "gameid.h"
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <vector>
#include <string>
#include <deque>

struct TCCState;
static struct {
    HMODULE h = nullptr;
    TCCState *(*nw)(void); void (*del)(TCCState *); void (*libpath)(TCCState *, const char *); void (*errf)(TCCState *, void *, void (*)(void *, const char *));
    void (*opts)(TCCState *, const char *); int (*inc)(TCCState *, const char *); void (*def)(TCCState *, const char *, const char *); int (*otype)(TCCState *, int);
    int (*cstr)(TCCState *, const char *); int (*addsym)(TCCState *, const char *, const void *); int (*reloc)(TCCState *, void *); void *(*getsym)(TCCState *, const char *);
} T;
static std::vector<GenModule> g_jit; static std::deque<std::string> g_names; static bool g_jit_on;
extern "C" uint32_t g_trace[1024]; extern "C" uint32_t g_trace_i, g_watch, g_watch_val;
extern "C" void psp_watch_hit(uint32_t, uint32_t, uint32_t);
extern "C" void tf_ftrace(uint32_t, CPU *); extern "C" void tf_wlog(uint32_t, uint32_t, int);

/* o TinyCC nao tem estas funcoes embutidas do GCC / de math.h: o codigo gerado as chama por nome */
extern "C" {
static unsigned jit_clz(unsigned v) { return v ? (unsigned)__builtin_clz(v) : 32u; }
static uint32_t jit_bswap32(uint32_t v) { return __builtin_bswap32(v); }
static int jit_isunordered(double a, double b) { return a != a || b != b; }
static double jit_nearbyint(double x) { if (!(__builtin_fabs(x) < 4503599627370496.0)) return x; double r = (double)(long long)x, d = x - r; if (d > 0.5 || (d == 0.5 && ((long long)r & 1))) r += 1.0; else if (d < -0.5 || (d == -0.5 && ((long long)r & 1))) r -= 1.0; return r; }
static float jit_sqrtf(float x) { return __builtin_sqrtf(x); }   /* sqrtss */
static double jit_floor(double x) { if (!(__builtin_fabs(x) < 4503599627370496.0)) return x; double r = (double)(long long)x; return r > x ? r - 1.0 : r; }
static double jit_ceil(double x) { if (!(__builtin_fabs(x) < 4503599627370496.0)) return x; double r = (double)(long long)x; return r < x ? r + 1.0 : r; }
static double jit_trunc(double x) { if (!(__builtin_fabs(x) < 4503599627370496.0)) return x; return (double)(long long)x; }
}
static bool load_tcc(const std::wstring &dir) {
    T.h = LoadLibraryW((dir + L"libtcc.dll").c_str()); if (!T.h) return false;
#define L_(f, n) *(void **)&T.f = (void *)GetProcAddress(T.h, n); if (!T.f) return false;
    L_(nw, "tcc_new") L_(del, "tcc_delete") L_(libpath, "tcc_set_lib_path") L_(errf, "tcc_set_error_func") L_(opts, "tcc_set_options") L_(inc, "tcc_add_include_path")
    L_(def, "tcc_define_symbol") L_(otype, "tcc_set_output_type") L_(cstr, "tcc_compile_string") L_(addsym, "tcc_add_symbol") L_(reloc, "tcc_relocate") L_(getsym, "tcc_get_symbol")
#undef L_
    return true;
}
static void tcc_error(void *, const char *m) { fprintf(stderr, "[jit] %s\n", m); }

static void jit_selftest(void);
bool jit_enabled(void) { return g_jit_on; }
bool jit_generic(void) { return g_gen_nmodules == 0; }
#include "hooks.h"
void hook_skip_next(uint32_t addr); extern uint32_t g_hook_cur;
static std::vector<std::pair<uint32_t, void (*)(CPU *)>> g_gccfn;
static bool gccfn_hook(CPU *c) {   /* depuracao: roda a versao compilada pelo gcc (estatica) desta funcao */
    for (auto &p : g_gccfn) if (p.first == g_hook_cur) { hook_skip_next(g_hook_cur); p.second(c); return true; }
    return false;
}
static void gccfn_install(void) {
    const char *fn = getenv("TF_JIT_GCCFN"); if (!fn) return;
    FILE *f = fopen(fn, "r"); if (!f) return; char ln[64]; int n = 0;
    while (fgets(ln, sizeof ln, f)) {
        uint32_t a = (uint32_t)strtoul(ln, nullptr, 16); if (!a) continue;
        for (unsigned k = 0; k < g_gen_nmodules; k++) for (unsigned q = 0; q < g_gen_modules[k].nfns; q++)
            if (g_gen_modules[k].fns[q].addr == a) { g_gccfn.push_back({a, g_gen_modules[k].fns[q].fn}); hook_install(a, gccfn_hook); n++; }
    }
    fclose(f); fprintf(stderr, "[jit] %d funcoes redirecionadas para a versao do gcc\n", n);
}
bool jit_init(void) {
    const char *e = getenv("TF_JIT"); if (!jit_generic() && (!e || !*e || *e == '0')) return false;   /* the release build always compiles the game code at start */
    std::wstring tccdir = exe_dir() + L"tcc\\", gen = jit_generic() ? code_dir(game_id_built()) : exe_dir();   /* libtcc.dll, include/ e lib/ do TinyCC em tcc/; modulos .c em gen/ (aqui: ao lado do .exe) */
    if (const wchar_t *d = _wgetenv(L"TF_JIT_TCC")) tccdir = d;
    if (const wchar_t *g = _wgetenv(L"TF_JIT_GEN")) gen = g;
    if (!tccdir.empty() && tccdir.back() != L'\\') tccdir += L'\\';
    if (!gen.empty() && gen.back() != L'\\') gen += L'\\';
    if (!load_tcc(tccdir)) { fprintf(stderr, "[jit] libtcc.dll nao encontrada em %ls\n", tccdir.c_str()); return false; }
    std::string incdir = to_utf8(exe_dir()) + "include";
    if (const char *i = getenv("TF_JIT_INC")) incdir = i;
    FILE *mf = _wfopen((gen + L"modulos.tsv").c_str(), L"rb"); if (!mf) { fprintf(stderr, "[jit] sem modulos.tsv\n"); return false; }
    std::string man; { char b[4096]; size_t n; while ((n = fread(b, 1, sizeof b, mf)) > 0) man.append(b, n); } fclose(mf);
    DWORD t0 = GetTickCount(); size_t pos = 0; int ok = 0;
    while (pos < man.size()) {
        size_t en = man.find('\n', pos); if (en == std::string::npos) en = man.size();
        std::string line = man.substr(pos, en - pos); pos = en + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t tab = line.find('\t'); if (tab == std::string::npos || line[0] == '#') continue;
        std::string c = line.substr(0, tab), iso = line.substr(tab + 1);
        { static const char *skip = getenv("TF_JIT_SKIP");   /* depuracao: modulos que ficam com a versao estatica (lista separada por virgula) */
          if (skip && (std::string(",") + skip + ",").find("," + c + ",") != std::string::npos) { for (unsigned k = 0; k < g_gen_nmodules; k++) if (c == g_gen_modules[k].name) g_jit.push_back(g_gen_modules[k]); ok++; continue; } }
        FILE *f = _wfopen((gen + to_wide(c) + L".c").c_str(), L"rb"); if (!f) { fprintf(stderr, "[jit] sem %s.c\n", c.c_str()); return false; }
        std::string src; { std::vector<char> b(1 << 20); size_t n; while ((n = fread(b.data(), 1, b.size(), f)) > 0) src.append(b.data(), n); } fclose(f);
        TCCState *s = T.nw();
        T.errf(s, nullptr, tcc_error); T.libpath(s, to_utf8(tccdir).c_str()); T.opts(s, "-w -nostdinc");   /* -nostdinc: so os cabecalhos abaixo */
        T.inc(s, incdir.c_str()); T.inc(s, (to_utf8(tccdir) + "include").c_str()); T.otype(s, 1);   /* 1 = TCC_OUTPUT_MEMORY */
        if (!getenv("TF_JIT_NOTRACE")) T.def(s, "PSP_TRACE", "1");   /* TF_JIT_NOTRACE=1: sem o rastreio de funcoes (TRC) */
        T.addsym(s, "psp_call", (const void *)psp_call); T.addsym(s, "psp_hook", (const void *)psp_hook); T.addsym(s, "psp_watch_hit", (const void *)psp_watch_hit);
        T.addsym(s, "vfpu_exec", (const void *)vfpu_exec); T.addsym(s, "psp_break", (const void *)psp_break); T.addsym(s, "psp_syscall", (const void *)psp_syscall);
        T.addsym(s, "__builtin_clz", (const void *)jit_clz); T.addsym(s, "__builtin_bswap32", (const void *)jit_bswap32); T.addsym(s, "isunordered", (const void *)jit_isunordered);
        T.addsym(s, "tf_nearbyint", (const void *)jit_nearbyint); T.addsym(s, "tf_sqrtf", (const void *)jit_sqrtf); T.addsym(s, "tf_floor", (const void *)jit_floor); T.addsym(s, "tf_trunc", (const void *)jit_trunc); T.addsym(s, "tf_ceil", (const void *)jit_ceil);
        T.addsym(s, "psp_unimpl", (const void *)psp_unimpl); T.def(s, "TF_FTRACE", "1"); T.addsym(s, "tf_ftrace", (const void *)tf_ftrace); T.addsym(s, "tf_wlog", (const void *)tf_wlog); T.addsym(s, "g_trace", g_trace); T.addsym(s, "g_trace_i", &g_trace_i);
        T.addsym(s, "g_watch", &g_watch); T.addsym(s, "g_watch_val", &g_watch_val); T.addsym(s, "g_hookbm", &g_hookbm); T.addsym(s, "g_hook_any", &g_hook_any);
        if (T.cstr(s, src.c_str()) < 0) { fprintf(stderr, "[jit] erro compilando %s\n", c.c_str()); return false; }
        if (T.reloc(s, (void *)1) < 0) { fprintf(stderr, "[jit] erro ligando %s\n", c.c_str()); return false; }   /* TCC_RELOCATE_AUTO */
        auto sym = [&](const char *suf) { return T.getsym(s, (c + "_" + suf).c_str()); };
        g_names.push_back(c); g_names.push_back(iso);
        GenModule m = {};
        m.name = g_names[g_names.size() - 2].c_str(); m.iso_path = g_names.back().c_str();
        m.base = *(uint32_t *)sym("base"); m.gp = *(uint32_t *)sym("gp"); m.memsz = *(uint32_t *)sym("memsz"); m.textsz = *(uint32_t *)sym("textsz");
        m.datasz = *(uint32_t *)sym("datasz"); m.bsssz = *(uint32_t *)sym("bsssz"); m.entry = *(uint32_t *)sym("entry");
        m.fns = (const FnEntry *)sym("fns"); m.nfns = *(unsigned *)sym("nfns"); m.imps = (const ImportEntry *)sym("imports"); m.nimps = *(unsigned *)sym("nimports");
        m.exps = (const ExportEntry *)sym("exports"); m.nexps = *(unsigned *)sym("nexports");
        g_jit.push_back(m); ok++;   /* o TCCState fica vivo: o codigo gerado mora na memoria dele */
    }
    fprintf(stderr, "[jit] %d modulos compilados em memoria com o TinyCC em %.1f s\n", ok, (GetTickCount() - t0) / 1000.0);
    if (getenv("TF_JIT_SELFTEST")) jit_selftest();
    g_jit_on = ok > 0; if (g_jit_on) gccfn_install(); return g_jit_on;
}
/* TF_JIT_SELFTEST=1: compila um trecho com o TinyCC (mesmos simbolos do JIT) e imprime conversoes float -> int */
static void jit_selftest(void) {
    static const char *src = "#include \"psp_rt.h\"\nint selftest(float *v, int n, int *out) { for (int i = 0; i < n; i++) for (int m = 0; m < 4; m++) out[i * 4 + m] = f2i_(v[i], m); return 0; }\n";
    TCCState *s = T.nw(); T.errf(s, nullptr, tcc_error); T.opts(s, "-w -nostdinc"); T.otype(s, 1);
    std::string incdir = getenv("TF_JIT_INC") ? getenv("TF_JIT_INC") : ""; T.inc(s, incdir.c_str());
    std::wstring tccdir = _wgetenv(L"TF_JIT_TCC") ? _wgetenv(L"TF_JIT_TCC") : L""; T.libpath(s, to_utf8(tccdir).c_str()); T.inc(s, (to_utf8(tccdir) + "\\include").c_str());
    T.addsym(s, "__builtin_clz", (const void *)jit_clz); T.addsym(s, "__builtin_bswap32", (const void *)jit_bswap32); T.addsym(s, "isunordered", (const void *)jit_isunordered);
    T.addsym(s, "tf_nearbyint", (const void *)jit_nearbyint); T.addsym(s, "tf_sqrtf", (const void *)jit_sqrtf); T.addsym(s, "tf_floor", (const void *)jit_floor); T.addsym(s, "tf_ceil", (const void *)jit_ceil); T.addsym(s, "tf_trunc", (const void *)jit_trunc);
    if (T.cstr(s, src) < 0 || T.reloc(s, (void *)1) < 0) { fprintf(stderr, "[jit] selftest nao compilou\n"); return; }
    int (*fn)(float *, int, int *) = (int (*)(float *, int, int *))T.getsym(s, "selftest");
    float v[] = {0.5f, 1.5f, 210.0f, -3.2f, 12.75f, 100000.f}; int out[24] = {};
    fn(v, 6, out);
    int bad = 0; for (int rep = 0; rep < 200000; rep++) { float w[2] = {210.0f, 355.0f + (rep & 7)}; int o[8]; fn(w, 2, o); if (o[1] != 210 || o[5] != 355 + (rep & 7)) { if (bad++ < 3) fprintf(stderr, "[jit] selftest REPETICAO %d: trunc(210)=%d trunc(%g)=%d\n", rep, o[1], w[1], o[5]); } }
    fprintf(stderr, "[jit] selftest: %d falhas em 200000 repeticoes\n", bad);
    for (int i = 0; i < 6; i++) fprintf(stderr, "[jit] selftest %g: nearest=%d trunc=%d ceil=%d floor=%d\n", v[i], out[i * 4], out[i * 4 + 1], out[i * 4 + 2], out[i * 4 + 3]);
}
const GenModule *gen_modules(unsigned *n) {
    if (g_jit_on) { *n = (unsigned)g_jit.size(); return g_jit.data(); }
    *n = g_gen_nmodules; return g_gen_modules;
}
