/* Host: memoria virtual, despacho de chamadas por endereco, modulos carregados e ligacao dos imports. */
#include "hle.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <map>
#include <string>
#include <vector>
#include <algorithm>

uint8_t *g_mem;
extern "C" { uint32_t g_trace[1024]; uint32_t g_trace_i; uint32_t g_watch, g_watch_val; }
static bool g_wlog_on; static FILE *g_wlog_f;
extern "C" void tf_wlog(uint32_t a, uint32_t v, int sz) { if (g_wlog_on && g_wlog_f) { uint32_t r[3] = {a, v, (uint32_t)sz}; fwrite(r, 4, 3, g_wlog_f); } }
extern "C" void tf_ftrace(uint32_t a, CPU *c) {   /* TF_FTRACE_FILE=arq: entradas de funcao + resumo dos registradores (so em builds com -DTF_FTRACE) */
    static FILE *f = getenv("TF_FTRACE_FILE") ? fopen(getenv("TF_FTRACE_FILE"), "wb") : nullptr; static uint64_t n; static const uint64_t lim = getenv("TF_FTRACE_MAX") ? strtoull(getenv("TF_FTRACE_MAX"), nullptr, 10) : 4000000;
    if (!f || n >= lim) return;
    uint32_t rec[4] = {a, 0, 0, c->r[29]};
    for (int i = 1; i < 32; i++) rec[1] = rec[1] * 16777619u ^ c->r[i];
    for (int i = 0; i < 32; i++) rec[2] = rec[2] * 16777619u ^ c->f[i].u;
    static const uint64_t from = getenv("TF_FTRACE_FROM") ? strtoull(getenv("TF_FTRACE_FROM"), nullptr, 10) : ~0ull;
    if (!g_wlog_f && getenv("TF_FTRACE_WLOG")) g_wlog_f = fopen(getenv("TF_FTRACE_WLOG"), "wb");
    static const uint64_t wspan = getenv("TF_FTRACE_WSPAN") ? strtoull(getenv("TF_FTRACE_WSPAN"), nullptr, 10) : 40; g_wlog_on = n >= from && n < from + wspan; if (g_wlog_on && g_wlog_f) { uint32_t mark[3] = {0xfffffff0u, a, (uint32_t)n}; fwrite(mark, 4, 3, g_wlog_f); }
    fwrite(rec, 4, 4, f); if (n >= from && n < from + 40) { fwrite(c->r, 4, 32, f); uint32_t fr[8]; for (int i = 0; i < 8; i++) fr[i] = c->f[i].u; fwrite(fr, 4, 8, f); fflush(f); }
    if (((++n) & 0x3fff) == 0) fflush(f);
}
extern "C" void psp_watch_hit(uint32_t fn, uint32_t before, uint32_t after) {
    fprintf(stderr, "[watch] %08x: %08x -> %08x (detectado ao entrar em %08x; funcoes anteriores:", g_watch, before, after, fn);
    for (int i = 2; i < 9; i++) fprintf(stderr, " %08x", g_trace[(g_trace_i - i) & 1023]);
    fputc(10, stderr); g_watch_val = after;
}
void diag_flush(void);
void audio_out_shutdown(void);
/* A mensagem do erro sai em stderr, que vai para o log da sessao por um pipe e uma thread (diag.cpp); exit()/o fim do processo matavam essa
 * thread antes de ela gravar as ultimas linhas, e justamente a mensagem do erro (ex.: "[HLE] nao implementado") podia faltar em gen/logs.
 * ExitProcess, not exit(): exit() runs the static destructors, and a std::thread that is still joinable there (HD movie decoder, Vulkan pipeline
 * warm-up, launcher worker) calls std::terminate, turning a clean error exit into an abort. Same path as display.cpp quit_process. */
[[noreturn]] void host_fatal(int code) { audio_out_shutdown(); fflush(nullptr); diag_flush(); ExitProcess((UINT)code); }
static LONG WINAPI crash_handler(EXCEPTION_POINTERS *ep) {
    uint8_t *fa = (uint8_t *)ep->ExceptionRecord->ExceptionInformation[1];
    fprintf(stderr, "\n[CRASH] excecao 0x%08lx, acesso em %p (end PSP 0x%08llx)\n", ep->ExceptionRecord->ExceptionCode,
            (void *)fa, (unsigned long long)(fa - g_mem));
    fprintf(stderr, "RIP nativo = exe+0x%llx (addr2line -f -e TagForceRecomp.exe 0x%llx)\n",
            (unsigned long long)(ep->ContextRecord->Rip - (DWORD64)GetModuleHandleA(nullptr)), (unsigned long long)ep->ContextRecord->Rip);
    fprintf(stderr, "ultimas funcoes recompiladas:");
    for (uint32_t i = 0; i < 24; i++) fprintf(stderr, " %08x", g_trace[(g_trace_i - 24 + i) & 1023]);
    fprintf(stderr, "\n");
    extern CPU *k_current_cpu(void);
    if (CPU *c = k_current_cpu()) {
        fprintf(stderr, "cpu:");
        for (int i = 1; i < 32; i++) fprintf(stderr, "%s r%d=%08x", (i % 8) ? "" : "\n    ", i, c->r[i]);
        fputc(10, stderr);
    }
    diag_flush();   /* o relatorio acima precisa chegar ao log antes de o processo morrer */
    return EXCEPTION_EXECUTE_HANDLER;
}

static CPU *g_main_cpu;
static std::vector<const GenModule *> g_modules;             /* modulos carregados */
static std::map<uint32_t, ImportEntry> g_imports;             /* stub -> import */
static std::map<uint32_t, HleFn> g_bound;                     /* stub -> handler HLE */
static std::map<uint32_t, void (*)(CPU *)> g_fn_cache;        /* endereco -> funcao recompilada */
static std::map<std::string, HleFn> &registry() { static std::map<std::string, HleFn> r; return r; }

void hle_register(const char *name, HleFn fn) { registry()[name] = fn; }

CPU *host_create(void) {
    CPU *c = (CPU *)calloc(1, sizeof(CPU));
    /* 1 GiB de espaco virtual; commit so do que o PSP tem (scratchpad, VRAM, RAM) */
    g_mem = (uint8_t *)VirtualAlloc(NULL, 0x40000000u, MEM_RESERVE, PAGE_READWRITE);
    if (!g_mem ||
        !VirtualAlloc(g_mem + 0x00010000, 0x4000, MEM_COMMIT, PAGE_READWRITE) ||
        !VirtualAlloc(g_mem + 0x04000000, 0x200000, MEM_COMMIT, PAGE_READWRITE) ||
        !VirtualAlloc(g_mem + 0x08000000, 0x02000000, MEM_COMMIT, PAGE_READWRITE)) {
        fprintf(stderr, "falha ao mapear memoria\n"); host_fatal(1);
    }
    c->mem = g_mem; g_main_cpu = c; cpu_reset_vfpu(c);
    SetUnhandledExceptionFilter(crash_handler);
    if (const char *w = getenv("WATCH_ADDR")) g_watch = (uint32_t)strtoul(w, nullptr, 16);
    { extern void hooks_dev_init(void); hooks_dev_init(); }
    return c;
}
void host_load(uint32_t addr, const void *p, size_t n) { memcpy(g_mem + MA(addr), p, n); }

void display_capture_event(const char *tag);
void host_module_add(const GenModule *m) {
    g_modules.push_back(m);
    if (m->name) display_capture_event(m->name);   /* modo de diagnostico grafico: captura a troca de tela */
    for (unsigned i = 0; i < m->nimps; i++) {
        g_imports[m->imps[i].stub] = m->imps[i];
        auto it = registry().find(m->imps[i].name);
        if (it != registry().end()) g_bound[m->imps[i].stub] = it->second;
    }
}
void host_module_remove(const GenModule *m) {
    g_modules.erase(std::remove(g_modules.begin(), g_modules.end(), m), g_modules.end());
    for (unsigned i = 0; i < m->nimps; i++) { g_imports.erase(m->imps[i].stub); g_bound.erase(m->imps[i].stub); }
    g_fn_cache.clear();
}
bool host_resolve_export(uint32_t nid, uint32_t *addr) {
    for (auto *m : g_modules) for (unsigned i = 0; i < m->nexps; i++)
        if (m->exps[i].nid == nid && m->exps[i].lib && m->exps[i].lib[0]) { *addr = m->exps[i].addr; return true; }
    return false;
}

static void (*find_fn(uint32_t addr))(CPU *) {
    auto ci = g_fn_cache.find(addr);
    if (ci != g_fn_cache.end()) return ci->second;
    for (auto *m : g_modules) {
        if (addr < m->base || addr >= m->base + m->memsz) continue;
        unsigned lo = 0, hi = m->nfns;
        while (lo < hi) {
            unsigned mid = (lo + hi) / 2;
            if (m->fns[mid].addr == addr) { g_fn_cache[addr] = m->fns[mid].fn; return m->fns[mid].fn; }
            if (m->fns[mid].addr < addr) lo = mid + 1; else hi = mid;
        }
    }
    return nullptr;
}

/* ferramenta de pesquisa da decomp: TF_CALL_TRACE=lo-hi (hex) registra toda chamada a um endereco desse intervalo (os stubs de import de um modulo),
 * com os oito argumentos inteiros e f12..f17; sp mascarado nos scripts de comparacao (tools/decomp/decomp_calls.sh) */
extern uint64_t display_frames(void);
static void call_trace(CPU *c, uint32_t addr) {
    fprintf(stderr, "[call] %llu %08x(%08x %08x %08x %08x %08x %08x %08x %08x) f=%08x %08x %08x %08x %08x %08x\n", (unsigned long long)display_frames(), addr,
            c->r[4], c->r[5], c->r[6], c->r[7], c->r[8], c->r[9], c->r[10], c->r[11], c->f[12].u, c->f[13].u, c->f[14].u, c->f[15].u, c->f[16].u, c->f[17].u);
}

void psp_call(CPU *c, uint32_t addr) {
    static uint32_t trace_lo, trace_hi;
    static const bool trace_init = [] { if (const char *e = getenv("TF_CALL_TRACE")) sscanf(e, "%x-%x", &trace_lo, &trace_hi); return true; }();
    (void)trace_init;
    if (trace_hi && addr >= trace_lo && addr < trace_hi) call_trace(c, addr);
    auto b = g_bound.find(addr);
    if (b != g_bound.end()) {
        static const bool trace = getenv("HLE_TRACE") != nullptr;
        if (trace) fprintf(stderr, "[hle] %s(%08x, %08x, %08x, %08x) ra=%08x\n", g_imports[addr].name, c->r[4], c->r[5], c->r[6], c->r[7], c->r[31]);
        static const char *wa = getenv("WATCH_ADDR");
        uint32_t waddr = wa ? (uint32_t)strtoul(wa, nullptr, 16) : 0, before = waddr ? *(uint32_t *)(g_mem + MA(waddr)) : 0;
        /* syscall que prende o jogo por tempo visivel (travadas que so acontecem jogando de verdade). Chamadas que bloqueiam
         * trocam de thread emulada e voltam quadros depois: so conta se nenhum quadro foi apresentado durante a chamada. */
        extern volatile uint64_t g_diag_frames;
        uint64_t f0 = g_diag_frames; LARGE_INTEGER t0, t1; QueryPerformanceCounter(&t0);
        b->second(c);
        QueryPerformanceCounter(&t1);
        if (g_diag_frames == f0) {
            static LARGE_INTEGER qf; if (!qf.QuadPart) QueryPerformanceFrequency(&qf);
            double ms = (t1.QuadPart - t0.QuadPart) * 1000.0 / qf.QuadPart;
            static const double slow_ms = getenv("TF_SYSCALL_SLOW_MS") ? atof(getenv("TF_SYSCALL_SLOW_MS")) : 100.0;   /* diagnostico: baixe (ex.: 3) para achar o que prende o jogo num quadro pesado */
            if (ms > slow_ms) fprintf(stderr, "[perf] syscall lenta no quadro %llu: %s levou %.0f ms\n", (unsigned long long)f0, g_imports[addr].name, ms);
        }
        if (waddr) { uint32_t after = *(uint32_t *)(g_mem + MA(waddr)); if (after != before) fprintf(stderr, "[watch] %08x mudou %08x -> %08x em %s\n", waddr, before, after, g_imports[addr].name); }
        if (trace) fprintf(stderr, "[hle]   -> %08x\n", c->r[2]);
        return;
    }
    if (auto fn = find_fn(addr)) { fn(c); return; }
    auto im = g_imports.find(addr);
    if (im != g_imports.end()) {
        uint32_t target;
        if (host_resolve_export(im->second.nid, &target)) {          /* import de outro modulo do jogo (ex.: libehsys) */
            if (auto fn = find_fn(target)) { g_fn_cache[addr] = fn; fn(c); return; }
            fprintf(stderr, "\nexport %s::%08x aponta para 0x%08x, que nao e inicio de funcao recompilada\n", im->second.lib, im->second.nid, target); host_fatal(2);
        }
        /* Modo Rede/ad-hoc ainda nao existe: qualquer funcao das bibliotecas sceNet e sceWlan sem implementacao devolve erro (SCE_NET_ERROR_*), para o
         * jogo mostrar a falha de conexao dele em vez de o executavel fechar. */
        if (!strncmp(im->second.lib, "sceNet", 6) || !strncmp(im->second.lib, "sceWlan", 7)) {
            static int avisos; if (avisos++ < 20) fprintf(stderr, "[rede] %s::%s sem suporte: devolvendo erro\n", im->second.lib, im->second.name);
            c->r[2] = 0x80410001; return;
        }
        fprintf(stderr, "\n[HLE] nao implementado: %s::%s (nid %08x) a0=%08x a1=%08x a2=%08x a3=%08x ra=%08x\n",
                im->second.lib, im->second.name, im->second.nid, c->r[4], c->r[5], c->r[6], c->r[7], c->r[31]);
        host_fatal(6);
    }
    fprintf(stderr, "\npsp_call: endereco sem funcao 0x%08x (ra=%08x)\n", addr, c->r[31]); host_fatal(2);
}
void psp_syscall(CPU *c, uint32_t pc, uint32_t code) { (void)c; fprintf(stderr, "syscall 0x%x em 0x%08x\n", code, pc); host_fatal(3); }
void psp_break(CPU *c, uint32_t pc) { (void)c; fprintf(stderr, "break em 0x%08x\n", pc); host_fatal(4); }
void psp_unimpl(CPU *c, uint32_t pc, uint32_t word) { (void)c; fprintf(stderr, "\ninstrucao nao implementada 0x%08x em 0x%08x\n", word, pc); host_fatal(5); }

#include "hooks.h"
/* ------------------------------------------------------------------ ganchos (hooks.h) */
uint8_t *g_hookbm; int g_hook_any;
/* cada endereco tem uma CADEIA de ganchos, na ordem de instalacao (TF_HOOK_TRACE, depois os plugins na ordem dos mods). Roda um por um ate
 * algum devolver true. hook_call_original dentro do gancho i continua a cadeia em i+1 e so depois roda o corpo original; fora de um
 * gancho daquele endereco (ou hook_skip_next) roda o corpo direto. */
struct HookEnt { PspHook fn; int (*cfn)(CPU *); void *owner; };
static std::map<uint32_t, std::vector<HookEnt>> g_hooks;
static const size_t kSkipAll = ~(size_t)0;
static uint32_t g_skip_addr; static size_t g_skip_from;   /* a proxima entrada em g_skip_addr comeca a cadeia em g_skip_from */
static uint32_t g_run_addr; static size_t g_run_idx;      /* gancho rodando agora (para hook_call_original) */
void *g_hook_owner;                                       /* dono do gancho rodando agora (plugins.cpp: prefixo do log) */
extern uint32_t g_hook_cur;
static uint32_t g_memlog_lo, g_memlog_hi; static uint64_t g_memlog_fr = ~0ull;
static uint32_t g_fprof_addr; static uint32_t *g_fprof[8]; static uint64_t g_fprof_last;   /* TF_FPROF (mais abaixo) */
static void hook_bit(uint32_t addr, bool on) {
    if (!g_hookbm) g_hookbm = (uint8_t *)calloc(0x01fffffcu / 4 / 8 + 8, 1);
    uint32_t i = (addr & 0x01fffffcu) >> 2;
    if (on) g_hookbm[i >> 3] |= (uint8_t)(1u << (i & 7)); else g_hookbm[i >> 3] &= (uint8_t)~(1u << (i & 7));
    g_hook_any = !g_hooks.empty() || g_fprof_addr != 0 || g_memlog_lo != 0;
}
static void hook_add(uint32_t addr, const HookEnt &e) {
    auto &v = g_hooks[addr];
    for (auto &o : v) if (o.fn == e.fn && o.cfn == e.cfn && o.owner == e.owner) return;   /* mesmo gancho duas vezes */
    v.push_back(e); hook_bit(addr, true);
}
void hook_install(uint32_t addr, PspHook fn, void *owner) { if (fn) hook_add(addr, HookEnt{fn, nullptr, owner}); }
void hook_install_c(uint32_t addr, int (*fn)(CPU *), void *owner) { if (fn) hook_add(addr, HookEnt{nullptr, fn, owner}); }
void hook_remove_owner(void *owner) {
    for (auto it = g_hooks.begin(); it != g_hooks.end();) {
        auto &v = it->second; for (size_t i = v.size(); i-- > 0;) if (v[i].owner == owner) v.erase(v.begin() + (ptrdiff_t)i);
        if (v.empty()) { const uint32_t a = it->first; it = g_hooks.erase(it); hook_bit(a, false); } else ++it;
    }
}
/* ---- ferramenta de pesquisa: TF_FPROF=addr_hex[,ini,fim] conta as entradas de cada funcao do jogo em [ini,fim) (padrao: libduel_eng) separadas pelo
 * valor (&7) da palavra em addr (ex.: de quem e a vez no duelo); a cada 600 quadros regrava gen/fprof_<valor>.tsv (funcao<TAB>entradas) */
extern uint64_t display_frames(void);
static void fprof_dump(uint64_t win) {   /* uma janela de 600 quadros por arquivo (fprof_<quadro inicial>.tsv); zera a contagem */
    for (int k = 0; k < 1; k++) if (g_fprof[k]) {
        char nm[64]; snprintf(nm, sizeof nm, "fprof_%llu.tsv", (unsigned long long)(win * 600));
        if (FILE *f = fopen(nm, "w")) { for (uint32_t i = 0; i < 0x800000; i++) if (g_fprof[k][i]) fprintf(f, "%08x %u\n", 0x08000000u + i * 4, g_fprof[k][i]); fclose(f); }
        memset(g_fprof[k], 0, 0x800000 * 4);
    }
}
static void fprof_init(void) {
    if (const char *m = getenv("TF_MEMLOG")) { g_memlog_lo = (uint32_t)strtoul(m, nullptr, 16) & ~3u; if (const char *p = strchr(m, ',')) g_memlog_hi = (uint32_t)strtoul(p + 1, nullptr, 16);
        if (g_memlog_hi <= g_memlog_lo || g_memlog_hi - g_memlog_lo > 0x100000u) g_memlog_hi = g_memlog_lo + 0x100;   /* sem fim (ou faixa absurda): 64 palavras */
        for (uint32_t x = 0x09e6b800u; x < 0x09f78000u; x += 4) hook_bit(x, true); g_hook_any = 1; }
    const char *e = getenv("TF_FPROF"); if (!e) return;
    uint32_t a = (uint32_t)strtoul(e, nullptr, 16), lo = 0x09e6b800u, hi = 0x09f78000u;
    if (const char *p = strchr(e, ',')) { lo = (uint32_t)strtoul(p + 1, nullptr, 16); if (const char *q = strchr(p + 1, ',')) hi = (uint32_t)strtoul(q + 1, nullptr, 16); }
    g_fprof_addr = a; g_fprof[0] = (uint32_t *)calloc(0x800000, 4);   /* uma tabela (32 MB): contagem por funcao na janela atual */
    if (!g_fprof[0]) { g_fprof_addr = 0; return; }
    for (uint32_t x = lo; x < hi; x += 4) hook_bit(x, true);
    g_hook_any = 1;
}
extern "C" int psp_hook(CPU *c, uint32_t addr) {
    if (g_fprof_addr) {
        if (addr - 0x08000000u < 0x02000000u) g_fprof[0][(addr - 0x08000000u) >> 2]++;   /* so a RAM do usuario (a tabela cobre 32 MB) */
        const uint64_t fr = display_frames(); if (fr / 600 != g_fprof_last) { fprof_dump(g_fprof_last); g_fprof_last = fr / 600; }
    }
    if (g_memlog_lo) {   /* TF_MEMLOG=ini,fim (hex): a cada 60 quadros imprime as palavras dessa faixa que mudaram desde a ultima vez */
        const uint64_t fr = display_frames() / 60;
        if (fr != g_memlog_fr) {
            g_memlog_fr = fr; static std::vector<uint32_t> prev;
            if (prev.empty()) prev.assign((g_memlog_hi - g_memlog_lo) / 4, 0);
            for (uint32_t a = g_memlog_lo; a < g_memlog_hi; a += 4) { uint32_t v = *(uint32_t *)(g_mem + MA(a)), &p = prev[(a - g_memlog_lo) / 4]; if (v != p) { fprintf(stderr, "[mem] q%llu %08x %08x->%08x\n", (unsigned long long)(fr * 60), a, p, v); p = v; } }
        }
    }
    size_t from = 0;
    if (g_skip_addr == addr) { from = g_skip_from; g_skip_addr = 0; if (from == kSkipAll) return 0; }   /* pedido por hook_call_original */
    for (size_t i = from;; i++) {
        auto it = g_hooks.find(addr);   /* busca a cada passo: um gancho pode instalar/remover ganchos (a lista pode ser realocada) */
        if (it == g_hooks.end() || i >= it->second.size()) return 0;
        const HookEnt e = it->second[i];
        const uint32_t sa = g_run_addr, sc = g_hook_cur; const size_t si = g_run_idx; void *so = g_hook_owner;
        g_run_addr = addr; g_run_idx = i; g_hook_owner = e.owner; g_hook_cur = addr;
        const bool done = e.fn ? e.fn(c) : e.cfn(c) != 0;
        g_run_addr = sa; g_run_idx = si; g_hook_owner = so; g_hook_cur = sc;
        if (done) return 1;
    }
}
void hook_skip_next(uint32_t addr) { g_skip_addr = addr; g_skip_from = kSkipAll; }   /* a proxima entrada com este endereco ignora todos os ganchos (jit.cpp) */
bool hook_call_original(CPU *c, uint32_t addr) {
    auto fn = find_fn(addr); if (!fn) return false;
    g_skip_addr = addr; g_skip_from = (addr == g_run_addr) ? g_run_idx + 1 : kSkipAll;
    fn(c); g_skip_addr = 0; return true;
}

/* ---- ferramenta de pesquisa: TF_HOOK_TRACE=addr,addr,... registra cada chamada dessas funcoes (quadro, argumentos, retorno nao) */
uint32_t g_hook_cur;
extern uint64_t display_frames(void);
static bool hook_trace_fn(CPU *c) {
    fprintf(stderr, "[hook] quadro %llu: %08x(%08x, %08x, %08x, %08x) ra=%08x sp=%08x\n", (unsigned long long)display_frames(), g_hook_cur, c->r[4], c->r[5], c->r[6], c->r[7], c->r[31], c->r[29]);
    return false;
}
void hooks_dev_init(void) {
    fprof_init();
    const char *e = getenv("TF_HOOK_TRACE"); if (!e) return;
    while (*e) {
        char *end; unsigned long a = strtoul(e, &end, 16);
        if (end != e) { hook_install((uint32_t)a, hook_trace_fn); fprintf(stderr, "[hook] observando %08lx\n", a); }
        e = *end ? end + 1 : end;
    }
}

bool host_module_loaded(const char *name) { for (auto *m : g_modules) if (m->name && !strcmp(m->name, name)) return true; return false; }

/* ---- state save / load (savestate.cpp): the table of loaded modules and what is being hooked right now. The translated code itself is compiled once at
 * start and never changes; "loading a module" only fills these tables (and the memory image, which the state keeps). */
#include <memory>
struct HostSnap {
    std::vector<const GenModule *> modules; std::map<uint32_t, ImportEntry> imports; std::map<uint32_t, HleFn> bound;
    uint32_t skip_addr, run_addr; size_t skip_from, run_idx; void *hook_owner; uint32_t hook_cur;
};
std::shared_ptr<void> host_state_save(void) {
    auto s = std::make_shared<HostSnap>();
    s->modules = g_modules; s->imports = g_imports; s->bound = g_bound;
    s->skip_addr = g_skip_addr; s->skip_from = g_skip_from; s->run_addr = g_run_addr; s->run_idx = g_run_idx; s->hook_owner = g_hook_owner; s->hook_cur = g_hook_cur;
    return s;
}
void host_state_load(const std::shared_ptr<void> &p) {
    const HostSnap &s = *(const HostSnap *)p.get();
    g_modules = s.modules; g_imports = s.imports; g_bound = s.bound; g_fn_cache.clear();
    g_skip_addr = s.skip_addr; g_skip_from = s.skip_from; g_run_addr = s.run_addr; g_run_idx = s.run_idx; g_hook_owner = s.hook_owner; g_hook_cur = s.hook_cur;
}
