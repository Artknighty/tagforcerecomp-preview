/* The translated game modules are compiled at run time, in memory, with TinyCC (libtcc.dll, LGPL): the game's C code (gen/<module>.c, written on
 * the player's machine from their own copy) is never compiled ahead of time nor shipped. The first start compiles the ~30 modules (~2.3 s) and
 * keeps the objects in gen/jitcache/; later starts only link them. The module table (GenModule) is built from each module's symbols
 * (NAME_fns, NAME_gp...) and from gen/modulos.tsv (name <tab> path on the disc). */
#include "hle.h"
#include "util.h"
#include "gameid.h"
#include "xxh.h"
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
    int (*addfile)(TCCState *, const char *); int (*outfile)(TCCState *, const char *);
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
    L_(addfile, "tcc_add_file") L_(outfile, "tcc_output_file")
#undef L_
    return true;
}
static void tcc_error(void *, const char *m) { fprintf(stderr, "[jit] %s\n", m); }
static std::string tcc_path(const std::wstring &w) {   /* TCC usa caminhos narrow via CRT/Win32, que neste build seguem a pagina ANSI, nao UTF-8. */
    int n = WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s(n, '\0');
    WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    s.pop_back(); return s;
}

static void jit_selftest(void);
static std::string read_all(const std::wstring &path) {   /* "" when missing or empty */
    std::string s; FILE *f = _wfopen(path.c_str(), L"rb"); if (!f) return s;
    std::vector<char> b(1 << 20); size_t n; while ((n = fread(b.data(), 1, b.size(), f)) > 0) s.append(b.data(), n);
    fclose(f); return s;
}
static void add_symbols(TCCState *s) {   /* runtime functions and variables the generated code calls by name */
    T.addsym(s, "psp_call", (const void *)psp_call); T.addsym(s, "psp_hook", (const void *)psp_hook); T.addsym(s, "psp_watch_hit", (const void *)psp_watch_hit);
    T.addsym(s, "vfpu_exec", (const void *)vfpu_exec); T.addsym(s, "psp_break", (const void *)psp_break); T.addsym(s, "psp_syscall", (const void *)psp_syscall);
    T.addsym(s, "__builtin_clz", (const void *)jit_clz); T.addsym(s, "__builtin_bswap32", (const void *)jit_bswap32); T.addsym(s, "isunordered", (const void *)jit_isunordered);
    T.addsym(s, "tf_nearbyint", (const void *)jit_nearbyint); T.addsym(s, "tf_sqrtf", (const void *)jit_sqrtf); T.addsym(s, "tf_floor", (const void *)jit_floor); T.addsym(s, "tf_trunc", (const void *)jit_trunc); T.addsym(s, "tf_ceil", (const void *)jit_ceil);
    T.addsym(s, "psp_unimpl", (const void *)psp_unimpl);
    T.addsym(s, "tf_ftrace", (const void *)tf_ftrace); T.addsym(s, "tf_wlog", (const void *)tf_wlog); T.addsym(s, "g_trace", g_trace); T.addsym(s, "g_trace_i", &g_trace_i);
    T.addsym(s, "g_watch", &g_watch); T.addsym(s, "g_watch_val", &g_watch_val); T.addsym(s, "g_hookbm", &g_hookbm); T.addsym(s, "g_hook_any", &g_hook_any);
}
static void drop_stale(const std::wstring &dir, const std::string &mod, const std::wstring &keep) {   /* older objects of this module: <mod>_<16 hex>.o */
    const std::wstring pre = to_wide(mod) + L"_"; WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + pre + L"*.o").c_str(), &fd); if (h == INVALID_HANDLE_VALUE) return;
    do {
        const std::wstring n = fd.cFileName;
        if (n.size() != pre.size() + 18 || dir + n == keep) continue;   /* libtutorial_*.o also matches libtutorial_09f7e200_<key>.o: length tells them apart */
        if (n.find_first_not_of(L"0123456789abcdef", pre.size()) == pre.size() + 16) DeleteFileW((dir + n).c_str());
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
bool jit_init(void) {
    std::wstring tccdir = exe_dir() + L"tcc\\", gen = code_dir(game_id_built());   /* TinyCC's libtcc.dll, include/ and lib/ in tcc/; the translated modules in gen/ */
    if (const wchar_t *d = _wgetenv(L"TF_JIT_TCC")) tccdir = d;
    if (const wchar_t *g = _wgetenv(L"TF_JIT_GEN")) gen = g;
    if (!tccdir.empty() && tccdir.back() != L'\\') tccdir += L'\\';
    if (!gen.empty() && gen.back() != L'\\') gen += L'\\';
    if (!load_tcc(tccdir)) { fprintf(stderr, "[jit] libtcc.dll nao encontrada em %ls\n", tccdir.c_str()); return false; }
    std::string incdir = tcc_path(exe_dir()) + "include";
    if (const char *i = getenv("TF_JIT_INC")) incdir = i;
    FILE *mf = _wfopen((gen + L"modulos.tsv").c_str(), L"rb"); if (!mf) { fprintf(stderr, "[jit] sem modulos.tsv\n"); return false; }
    std::string man; { char b[4096]; size_t n; while ((n = fread(b, 1, sizeof b, mf)) > 0) man.append(b, n); } fclose(mf);
    /* Compiled-code cache (2026-10-20): compiling the ~35 MB of translated C took ~2.3 s at every start. Each module is now compiled once to an
     * object file, gen/jitcache/<module>_<key>.o, and the following starts only load and link it. The key hashes the module source, the runtime
     * header the code includes (include/psp_rt.h), libtcc.dll's size and the compile flags: a new translation, a new runtime or other flags
     * compile again. Any cache failure (path TinyCC cannot open, disk full) falls back to compiling in memory. TF_JIT_NOCACHE=1 skips the cache. */
    const bool trace = !getenv("TF_JIT_NOTRACE"), ftrace = getenv("TF_JIT_FTRACE") != nullptr;   /* no TRC function trace / TF_FTRACE bisection hooks */
    const std::wstring cache = gen + L"jitcache\\";
    const bool use_cache = !getenv("TF_JIT_NOCACHE") && tcc_path(cache).find('?') == std::string::npos;   /* '?': the path has characters TinyCC cannot open */
    uint64_t seed = (trace ? 1 : 0) | (ftrace ? 2 : 0);
    { const char *i = getenv("TF_JIT_INC"); const std::string h = read_all((i ? to_wide(i) : exe_dir() + L"include") + L"\\psp_rt.h"); seed = xxh64(h.data(), h.size(), seed);
      WIN32_FILE_ATTRIBUTE_DATA a; if (GetFileAttributesExW((tccdir + L"libtcc.dll").c_str(), GetFileExInfoStandard, &a)) seed ^= ((uint64_t)a.nFileSizeHigh << 32 | a.nFileSizeLow) * 0x9E3779B97F4A7C15ull; }
    if (use_cache) CreateDirectoryW(cache.c_str(), nullptr);
    DWORD t0 = GetTickCount(); size_t pos = 0; int ok = 0, hits = 0;
    while (pos < man.size()) {
        size_t en = man.find('\n', pos); if (en == std::string::npos) en = man.size();
        std::string line = man.substr(pos, en - pos); pos = en + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t tab = line.find('\t'); if (tab == std::string::npos || line[0] == '#') continue;
        std::string c = line.substr(0, tab), iso = line.substr(tab + 1);
        const std::string src = read_all(gen + to_wide(c) + L".c"); if (src.empty()) { fprintf(stderr, "[jit] sem %s.c\n", c.c_str()); return false; }
        auto setup = [&](int otype) {   /* -nostdinc: only the headers below */
            TCCState *s = T.nw();
            T.errf(s, nullptr, tcc_error); T.libpath(s, tcc_path(tccdir).c_str()); T.opts(s, "-w -nostdinc");
            T.inc(s, incdir.c_str()); T.inc(s, (tcc_path(tccdir) + "include").c_str()); T.otype(s, otype);
            if (trace) T.def(s, "PSP_TRACE", "1");
            if (ftrace) T.def(s, "TF_FTRACE", "1");   /* bisection tool only (TF_FTRACE_FILE): it makes every function entry and every guest store a call */
            return s;
        };
        TCCState *s = nullptr;
        if (use_cache) {
            char key[17]; snprintf(key, sizeof key, "%016llx", (unsigned long long)xxh64(src.data(), src.size(), seed));
            const std::wstring obj = cache + to_wide(c) + L"_" + to_wide(key) + L".o";
            if (GetFileAttributesW(obj.c_str()) == INVALID_FILE_ATTRIBUTES) {   /* miss: compile to a temporary object, then move it in place (parallel replays) */
                const std::wstring tmp = obj + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
                TCCState *o = setup(4);   /* 4 = TCC_OUTPUT_OBJ */
                const bool built = T.cstr(o, src.c_str()) >= 0 && T.outfile(o, tcc_path(tmp).c_str()) >= 0;
                T.del(o);
                if (!built) { DeleteFileW(tmp.c_str()); fprintf(stderr, "[jit] %s: cache nao gravado, compilando em memoria\n", c.c_str()); }
                else if (MoveFileExW(tmp.c_str(), obj.c_str(), MOVEFILE_REPLACE_EXISTING)) drop_stale(cache, c, obj);
                else DeleteFileW(tmp.c_str());
            } else hits++;
            if (GetFileAttributesW(obj.c_str()) != INVALID_FILE_ATTRIBUTES) {
                s = setup(1); add_symbols(s);
                if (T.addfile(s, tcc_path(obj).c_str()) < 0 || T.reloc(s, (void *)1) < 0) { fprintf(stderr, "[jit] %s: cache invalido, compilando em memoria\n", c.c_str()); T.del(s); DeleteFileW(obj.c_str()); s = nullptr; }
            }
        }
        if (!s) {
            s = setup(1); add_symbols(s);   /* 1 = TCC_OUTPUT_MEMORY */
            if (T.cstr(s, src.c_str()) < 0) { fprintf(stderr, "[jit] erro compilando %s\n", c.c_str()); return false; }
            if (T.reloc(s, (void *)1) < 0) { fprintf(stderr, "[jit] erro ligando %s\n", c.c_str()); return false; }   /* TCC_RELOCATE_AUTO */
        }
        auto sym = [&](const char *suf) { return T.getsym(s, (c + "_" + suf).c_str()); };
        for (const char *need : {"base", "gp", "memsz", "textsz", "datasz", "bsssz", "entry", "fns", "nfns", "imports", "nimports", "exports", "nexports"})
            if (!sym(need)) { fprintf(stderr, "[jit] %s: o codigo gerado nao tem o simbolo %s_%s\n", c.c_str(), c.c_str(), need); return false; }   /* sem isto o ponteiro nulo era lido logo abaixo */
        g_names.push_back(c); g_names.push_back(iso);
        GenModule m = {};
        m.name = g_names[g_names.size() - 2].c_str(); m.iso_path = g_names.back().c_str();
        m.base = *(uint32_t *)sym("base"); m.gp = *(uint32_t *)sym("gp"); m.memsz = *(uint32_t *)sym("memsz"); m.textsz = *(uint32_t *)sym("textsz");
        m.datasz = *(uint32_t *)sym("datasz"); m.bsssz = *(uint32_t *)sym("bsssz"); m.entry = *(uint32_t *)sym("entry");
        m.fns = (const FnEntry *)sym("fns"); m.nfns = *(unsigned *)sym("nfns"); m.imps = (const ImportEntry *)sym("imports"); m.nimps = *(unsigned *)sym("nimports");
        m.exps = (const ExportEntry *)sym("exports"); m.nexps = *(unsigned *)sym("nexports");
        g_jit.push_back(m); ok++;   /* o TCCState fica vivo: o codigo gerado mora na memoria dele */
    }
    fprintf(stderr, "[jit] %d modulos compilados em memoria com o TinyCC em %.1f s (%d do cache)\n", ok, (GetTickCount() - t0) / 1000.0, hits);
    if (getenv("TF_JIT_SELFTEST")) jit_selftest();
    g_jit_on = ok > 0; return g_jit_on;
}
/* TF_JIT_SELFTEST=1: compila um trecho com o TinyCC (mesmos simbolos do JIT) e imprime conversoes float -> int */
static void jit_selftest(void) {
    static const char *src = "#include \"psp_rt.h\"\nint selftest(float *v, int n, int *out) { for (int i = 0; i < n; i++) for (int m = 0; m < 4; m++) out[i * 4 + m] = f2i_(v[i], m); return 0; }\n";
    TCCState *s = T.nw(); T.errf(s, nullptr, tcc_error); T.opts(s, "-w -nostdinc"); T.otype(s, 1);
    std::string incdir = getenv("TF_JIT_INC") ? getenv("TF_JIT_INC") : tcc_path(exe_dir()) + "include"; T.inc(s, incdir.c_str());
    std::wstring tccdir = _wgetenv(L"TF_JIT_TCC") ? _wgetenv(L"TF_JIT_TCC") : exe_dir() + L"tcc\\"; T.libpath(s, tcc_path(tccdir).c_str()); T.inc(s, (tcc_path(tccdir) + "include").c_str());
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
const GenModule *gen_modules(unsigned *n) { *n = g_jit_on ? (unsigned)g_jit.size() : 0; return g_jit.data(); }
