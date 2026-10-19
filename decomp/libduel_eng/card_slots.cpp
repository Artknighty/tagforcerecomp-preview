/* libduel_eng: queries over cards occupying the duel field. */
#include "../common/swap.h"
#include "../common/ehsys.h"
#include "duel_data.h"

#define MODULE "libduel_eng"

namespace {
using namespace duel_eng;

template <class Match>
uint32_t findEffectEntry(SwapCtx &x, uint32_t index, Match matches) {
    while (index != 0) {
        const uint32_t address = kCardEffectEntries + index * (uint32_t)sizeof(CardEffectEntry);
        const CardEffectEntry &entry = x.at<CardEffectEntry>(address);
        const uint16_t next = entry.next;
        if (matches(entry)) return address;
        index = next;
    }
    return 0;
}

template <class Match>
uint32_t countEffectEntries(SwapCtx &x, uint32_t index, Match matches) {
    uint32_t count = 0;
    while (index != 0) {
        const uint32_t address = kCardEffectEntries + index * (uint32_t)sizeof(CardEffectEntry);
        const CardEffectEntry &entry = x.at<CardEffectEntry>(address);
        const uint16_t next = entry.next;
        if (matches(entry)) count++;
        index = next;
    }
    return count;
}

static uint32_t duelPlayerRecord(uint32_t player) {
    return kDuelPlayerRecords + (player & 1u) * kPlayerRecordStride;
}

static uint16_t persistentEffectHead(SwapCtx &x, uint32_t player) {
    return x.at<uint16_t>(duelPlayerRecord(player) + kPersistentEffectHeadOffset);
}

static bool fieldSlotHasSelectableEffectState(const FieldCardSlot &slot) {
    const uint32_t status = slot.stateFlags;
    return (~((status & 0x03u) >> 1) & slot.unknown08 & ~((status & 0x3Fu) >> 5)) != 0;
}

static bool fieldSlotTargetMaskContains(const FieldCardSlot &slot, uint32_t targetPlayer, uint32_t targetZone) {
    const uint32_t bit = (targetZone + targetPlayer * 0x10u) & 0x1Fu;
    return (slot.unknown0C & (1u << bit)) != 0;
}

/* FUN_09f49ca0(player, slot): returns bit 9 of that field slot's state word. Its exact gameplay meaning is not yet known. */
void swap_ReadCardSlotFlag9(SwapCtx &x) {
    const uint32_t player = x.arg(0);
    const int32_t slot = (int32_t)x.arg(1);
    x.ret((fieldCardSlot(x, player, slot).stateFlags >> 9) & 1u);
}
SwapReg reg_ReadCardSlotFlag9({"libduel_eng.ReadCardSlotFlag9", MODULE, fn::ReadCardSlotFlag9, swap_ReadCardSlotFlag9, true});

/* FUN_09f49e18(cardId, attribute): compares the card's attribute with the requested attribute code. */
void swap_CardHasAttribute(SwapCtx &x) {
    const uint32_t cardId = x.arg(0), attribute = x.arg(1);
    /* The original leaves the requested attribute in a1 while calling this import; preserve that hidden register. */
    x.c->r[4] = cardId;
    x.c->r[5] = attribute;
    x.ret(x.callNidKeepRegs(ehsys::nid::CardAttribute) == attribute ? 1u : 0u);
}
SwapReg reg_CardHasAttribute({"libduel_eng.CardHasAttribute", MODULE, fn::CardHasAttribute, swap_CardHasAttribute, true});

/* FUN_09f49cd8(player, slot): special card ids use slot flag 9; other ids use the engine's card-category set. */
void swap_IsFieldCardCategoryInEffectSet(SwapCtx &x) {
    constexpr uint32_t kSpecialCards[] = {0x1809, 0x172F, 0x1636};
    const uint32_t player = x.arg(0);
    const int32_t slotIndex = (int32_t)x.arg(1);
    const uint32_t cardId = fieldCardSlot(x, player, slotIndex).cardIdAndFlags & 0x1FFFu;
    for (uint32_t specialCard : kSpecialCards) {
        if (cardId == specialCard) {
            x.ret(x.callLocal(fn::ReadCardSlotFlag9, player, (uint32_t)slotIndex));
            return;
        }
    }
    x.ret(x.callNid(ehsys::nid::CardCategoryInEffectSet, cardId));
}
SwapReg reg_IsFieldCardCategoryInEffectSet({"libduel_eng.IsFieldCardCategoryInEffectSet", MODULE, fn::IsFieldCardCategoryInEffectSet, swap_IsFieldCardCategoryInEffectSet, true});

/* FUN_09f4c704(cardId): counts eligible field zones whose effective card id matches. Monster, field-spell and
 * spell/trap cards use their respective zones. */
uint32_t countActiveCardSlots(SwapCtx &x, uint32_t player, uint32_t cardId, int32_t first, int32_t end, int32_t excluded = -1) {
    uint32_t count = 0;
    for (int32_t slot = first; slot < end; slot++) {
        if (slot != excluded && x.callLocal(fn::IsActiveFieldCardWithId, player, (uint32_t)slot, cardId) != 0) count++;
    }
    return count;
}

void swap_CountActiveFieldCardsWithId(SwapCtx &x) {
    const uint32_t cardId = x.arg(0);
    const bool isMonster = x.callNid(ehsys::nid::CardIsMonster, cardId) != 0;
    const uint32_t icon = isMonster ? 0 : x.callNid(ehsys::nid::CardIcon, cardId);
    uint32_t count = 0;

    const int32_t first = isMonster ? kMonsterZoneFirst : icon == 2 ? kFieldSpellZone : kSpellTrapZoneFirst;
    const int32_t end = isMonster ? kMonsterZoneEnd : icon == 2 ? kFieldSpellZone + 1 : kSpellTrapZoneEnd;
    for (uint32_t player = 0; player < 2; player++) count += countActiveCardSlots(x, player, cardId, first, end);
    x.ret(count);
}
SwapReg reg_CountActiveFieldCardsWithId({"libduel_eng.CountActiveFieldCardsWithId", MODULE, fn::CountActiveFieldCardsWithId, swap_CountActiveFieldCardsWithId, true});

/* FUN_09f4c490(player, cardId, excludedSlot): counts active matches for one player, except the given zone.
 * Field spells are in a separate branch in the original and are not filtered by excludedSlot. */
void swap_CountOtherActiveFieldCardsWithId(SwapCtx &x) {
    const uint32_t player = x.arg(0), cardId = x.arg(1);
    const int32_t excludedSlot = (int32_t)x.arg(2);
    const bool isMonster = x.callNid(ehsys::nid::CardIsMonster, cardId) != 0;
    const uint32_t icon = isMonster ? 0 : x.callNid(ehsys::nid::CardIcon, cardId);

    if (!isMonster && icon == 2) {
        x.ret(countActiveCardSlots(x, player, cardId, kFieldSpellZone, kFieldSpellZone + 1));
        return;
    }
    const int32_t first = isMonster ? kMonsterZoneFirst : kSpellTrapZoneFirst;
    const int32_t end = isMonster ? kMonsterZoneEnd : kSpellTrapZoneEnd;
    x.ret(countActiveCardSlots(x, player, cardId, first, end, excludedSlot));
}
SwapReg reg_CountOtherActiveFieldCardsWithId({"libduel_eng.CountOtherActiveFieldCardsWithId", MODULE, fn::CountOtherActiveFieldCardsWithId, swap_CountOtherActiveFieldCardsWithId, true});

/* FUN_09f4c338(player, slot): target-selection callers use this when the slot has +0x08 data and state bit 1 is clear. */
void swap_IsFieldCardTargetSelectable(SwapCtx &x) {
    const uint32_t player = x.arg(0);
    const int32_t slotIndex = (int32_t)x.arg(1);
    const FieldCardSlot &slot = fieldCardSlot(x, player, slotIndex);
    x.ret(slot.unknown08 != 0 && (slot.stateFlags & 0x02u) == 0 ? 1u : 0u);
}
SwapReg reg_IsFieldCardTargetSelectable({"libduel_eng.IsFieldCardTargetSelectable", MODULE, fn::IsFieldCardTargetSelectable, swap_IsFieldCardTargetSelectable, true});

/* FUN_09f4c238(player, slot, expectedId): checks the effective card id, its field-slot state, and the per-slot suppression bit. */
void swap_IsActiveFieldCardWithId(SwapCtx &x) {
    const uint32_t player = x.arg(0);
    const int32_t slotIndex = (int32_t)x.arg(1);
    const uint32_t expectedId = x.arg(2);
    const uint32_t effectiveId = x.callLocal(fn::GetEffectiveFieldCardId, player, (uint32_t)slotIndex);
    if (effectiveId != expectedId) { x.ret(0); return; }

    const FieldCardSlot &slot = fieldCardSlot(x, player, slotIndex);
    const uint32_t status = slot.stateFlags;
    const uint32_t eligible = ~((status & 0x03u) >> 1) & slot.unknown08 & ~((status & 0x3Fu) >> 5);
    if (eligible == 0) { x.ret(0); return; }

    const uint32_t bit = ((uint32_t)slotIndex + player * 0x10u) & 0x1Fu;
    const uint32_t suppressed = x.at<uint32_t>(kFieldCardSuppressionMask);
    x.ret((suppressed & (1u << bit)) == 0 ? 1u : 0u);
}
SwapReg reg_IsActiveFieldCardWithId({"libduel_eng.IsActiveFieldCardWithId", MODULE, fn::IsActiveFieldCardWithId, swap_IsActiveFieldCardWithId, true});

/* FUN_09f462c8(listHead, effectCode): walks the indexed effect list and returns the first matching entry of kind 0..5. */
void swap_FindCardEffectEntry(SwapCtx &x) {
    const uint32_t index = x.arg(0);
    const uint32_t effectCode = x.arg(1);
    x.ret(findEffectEntry(x, index, [effectCode](const CardEffectEntry &entry) {
        return (entry.kindAndFlags & 0x0Fu) <= 5 && entry.effectCode == effectCode;
    }));
}
SwapReg reg_FindCardEffectEntry({"libduel_eng.FindCardEffectEntry", MODULE, fn::FindCardEffectEntry, swap_FindCardEffectEntry, true});

/* FUN_09f4631c(listHead, effectCode, kind): first matching entry of that exact kind (0..5). */
void swap_FindCardEffectEntryOfKind(SwapCtx &x) {
    const uint32_t listHead = x.arg(0), effectCode = x.arg(1), kind = x.arg(2);
    x.ret(findEffectEntry(x, listHead, [effectCode, kind](const CardEffectEntry &entry) {
        const uint32_t entryKind = entry.kindAndFlags & 0x0Fu;
        return entryKind <= 5 && entry.effectCode == effectCode && entryKind == kind;
    }));
}
SwapReg reg_FindCardEffectEntryOfKind({"libduel_eng.FindCardEffectEntryOfKind", MODULE, fn::FindCardEffectEntryOfKind, swap_FindCardEffectEntryOfKind, true});

/* FUN_09f46378(listHead, effectCode, value): searches kinds 1/2 with no high-nibble flags; negative value means any value. */
void swap_FindCardEffectEntryByValue(SwapCtx &x) {
    const uint32_t listHead = x.arg(0), effectCode = x.arg(1), requestedValue = x.arg(2);
    const bool anyValue = (int32_t)requestedValue < 0;
    x.ret(findEffectEntry(x, listHead, [effectCode, requestedValue, anyValue](const CardEffectEntry &entry) {
        const uint32_t kind = entry.kindAndFlags & 0x0Fu;
        return (kind == 1 || kind == 2) && entry.effectCode == effectCode && (entry.kindAndFlags >> 4) == 0 &&
               (anyValue || entry.value == requestedValue);
    }));
}
SwapReg reg_FindCardEffectEntryByValue({"libduel_eng.FindCardEffectEntryByValue", MODULE, fn::FindCardEffectEntryByValue, swap_FindCardEffectEntryByValue, true});

/* FUN_09f4640c(listHead, effectCode): counts matching entries whose kind is 0..5. */
void swap_CountCardEffectEntries(SwapCtx &x) {
    const uint32_t listHead = x.arg(0), effectCode = x.arg(1);
    x.ret(countEffectEntries(x, listHead, [effectCode](const CardEffectEntry &entry) {
        return (entry.kindAndFlags & 0x0Fu) < 6 && entry.effectCode == effectCode;
    }));
}
SwapReg reg_CountCardEffectEntries({"libduel_eng.CountCardEffectEntries", MODULE, fn::CountCardEffectEntries, swap_CountCardEffectEntries, true});

/* FUN_09f464b4(listHead, effectCode, value): counts matching entries by their 16-bit value. */
void swap_CountCardEffectEntriesByValue(SwapCtx &x) {
    const uint32_t listHead = x.arg(0), effectCode = x.arg(1), value = x.arg(2);
    x.ret(countEffectEntries(x, listHead, [effectCode, value](const CardEffectEntry &entry) {
        return (entry.kindAndFlags & 0x0Fu) < 6 && entry.effectCode == effectCode && entry.value == value;
    }));
}
SwapReg reg_CountCardEffectEntriesByValue({"libduel_eng.CountCardEffectEntriesByValue", MODULE, fn::CountCardEffectEntriesByValue, swap_CountCardEffectEntriesByValue, true});

/* FUN_09f465c0(player, slot, effectCode): whether this field card has a matching effect entry. */
void swap_HasFieldCardEffect(SwapCtx &x) {
    const uint32_t player = x.arg(0);
    const int32_t slotIndex = (int32_t)x.arg(1);
    const uint32_t effectCode = x.arg(2);
    const uint16_t listHead = fieldCardSlot(x, player, slotIndex).effectListHead;
    x.ret(x.callLocal(fn::FindCardEffectEntry, listHead, effectCode) != 0 ? 1u : 0u);
}
SwapReg reg_HasFieldCardEffect({"libduel_eng.HasFieldCardEffect", MODULE, fn::HasFieldCardEffect, swap_HasFieldCardEffect, true});

/* FUN_09f4670c(player, slot, effectCode, value): true if a kind 0..5 entry has both the code and value. */
void swap_HasFieldCardEffectWithValue(SwapCtx &x) {
    const uint32_t player = x.arg(0);
    const int32_t slotIndex = (int32_t)x.arg(1);
    const uint32_t effectCode = x.arg(2), value = x.arg(3);
    const uint16_t listHead = fieldCardSlot(x, player, slotIndex).effectListHead;
    x.ret(findEffectEntry(x, listHead, [effectCode, value](const CardEffectEntry &entry) {
        return (entry.kindAndFlags & 0x0Fu) < 6 && entry.effectCode == effectCode && entry.value == value;
    }) != 0 ? 1u : 0u);
}
SwapReg reg_HasFieldCardEffectWithValue({"libduel_eng.HasFieldCardEffectWithValue", MODULE, fn::HasFieldCardEffectWithValue, swap_HasFieldCardEffectWithValue, true});

/* FUN_09f4679c(player, slot, effectCode, kind): true if this field card has that effect in the requested kind. */
void swap_HasFieldCardEffectOfKind(SwapCtx &x) {
    const uint32_t player = x.arg(0);
    const int32_t slotIndex = (int32_t)x.arg(1);
    const uint32_t effectCode = x.arg(2), kind = x.arg(3);
    const uint16_t listHead = fieldCardSlot(x, player, slotIndex).effectListHead;
    x.ret(x.callLocal(fn::FindCardEffectEntryOfKind, listHead, effectCode, kind) != 0 ? 1u : 0u);
}
SwapReg reg_HasFieldCardEffectOfKind({"libduel_eng.HasFieldCardEffectOfKind", MODULE, fn::HasFieldCardEffectOfKind, swap_HasFieldCardEffectOfKind, true});

/* FUN_09f467f0: tests for an effect in kind 1/2 with no extra flags, regardless of its value. */
void swap_HasFieldCardEffectKind1Or2(SwapCtx &x) {
    const uint32_t player = x.arg(0);
    const int32_t slotIndex = (int32_t)x.arg(1);
    const uint32_t effectCode = x.arg(2);
    const uint16_t listHead = fieldCardSlot(x, player, slotIndex).effectListHead;
    x.ret(x.callLocal(fn::FindCardEffectEntryByValue, listHead, effectCode, 0xFFFFFFFFu) != 0 ? 1u : 0u);
}
SwapReg reg_HasFieldCardEffectKind1Or2({"libduel_eng.HasFieldCardEffectKind1Or2", MODULE, fn::HasFieldCardEffectKind1Or2, swap_HasFieldCardEffectKind1Or2, true});

/* FUN_09f46844(player, slot, effectCode, value): kind 1/2 entry with no extra flags and this value. */
void swap_HasFieldCardEffectKind1Or2WithValue(SwapCtx &x) {
    const uint32_t player = x.arg(0);
    const int32_t slotIndex = (int32_t)x.arg(1);
    const uint32_t effectCode = x.arg(2), value = x.arg(3);
    const uint16_t listHead = fieldCardSlot(x, player, slotIndex).effectListHead;
    x.ret(x.callLocal(fn::FindCardEffectEntryByValue, listHead, effectCode, value) != 0 ? 1u : 0u);
}
SwapReg reg_HasFieldCardEffectKind1Or2WithValue({"libduel_eng.HasFieldCardEffectKind1Or2WithValue", MODULE, fn::HasFieldCardEffectKind1Or2WithValue, swap_HasFieldCardEffectKind1Or2WithValue, true});

/* FUN_09f46898(player, slot, effectCode): returns the effect value on this field card, or -1 when absent. */
void swap_GetFieldCardEffectValue(SwapCtx &x) {
    const uint32_t player = x.arg(0);
    const int32_t slotIndex = (int32_t)x.arg(1);
    const uint32_t effectCode = x.arg(2);
    const uint16_t listHead = fieldCardSlot(x, player, slotIndex).effectListHead;
    const uint32_t entryAddress = x.callLocal(fn::FindCardEffectEntry, listHead, effectCode);
    if (entryAddress == 0) {
        x.ret(0xFFFFFFFFu);
        return;
    }
    x.ret(x.at<CardEffectEntry>(entryAddress).value);
}
SwapReg reg_GetFieldCardEffectValue({"libduel_eng.GetFieldCardEffectValue", MODULE, fn::GetFieldCardEffectValue, swap_GetFieldCardEffectValue, true});

/* FUN_09f4faec(cardId, effectValue): checks whether an active field card 0x1368 restricts this card id. (The exact
 * player-facing meaning of this rule card is still being identified.) */
void swap_IsCardNameRestricted(SwapCtx &x) {
    const uint32_t cardId = x.arg(0);
    const int32_t effectValue = (int32_t)x.arg(1);
    if (effectValue == 0 || effectValue == 1) { x.ret(0); return; }

    constexpr uint32_t kRestrictionCardId = 0x1368;
    constexpr int32_t kFirstSpellTrapSlot = 5, kLastSpellTrapSlot = 10;
    for (uint32_t player = 0; player < 2; player++) {
        for (int32_t slotIndex = kFirstSpellTrapSlot; slotIndex < kLastSpellTrapSlot; slotIndex++) {
            const FieldCardSlot &slot = fieldCardSlot(x, player, slotIndex);
            if ((slot.cardIdAndFlags & 0x1FFFu) != kRestrictionCardId) continue;

            const uint32_t status = slot.stateFlags;
            const uint32_t visibleEffect = ~((status & 0x03u) >> 1) & slot.unknown08 & ~((status & 0x3Fu) >> 5);
            if (visibleEffect == 0) continue;
            if (x.callNid(ehsys::nid::CardIdsEquivalent, cardId, slot.unknown0C) == 0) continue;
            if (effectValue < 0 || x.callLocal(fn::HasFieldCardEffectWithValue, player, (uint32_t)slotIndex, kRestrictionCardId, (uint32_t)effectValue) == 0) {
                x.ret(1);
                return;
            }
        }
    }
    x.ret(0);
}
SwapReg reg_IsCardNameRestricted({"libduel_eng.IsCardNameRestricted", MODULE, fn::IsCardNameRestricted, swap_IsCardNameRestricted, true});

/* libduel_eng_EA6BA0BA(player, slot): resolves the card id after the duel's name-changing field effects. */
void swap_GetEffectiveFieldCardId(SwapCtx &x) {
    constexpr uint32_t kIdentityEffectCard = 0x1A74;
    constexpr uint32_t kNameVariantA = 0x19BC, kNameVariantB = 0x19A6;
    constexpr uint32_t kEffectiveNameA = 0x18F6, kEffectiveNameB = 0x18F9;
    const uint32_t player = x.arg(0);
    const int32_t slotIndex = (int32_t)x.arg(1);
    const FieldCardSlot &slot = fieldCardSlot(x, player, slotIndex);
    const uint32_t cardId = slot.cardIdAndFlags & 0x1FFFu;

    /* Only monster zones (0..4) use these identity effects. */
    if (slotIndex >= 5) { x.ret(cardId); return; }

    if (cardId == kIdentityEffectCard) {
        uint32_t effectiveId = x.callLocal(fn::GetFieldCardEffectValue, player, (uint32_t)slotIndex, kIdentityEffectCard);
        if (effectiveId == kNameVariantA) effectiveId = kEffectiveNameA;
        else if (effectiveId == kNameVariantB) effectiveId = kEffectiveNameB;
        if (effectiveId == 0xFFFFFFFFu || x.callLocal(fn::IsCardNameRestricted, effectiveId, 0xFFFFFFFFu) != 0) {
            effectiveId = kIdentityEffectCard;
        }
        x.ret(effectiveId);
        return;
    }

    uint32_t effectiveId = 0;
    if (cardId == kNameVariantA) effectiveId = kEffectiveNameA;
    else if (cardId == kNameVariantB) effectiveId = kEffectiveNameB;
    else { x.ret(cardId); return; }

    const uint32_t status = slot.stateFlags;
    const uint32_t eligible = ~((status & 0x03u) >> 1) & slot.unknown08 & ~((status & 0x3Fu) >> 5);
    if (eligible == 0 || x.callLocal(fn::IsCardNameRestricted, effectiveId, 0xFFFFFFFFu) != 0) {
        x.ret(cardId);
        return;
    }
    x.ret(effectiveId);
}
SwapReg reg_GetEffectiveFieldCardId({"libduel_eng.GetEffectiveFieldCardId", MODULE, fn::GetEffectiveFieldCardId, swap_GetEffectiveFieldCardId, true});

/* FUN_09f4c984(cardId, player, excludedSides): counts matches across both sides when the mask is zero;
 * otherwise bit 0/1 excludes the selected player/opponent from the count. */
void swap_CountActiveCardIdOnAllowedSides(SwapCtx &x) {
    const uint32_t cardId = x.arg(0), player = x.arg(1), excludedSides = x.arg(2);
    if (excludedSides == 0) {
        x.ret(x.callLocal(fn::CountActiveFieldCardsWithId, cardId));
        return;
    }

    uint32_t count = 0;
    if ((excludedSides & 1u) == 0) count += x.callLocal(fn::CountOtherActiveFieldCardsWithId, player, cardId, 0xFFFFFFFFu);
    if ((excludedSides & 2u) == 0) count += x.callLocal(fn::CountOtherActiveFieldCardsWithId, 1u - player, cardId, 0xFFFFFFFFu);
    x.ret(count);
}
SwapReg reg_CountActiveCardIdOnAllowedSides({"libduel_eng.CountActiveCardIdOnAllowedSides", MODULE, fn::CountActiveCardIdOnAllowedSides, swap_CountActiveCardIdOnAllowedSides, true});

/* libduel_eng_331C6B94(player, zone): true when no active continuous effect or target record forbids selecting the zone. */
void swap_IsDuelTargetAllowed(SwapCtx &x) {
    constexpr uint32_t kContinuousEffectKind = 6;
    constexpr uint32_t kMonsterTargetBlockerA = 0x17EE, kMonsterTargetBlockerB = 0x1432;
    constexpr uint32_t kSpellTrapTargetBlocker = 0x1AA9, kTargetEffectCode = 0x1472;
    constexpr uint16_t kTargetRecordKind = 0x40;
    const uint32_t targetPlayer = x.arg(0);
    const int32_t targetZone = (int32_t)x.arg(1);

    if (targetZone < kMonsterZoneEnd) {
        for (uint32_t owner = 0; owner < 2; owner++) {
            uint32_t index = persistentEffectHead(x, owner);
            while (index != 0) {
                const CardEffectEntry &entry = x.at<CardEffectEntry>(kCardEffectEntries + index * (uint32_t)sizeof(CardEffectEntry));
                const uint16_t next = entry.next;
                if ((entry.kindAndFlags & 0x0Fu) == kContinuousEffectKind) {
                    const uint32_t packedSource = entry.effectCode;
                    const uint32_t sourcePlayer = packedSource & 0xFFu;
                    const int32_t sourceZone = (int32_t)(packedSource >> 8);
                    const uint32_t effectiveId = x.callLocal(fn::GetEffectiveFieldCardId, sourcePlayer, (uint32_t)sourceZone);
                    if (effectiveId == kMonsterTargetBlockerA || effectiveId == kMonsterTargetBlockerB) {
                        const FieldCardSlot &source = fieldCardSlot(x, sourcePlayer, sourceZone);
                        if (fieldSlotTargetMaskContains(source, targetPlayer, (uint32_t)targetZone) &&
                            fieldSlotHasSelectableEffectState(source)) {
                            x.ret(0);
                            return;
                        }
                    }
                }
                index = next;
            }
        }

        for (uint32_t owner = 0; owner < 2; owner++) {
            const uint32_t record = duelPlayerRecord(owner);
            const uint32_t count = x.at<uint32_t>(record + kTargetRestrictionCountOffset);
            for (uint32_t i = 0; i < count; i++) {
                const uint16_t restriction = x.at<uint16_t>(record + kTargetRestrictionListOffset + i * 2u);
                if ((restriction & 0xFFu) == kTargetRecordKind &&
                    targetPlayer == ((restriction >> 8) & 1u) && targetZone == (restriction >> 12)) {
                    x.ret(0);
                    return;
                }
            }
        }
        x.ret(1);
        return;
    }

    const uint32_t otherPlayer = (1u - targetPlayer) & 1u;
    uint32_t index = persistentEffectHead(x, otherPlayer);
    while (index != 0) {
        const CardEffectEntry &entry = x.at<CardEffectEntry>(kCardEffectEntries + index * (uint32_t)sizeof(CardEffectEntry));
        const uint16_t next = entry.next;
        if ((entry.kindAndFlags & 0x0Fu) == kContinuousEffectKind) {
            const uint32_t packedSource = entry.effectCode;
            const uint32_t sourcePlayer = packedSource & 1u;
            const int32_t sourceZone = (int32_t)(packedSource >> 8);
            const FieldCardSlot &source = fieldCardSlot(x, sourcePlayer, sourceZone);
            const uint32_t targetBit = ((uint32_t)targetZone + 0x10u) & 0x1Fu;
            if ((source.cardIdAndFlags & 0x1FFFu) == kSpellTrapTargetBlocker &&
                (source.unknown0C & (1u << targetBit)) != 0 && fieldSlotHasSelectableEffectState(source)) {
                x.ret(0);
                return;
            }
        }
        index = next;
    }

    const uint32_t packedTarget = (targetPlayer & 0xFFu) | (((uint32_t)targetZone & 0xFFu) << 8);
    for (uint32_t owner = 0; owner < 2; owner++) {
        for (int32_t slot = kMonsterZoneFirst; slot < kMonsterZoneEnd; slot++) {
            if (x.callLocal(fn::ReadCardSlotFlag9, owner, (uint32_t)slot) == 0) continue;
            const uint32_t effectValue = x.callLocal(fn::GetFieldCardEffectValue, owner, (uint32_t)slot, kTargetEffectCode);
            if (effectValue == packedTarget) { x.ret(0); return; }
        }
    }
    x.ret(1);
}
SwapReg reg_IsDuelTargetAllowed({"libduel_eng.IsDuelTargetAllowed", MODULE, fn::IsDuelTargetAllowed, swap_IsDuelTargetAllowed, true});

/* FUN_09f4e2c4(player, zone): the zone is empty, is not occupied by a selected override card, and passes duel target rules. */
void swap_CanSelectFieldZone(SwapCtx &x) {
    const uint32_t player = x.arg(0);
    const int32_t zone = (int32_t)x.arg(1);
    if ((fieldCardSlot(x, player, zone).cardIdAndFlags & 0x1FFFu) != 0) { x.ret(0); return; }

    if ((x.at<uint32_t>(kTargetOverrideEnabled) & 1u) != 0) {
        const bool occupiedByOverrideA = player == x.at<uint32_t>(kTargetOverridePlayerA) &&
            zone == (int32_t)x.at<uint32_t>(kTargetOverrideZoneA) && (x.at<uint16_t>(kTargetOverrideCardA) & 0x1FFFu) != 0;
        const bool occupiedByOverrideB = player == x.at<uint32_t>(kTargetOverridePlayerB) &&
            zone == (int32_t)x.at<uint32_t>(kTargetOverrideZoneB) && (x.at<uint16_t>(kTargetOverrideCardB) & 0x1FFFu) != 0;
        if (occupiedByOverrideA || occupiedByOverrideB) { x.ret(0); return; }
    }
    x.ret(x.callLocal(fn::IsDuelTargetAllowed, player, (uint32_t)zone) != 0 ? 1u : 0u);
}
SwapReg reg_CanSelectFieldZone({"libduel_eng.CanSelectFieldZone", MODULE, fn::CanSelectFieldZone, swap_CanSelectFieldZone, true});

/* FUN_09f4e500(player): returns the first zone in the game's priority table that can accept a monster, or -1. */
void swap_FirstSelectableMonsterZone(SwapCtx &x) {
    const uint32_t player = x.arg(0);
    for (uint32_t i = 0; i < 5; i++) {
        const uint32_t zone = x.at<uint32_t>(kMonsterZonePriorityOrder + i * sizeof(uint32_t));
        if (x.callLocal(fn::CanSelectFieldZone, player, zone) != 0) { x.ret(zone); return; }
    }
    x.ret(0xFFFFFFFFu);
}
SwapReg reg_FirstSelectableMonsterZone({"libduel_eng.FirstSelectableMonsterZone", MODULE, fn::FirstSelectableMonsterZone, swap_FirstSelectableMonsterZone, true});

static uint32_t countOccupiedFieldSlots(SwapCtx &x, uint32_t player) {
    uint32_t count = 0;
    for (int32_t slot = 0; slot < kFieldZoneCount; slot++) {
        if ((fieldCardSlot(x, player, slot).cardIdAndFlags & 0x1FFFu) != 0) count++;
    }
    if ((x.at<uint32_t>(kTargetOverrideEnabled) & 1u) != 0) {
        if (player == x.at<uint32_t>(kTargetOverridePlayerA) && (x.at<uint16_t>(kTargetOverrideCardA) & 0x1FFFu) != 0) count++;
        if (player == x.at<uint32_t>(kTargetOverridePlayerB) && (x.at<uint16_t>(kTargetOverrideCardB) & 0x1FFFu) != 0) count++;
    }
    return count;
}

/* FUN_09f4e3b0(player): counts selectable monster zones, capped by card 0x16DF's total-field-card limit when active. */
void swap_CountAvailableMonsterZones(SwapCtx &x) {
    const uint32_t player = x.arg(0);
    int32_t available = 0;
    for (int32_t zone = kMonsterZoneFirst; zone < kMonsterZoneEnd; zone++) {
        if (x.callLocal(fn::CanSelectFieldZone, player, (uint32_t)zone) != 0) available++;
    }

    if (x.callLocal(fn::CountActiveFieldCardsWithId, kMonsterZoneLimitCardId) != 0) {
        const int32_t remaining = 5 - (int32_t)countOccupiedFieldSlots(x, player);
        if (remaining <= available) available = remaining;
        if (available < 1) available = 0;
    }
    x.ret((uint32_t)available);
}
SwapReg reg_CountAvailableMonsterZones({"libduel_eng.CountAvailableMonsterZones", MODULE, fn::CountAvailableMonsterZones, swap_CountAvailableMonsterZones, true});

}  // namespace
