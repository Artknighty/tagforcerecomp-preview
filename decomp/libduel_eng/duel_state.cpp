/* libduel_eng: tiny state predicates and copies over the duel's globals and field-card slots. */
#include "../common/swap.h"
#include "duel_data.h"

#define MODULE "libduel_eng"

namespace {
using namespace duel_eng;

/* FUN_09f49e44(dst, src): copies one 32-bit word between guest addresses. The original also leaves it in f0. */
void swap_CopyGuestWord(SwapCtx &x) {
    const uint32_t dst = x.arg(0), src = x.arg(1);
    const uint32_t value = x.at<uint32_t>(src);
    x.c->f[0].u = value;
    x.at<uint32_t>(dst) = value;
}
void regions_CopyGuestWord(SwapCtx &x, Regions &out) { out.push_back({x.arg(0), 4}); }
SwapReg reg_CopyGuestWord({"libduel_eng.CopyGuestWord", MODULE, offsetOf(0x09F49E44), swap_CopyGuestWord, false, regions_CopyGuestWord});

/* FUN_09f49e64(a, b): true when the words at the two guest addresses are equal. */
void swap_GuestWordsEqual(SwapCtx &x) {
    x.ret(x.at<uint32_t>(x.arg(0)) == x.at<uint32_t>(x.arg(1)) ? 1u : 0u);
}
SwapReg reg_GuestWordsEqual({"libduel_eng.GuestWordsEqual", MODULE, offsetOf(0x09F49E64), swap_GuestWordsEqual, true});

/* FUN_09e9c844(): true when global 7D4C8 is non-zero and global 7D4D0 is 11. */
void swap_IsGlobal7D4C8SetAndState11(SwapCtx &x) {
    x.ret(x.at<uint32_t>(kDuelGlobal7D4C8) != 0 && x.at<uint32_t>(kDuelGlobal7D4D0) == 11 ? 1u : 0u);
}
SwapReg reg_IsGlobal7D4C8SetAndState11({"libduel_eng.IsGlobal7D4C8SetAndState11", MODULE, offsetOf(0x09E9C844), swap_IsGlobal7D4C8SetAndState11, true});

/* FUN_09e76edc(record): when bit 0 of the record's byte +2 differs from global 7D460, returns whether global 7D46C is 5; else 0. */
void swap_IsRecordBitDiffersAndState5(SwapCtx &x) {
    const uint32_t bit = x.at<uint8_t>(x.arg(0) + 2) & 1u;
    const bool differs = (x.at<uint32_t>(kDuelGlobal7D460) ^ bit) != 0;
    x.ret(differs && x.at<uint32_t>(kDuelGlobal7D46C) == 5 ? 1u : 0u);
}
SwapReg reg_IsRecordBitDiffersAndState5({"libduel_eng.IsRecordBitDiffersAndState5", MODULE, offsetOf(0x09E76EDC), swap_IsRecordBitDiffersAndState5, true});

/* FUN_09eaa4f4(): true when global 7D4A8 is 6. */
void swap_IsGlobal7D4A8State6(SwapCtx &x) {
    x.ret(x.at<uint32_t>(kDuelGlobal7D4A8) == 6 ? 1u : 0u);
}
SwapReg reg_IsGlobal7D4A8State6({"libduel_eng.IsGlobal7D4A8State6", MODULE, offsetOf(0x09EAA4F4), swap_IsGlobal7D4A8State6, true});

/* FUN_09f55cdc(player): bit 1 of the word at +0x11C of the player's record. */
void swap_PlayerStateBit1(SwapCtx &x) {
    const uint32_t record = kDuelPlayerRecords + (x.arg(0) & 1u) * kPlayerRecordStride;
    x.ret((x.at<uint32_t>(record + kPlayerStateFlagsOffset) >> 1) & 1u);
}
SwapReg reg_PlayerStateBit1({"libduel_eng.PlayerStateBit1", MODULE, offsetOf(0x09F55CDC), swap_PlayerStateBit1, true});

/* FUN_09f62cd4(): address of the current item of the list at 7E00C (counter at 7E008, 8-byte items after a 4-byte header). */
void swap_CurrentListItemAddress(SwapCtx &x) {
    x.ret(x.at<uint32_t>(kCursorList) + x.at<uint32_t>(kCursorCounter) * 8u + 4u);
}
SwapReg reg_CurrentListItemAddress({"libduel_eng.CurrentListItemAddress", MODULE, offsetOf(0x09F62CD4), swap_CurrentListItemAddress, true});

/* FUN_09f62d18(): true when the counter at 7E008 has reached the list's length (the first word of the list). */
void swap_IsCursorAtListEnd(SwapCtx &x) {
    const int32_t length = x.at<int32_t>(x.at<uint32_t>(kCursorList));
    x.ret((int32_t)x.at<uint32_t>(kCursorCounter) < length ? 0u : 1u);
}
SwapReg reg_IsCursorAtListEnd({"libduel_eng.IsCursorAtListEnd", MODULE, offsetOf(0x09F62D18), swap_IsCursorAtListEnd, true});

/* FUN_09e75994(buffer): saves both players' five monster slots (100 bytes each) into the buffer. */
void swap_SaveMonsterZones(SwapCtx &x) {
    const uint32_t buffer = x.arg(0);
    x.callLocal(fn::CopyBytes, buffer, kFieldCardSlots, kMonsterZoneBytes);
    x.callLocal(fn::CopyBytes, buffer + kMonsterZoneBytes, kPlayer1FieldCardSlots, kMonsterZoneBytes);
}
SwapReg reg_SaveMonsterZones({"libduel_eng.SaveMonsterZones", MODULE, offsetOf(0x09E75994), swap_SaveMonsterZones, false,
    [](SwapCtx &x, Regions &out) { out.push_back({x.arg(0), 2 * kMonsterZoneBytes}); }});

/* FUN_09e759d8(buffer): restores both players' five monster slots from a buffer made by SaveMonsterZones. */
void swap_RestoreMonsterZones(SwapCtx &x) {
    const uint32_t buffer = x.arg(0);
    x.callLocal(fn::CopyBytes, kFieldCardSlots, buffer, kMonsterZoneBytes);
    x.callLocal(fn::CopyBytes, kPlayer1FieldCardSlots, buffer + kMonsterZoneBytes, kMonsterZoneBytes);
}
SwapReg reg_RestoreMonsterZones({"libduel_eng.RestoreMonsterZones", MODULE, offsetOf(0x09E759D8), swap_RestoreMonsterZones, false,
    [](SwapCtx &, Regions &out) { out.push_back({kFieldCardSlots, kMonsterZoneBytes}); out.push_back({kPlayer1FieldCardSlots, kMonsterZoneBytes}); }});

}  // namespace
