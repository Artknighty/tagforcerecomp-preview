/* Kernel do PSP em HLE: memoria, threads cooperativas (fibers), semaforos, event flags, tempo virtual. */
#include "hle.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <deque>
#include <map>
#include <vector>
#include <string>
#include <algorithm>

#define USER_BASE 0x08800000u
#define USER_END  0x0a000000u
#define VBLANK_US 16683u          /* 1e6 / 59.94 */
#define EXIT_STUB 0x08800000u     /* endereco "magico" de retorno de thread (nunca executado) */

/* ------------------------------------------------------------------ memoria */
struct Block { uint32_t start, size; uint32_t uid; std::string name; };
static std::vector<Block> g_blocks;   /* ordenado por start */
static uint32_t g_next_uid = 0x1000;
static uint32_t new_uid() { return g_next_uid++; }

static bool region_free(uint32_t s, uint32_t n) {
    if (s < USER_BASE || (uint64_t)s + n > USER_END) return false;
    for (auto &b : g_blocks) if (s < b.start + b.size && b.start < s + n) return false;
    return true;
}
uint32_t k_alloc(uint32_t size, int type, uint32_t addr, const char *name) {
    size = (size + 0xff) & ~0xffu; if (!size) size = 0x100;
    uint32_t at = 0;
    if (type == 2) { if (region_free(addr, size)) at = addr; }
    else if (type == 1) {   /* HIGH: de cima para baixo */
        uint32_t s = (USER_END - size) & ~0xffu;
        for (;;) {
            if (s < USER_BASE) break;
            const Block *hit = nullptr;
            for (auto &b : g_blocks) if (s < b.start + b.size && b.start < s + size) hit = &b;
            if (!hit) { at = s; break; }
            if (hit->start < USER_BASE + size) break;
            s = (hit->start - size) & ~0xffu;
        }
    } else {                /* LOW: de baixo para cima */
        uint32_t s = USER_BASE;
        for (auto &b : g_blocks) { if (s + size <= b.start) break; s = std::max(s, (b.start + b.size + 0xff) & ~0xffu); }
        if (region_free(s, size)) at = s;
    }
    if (!at) return 0;
    Block b{at, size, new_uid(), name ? name : ""};
    g_blocks.insert(std::upper_bound(g_blocks.begin(), g_blocks.end(), b, [](const Block &x, const Block &y) { return x.start < y.start; }), b);
    if (type != 2) memset(g_mem + at, 0, size);
    return at;
}
bool k_region_free(uint32_t start, uint32_t size) { return region_free(start, (size + 0xff) & ~0xffu); }
uint32_t k_probe_low(uint32_t size) {   /* onde uma alocacao LOW cairia agora (sem alocar) */
    size = (size + 0xff) & ~0xffu; uint32_t s = USER_BASE;
    for (auto &b : g_blocks) { if (s + size <= b.start) break; s = std::max(s, (b.start + b.size + 0xff) & ~0xffu); }
    return region_free(s, size) ? s : 0;
}
void k_free(uint32_t addr) {
    for (size_t i = 0; i < g_blocks.size(); i++) if (g_blocks[i].start == addr || g_blocks[i].uid == addr) { g_blocks.erase(g_blocks.begin() + i); return; }
}
static uint32_t total_free() { uint32_t used = 0; for (auto &b : g_blocks) used += b.size; return (USER_END - USER_BASE) - used; }
static uint32_t max_free() {
    uint32_t best = 0, s = USER_BASE;
    for (auto &b : g_blocks) { if (b.start > s) best = std::max(best, b.start - s); s = b.start + b.size; }
    return std::max(best, USER_END - s);
}
void k_reserve(uint32_t start, uint32_t size, const char *name) { k_alloc(size, 2, start & ~0xffu, name); }

/* ------------------------------------------------------------------ threads */
enum St { DORMANT, READY, RUNNING, WAITING, DEAD };
enum Wait { W_NONE, W_DELAY, W_SLEEP, W_SEMA, W_EVF, W_VBLANK, W_THREADEND, W_MBX };
struct Thread {
    uint32_t uid; std::string name; CPU cpu; void *fiber = nullptr;
    uint32_t entry = 0, prio = 0x20, stack = 0, stack_size = 0, attr = 0, arglen = 0, argp = 0, gp = 0;
    St st = DORMANT; Wait wk = W_NONE; uint32_t wobj = 0; uint32_t need = 0, mode = 0, out_ptr = 0;
    bool has_to = false; uint64_t wake_at = 0; uint32_t result = 0; uint32_t exit_status = 0; bool wake_pending = false;
    uint64_t seq = 0; uint32_t tls = 0; bool suspended = false;
};
static std::map<uint32_t, Thread *> g_threads;
static std::vector<Thread *> g_all;
static Thread *g_cur = nullptr;
static void *g_sched_fiber = nullptr;
static uint64_t g_now = 0, g_next_vblank = VBLANK_US, g_seq = 0;
static std::deque<Thread *> g_vblank_waiters;
void display_present(void);

uint64_t k_now_us(void) { return g_now; }
void k_advance(uint32_t us) { g_now += us; }
uint32_t k_current_uid(void) { return g_cur ? g_cur->uid : 0; }
static Thread *find_thread(uint32_t uid) { auto it = g_threads.find(uid); return it == g_threads.end() ? nullptr : it->second; }

static void make_ready(Thread *t, uint32_t result) {
    t->st = READY; t->wk = W_NONE; t->has_to = false; t->result = result; t->seq = ++g_seq;
}
static void to_sched() { SwitchToFiber(g_sched_fiber); }

void k_block_current(uint64_t timeout_us, bool has_timeout) {
    Thread *t = g_cur;
    t->st = WAITING; t->has_to = has_timeout; t->wake_at = g_now + timeout_us;
    to_sched();
}
/* cede a CPU se ha thread pronta de prioridade maior (ou igual, se yield) */
static bool g_in_intr;   /* dentro de uma rotina de interrupcao: nada de trocar de thread (ver vblank_interrupts) */
/* Sub-interrupcoes do vblank (interrupcao 30 do PSP): o player de filmes do jogo registra uma rotina que, a cada vblank,
 * liga um event flag que o laco de decodificacao espera. Sem chama-la o filme travava logo no inicio (tela preta). */
struct SubIntr { uint32_t handler, arg; bool enabled; };
static std::map<uint32_t, SubIntr> g_vblank_subintr;   /* sub-numero -> rotina */
static void vblank_interrupts(void) {
    if (g_vblank_subintr.empty() || g_in_intr) return;
    g_in_intr = true;
    for (auto &kv : g_vblank_subintr) if (kv.second.enabled && kv.second.handler) k_call_guest(kv.second.handler, kv.first, kv.second.arg, 0, 0);
    g_in_intr = false;
}
HLE(sceKernelRegisterSubIntrHandler) {   /* (intr, sub, handler, arg) */
    if (ARG(0) == 30) g_vblank_subintr[ARG(1)] = SubIntr{ARG(2), ARG(3), false};
    RET(0);
}
HLE(sceKernelReleaseSubIntrHandler) { if (ARG(0) == 30) g_vblank_subintr.erase(ARG(1)); RET(0); }
HLE(sceKernelEnableSubIntr) { if (ARG(0) == 30) { auto it = g_vblank_subintr.find(ARG(1)); if (it != g_vblank_subintr.end()) it->second.enabled = true; } RET(0); }
HLE(sceKernelDisableSubIntr) { if (ARG(0) == 30) { auto it = g_vblank_subintr.find(ARG(1)); if (it != g_vblank_subintr.end()) it->second.enabled = false; } RET(0); }
static void preempt_check(bool yield_equal = false) {
    Thread *t = g_cur; if (!t || g_in_intr) return;
    for (auto *o : g_all) if (o->st == READY && !o->suspended && (o->prio < t->prio || (yield_equal && o->prio == t->prio))) {
        t->st = READY; t->seq = ++g_seq; to_sched(); return;
    }
}

static VOID CALLBACK fiber_entry(void *p) {
    Thread *t = (Thread *)p;
    psp_call(&t->cpu, t->entry);
    /* retorno normal da funcao da thread == sceKernelExitThread(v0) */
    t->exit_status = t->cpu.r[2]; t->st = DORMANT;
    for (auto *o : g_all) if (o->st == WAITING && o->wk == W_THREADEND && o->wobj == t->uid) make_ready(o, t->exit_status);
    to_sched();
}

static uint32_t make_thread(const char *name, uint32_t entry, uint32_t prio, uint32_t stack_size, uint32_t attr) {
    Thread *t = new Thread(); memset(&t->cpu, 0, sizeof t->cpu);
    t->cpu.mem = g_mem; cpu_reset_vfpu(&t->cpu); t->uid = new_uid(); t->name = name; t->entry = entry; t->prio = prio; t->attr = attr;
    if (stack_size < 0x1000) stack_size = 0x1000;
    t->stack_size = stack_size;
    t->tls = k_alloc(0x100, 1, 0, "tls");   /* k0 aponta para dados por thread (o crt escreve em 4(k0)) */
    t->stack = k_alloc(stack_size, 1, 0, name);
    if (!t->stack) { k_free(t->tls); delete t; return KERR_NO_MEMORY; }
    g_threads[t->uid] = t; g_all.push_back(t);
    return t->uid;
}
static void start_thread(Thread *t, uint32_t arglen, uint32_t argp, uint32_t gp) {
    uint32_t top = t->stack + t->stack_size, sp = top;
    uint32_t copied = 0;
    if (arglen && argp) {
        sp -= (arglen + 0xf) & ~0xfu; copied = sp; memcpy(g_mem + MA(sp), g_mem + MA(argp), arglen);
    }
    sp -= 0x40;
    memset(&t->cpu.r, 0, sizeof t->cpu.r);
    t->cpu.r[4] = arglen; t->cpu.r[5] = copied; t->cpu.r[29] = sp; t->cpu.r[28] = gp; t->cpu.r[26] = t->tls; t->cpu.r[31] = EXIT_STUB;
    t->gp = gp;
    if (t->fiber) DeleteFiber(t->fiber);
    t->suspended = false;
    t->fiber = CreateFiberEx(0, 8u << 20, 0, fiber_entry, t);   /* pilha do host: 8 MiB reservados */
    make_ready(t, 0);
}

uint32_t k_start_root(uint32_t entry, uint32_t gp, uint32_t arglen, uint32_t argp) {
    uint32_t uid = make_thread("root", entry, 0x20, 0x40000, 0);
    start_thread(find_thread(uid), arglen, argp, gp);
    return uid;
}

/* Executa ate nao restar thread viva. Devolve 0. */
int k_run(void) {
    g_sched_fiber = ConvertThreadToFiber(nullptr);
    for (;;) {
        Thread *best = nullptr;
        for (auto *t : g_all) if (t->st == READY && !t->suspended && (!best || t->prio < best->prio || (t->prio == best->prio && t->seq < best->seq))) best = t;
        if (!best) {
            /* ocioso: avanca o tempo virtual ate o proximo evento */
            uint64_t next = g_next_vblank; bool any_alive = false;
            for (auto *t : g_all) { if (t->st == WAITING) { any_alive = true; if (t->has_to) next = std::min(next, t->wake_at); } }
            if (!any_alive) return 0;
            g_now = std::max(g_now, next);
            if (g_now >= g_next_vblank) {
                g_next_vblank += VBLANK_US; display_present(); vblank_interrupts();
                for (auto *t : g_all) if (t->st == WAITING && t->wk == W_VBLANK) make_ready(t, 0);
            }
            for (auto *t : g_all) if (t->st == WAITING && t->has_to && t->wake_at <= g_now)
                make_ready(t, t->wk == W_DELAY || t->wk == W_SLEEP ? 0 : KERR_TIMEOUT);
            continue;
        }
        g_cur = best; best->st = RUNNING;
        SwitchToFiber(best->fiber);
        if (g_cur && g_cur->st == RUNNING) g_cur->st = READY;
        g_cur = nullptr;
    }
}

/* ------------------------------------------------------------------ semaforos / event flags */
struct Sema { int count, max; std::deque<Thread *> q; };
struct Evf  { uint32_t pat; std::deque<Thread *> q; uint32_t attr = 0, init = 0; char name[32] = {}; };
static std::map<uint32_t, Sema> g_sema;
static std::map<uint32_t, Evf> g_evf;

static void sema_dispatch(Sema &s) {
    while (!s.q.empty()) {
        Thread *t = s.q.front();
        if (t->st != WAITING || t->wk != W_SEMA) { s.q.pop_front(); continue; }
        if (s.count < (int)t->need) break;
        s.count -= t->need; s.q.pop_front(); make_ready(t, 0);
    }
}
static bool evf_match(Evf &e, Thread *t) {
    bool ok = (t->mode & 1) ? (e.pat & t->need) != 0 : (e.pat & t->need) == t->need;
    if (ok) { if (t->out_ptr) wr32(t->out_ptr, e.pat); if (t->mode & 0x20) e.pat &= ~t->need; else if (t->mode & 0x10) e.pat = 0; }
    return ok;
}
static void evf_dispatch(Evf &e) {
    for (auto it = e.q.begin(); it != e.q.end();) {
        Thread *t = *it;
        if (t->st != WAITING || t->wk != W_EVF) { it = e.q.erase(it); continue; }
        if (evf_match(e, t)) { make_ready(t, 0); it = e.q.erase(it); } else ++it;
    }
}
static uint32_t timeout_of(uint32_t ptr, bool &has) { has = ptr != 0; return has ? rd32(ptr) : 0; }

/* ------------------------------------------------------------------ syscalls: SysMem */
HLE(sceKernelSetCompiledSdkVersion) { RET(0); }
HLE(sceKernelSetCompilerVersion) { RET(0); }
HLE(sceKernelAllocPartitionMemory) {   /* (partition, name, type, size, addr) */
    uint32_t at = k_alloc(ARG(3), ARG(2), ARG(4), mem_cstr(ARG(1)));
    if (!at) { RET(0x800200d9); return; }
    for (auto &b : g_blocks) if (b.start == at) { RET(b.uid); return; }
}
HLE(sceKernelGetBlockHeadAddr) { for (auto &b : g_blocks) if (b.uid == ARG(0)) { RET(b.start); return; } RET(0); }
HLE(sceKernelFreePartitionMemory) { k_free(ARG(0)); RET(0); }
HLE(sceKernelTotalFreeMemSize) { RET(total_free()); }
HLE(sceKernelMaxFreeMemSize) { RET(max_free()); }

/* ------------------------------------------------------------------ syscalls: ThreadMan */
HLE(sceKernelCreateThread) { RET(make_thread(mem_cstr(ARG(0)), ARG(1), ARG(2), ARG(3), ARG(4))); }
HLE(sceKernelStartThread) {
    Thread *t = find_thread(ARG(0)); if (!t) { RET(KERR_UNKNOWN_UID); return; }
    start_thread(t, ARG(1), ARG(2), g_cur ? g_cur->gp : 0); RET(0); preempt_check();
}
HLE(sceKernelGetThreadId) { RET(k_current_uid()); }
HLE(sceKernelGetThreadCurrentPriority) { RET(g_cur ? g_cur->prio : 0); }
HLE(sceKernelChangeThreadPriority) { Thread *t = ARG(0) ? find_thread(ARG(0)) : g_cur; if (!t) { RET(KERR_UNKNOWN_UID); return; } t->prio = ARG(1); RET(0); preempt_check(); }
HLE(sceKernelChangeCurrentThreadAttr) { RET(0); }
HLE(sceKernelDeleteThread) {   /* o objeto Thread fica (pode haver ponteiros nas filas de espera), mas pilha, TLS e a fiber do host sao liberados */
    Thread *t = find_thread(ARG(0)); if (!t) { RET(0); return; }   /* como antes (o PSP devolveria erro; o jogo funcionou assim) */
    t->st = DEAD; k_free(t->stack); k_free(t->tls); t->stack = t->tls = 0;
    if (t->fiber && t != g_cur) { DeleteFiber(t->fiber); t->fiber = nullptr; }
    g_threads.erase(t->uid); g_all.erase(std::find(g_all.begin(), g_all.end(), t)); RET(0);
}
HLE(sceKernelExitThread) {
    Thread *t = g_cur; t->exit_status = ARG(0); t->st = DORMANT;
    for (auto *o : g_all) if (o->st == WAITING && o->wk == W_THREADEND && o->wobj == t->uid) make_ready(o, t->exit_status);
    to_sched();
}
HLE(sceKernelSuspendThread) {
    Thread *t = find_thread(ARG(0)); if (!t) { RET(KERR_UNKNOWN_UID); return; }
    t->suspended = true; RET(0); if (t == g_cur) { t->st = READY; t->seq = ++g_seq; to_sched(); }
}
HLE(sceKernelResumeThread) {
    Thread *t = find_thread(ARG(0)); if (!t) { RET(KERR_UNKNOWN_UID); return; }
    t->suspended = false; RET(0); preempt_check();
}
HLE(sceKernelDelayThread) { g_cur->wk = W_DELAY; g_now += 1; k_block_current(ARG(0), true); RET(0); }
HLE(sceKernelDelayThreadCB) { g_cur->wk = W_DELAY; k_block_current(ARG(0), true); RET(0); }
HLE(sceKernelSleepThread) {
    if (g_cur->wake_pending) { g_cur->wake_pending = false; RET(0); return; }
    g_cur->wk = W_SLEEP; k_block_current(0, false); RET(0);
}
HLE(sceKernelWakeupThread) {
    Thread *t = find_thread(ARG(0)); if (!t) { RET(KERR_UNKNOWN_UID); return; }
    if (t->st == WAITING && t->wk == W_SLEEP) make_ready(t, 0); else t->wake_pending = true;
    RET(0); preempt_check();
}
static void wait_thread_end(CPU *c) {
    Thread *t = find_thread(ARG(0)); if (!t) { RET(KERR_UNKNOWN_UID); return; }
    if (t->st == DORMANT) { RET(t->exit_status); return; }
    g_cur->wk = W_THREADEND; g_cur->wobj = t->uid; k_block_current(0, false); RET(g_cur->result);
}
HLE(sceKernelWaitThreadEnd) { wait_thread_end(c); }
HLE(sceKernelWaitThreadEndCB) { wait_thread_end(c); }

/* Consultas de tempo feitas em laco ("espera ocupada"): o relogio virtual so anda quando todas as threads param, entao
 * um laco consultando o tempo/vcount nunca veria o tempo passar e o jogo congelaria. Cada consulta avanca um pouco o
 * relogio e, se isso cruzar um vblank (ou, para o relogio comum, se a thread ja esta ha mais de um quadro sem ceder),
 * processa o vblank como a interrupcao do PSP faria e da a vez a threads de prioridade maior. */
static void time_poll(uint32_t us, bool vblank_poll) {
    g_now += us;
    if (g_now < g_next_vblank + (vblank_poll ? 0 : VBLANK_US)) return;
    bool woke = false;
    while (g_now >= g_next_vblank) {
        g_next_vblank += VBLANK_US; display_present(); vblank_interrupts();
        if (!g_vblank_subintr.empty()) woke = true;   /* a rotina de vblank pode ter acordado alguem (event flag) */
        for (auto *t : g_all) if (t->st == WAITING && t->wk == W_VBLANK) { make_ready(t, 0); woke = true; }
    }
    for (auto *t : g_all) if (t->st == WAITING && t->has_to && t->wake_at <= g_now) {
        make_ready(t, t->wk == W_DELAY || t->wk == W_SLEEP ? 0 : KERR_TIMEOUT); woke = true;
    }
    if (woke) preempt_check();
}
HLE(sceKernelGetSystemTimeLow) { time_poll(1, false); RET((uint32_t)g_now); }
HLE(sceKernelGetSystemTimeWide) { time_poll(1, false); RET64(g_now); }
HLE(sceRtcGetCurrentTick) { time_poll(1, false); wr64(ARG(0), g_now + 62135596800ull * 1000000ull); RET(0); }

HLE(sceKernelCreateSema) { uint32_t u = new_uid(); g_sema[u] = Sema{(int)ARG(2), (int)ARG(3), {}}; RET(u); }
#define KERR_WAIT_DELETE 0x800201b5u   /* o objeto esperado foi apagado */
HLE(sceKernelDeleteSema) {   /* quem esperava acorda com WAIT_DELETE (antes ficava preso para sempre) */
    auto it = g_sema.find(ARG(0)); if (it == g_sema.end()) { RET(KERR_UNKNOWN_UID); return; }
    for (Thread *t : it->second.q) if (t->st == WAITING && t->wk == W_SEMA && t->wobj == ARG(0)) make_ready(t, KERR_WAIT_DELETE);
    g_sema.erase(it); RET(0); preempt_check();
}
HLE(sceKernelSignalSema) {
    auto it = g_sema.find(ARG(0)); if (it == g_sema.end()) { RET(KERR_UNKNOWN_UID); return; }
    it->second.count += ARG(1); sema_dispatch(it->second); RET(0); preempt_check();
}
static void wait_sema(CPU *c) {
    auto it = g_sema.find(ARG(0)); if (it == g_sema.end()) { RET(KERR_UNKNOWN_UID); return; }
    Sema &s = it->second; uint32_t need = ARG(1);
    if (s.count >= (int)need && s.q.empty()) { s.count -= need; RET(0); return; }
    bool has; uint32_t to = timeout_of(ARG(2), has);
    g_cur->wk = W_SEMA; g_cur->wobj = ARG(0); g_cur->need = need; s.q.push_back(g_cur);
    k_block_current(to, has); RET(g_cur->result);
}
HLE(sceKernelWaitSema) { wait_sema(c); }
HLE(sceKernelWaitSemaCB) { wait_sema(c); }

HLE(sceKernelCreateEventFlag) {   /* (nome, attr, padrao inicial, opt) */
    uint32_t u = new_uid(); Evf e; e.pat = e.init = ARG(2); e.attr = ARG(1);
    if (ARG(0)) strncpy(e.name, mem_cstr(ARG(0)), 31);
    g_evf[u] = e; RET(u);
}
HLE(sceKernelDeleteEventFlag) {
    auto it = g_evf.find(ARG(0)); if (it == g_evf.end()) { RET(KERR_UNKNOWN_UID); return; }
    for (Thread *t : it->second.q) if (t->st == WAITING && t->wk == W_EVF && t->wobj == ARG(0)) make_ready(t, KERR_WAIT_DELETE);
    g_evf.erase(it); RET(0); preempt_check();
}
HLE(sceKernelSetEventFlag) {
    auto it = g_evf.find(ARG(0)); if (it == g_evf.end()) { RET(KERR_UNKNOWN_UID); return; }
    it->second.pat |= ARG(1); evf_dispatch(it->second); RET(0); preempt_check();
}
HLE(sceKernelClearEventFlag) { auto it = g_evf.find(ARG(0)); if (it == g_evf.end()) { RET(KERR_UNKNOWN_UID); return; } it->second.pat &= ARG(1); RET(0); }
HLE(sceKernelWaitEventFlag) {   /* (id, bits, mode, outBits*, timeout*) */
    auto it = g_evf.find(ARG(0)); if (it == g_evf.end()) { RET(KERR_UNKNOWN_UID); return; }
    Evf &e = it->second;
    g_cur->need = ARG(1); g_cur->mode = ARG(2); g_cur->out_ptr = ARG(3);
    if (evf_match(e, g_cur)) { RET(0); return; }
    bool has; uint32_t to = timeout_of(ARG(4), has);
    g_cur->wk = W_EVF; g_cur->wobj = ARG(0); e.q.push_back(g_cur);
    k_block_current(to, has); RET(g_cur->result);
}
/* SceKernelEventFlagInfo: size, name[32], attr, initPattern, currentPattern, numWaitThreads. Antes nada era preenchido e o
 * player de filmes lia lixo da pilha como padrao atual (bit 0x800 = "abortar": o filme parava antes de comecar). */
HLE(sceKernelReferEventFlagStatus) {
    auto it = g_evf.find(ARG(0)); if (it == g_evf.end()) { RET(KERR_UNKNOWN_UID); return; }
    uint32_t p = ARG(1); if (!p) { RET(0); return; }
    uint32_t size = rd32(p); const Evf &e = it->second;
    auto put = [&](uint32_t off, uint32_t v) { if (off + 4 <= size) wr32(p + off, v); };
    if (size >= 36) memcpy(mem_ptr(p + 4), e.name, 32);
    put(36, e.attr); put(40, e.init); put(44, e.pat); put(48, (uint32_t)e.q.size());
    RET(0);
}

HLE(sceKernelCpuSuspendIntr) { RET(1); }
HLE(sceKernelCpuResumeIntr) { RET(0); }
HLE(sceKernelPowerTick) { RET(0); }

/* ------------------------------------------------------------------ display (so o necessario ao agendador) */
HLE(sceDisplayWaitVblankStartCB) { g_cur->wk = W_VBLANK; k_block_current(0, false); RET(0); }
HLE(sceDisplayGetVcount) { time_poll(1, true); RET((uint32_t)(g_now / VBLANK_US)); }
HLE(sceDisplayGetAccumulatedHcount) { time_poll(1, true); RET((uint32_t)(g_now / 60)); }

void k_init(void) { g_blocks.clear(); }

/* ------------------------------------------------------------------ modulos */
struct ModInfo { uint32_t uid, base, memsz, textsz, datasz, bsssz, gp; std::string name; };
static std::vector<ModInfo> g_mods;
uint32_t k_register_module(uint32_t base, uint32_t memsz, uint32_t textsz, uint32_t datasz, uint32_t bsssz, uint32_t gp, const char *name) {
    g_mods.push_back({new_uid(), base, memsz, textsz, datasz, bsssz, gp, name}); return g_mods.back().uid;
}
HLE(sceKernelGetModuleIdByAddress) { for (auto &m : g_mods) if (ARG(0) >= m.base && ARG(0) < m.base + m.memsz) { RET(m.uid); return; } RET(KERR_UNKNOWN_UID); }
HLE(sceKernelGetModuleId) { RET(g_mods.empty() ? KERR_UNKNOWN_UID : g_mods[0].uid); }
HLE(sceKernelQueryModuleInfo) {
    for (auto &m : g_mods) if (m.uid == ARG(0)) {
        uint32_t p = ARG(1); memset(mem_ptr(p), 0, 0x60);
        wr32(p, 0x60); mem_ptr(p)[4] = 1; wr32(p + 8, m.base); wr32(p + 0x18, m.memsz);
        wr32(p + 0x28, m.base); wr32(p + 0x2c, m.gp); wr32(p + 0x30, m.base); wr32(p + 0x34, m.textsz);
        wr32(p + 0x38, m.datasz); wr32(p + 0x3c, m.bsssz);
        strncpy((char *)mem_ptr(p + 0x44), m.name.c_str(), 27); RET(0); return;
    }
    RET(KERR_UNKNOWN_UID);
}

/* ------------------------------------------------------------------ pools (Fpl/Vpl), caixas de mensagem, callbacks */
struct Fpl { uint32_t base, bsize, n; std::vector<bool> used; std::deque<Thread *> q; };
static std::map<uint32_t, Fpl> g_fpl;
HLE(sceKernelCreateFpl) {   /* (name, part, attr, blocksize, nblocks, opt) */
    uint32_t bs = (ARG(3) + 0x3f) & ~0x3fu, n = ARG(4);
    uint32_t base = k_alloc(bs * n, 0, 0, mem_cstr(ARG(0)));
    if (!base) { RET(KERR_NO_MEMORY); return; }
    uint32_t u = new_uid(); g_fpl[u] = Fpl{base, bs, n, std::vector<bool>(n, false), {}}; RET(u);
}
HLE(sceKernelDeleteFpl) { auto it = g_fpl.find(ARG(0)); if (it != g_fpl.end()) { k_free(it->second.base); g_fpl.erase(it); } RET(0); }
HLE(sceKernelAllocateFpl) {   /* (uid, void **data, timeout*) */
    auto it = g_fpl.find(ARG(0)); if (it == g_fpl.end()) { RET(KERR_UNKNOWN_UID); return; }
    Fpl &f = it->second;
    for (;;) {
        for (uint32_t i = 0; i < f.n; i++) if (!f.used[i]) { f.used[i] = true; wr32(ARG(1), f.base + i * f.bsize); RET(0); return; }
        bool has; uint32_t to = timeout_of(ARG(2), has);
        g_cur->wk = W_MBX; g_cur->wobj = ARG(0); f.q.push_back(g_cur); k_block_current(to, has);
        if (g_cur->result) { RET(g_cur->result); return; }
    }
}
HLE(sceKernelFreeFpl) {
    auto it = g_fpl.find(ARG(0)); if (it == g_fpl.end()) { RET(KERR_UNKNOWN_UID); return; }
    Fpl &f = it->second; uint32_t i = (ARG(1) - f.base) / f.bsize; if (i < f.n) f.used[i] = false;
    while (!f.q.empty()) { Thread *t = f.q.front(); f.q.pop_front(); if (t->st == WAITING && t->wk == W_MBX) { make_ready(t, 0); break; } }
    RET(0); preempt_check();
}
static std::map<uint32_t, uint32_t> g_vpl_size;      /* uid -> tamanho do pool (nao restringimos; so registro) */
static std::map<uint32_t, uint32_t> g_vpl_blocks;    /* bloco alocado por um VPL -> uid do pool */
HLE(sceKernelCreateVpl) { uint32_t u = new_uid(); g_vpl_size[u] = ARG(3); RET(u); }
HLE(sceKernelDeleteVpl) { g_vpl_size.erase(ARG(0)); RET(0); }
static void vpl_alloc(CPU *c) {   /* (uid, size, void **data, timeout*) */
    if (!g_vpl_size.count(ARG(0))) { RET(KERR_UNKNOWN_UID); return; }
    uint32_t at = k_alloc(ARG(1) + 8, 0, 0, "vpl");
    if (!at) { RET(0x800200d9); return; }
    g_vpl_blocks[at] = ARG(0); wr32(ARG(2), at); RET(0);
}
HLE(sceKernelTryAllocateVpl) { vpl_alloc(c); }
HLE(sceKernelFreeVpl) {   /* (uid, data): antes liberava ARG(2) (lixo em a2: vazava o bloco e podia liberar outro) */
    auto it = g_vpl_blocks.find(ARG(1)); if (it == g_vpl_blocks.end()) { RET(0x800201b4u); return; }   /* SCE_KERNEL_ERROR_ILLEGAL_MEMBLOCK */
    k_free(it->first); g_vpl_blocks.erase(it); RET(0);
}
struct Mbx { std::deque<uint32_t> msgs; };
static std::map<uint32_t, Mbx> g_mbx;
HLE(sceKernelCreateMbx) { uint32_t u = new_uid(); g_mbx[u] = Mbx{}; RET(u); }
HLE(sceKernelDeleteMbx) { g_mbx.erase(ARG(0)); RET(0); }
HLE(sceKernelSendMbx) { auto it = g_mbx.find(ARG(0)); if (it == g_mbx.end()) { RET(KERR_UNKNOWN_UID); return; } it->second.msgs.push_back(ARG(1)); RET(0); }
HLE(sceKernelPollMbx) {
    auto it = g_mbx.find(ARG(0)); if (it == g_mbx.end()) { RET(KERR_UNKNOWN_UID); return; }
    if (it->second.msgs.empty()) { RET(0x800201a9u); return; }
    wr32(ARG(1), it->second.msgs.front()); it->second.msgs.pop_front(); RET(0);
}
HLE(sceKernelCreateCallback) { RET(new_uid()); }
HLE(sceKernelCheckCallback) { RET(0); }
HLE(sceKernelRegisterExitCallback) { RET(0); }
void display_quit(void);
HLE(sceKernelExitGame) { fprintf(stderr, "[run] sceKernelExitGame\n"); display_quit(); }   /* mesmo caminho de fechar a janela: resumo [perf], log completo, gravacoes pendentes */

/* ------------------------------------------------------------------ callbacks de "interrupcao" (GE, vblank...) */
static CPU g_intr; static bool g_intr_init;
uint32_t k_call_guest(uint32_t addr, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) {
    if (!g_intr_init) {
        memset(&g_intr, 0, sizeof g_intr); g_intr.mem = g_mem; cpu_reset_vfpu(&g_intr);
        uint32_t stk = k_alloc(0x8000, 1, 0, "intr-stack"); g_intr.r[29] = stk + 0x8000 - 0x40;
        g_intr.r[28] = g_mods.empty() ? 0 : g_mods[0].gp; g_intr.r[26] = k_alloc(0x100, 1, 0, "intr-tls"); g_intr_init = true;
    }
    /* aninhamento: uma rotina chamada daqui pode consultar o relogio (time_poll) e disparar o vblank, que chama outra rotina por aqui de novo,
     * no MESMO conjunto de registradores. Como numa interrupcao de verdade, a chamada interna salva e restaura o estado inteiro (antes so sp/ra:
     * s0-s7, v0 etc. da rotina externa, onde o codigo recompilado guarda as variaveis, voltavam corrompidos). A pilha interna comeca abaixo
     * da externa (sp atual). */
    static int depth;
    const bool nested = depth > 0;
    static std::vector<CPU> saved; if (nested) { saved.push_back(g_intr); static bool told; if (!told) { told = true; fprintf(stderr, "[kernel] rotina do jogo chamada dentro de outra (aninhada): estado salvo e restaurado\n"); } }
    const uint32_t sp = g_intr.r[29], ra = g_intr.r[31];
    g_intr.r[4] = a0; g_intr.r[5] = a1; g_intr.r[6] = a2; g_intr.r[7] = a3;
    depth++; psp_call(&g_intr, addr); depth--;
    const uint32_t ret = g_intr.r[2];
    if (nested) { g_intr = saved.back(); saved.pop_back(); }
    else { g_intr.r[29] = sp; g_intr.r[31] = ra; }
    return ret;
}
void k_unregister_module(uint32_t uid) {
    for (size_t i = 0; i < g_mods.size(); i++) if (g_mods[i].uid == uid) { g_mods.erase(g_mods.begin() + i); return; }
}
void k_delay_current(uint64_t us) { g_cur->wk = W_DELAY; k_block_current(us, true); }

extern "C" { extern uint32_t g_trace[1024]; extern uint32_t g_trace_i; }
void k_dump_threads(void) {
    static const char *stn[] = {"DORMANT", "READY", "RUNNING", "WAITING", "DEAD"};
    static const char *wkn[] = {"-", "DELAY", "SLEEP", "SEMA", "EVF", "VBLANK", "THREADEND", "MBX"};
    fprintf(stderr, "---- threads (t=%llu us)\n", (unsigned long long)g_now);
    for (auto *t : g_all)
        fprintf(stderr, "  %-14s uid=%x prio=%u %s%s wait=%s obj=%x need=%x mode=%x to=%d%s\n", t->name.c_str(), t->uid, t->prio, stn[t->st], t->suspended ? "+SUSP" : "", wkn[t->wk], t->wobj, t->need, t->mode, t->has_to ? (int)(t->wake_at - g_now) : -1, t->wake_pending ? " wakeup-pending" : "");
    for (auto *t : g_all) fprintf(stderr, "  %-14s ra=%08x sp=%08x r4=%08x r5=%08x\n", t->name.c_str(), t->cpu.r[31], t->cpu.r[29], t->cpu.r[4], t->cpu.r[5]);
}


CPU *k_current_cpu(void) { return g_cur ? &g_cur->cpu : nullptr; }
