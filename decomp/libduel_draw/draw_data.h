/* libduel_draw: the duel's presentation (the field, cards, effects, HUD). This module reads the rules engine's state (libduel_eng) and the
 * duel's scene data; it owns almost no rules. Typed names for what the swaps in this directory touch. */
#pragma once
#include "../common/swap.h"
#include <stdint.h>

namespace duel_draw {

constexpr uint32_t kModuleBase = 0x09A15300;
constexpr uint32_t offsetOf(uint32_t address) { return address - kModuleBase; }

/* Drawing options kept as bits in one global word; SetDrawFlags changes and queries them. */
constexpr uint32_t kDrawFlagsWord = 0x09E4CAE4;

/* The two screen sides (0 and 1) map to the engine's player index through this table. The viewer's side is not always player 0. */
constexpr uint32_t kSideToDuelPlayer = 0x09E6B6A8;
/* A word the drawing code tests for "not zero"; its meaning is not mapped yet. */
constexpr uint32_t kDrawStateWord09E6B6F4 = 0x09E6B6F4;

/* Per-slot scene records: 0xB00 bytes per player, 0xB0 bytes per slot, starting at kSceneRecords + kSceneRecordsFieldOffset. Slots 0xB..0xD
 * are shared "special" records, found through the player's entry in kSpecialSlotPlayerIndex. */
constexpr uint32_t kSceneRecords = 0x09E4D660;
constexpr uint32_t kSceneRecordPlayerStride = 0xB00;
constexpr uint32_t kSceneRecordSlotStride = 0xB0;
constexpr uint32_t kSceneRecordsFieldOffset = 0x13CF0;
constexpr uint32_t kSpecialSlotPlayerIndex = 0x09E63F54;
constexpr uint32_t kSpecialSlotFirst = 0xB, kSpecialSlotLast = 0xD;

/* Duel state that libduel_eng exposes through a getter. */
constexpr uint32_t kNeutralTurnPlayer = 2;   /* the engine's turn-player value when nobody is the turn player (guess) */

/* Exports of libduel_eng that the drawing code calls (by NID; names are what the function returns or does). */
namespace eng_export {
constexpr uint32_t IsDuelTargetAllowed = 0x331C6B94;
constexpr uint32_t GetPlayerWord14 = 0x5150628D;       /* word at +0x14 of the player's duel record */
constexpr uint32_t GetPlayerWord1C = 0xC16BC388;       /* word at +0x1C of the player's duel record */
constexpr uint32_t IsPlayerActing = 0xDD6E593F;        /* true while the player can act in the current phase (guess) */
constexpr uint32_t GetTurnPlayer = 0x488D2E16;
}

namespace fn {
constexpr uint32_t SetDrawFlags = offsetOf(0x09A1779C);
constexpr uint32_t FindSortedTableIndex = offsetOf(0x09A16494);
constexpr uint32_t GetSceneRecord = offsetOf(0x09A21638);
constexpr uint32_t GetSideWord14 = offsetOf(0x09A76514);
constexpr uint32_t GetSideWord1C = offsetOf(0x09A7652C);
constexpr uint32_t IsSideTargetAllowed = offsetOf(0x09A76808);
constexpr uint32_t IsSideActing = offsetOf(0x09A76644);
constexpr uint32_t IsDrawStateWordSet = offsetOf(0x09A7AB70);
constexpr uint32_t GetNeutralTurnCode = offsetOf(0x09A765A4);
}

}  // namespace duel_draw
