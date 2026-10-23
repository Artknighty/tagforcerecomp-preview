#include "common/swap.h"
#include "../runtime/mods/hooks.h"
#include "../runtime/core/gameid.h"
#include <map>
#include <set>
#include <string>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

extern uint32_t g_hook_cur;               /* address of the hook running now (host.cpp) */
bool host_module_loaded(const char *name);

namespace {
std::vector<Swap> &registry() { static std::vector<Swap> r; return r; }
struct Active { Swap s; const GenModule *mod; };
std::map<uint32_t, Active> g_active;      /* by guest address */
bool g_verify;
std::vector<std::string> g_verify_only;   /* TF_DECOMP_VERIFY_ONLY=prefix,prefix: verify only swaps whose name starts with one of these (the others run without the check) */
int g_in_original;   /* >0 while verify runs the original: no swap fires inside it, so the original runs entirely as generated */
void runOriginal(CPU *c) { g_in_original++; hook_call_original(c, g_hook_cur); g_in_original--; }

typedef std::vector<std::vector<uint8_t>> Snap;
Snap snapshot(const Regions &r) { Snap s; for (auto &p : r) s.emplace_back(mem_ptr(p.first), mem_ptr(p.first) + p.second); return s; }
void restore(const Regions &r, const Snap &s) { for (size_t i = 0; i < r.size(); i++) memcpy(mem_ptr(r[i].first), s[i].data(), s[i].size()); }

/* run the swap and the original from the same starting point and compare the result register and the declared memory regions */
void verify(SwapCtx &x, const Swap &s) {
    CPU *c = x.c;
    const CPU saved = *c;
    Regions regs; if (s.regions) { s.regions(x, regs); *c = saved; }   /* the callback may call into the game */
    const Snap before = snapshot(regs);
    /* Running a function can prime caches outside the compared regions (the first use of a character in the text engine, for
     * example), so the second run would not behave like the first. Run the swap once to prime them, then the original and the swap
     * from the same starting point, compare those two, and finish with the original so the game continues as it would have. */
    if (!regs.empty()) { s.fn(x); *c = saved; restore(regs, before); }
    runOriginal(c);
    const uint32_t origR2 = c->r[2];
    const Snap orig = snapshot(regs);
    *c = saved; restore(regs, before);
    s.fn(x);
    const uint32_t mineR2 = c->r[2];
    const Snap mine = snapshot(regs);
    if (!regs.empty()) { *c = saved; restore(regs, before); runOriginal(c); }
    else c->r[2] = origR2;
    if (origR2 != mineR2) {
        fprintf(stderr, "[decomp] MISMATCH %s: swap=%08x original=%08x (a0=%08x a1=%08x a2=%08x a3=%08x)\n", s.name, mineR2, origR2, saved.r[4], saved.r[5], saved.r[6], saved.r[7]);
        return;
    }
    for (size_t i = 0; i < regs.size(); i++) if (mine[i] != orig[i]) {
        size_t k = 0; while (mine[i][k] == orig[i][k]) k++;
        fprintf(stderr, "[decomp] MISMATCH %s: memory %08x+%zx differs (swap=%02x original=%02x)\n", s.name, regs[i].first, k, mine[i][k], orig[i][k]);
        const size_t from = k >= 16 ? k - 16 : 0, to = k + 16 < mine[i].size() ? k + 16 : mine[i].size();
        fprintf(stderr, "[decomp]   swap    :"); for (size_t j = from; j < to; j++) fprintf(stderr, " %02x", mine[i][j]);
        fprintf(stderr, "\n[decomp]   original:"); for (size_t j = from; j < to; j++) fprintf(stderr, " %02x", orig[i][j]);
        fprintf(stderr, "\n");
        return;
    }
}

static bool verifyWanted(const char *name) {
    if (g_verify_only.empty()) return true;
    for (const std::string &p : g_verify_only) if (!strncmp(name, p.c_str(), p.size())) return true;
    return false;
}

/* the same addresses are reused by other Modules loaded at the same base at other times, so only fire for this one */
bool swap_hook(CPU *c) {
    const auto it = g_active.find(g_hook_cur);
    if (it == g_active.end() || g_in_original) return false;
    const Swap &s = it->second.s;
    if (!host_module_loaded(s.module)) return false;
    static std::set<uint32_t> seen;
    if (seen.insert(g_hook_cur).second) fprintf(stderr, "[decomp] first call of %s\n", s.name);
    SwapCtx x{c, it->second.mod, it->second.mod->base};
    /* a swap behaves like the function it replaces: the registers the callee must preserve (s0-s7, gp, sp, fp, ra) come back unchanged */
    uint32_t saved[12]; for (int i = 0; i < 8; i++) saved[i] = c->r[16 + i]; for (int i = 0; i < 4; i++) saved[8 + i] = c->r[28 + i];
    if (g_verify && (s.pure || s.regions) && verifyWanted(s.name)) verify(x, s); else s.fn(x);
    for (int i = 0; i < 8; i++) c->r[16 + i] = saved[i]; for (int i = 0; i < 4; i++) c->r[28 + i] = saved[8 + i];
    return true;
}
}  // namespace

uint32_t SwapCtx::callAddr(uint32_t addr, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7) {
    c->r[4] = a0; c->r[5] = a1; c->r[6] = a2; c->r[7] = a3; c->r[8] = a4; c->r[9] = a5; c->r[10] = a6; c->r[11] = a7;
    psp_call(c, addr);
    return c->r[2];
}

/* The guest address to call for a NID: this module's own import stub, or another loaded module's export. 0 when there is none. */
static uint32_t resolveNid(const GenModule *mod, uint32_t nid) {
    for (unsigned i = 0; i < mod->nimps; i++) if (mod->imps[i].nid == nid) return mod->imps[i].stub;
    uint32_t target = 0;
    if (!host_resolve_export(nid, &target)) { fprintf(stderr, "[decomp] no import or export for nid %08x\n", nid); return 0; }
    return target;
}

uint32_t SwapCtx::callNidKeepRegs(uint32_t nid) {
    const uint32_t target = resolveNid(mod, nid);
    if (!target) return 0;
    psp_call(c, target);
    return c->r[2];
}

uint32_t SwapCtx::callNid(uint32_t nid, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7) {
    const uint32_t target = resolveNid(mod, nid);
    if (!target) return 0;
    c->r[4] = a0; c->r[5] = a1; c->r[6] = a2; c->r[7] = a3; c->r[8] = a4; c->r[9] = a5; c->r[10] = a6; c->r[11] = a7;
    psp_call(c, target);
    return c->r[2];
}
uint32_t SwapCtx::callImport(const char *name, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7) {
    for (unsigned i = 0; i < mod->nimps; i++) if (!strcmp(mod->imps[i].name, name)) return callNid(mod->imps[i].nid, a0, a1, a2, a3, a4, a5, a6, a7);
    fprintf(stderr, "[decomp] module %s has no import %s\n", mod->name, name);
    return 0;
}
uint32_t SwapCtx::callLocal(uint32_t offset, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7) {
    c->r[4] = a0; c->r[5] = a1; c->r[6] = a2; c->r[7] = a3; c->r[8] = a4; c->r[9] = a5; c->r[10] = a6; c->r[11] = a7;
    psp_call(c, base + offset);
    return c->r[2];
}

bool swap_module_ctx(const char *moduleName, CPU *c, SwapCtx &out) {
    if (!host_module_loaded(moduleName)) return false;
    unsigned count; const GenModule *modules = gen_modules(&count);
    for (unsigned i = 0; i < count; i++) if (!strcmp(modules[i].name, moduleName)) { out = SwapCtx{c, &modules[i], modules[i].base}; return true; }
    return false;
}

void decomp_register(const Swap &s) { registry().push_back(s); }

void decomp_init(void) {
    const char *e = getenv("TF_DECOMP");
    if (e && !strcmp(e, "off")) { fprintf(stderr, "[decomp] disabled (TF_DECOMP=off)\n"); return; }
    /* every swap names a function by its offset in the European build; another version has other code at those offsets */
    if (game_id_built() != "ULES-00600") { fprintf(stderr, "[decomp] disabled: the swaps are for ULES-00600, this game is %s\n", game_id_built().c_str()); return; }
    std::set<std::string> only;
    if (e && strcmp(e, "all")) for (const char *p = e; *p;) { const char *q = strchr(p, ','); std::string n(p, q ? (size_t)(q - p) : strlen(p)); if (!n.empty()) only.insert(n); if (!q) break; p = q + 1; }
    /* TF_DECOMP_SKIP=prefix,prefix: leave out every swap whose name starts with one of the prefixes (bisecting a regression) */
    std::vector<std::string> skip;
    if (const char *k = getenv("TF_DECOMP_SKIP")) for (const char *p = k; *p;) { const char *q = strchr(p, ','); std::string n(p, q ? (size_t)(q - p) : strlen(p)); if (!n.empty()) skip.push_back(n); if (!q) break; p = q + 1; }
    g_verify = getenv("TF_DECOMP_VERIFY") != nullptr;
    if (const char *k = getenv("TF_DECOMP_VERIFY_ONLY")) for (const char *p = k; *p;) { const char *q = strchr(p, ','); std::string n(p, q ? (size_t)(q - p) : strlen(p)); if (!n.empty()) g_verify_only.push_back(n); if (!q) break; p = q + 1; }
    unsigned nm; const GenModule *gm = gen_modules(&nm);
    int n = 0;
    for (const Swap &s : registry()) {
        if (!only.empty() && !only.count(s.name)) continue;
        bool skipped = false;
        for (const std::string &k : skip) if (!strncmp(s.name, k.c_str(), k.size())) skipped = true;
        if (skipped) continue;
        for (unsigned i = 0; i < nm; i++) if (!strcmp(gm[i].name, s.module)) {
            const uint32_t addr = gm[i].base + s.offset;
            g_active[addr] = {s, &gm[i]}; hook_install(addr, swap_hook); n++; break;
        }
    }
    /* coverage: how much of each module is swapped */
    std::map<std::string, int> perModule;
    for (auto &kv : g_active) perModule[kv.second.mod->name]++;
    for (auto &kv : perModule) for (unsigned i = 0; i < nm; i++) if (!strcmp(gm[i].name, kv.first.c_str()))
        fprintf(stderr, "[decomp] %s: %d of %u functions swapped\n", kv.first.c_str(), kv.second, gm[i].nfns);
    fprintf(stderr, "[decomp] %d swap(s) active%s\n", n, g_verify ? " (verify on)" : "");
}
