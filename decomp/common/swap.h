/* Decomp swaps: hand-written C++ replacements for generated game functions (see docs/DECOMP.md).
 * A swap is installed as a hook on the function's entry (runtime/mods/hooks.h), so the original body stays available for comparison.
 *   TF_DECOMP=off          no swaps (original generated code only)
 *   TF_DECOMP=a,b          only the named swaps
 *   (unset / "all")        every registered swap
 *   TF_DECOMP_VERIFY=1     for swaps that declare what they touch: run the original too and log any difference */
#pragma once
#include "../runtime/hle/hle.h"
#include <stdint.h>
#include <utility>
#include <vector>

/* Everything a swap needs: the CPU registers (arguments in r4.., result in r2), the module it belongs to, and calls into the game. */
struct SwapCtx {
    CPU *c;
    const GenModule *mod;
    uint32_t base;                                   /* where the module is loaded */
    uint32_t arg(int i) const { return c->r[4 + i]; }
    void ret(uint32_t v) { c->r[2] = v; }
    uint32_t addr(uint32_t offset) const { return base + offset; }
    uint8_t *mem(uint32_t guest) const { return mem_ptr(guest); }
    template <class T> T &at(uint32_t guest) const { return *(T *)mem_ptr(guest); }
    /* Calls into the game with up to 8 arguments; the result is r2. */
    uint32_t callNid(uint32_t nid, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0, uint32_t a4 = 0, uint32_t a5 = 0, uint32_t a6 = 0, uint32_t a7 = 0);          /* a function of another module by NID (e.g. ehsys in BOOT) */
    uint32_t callNidKeepRegs(uint32_t nid);   /* same, but leaves r4..r11 as the previous call left them (the original code passes whatever is there to functions Ghidra shows without parameters) */
    uint32_t callAddr(uint32_t guestAddress, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0, uint32_t a4 = 0, uint32_t a5 = 0, uint32_t a6 = 0, uint32_t a7 = 0);   /* an indirect call through a guest function pointer */
    uint32_t callImport(const char *name, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0, uint32_t a4 = 0, uint32_t a5 = 0, uint32_t a6 = 0, uint32_t a7 = 0);   /* an sce* / libc import of this module, by name */
    uint32_t callLocal(uint32_t offset, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0, uint32_t a4 = 0, uint32_t a5 = 0, uint32_t a6 = 0, uint32_t a7 = 0);     /* a function of this module not swapped yet */
};

/* Scratch space on the guest stack, below the caller's frame, for the lifetime of the object. */
class GuestStack {
public:
    GuestStack(SwapCtx &x, uint32_t bytes) : c_(x.c), bytes_(bytes) { c_->r[29] -= bytes_; }
    ~GuestStack() { c_->r[29] += bytes_; }
    uint32_t addr() const { return c_->r[29]; }
private:
    CPU *c_;
    uint32_t bytes_;
};

/* A context for calling into another Module that is loaded now (a swap of one Module that needs a helper of another); false when it is not loaded. */
bool swap_module_ctx(const char *moduleName, CPU *c, SwapCtx &out);

typedef std::vector<std::pair<uint32_t, uint32_t>> Regions;   /* guest address, length */

struct Swap {
    const char *name;     /* unique, e.g. "libduelrecord.FindEntryIndex" */
    const char *module;   /* generated module name, e.g. "libduelrecord_08eeb400" */
    uint32_t offset;      /* function address relative to the module base */
    void (*fn)(SwapCtx &x);
    /* TF_DECOMP_VERIFY support. pure: no side effects, only r2 is compared. regions: guest memory the function may write, listed at
     * entry; memory is restored before the original runs and both results are compared. Neither: not verifiable (kernel objects etc). */
    bool pure = false;
    void (*regions)(SwapCtx &x, Regions &out) = nullptr;
};
void decomp_register(const Swap &s);
void decomp_init(void);   /* call once the module table is available */
struct SwapReg { SwapReg(const Swap &s) { decomp_register(s); } };
