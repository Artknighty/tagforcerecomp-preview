/* Host coroutines for the PSP threads (they replaced the Windows fibers). Every PSP thread runs the recompiled C code on its own host stack; a state
 * save (savestate.cpp) has to copy those stacks and put them back at the SAME addresses, which fibers cannot do (the OS keeps their registers).
 * Here the stack is a plain VirtualAlloc block that never moves, and a switch is a few instructions that push the callee-saved registers on the
 * stack being left and store its stack pointer: "the state of a switched-out coroutine" is just [sp, top) of its stack plus Coro::sp. */
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef void (*CoroFn)(void *);
struct Coro {
    uint8_t *base = nullptr;   /* start of the reserved block; the first page is a PAGE_NOACCESS guard (stack overflow = crash report, not silent corruption) */
    size_t size = 0;
    void *sp = nullptr;        /* saved stack pointer while the coroutine is switched out */
    bool committed = false;
    uint8_t *top() const { return base + size; }
};
bool coro_create(Coro &c, CoroFn fn, void *arg);   /* gets a stack if there is none (an old one is reused) and prepares fn(arg) to start at the first switch to it */
void coro_release(Coro &c);                        /* gives the memory back but keeps the address range, so a saved state can bring the stack back at the same place */
bool coro_recommit(Coro &c);                       /* makes a released stack usable again (contents are zero) */
extern "C" void tf_coro_switch(void **save_sp, void *to_sp);   /* saves the current context in *save_sp and continues the one stored in to_sp */
