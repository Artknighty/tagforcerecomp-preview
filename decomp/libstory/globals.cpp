/* libstory: small functions over the module's globals: the player block, the registers, the story flags, the three heaps. */
#include "../common/swap.h"
#include "../common/ehsys.h"
#include "story_data.h"

#define MODULE "libstory"

namespace {
using namespace story;

constexpr uint32_t kPlayerPointer = 0x08F2189C;           /* -> the player's block in the save data */
constexpr uint32_t kRegisters = 0x08F218A8, kRegistersBytes = 0x298;
constexpr uint32_t kRegisterCount = 0x08F218B0, kRegisterMark = 0x08F218BC;
constexpr uint32_t kHeapBlock = 0x091C1B80, kHeapBlockBytes = 0x1A8;
constexpr uint32_t kFirstHeapStart = 0x08F81B80, kFirstHeapSize = 0x240000, kSecondHeapStart = 0x08F41B80, kSecondHeapSize = 0x40000;
constexpr uint32_t kThirdHeapStart = 0x08F21B80, kThirdHeapSize = 0x20000;
constexpr uint32_t kRegisterMarkDefault = 0x6B;

/* FUN_08efecfc: does nothing (a hook the story never fills) */
void swap_Nothing(SwapCtx &) {}
SwapReg reg_Nothing({"libstory.Nothing", MODULE, offsetOf(0x08EFECFC), swap_Nothing, true});

/* FUN_08efed70: the player's first value, 1 when it is 0 */
void swap_PlayerValue(SwapCtx &x) { const uint32_t value = x.at<uint32_t>(datum<uint32_t>(x, kPlayerPointer)); x.ret(value != 0 ? value : 1); }
SwapReg reg_PlayerValue({"libstory.PlayerValue", MODULE, offsetOf(0x08EFED70), swap_PlayerValue, true});
/* FUN_08efedf8 / 08eff008 / 08eff17c: the addresses of three fields of the player's block; FUN_08eff2a0: the word at +0x14 */
void swap_PlayerField30(SwapCtx &x) { x.ret(datum<uint32_t>(x, kPlayerPointer) + 0x30); }
SwapReg reg_PlayerField30({"libstory.PlayerField30", MODULE, offsetOf(0x08EFEDF8), swap_PlayerField30, true});
void swap_PlayerField84(SwapCtx &x) { x.ret(datum<uint32_t>(x, kPlayerPointer) + 0x84); }
SwapReg reg_PlayerField84({"libstory.PlayerField84", MODULE, offsetOf(0x08EFF008), swap_PlayerField84, true});
void swap_PlayerField2C(SwapCtx &x) { x.ret(datum<uint32_t>(x, kPlayerPointer) + 0x2C); }
SwapReg reg_PlayerField2C({"libstory.PlayerField2C", MODULE, offsetOf(0x08EFF17C), swap_PlayerField2C, true});
void swap_PlayerCharacter(SwapCtx &x) { x.ret(x.at<uint32_t>(datum<uint32_t>(x, kPlayerPointer) + 0x14)); }   /* the character the story is following, -1 for none */
SwapReg reg_PlayerCharacter({"libstory.PlayerCharacter", MODULE, offsetOf(0x08EFF2A0), swap_PlayerCharacter, true});

/* historia_registros_reset (FUN_08eff47c): the script registers start empty, at index 1, with their mark at 0x6B */
void resetRegisterCursor(SwapCtx &x) { datum<uint32_t>(x, kRegisters) = 0; datum<uint32_t>(x, kRegisterCount) = 1; datum<uint32_t>(x, kRegisterMark) = kRegisterMarkDefault; }
void regions_RegisterCursor(SwapCtx &x, Regions &out) { out.push_back({guest(x, kRegisters), 4}); out.push_back({guest(x, kRegisterCount), 4}); out.push_back({guest(x, kRegisterMark), 4}); }
void swap_ResetRegisterCursor(SwapCtx &x) { resetRegisterCursor(x); }
SwapReg reg_ResetRegisterCursor({"libstory.ResetRegisterCursor", MODULE, offsetOf(0x08EFF47C), swap_ResetRegisterCursor, false, regions_RegisterCursor});
/* FUN_08eff418: clears the whole register block, then resets its cursor */
void swap_ResetRegisters(SwapCtx &x) { x.callNid(ehsys::nid::MemSet, guest(x, kRegisters), 0, kRegistersBytes); resetRegisterCursor(x); }
void regions_Registers(SwapCtx &x, Regions &out) { out.push_back({guest(x, kRegisters), kRegistersBytes}); }
SwapReg reg_ResetRegisters({"libstory.ResetRegisters", MODULE, offsetOf(0x08EFF418), swap_ResetRegisters, false, regions_Registers});

/* FUN_08f043c0(flag): the story flags of 1 .. 32 (a bit set at +8 of the flags block); 0 and above 32 are false */
void swap_FlagBit(SwapCtx &x) {
    const uint32_t flag = x.arg(0);
    if (flag == 0 || flag >= 0x21) { x.ret(0); return; }
    const uint32_t bits = x.at<uint32_t>(datum<uint32_t>(x, kFlagsPointer) + 8);
    x.ret((bits & (1u << ((flag - 1) & 0x1F))) != 0 ? 1 : 0);
}
SwapReg reg_FlagBit({"libstory.FlagBit", MODULE, offsetOf(0x08F043C0), swap_FlagBit, true});

/* FUN_08f0494c(out): the addresses of the six numbers of the date the clock shows, in the order the field module wants them */
void swap_DateFieldPointers(SwapCtx &x) {
    const uint32_t date = datum<uint32_t>(x, kDatePointer);
    uint32_t *out = (uint32_t *)x.mem(x.arg(0));
    const uint32_t offsets[6] = {offsetof(Date, calendarMonth), offsetof(Date, dayOfMonth), offsetof(Date, weekday), offsetof(Date, hour), offsetof(Date, minute), offsetof(Date, daysLeft)};
    for (int32_t i = 0; i < 6; i++) out[i] = date + offsets[i];
}
void regions_DateFieldPointers(SwapCtx &x, Regions &out) { out.push_back({x.arg(0), 24}); }
SwapReg reg_DateFieldPointers({"libstory.DateFieldPointers", MODULE, offsetOf(0x08F0494C), swap_DateFieldPointers, false, regions_DateFieldPointers});

/* FUN_08f05128: clears the heap block and makes the three heaps */
void swap_MakeHeaps(SwapCtx &x) {
    x.callNid(ehsys::nid::MemSet, guest(x, kHeapBlock), 0, kHeapBlockBytes);
    datum<uint32_t>(x, kStoryHeaps) = x.callNid(ehsys::nid::InitHeap, kFirstHeapStart, kFirstHeapSize);
    datum<uint32_t>(x, kStoryHeaps + 4) = x.callNid(ehsys::nid::InitHeap, kSecondHeapStart, kSecondHeapSize);
    datum<uint32_t>(x, kStoryHeaps + 8) = x.callNid(ehsys::nid::InitHeap, kThirdHeapStart, kThirdHeapSize);
}
SwapReg reg_MakeHeaps({"libstory.MakeHeaps", MODULE, offsetOf(0x08F05128), swap_MakeHeaps});   /* not verifiable: it makes heaps */

}  // namespace
