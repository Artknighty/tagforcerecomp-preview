/* libduel_draw: small helpers the drawing code calls all the time (flag word, sorted-table lookup, scene-record addresses, thin wrappers over
 * libduel_eng getters). */
#include "../common/swap.h"
#include "draw_data.h"

#define MODULE "libduel_draw"

namespace {
using namespace duel_draw;

/* The engine player index behind a screen side (0 or 1). */
uint32_t duelPlayerOfSide(SwapCtx &x, int32_t side) {
    return x.at<uint32_t>(kSideToDuelPlayer + (uint32_t)side * 4u);
}

/* Changes and queries the drawing option bits: mode 0 clears `mask`, mode > 0 sets it, mode < 0 only queries. Returns the bits of `mask`
 * that were set before the call. */
uint32_t setDrawFlags(SwapCtx &x, uint32_t mask, int32_t mode) {
    uint32_t &flags = x.at<uint32_t>(kDrawFlagsWord);
    const uint32_t before = flags & mask;
    if (mode == 0) flags &= ~mask;
    else if (mode > 0) flags |= mask;
    return before;
}

/* Binary search in a table of records sorted by their first word (unsigned). The table has `count` records of `strideBytes` bytes each.
 * Returns the index of the record whose key equals `key`, or -1. (The game's search treats the records as 1-based internally.) */
int32_t findSortedTableIndex(SwapCtx &x, uint32_t key, uint32_t table, int32_t count, int32_t strideBytes) {
    const int32_t strideWords = strideBytes / 4;
    int32_t low = 0, high = count + 1;
    int32_t mid = high / 2;
    while (low < mid) {
        const uint32_t recordKey = x.at<uint32_t>(table + (uint32_t)((mid - 1) * strideWords * 4));
        if (recordKey == key) return mid - 1;
        if (key > recordKey) low = mid;
        else high = mid;
        mid = (low + high) / 2;
    }
    return -1;
}

/* Address of the scene record of one slot of one player. The three special slots are shared: they live in the record block of the player
 * named by kSpecialSlotPlayerIndex. */
uint32_t sceneRecordAddress(SwapCtx &x, int32_t player, int32_t slot) {
    uint32_t block = (uint32_t)player;
    if (slot >= (int32_t)kSpecialSlotFirst && slot <= (int32_t)kSpecialSlotLast) block = x.at<uint32_t>(kSpecialSlotPlayerIndex + (uint32_t)player * 4u);
    return kSceneRecords + block * kSceneRecordPlayerStride + (uint32_t)slot * kSceneRecordSlotStride + kSceneRecordsFieldOffset;
}

void drawFlagsRegions(SwapCtx &, Regions &out) { out.push_back({kDrawFlagsWord, 4}); }

/* FUN_09a1779c(mask, mode) */
void swap_SetDrawFlags(SwapCtx &x) { x.ret(setDrawFlags(x, x.arg(0), (int32_t)x.arg(1))); }
SwapReg reg_SetDrawFlags({"libduel_draw.SetDrawFlags", MODULE, fn::SetDrawFlags, swap_SetDrawFlags, false, drawFlagsRegions});

/* FUN_09a16494(key, table, count, strideBytes) */
void swap_FindSortedTableIndex(SwapCtx &x) {
    x.ret((uint32_t)findSortedTableIndex(x, x.arg(0), x.arg(1), (int32_t)x.arg(2), (int32_t)x.arg(3)));
}
SwapReg reg_FindSortedTableIndex({"libduel_draw.FindSortedTableIndex", MODULE, fn::FindSortedTableIndex, swap_FindSortedTableIndex, true});

/* FUN_09a21638(player, slot) */
void swap_GetSceneRecord(SwapCtx &x) { x.ret(sceneRecordAddress(x, (int32_t)x.arg(0), (int32_t)x.arg(1))); }
SwapReg reg_GetSceneRecord({"libduel_draw.GetSceneRecord", MODULE, fn::GetSceneRecord, swap_GetSceneRecord, true});

/* FUN_09a76514(side): the engine's word at +0x14 of that side's player record. */
void swap_GetSideWord14(SwapCtx &x) { x.ret(x.callNid(eng_export::GetPlayerWord14, duelPlayerOfSide(x, (int32_t)x.arg(0)))); }
SwapReg reg_GetSideWord14({"libduel_draw.GetSideWord14", MODULE, fn::GetSideWord14, swap_GetSideWord14, true});

/* FUN_09a7652c(side): the engine's word at +0x1C of that side's player record. */
void swap_GetSideWord1C(SwapCtx &x) { x.ret(x.callNid(eng_export::GetPlayerWord1C, duelPlayerOfSide(x, (int32_t)x.arg(0)))); }
SwapReg reg_GetSideWord1C({"libduel_draw.GetSideWord1C", MODULE, fn::GetSideWord1C, swap_GetSideWord1C, true});

/* FUN_09a76808(side) */
void swap_IsSideTargetAllowed(SwapCtx &x) { x.ret(x.callNid(eng_export::IsDuelTargetAllowed, duelPlayerOfSide(x, (int32_t)x.arg(0)))); }
SwapReg reg_IsSideTargetAllowed({"libduel_draw.IsSideTargetAllowed", MODULE, fn::IsSideTargetAllowed, swap_IsSideTargetAllowed, true});

/* FUN_09a76644(side) */
void swap_IsSideActing(SwapCtx &x) { x.ret(x.callNid(eng_export::IsPlayerActing, duelPlayerOfSide(x, (int32_t)x.arg(0)))); }
SwapReg reg_IsSideActing({"libduel_draw.IsSideActing", MODULE, fn::IsSideActing, swap_IsSideActing, true});

/* FUN_09a7ab70() */
void swap_IsDrawStateWordSet(SwapCtx &x) { x.ret(x.at<uint32_t>(kDrawStateWord09E6B6F4) != 0 ? 1u : 0u); }
SwapReg reg_IsDrawStateWordSet({"libduel_draw.IsDrawStateWordSet", MODULE, fn::IsDrawStateWordSet, swap_IsDrawStateWordSet, true});

/* FUN_09a765a4(): 2 when the engine reports the neutral turn player, else 0. */
void swap_GetNeutralTurnCode(SwapCtx &x) { x.ret(x.callNid(eng_export::GetTurnPlayer) == kNeutralTurnPlayer ? 2u : 0u); }
SwapReg reg_GetNeutralTurnCode({"libduel_draw.GetNeutralTurnCode", MODULE, fn::GetNeutralTurnCode, swap_GetNeutralTurnCode, true});

}  // namespace
