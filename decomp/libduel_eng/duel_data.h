/* libduel_eng: the duel rules and state. This is a typed view of the PSP's field-card slots, not host-owned memory. */
#pragma once
#include "../swap.h"
#include <stdint.h>

namespace duel_eng {

constexpr uint32_t kModuleBase = 0x09E6B800;
constexpr uint32_t offsetOf(uint32_t address) { return address - kModuleBase; }

/* Two players' duel records are 0xFAC bytes apart. Each field zone is a 0x14-byte record. The first word combines the
 * card id with flags; the final word contains the face-up/defense flags and other state bits. */
constexpr uint32_t kFieldCardSlots = 0x09F79D1C;
constexpr uint32_t kDuelPlayerRecords = 0x09F79CEC;
constexpr uint32_t kPlayerRecordStride = 0xFAC;
constexpr uint32_t kFieldCardSlotStride = 0x14;
constexpr uint32_t kCardEffectEntries = 0x09F7C458;
constexpr uint32_t kFieldCardSuppressionMask = 0x09F7BC48;
constexpr uint32_t kTargetOverrideEnabled = 0x09F7BC44;
constexpr uint32_t kTargetOverridePlayerA = 0x09F77DA0, kTargetOverrideZoneA = 0x09F77DBC, kTargetOverrideCardA = 0x09F77E64;
constexpr uint32_t kTargetOverridePlayerB = 0x09F77DA4, kTargetOverrideZoneB = 0x09F77DC0, kTargetOverrideCardB = 0x09F77E78;
constexpr uint32_t kMonsterZonePriorityOrder = 0x09F75C30;
constexpr uint32_t kPersistentEffectHeadOffset = 0x116;
constexpr uint32_t kTargetRestrictionCountOffset = 0x01C;
constexpr uint32_t kTargetRestrictionListOffset = 0x3F0;
constexpr int32_t kMonsterZoneFirst = 0, kMonsterZoneEnd = 5;
constexpr int32_t kSpellTrapZoneFirst = 5, kSpellTrapZoneEnd = 10, kFieldSpellZone = 10;
constexpr int32_t kFieldZoneCount = 11;
constexpr uint32_t kMonsterZoneLimitCardId = 0x16DF;

struct FieldCardSlot {
    uint16_t cardIdAndFlags; /* +0x00; low 13 bits are the card id */
    uint16_t unknown02;
    uint16_t unknown04;
    uint16_t unknown06;
    uint16_t unknown08;
    uint16_t effectListHead; /* +0x0A; index of the first entry in this field card's effect list */
    uint32_t unknown0C;
    uint32_t stateFlags;     /* +0x10; bit 7 = face up, bit 3 = defense; other bits still being mapped */
};
static_assert(sizeof(FieldCardSlot) == kFieldCardSlotStride, "duel field-card slot layout");

/* Entries are linked by 16-bit indices; 0 is the end of the list. Only kinds 0..5 are considered by FindCardEffectEntry. */
struct CardEffectEntry {
    uint16_t effectCode;     /* +0x00 */
    uint8_t kindAndFlags;    /* +0x02; low nibble is the entry kind */
    uint8_t unknown03;
    uint16_t value;          /* +0x04; returned by GetFieldCardEffectValue */
    uint16_t next;           /* +0x06; next entry index */
};
static_assert(sizeof(CardEffectEntry) == 8, "duel card-effect entry layout");

inline FieldCardSlot &fieldCardSlot(SwapCtx &x, uint32_t player, int32_t slot) {
    const uint32_t address = kFieldCardSlots + (player & 1u) * kPlayerRecordStride + (uint32_t)slot * kFieldCardSlotStride;
    return x.at<FieldCardSlot>(address);
}

namespace fn {
constexpr uint32_t ReadCardSlotFlag9 = offsetOf(0x09F49CA0);
constexpr uint32_t CardHasAttribute = offsetOf(0x09F49E18);
constexpr uint32_t IsFieldCardCategoryInEffectSet = offsetOf(0x09F49CD8);
constexpr uint32_t CountActiveFieldCardsWithId = offsetOf(0x09F4C704);
constexpr uint32_t CountOtherActiveFieldCardsWithId = offsetOf(0x09F4C490);
constexpr uint32_t CountActiveCardIdOnAllowedSides = offsetOf(0x09F4C984);
constexpr uint32_t CountAvailableMonsterZones = offsetOf(0x09F4E3B0);
constexpr uint32_t IsDuelTargetAllowed = offsetOf(0x09F4DF78);
constexpr uint32_t CanSelectFieldZone = offsetOf(0x09F4E2C4);
constexpr uint32_t FirstSelectableMonsterZone = offsetOf(0x09F4E500);
constexpr uint32_t FindCardEffectEntry = offsetOf(0x09F462C8);
constexpr uint32_t FindCardEffectEntryOfKind = offsetOf(0x09F4631C);
constexpr uint32_t FindCardEffectEntryByValue = offsetOf(0x09F46378);
constexpr uint32_t CountCardEffectEntries = offsetOf(0x09F4640C);
constexpr uint32_t CountCardEffectEntriesByValue = offsetOf(0x09F464B4);
constexpr uint32_t IsFieldCardTargetSelectable = offsetOf(0x09F4C338);
constexpr uint32_t HasFieldCardEffect = offsetOf(0x09F465C0);
constexpr uint32_t IsActiveFieldCardWithId = offsetOf(0x09F4C238);
constexpr uint32_t IsCardNameRestricted = offsetOf(0x09F4FAEC);
constexpr uint32_t HasFieldCardEffectWithValue = offsetOf(0x09F4670C);
constexpr uint32_t HasFieldCardEffectOfKind = offsetOf(0x09F4679C);
constexpr uint32_t HasFieldCardEffectKind1Or2 = offsetOf(0x09F467F0);
constexpr uint32_t HasFieldCardEffectKind1Or2WithValue = offsetOf(0x09F46844);
constexpr uint32_t GetFieldCardEffectValue = offsetOf(0x09F46898);
constexpr uint32_t GetEffectiveFieldCardId = offsetOf(0x09F4ABF0);
}

}  // namespace duel_eng
