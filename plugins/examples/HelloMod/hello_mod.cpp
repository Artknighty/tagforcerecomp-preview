/* Hello Mod: the smallest useful behavior mod. Copy this folder to plugins/src/MyMod, rename it, and build with
 * `sh tools/build/build_plugins.sh MyMod` (the result is gen/mods/MyMod/plugin.dll). It shows the three things almost every mod does:
 *   1. check that the runtime is new enough (TFMOD_REQUIRE),
 *   2. hook a game function by its PSP address (here the one called once per frame, see docs/HOOKS.md),
 *   3. read game memory and show something to the player (a message box and a log line).
 * Hooks run on the game thread, inside the frame: never block or wait. */
#include "tfmod.h"

static const TfModApi *A;
static const uint32_t F_FRAME = 0x08833fc4;   /* called once per frame (docs/HOOKS.md) */

static int hk_frame(TfCpu *c) {
    (void)c;
    const uint64_t f = A->frame();
    if (f < 180) {   /* the first three seconds: a message box over the game (call it every frame it should be visible) */
        const wchar_t *lines[] = {L"Hello from a mod!", L"Edit plugins/src/MyMod/hello_mod.cpp"};
        A->message_box(L"Hello Mod", lines, 2, nullptr);
    }
    if (f % 600 == 0) A->log("frame %llu, first word of PSP RAM = %08x", (unsigned long long)f, *(uint32_t *)A->mem(0x08800000));
    return 0;   /* 0 = let the original function run next (we only watched); 1 = handled, the original is skipped */
}

extern "C" __declspec(dllexport) int tfmod_init(const TfModApi *api) {
    A = api;
    TFMOD_REQUIRE(api, 6);   /* refuses to load (and says why in the log) on a runtime older than API 6 */
    A->hook(F_FRAME, hk_frame);
    A->log("Hello Mod loaded (API %u)", api->version);
    return 0;
}
/* Optional: export `const char *tfmod_game_ids = "ULES-00600";` (comma list) when the hook addresses are valid for specific game versions only. */
extern "C" __declspec(dllexport) const char *tfmod_game_ids = "ULES-00600";
