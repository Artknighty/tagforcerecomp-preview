/* Save states: the whole emulated machine in memory, to go back to a point of the game in a few milliseconds (tests, mods, practising a duel).
 *
 * What a state holds: scratchpad + VRAM + RAM of the PSP (34 MB), the kernel (threads with their host stacks, semaphores, event flags, pools, time,
 * memory blocks; kernel.cpp + coro.cpp), the loaded modules (host.cpp, modules.cpp), the GE registers and waiting display lists (hle_ge.cpp),
 * audio channels / SAS voices / ATRAC (hle_audio.cpp), the open files (hle_io.cpp) and the utility dialogs. What is derived from memory is rebuilt after a
 * load: the GPU frame buffers, texture caches and the 60/120 FPS interpolation history.
 *
 * Limits: a state lives in this process only (the host stacks hold pointers into the running program), so it is gone when the game closes. It is
 * refused while a movie plays (the decoder is not part of it). Files the game wrote to ms0 after the save are not rolled back. Plugin state that is not in
 * the game's memory (a mod's own caches) is not part of it either.
 *
 * It runs from the scheduler loop of kernel.cpp (k_run), where every PSP thread is parked at a system call: the only moment the host stacks are in a state
 * that can be copied and put back. */
#include "savestate.h"
#include "hle.h"
#include <windows.h>
#include <stdio.h>
#include <memory>
#include <vector>

std::shared_ptr<void> k_state_save(void);            bool k_state_load(const std::shared_ptr<void> &);
std::shared_ptr<void> host_state_save(void);         void host_state_load(const std::shared_ptr<void> &);
std::shared_ptr<void> modules_state_save(void);      void modules_state_load(const std::shared_ptr<void> &);
std::shared_ptr<void> ge_state_save(void);           void ge_state_load(const std::shared_ptr<void> &);   void ge_state_prepare(void);
std::shared_ptr<void> audio_state_save(void);        void audio_state_load(const std::shared_ptr<void> &);
std::shared_ptr<void> io_state_save(void);           void io_state_load(const std::shared_ptr<void> &);
std::shared_ptr<void> utility_state_save(void);      void utility_state_load(const std::shared_ptr<void> &);
bool mpeg_active(void);
void display_notify(const char *msg);
extern uint64_t display_frames(void);

struct Slot {
    bool used = false; uint64_t frame = 0, t_us = 0;
    std::vector<uint8_t> mem;   /* the three memory regions one after the other */
    std::shared_ptr<void> kernel, host, modules, ge, audio, io, util;
};
static Slot g_slot[SAVESTATE_SLOTS];
static volatile LONG g_req;   /* 0 = nothing; else 1 + slot * 2 + (load ? 1 : 0) */
static char g_result[160] = "";
static const struct { uint32_t addr, size; } REGION[] = {{0x00010000u, 0x4000u}, {0x04000000u, 0x200000u}, {0x08000000u, 0x2000000u}};   /* scratchpad, VRAM, RAM */

void savestate_request(int slot, bool load) { if (slot >= 0 && slot < SAVESTATE_SLOTS) InterlockedExchange(&g_req, 1 + slot * 2 + (load ? 1 : 0)); }
bool savestate_has(int slot) { return slot >= 0 && slot < SAVESTATE_SLOTS && g_slot[slot].used; }
const char *savestate_last_result(void) { return g_result; }

static void done(const char *toast, const char *fmt, int slot, double ms) {
    snprintf(g_result, sizeof g_result, fmt, slot + 1);
    fprintf(stderr, "[savestate] %s (%.0f ms)\n", g_result, ms);
    char t[96]; snprintf(t, sizeof t, "%s %d", toast, slot + 1); display_notify(t);
}
static void fail(const char *why, int slot) {
    snprintf(g_result, sizeof g_result, "ERROR slot %d: %s", slot + 1, why);
    fprintf(stderr, "[savestate] %s\n", g_result);
    char t[160]; snprintf(t, sizeof t, "State: %s", why); display_notify(t);
}
static double now_ms(void) { LARGE_INTEGER q, f; QueryPerformanceCounter(&q); QueryPerformanceFrequency(&f); return (double)q.QuadPart * 1000.0 / (double)f.QuadPart; }

static void do_save(int n) {
    if (mpeg_active()) { fail("not while a movie plays", n); return; }
    const double t0 = now_ms();
    Slot &s = g_slot[n];
    ge_state_prepare();   /* VRAM must be readable and current */
    size_t total = 0; for (auto &r : REGION) total += r.size;
    s.mem.resize(total);
    size_t off = 0; for (auto &r : REGION) { memcpy(s.mem.data() + off, g_mem + r.addr, r.size); off += r.size; }
    s.kernel = k_state_save(); s.host = host_state_save(); s.modules = modules_state_save(); s.ge = ge_state_save();
    s.audio = audio_state_save(); s.io = io_state_save(); s.util = utility_state_save();
    s.frame = display_frames(); s.t_us = k_now_us(); s.used = true;
    done("State saved to slot", "saved %d", n, now_ms() - t0);
}
static void do_load(int n) {
    Slot &s = g_slot[n];
    if (!s.used) { fail("empty slot", n); return; }
    if (mpeg_active()) { fail("not while a movie plays", n); return; }
    const double t0 = now_ms();
    ge_state_prepare();   /* so the pages the GPU renderer protects can be written */
    size_t off = 0; for (auto &r : REGION) { memcpy(g_mem + r.addr, s.mem.data() + off, r.size); off += r.size; }
    if (!k_state_load(s.kernel)) { fail("could not bring the thread stacks back", n); host_fatal(9); }   /* half-restored: nothing sensible to continue with */
    host_state_load(s.host); modules_state_load(s.modules); audio_state_load(s.audio); io_state_load(s.io); utility_state_load(s.util);
    ge_state_load(s.ge);
    done("State loaded from slot", "loaded %d", n, now_ms() - t0);
}
void savestate_service(void) {   /* kernel.cpp, between two thread slices */
    const LONG r = g_req; if (!r) return;
    InterlockedExchange(&g_req, 0);
    const int slot = (r - 1) >> 1;
    if ((r - 1) & 1) do_load(slot); else do_save(slot);
}
