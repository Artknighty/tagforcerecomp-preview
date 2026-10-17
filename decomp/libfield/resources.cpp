/* libfield: its heap, the field UI's cached sprites, and sounds that wait for resources to load. */
#include "../swap.h"
#include "../ehsys.h"
#include "field_data.h"

#define MODULE "libfield"

namespace {
using namespace field;

/* ---- FUN_091cc4f0(bytes): allocates from the module's shared heap ---- */
void swap_Allocate(SwapCtx &x) {
    const uint32_t heap = *view<uint32_t>(x, kFieldHeapHandle);
    x.callNid(ehsys::nid::Alloc, heap, x.arg(0));
}
SwapReg reg_Allocate({"libfield.Allocate", MODULE, offsetOf(0x091CC4F0), swap_Allocate});

/* ---- FUN_091cc500(block): releases a block from that heap ---- */
void swap_Release(SwapCtx &x) {
    const uint32_t heap = *view<uint32_t>(x, kFieldHeapHandle);
    x.callNid(ehsys::nid::Free, heap, x.arg(0));
}
SwapReg reg_Release({"libfield.Release", MODULE, offsetOf(0x091CC500), swap_Release});

/* ---- FUN_091cc510(): returns the module's heap handle ---- */
void swap_HeapHandle(SwapCtx &x) { x.ret(*view<uint32_t>(x, kFieldHeapHandle)); }
SwapReg reg_HeapHandle({"libfield.HeapHandle", MODULE, offsetOf(0x091CC510), swap_HeapHandle});

/* ---- FUN_091cc750(index): looks up a field UI sprite in story_sys01.gim and caches its handle ---- */
void swap_SystemSprite(SwapCtx &x) {
    const uint32_t archive = *view<uint32_t>(x, kSystemSpriteArchive);
    if (archive == 0) { x.ret(0); return; }

    /* The original indexes this fixed table directly; there is no bounds check. */
    const uint32_t index = x.arg(0);
    SystemSpriteRecord &record = *view<SystemSpriteRecord>(x, guest(x, kSystemSpriteRecords) + index * sizeof(SystemSpriteRecord));
    if (record.texture == 0) record.texture = x.callNid(ehsys::nid::FindTexture, archive, record.name, 0);
    x.ret(record.texture);
}
SwapReg reg_SystemSprite({"libfield.SystemSprite", MODULE, offsetOf(0x091CC750), swap_SystemSprite});

/* ---- FUN_091cf828(sound, loadMode): plays a sound only when the field's audio gate is open and its requested loads are ready ---- */
void swap_PlaySoundWhenReady(SwapCtx &x) {
    const uint32_t sound = x.arg(0);
    const uint32_t loadMode = x.arg(1);
    if (fieldState(x).flag0C == 0) { x.ret(0); return; }

    bool ready = true;
    if (loadMode == 2) {
        x.callNid(ehsys::nid::WaitForLoadGroupA);
    } else if (loadMode == 1) {
        ready = x.callNid(ehsys::nid::PendingLoadsB) == 0;
    }
    x.ret(ready ? x.callNid(ehsys::nid::PlaySound, sound) : 0);
}
SwapReg reg_PlaySoundWhenReady({"libfield.PlaySoundWhenReady", MODULE, offsetOf(0x091CF828), swap_PlaySoundWhenReady});

}  // namespace
