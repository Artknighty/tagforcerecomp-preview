/* libstory: loading and unloading the modules of the places (the field, the shop, the deck editor...) and the sound scenes that go with them. The story runs
 * one place module at a time; `SwitchModule` unloads the one that is loaded and starts the one asked for. */
#include "../common/swap.h"
#include "../common/ehsys.h"
#include "story_data.h"
#include "story_functions.h"

#define MODULE "libstory"

namespace {
using namespace story;

constexpr uint32_t kModuleState = 0x091C44F4;          /* ModuleState */
constexpr uint32_t kModuleTable = 0x08F160F0;          /* ModuleEntry[12] */
constexpr uint32_t kSoundState = 0x091C4530;           /* -> {sound scene started, sound scene to stop} */
constexpr int32_t kModuleCount = 12, kFieldModule = 0, kShopModule = 6, kCardModule = 11;   /* the modules that are treated apart when switching */
constexpr uint32_t kModuleQueueKind = 2, kModuleStartArgumentBytes = 4, kStopTimeFrames = 0x5DC, kEmptySoundScene = 0xFFFFFFFFu;
constexpr uint32_t kModuleLoadedCallback = offsetOf(0x08F10188);   /* what the loader runs when the file is in memory */

/* The state of the loaded modules (0x38 bytes at kModuleState). */
struct ModuleState {
    int32_t current;           /* +0x00 the module being loaded or just loaded */
    int32_t moduleId[kModuleCount];   /* +0x04 the kernel module id of each place module, 0 while it is not loaded */
    uint32_t handler;          /* +0x34 the function the story runs every frame for the loaded module (0x091C4528) */
};
static_assert(sizeof(ModuleState) == 0x38, "ModuleState layout");

/* One place module of the table: its file, the function to run once it is loaded, the handler and the function that unloads it. */
struct ModuleEntry {
    uint32_t fileName;         /* +0x00 guest address of the file name ("libfield.prx" ...) */
    uint32_t onLoaded;         /* +0x04 */
    uint32_t handler;          /* +0x08 */
    uint32_t onUnload;         /* +0x0C */
};
static_assert(sizeof(ModuleEntry) == 16, "ModuleEntry layout");

ModuleState &moduleState(SwapCtx &x) { return datum<ModuleState>(x, kModuleState); }
ModuleEntry *moduleTable(SwapCtx &x) { return (ModuleEntry *)x.mem(guest(x, kModuleTable)); }
int32_t *soundState(SwapCtx &x) { return (int32_t *)x.mem(datum<uint32_t>(x, kSoundState)); }
void waitFrame(SwapCtx &x) { x.callNid(ehsys::nid::WaitFrame); }
void waitWhileBusy(SwapCtx &x) { while (x.callNid(ehsys::nid::PendingLoadsA) != 0) waitFrame(x); }

/* ---- FUN_08f10558: stops the sound scene that is playing: waits for the loads, stops it, unloads it ---- */
void stopSoundScene(SwapCtx &x) {
    int32_t *sound = soundState(x);
    if (sound[1] == -1) return;
    x.callNid(ehsys::nid::SetAttractTimer, kStopTimeFrames);
    waitWhileBusy(x);
    x.callNid(ehsys::nid::StopSoundScene, (uint32_t)sound[1]);
    waitWhileBusy(x);
    x.callNid(ehsys::nid::UnloadSoundScene, (uint32_t)sound[1]);
    while (x.callNid(ehsys::nid::IsSoundSceneBusy, (uint32_t)sound[1]) != 0) waitFrame(x);
    sound[1] = -1;
}
void swap_StopSoundScene(SwapCtx &x) { stopSoundScene(x); }
SwapReg reg_StopSoundScene({"libstory.StopSoundScene", MODULE, offsetOf(0x08F10558), swap_StopSoundScene});

/* ---- FUN_08f1044c(scene) / FUN_08f10634: starts a sound scene (the second always plays scene 0x18), first stopping the one that plays ---- */
void startSoundScene(SwapCtx &x, uint32_t scene) {
    int32_t *sound = soundState(x);
    if (sound[0] != -1) { x.callNid(ehsys::nid::FadeOutSound); stopSoundScene(x); }
    x.callNid(ehsys::nid::StartSoundScene, scene);
    while (x.callNid(ehsys::nid::IsSoundSceneBusy, scene) != 0) waitFrame(x);
    sound[0] = (int32_t)scene;
    sound[1] = (int32_t)scene;
    x.callNid(ehsys::nid::StopSoundChannel, scene);
}
void swap_StartSoundScene(SwapCtx &x) { startSoundScene(x, x.arg(0)); }
SwapReg reg_StartSoundScene({"libstory.StartSoundScene", MODULE, offsetOf(0x08F1044C), swap_StartSoundScene});
constexpr uint32_t kMapSoundScene = 0x18;
void swap_StartMapSoundScene(SwapCtx &x) { startSoundScene(x, kMapSoundScene); }
SwapReg reg_StartMapSoundScene({"libstory.StartMapSoundScene", MODULE, offsetOf(0x08F10634), swap_StartMapSoundScene});

/* ---- FUN_08f104d8: waits until every load is done, then forgets the sound scene to stop ---- */
void swap_FinishSoundLoads(SwapCtx &x) {
    int32_t *sound = soundState(x);
    x.callNid(ehsys::nid::SetLoadWaitTime, kStopTimeFrames);
    while (x.callNid(ehsys::nid::PendingLoadsA) + x.callNid(ehsys::nid::PendingLoadsB) != 0) waitFrame(x);
    sound[1] = (int32_t)kEmptySoundScene;
    x.callNid(ehsys::nid::EndLoadWait);
}
SwapReg reg_FinishSoundLoads({"libstory.FinishSoundLoads", MODULE, offsetOf(0x08F104D8), swap_FinishSoundLoads});

/* ---- FUN_08f10398: starts the seven sound scenes of the story's voices ---- */
void swap_StartVoiceScenes(SwapCtx &x) {
    for (uint32_t scene : {0x14A, 0x14B, 0x14D, 0x14E, 0x14F, 0x148, 0x149}) x.callNid(ehsys::nid::StartSoundScene, scene);
}
SwapReg reg_StartVoiceScenes({"libstory.StartVoiceScenes", MODULE, offsetOf(0x08F10398), swap_StartVoiceScenes});

/* ---- FUN_08f0ff1c(module, file, onLoaded, handler): loads and starts a place module when no other is loaded ---- */
void swap_LoadModule(SwapCtx &x) {
    const int32_t module = (int32_t)x.arg(0);
    const uint32_t fileName = x.arg(1), onLoaded = x.arg(2), handler = x.arg(3);
    uint32_t startArgument = 1;
    ModuleState &state = moduleState(x);
    if (module < 0 || module >= kModuleCount || state.moduleId[module] > 0) return;
    for (int32_t other = 0; other < kModuleCount; other++) if (x.callLocal(offsetOf(0x08F10154), (uint32_t)other) != 0) return;
    state.current = module;
    x.callNid(ehsys::nid::QueueLoad, kModuleQueueKind, fileName, x.addr(kModuleLoadedCallback), 0);
    while (state.moduleId[state.current] == 0) waitFrame(x);
    GuestStack argument(x, 8);
    x.at<uint32_t>(argument.addr()) = startArgument;
    const int32_t started = (int32_t)x.callImport("sceKernelStartModule", (uint32_t)state.moduleId[state.current], kModuleStartArgumentBytes, argument.addr(), 0, 0);
    if (started < 0) return;
    if (onLoaded != 0) x.callAddr(onLoaded);
    if (handler != 0) state.handler = handler;
}
SwapReg reg_LoadModule({"libstory.LoadModule", MODULE, offsetOf(0x08F0FF1C), swap_LoadModule});

/* ---- FUN_08f085e4(module, load): load = 0 unloads the module; otherwise unloads the other place modules that are loaded (stopping the sound unless
 * the switch is between the field, the shop and the card module) and loads this one ---- */
void swap_SwitchModule(SwapCtx &x) {
    const int32_t module = (int32_t)x.arg(0);
    if (x.arg(1) == 0) { x.callLocal(offsetOf(0x08F100A4), (uint32_t)module, moduleTable(x)[module].onUnload); return; }
    for (int32_t other = 0; other < kModuleCount; other++) {
        if (module == other || x.callLocal(offsetOf(0x08F10154), (uint32_t)other) == 0) continue;
        if (module != kCardModule && module != kShopModule) {
            if (module == kFieldModule) { if (other != kCardModule && other != kShopModule) stopSoundScene(x); }
            else stopSoundScene(x);
        }
        x.callLocal(offsetOf(0x08F100A4), (uint32_t)other, moduleTable(x)[other].onUnload);
    }
    const ModuleEntry &entry = moduleTable(x)[module];
    x.callLocal(offsetOf(0x08F0FF1C), (uint32_t)module, entry.fileName, entry.onLoaded, entry.handler);
}
SwapReg reg_SwitchModule({"libstory.SwitchModule", MODULE, offsetOf(0x08F085E4), swap_SwitchModule});

/* ---- FUN_08f08704 / FUN_08f08730: the field module's entry points the story calls: when the field starts, and every frame ---- */
constexpr uint32_t kFieldStart = 0xE92DC2F2, kFieldFrameA = 0xFABFD3C6, kFieldFrameB = 0x74B014A2;   /* libfield exports (guess) */
void swap_FieldStart(SwapCtx &x) { x.callNid(kFieldStart); waitFrame(x); ehsys::SetScreenOverlay(x, true); }
SwapReg reg_FieldStart({"libstory.FieldStart", MODULE, offsetOf(0x08F08704), swap_FieldStart});
void swap_FieldFrame(SwapCtx &x) { x.callNid(kFieldFrameA); x.callNid(kFieldFrameB); }
SwapReg reg_FieldFrame({"libstory.FieldFrame", MODULE, offsetOf(0x08F08730), swap_FieldFrame});

}  // namespace
