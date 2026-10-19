/* Duelists (characters) and their decks, in one place. The rest of the decomp talks about "character" and "deck" with these names and helpers.
 * The full map of the 351 decks is in docs/DECKS.md ("Deck map"); tools/game/decks_mapa.py builds it from the game and checks the rules below.
 *
 * Two different numbers, easy to mix up:
 *   - CHARACTER id: who the duelist is (name, portrait, progress record in the save). Ids go from kFirstCharacterId (2) to kLastCharacterId (151); the
 *     enum CharacterId names them all (duelist_ids.inc, generated). The title's Free Duel list calls the character of a slot `character` (DuelSlot) or,
 *     in the card list columns, `kind` (ColumnRecord).
 *   - DECK id: which duelist deck the game loads, = the number n of the archive file RCPnnn.ydc (docs/DECKS.md) = the `index` of ehsys
 *     ReadDeckRecord (BOOT 0884a694, which the Better Decks plugin hooks). 0 is "no duelist deck" (the player's own deck comes from elsewhere).
 *
 * Where the story gets a character's deck: the character's script table ScrNNNDeck.bin (NNN = character id - 1) in the story's "brain" archives
 * (st01 = first year: levels 1, 2 and the June duel; st02 = second year: levels 3, 4, 5); the last u16 of each 16-byte record is the deck id.
 *
 * Who owns which deck (checked against every script by tools/game/decks_mapa.py; the exceptions are the constants below):
 *   - characters 2..39 (the 7 main duelists, Crowler, Banner, ... Sheppard) have a LADDER: 6 consecutive decks, level 1..6
 *         deck id = 1 + 6 * (character - 2) + (level - 1)       Jaden = 2 -> RCP001..006, ..., Zane = 8 -> RCP037..042, Sheppard = 39 -> RCP223..228
 *     For the main duelists the order inside the ladder is the story progression, not a strength order. Many other ladders use only some of their
 *     levels (LadderUsedMask); the rest are the placeholder deck.
 *   - characters 41..142 have ONE deck: deck id = character + 199 (RCP240..341); Colin (85) is the exception (his slot, RCP284, is a dummy).
 *   - the placeholder deck is a Spellcaster deck repeated in 171 decks (IsPlaceholder): the game fills with it every deck nobody really has. It is also
 *     the stand-in (kStandInDeck, RCP235) of the characters who do not duel for real (Doctor, Researchers, ...) or whose scripts give it as a fallback.
 * Other roles of the Free Duel setup (FUN_08f02bc0): the special deck ids 351 (slot type 2), 352 (type 0 with a variant) and 354 (type 0 without one).
 * What they stand for is not decoded yet. */
#pragma once
#include <stdint.h>

namespace duelist {

constexpr uint32_t kFirstCharacterId = 2;              /* the first duelist (Jaden) */
constexpr uint32_t kLastCharacterId = 151;
constexpr uint32_t kMainDuelists = 7;                  /* Jaden, Syrus, Chumley, Alexis, Chazz, Bastion, Zane */
constexpr uint32_t kDecksPerMainDuelist = 6;
constexpr uint32_t kDecksPerLadder = 6;
constexpr uint32_t kLastLadderCharacter = 39;          /* characters 2..39 have a ladder of 6 decks */
constexpr uint32_t kFirstSingleDeckCharacter = 41, kLastSingleDeckCharacter = 142;   /* deck = character + kSingleDeckOffset */
constexpr uint32_t kSingleDeckOffset = 199;
constexpr uint32_t kDeckCount = 351;                   /* RCP000 .. RCP350 */

#define DUELIST_ENUM_ENTRY(name, id) name = id,
enum CharacterId : uint16_t {
#include "duelist_ids.inc"
    DUELIST_CHARACTER_IDS(DUELIST_ENUM_ENTRY)
};
#undef DUELIST_ENUM_ENTRY

constexpr uint16_t kNoDeck = 0;
constexpr uint16_t kPersonalModelDeck = 0;              /* RCP000: the model of the player's own deck (a Water deck) */
constexpr uint16_t kPlaceholderModelDeck = 51;          /* RCP051: the placeholder deck; every placeholder has the same cards */
constexpr uint16_t kStandInDeck = 235;                  /* placeholder given to characters that do not really duel */
constexpr uint16_t kSpecialDeckTypeTwo = 351, kSpecialDeckVariant = 352, kSpecialDeckNoVariant = 354;   /* meaning not decoded */

/* The exceptions to the rules above (the story scripts do this on purpose): */
constexpr uint16_t kNightshroudDeck = 55;               /* Nightshroud (40) duels with Atticus's level 1 deck */
constexpr uint16_t kBlairExtraDeck = 109;               /* Blair's script also names this deck, which by the rule is Belowski's level 1 */
constexpr uint16_t kColinDeckA = 229, kColinDeckB = 230;   /* Colin (85): two special decks next to the end of the ladders (229..234 would be character 40's block) */

constexpr bool HasLadder(uint32_t character) { return character >= kFirstCharacterId && character <= kLastLadderCharacter; }
constexpr bool HasSingleDeck(uint32_t character) { return character >= kFirstSingleDeckCharacter && character <= kLastSingleDeckCharacter && character != kColin; }

/* first deck id of a ladder (his level 1 deck, valid for HasLadder characters); the last one is FirstDeck + kDecksPerLadder - 1 */
constexpr uint32_t FirstDeck(uint32_t character) { return 1 + kDecksPerLadder * (character - kFirstCharacterId); }
constexpr uint32_t LadderDeck(uint32_t character, uint32_t level) { return FirstDeck(character) + level - 1; }   /* level 1..6 */
constexpr uint32_t SingleDeck(uint32_t character) { return character + kSingleDeckOffset; }
constexpr bool IsDeckOf(uint32_t character, uint32_t deck) {
    if (HasLadder(character)) return deck >= FirstDeck(character) && deck < FirstDeck(character) + kDecksPerLadder;
    if (HasSingleDeck(character)) return deck == SingleDeck(character);
    return false;
}

/* levels (bit 0 = level 1) of a character's ladder that the story really uses; the other decks of the ladder are the placeholder deck. 0 for
 * characters without a ladder. Atticus's level 1 shows as unused because only Nightshroud's script names it; Belowski's ladder shows as unused for the
 * same reason (Blair's script names its first deck). */
constexpr uint8_t LadderUsedMask(uint32_t character) {
    switch (character) {
#define DUELIST_USED_CASE(name, mask) case name: return mask;
    DUELIST_LADDER_USED(DUELIST_USED_CASE)
#undef DUELIST_USED_CASE
    default: return 0;
    }
}

/* the deck is one of the repeated placeholders (same cards as kPlaceholderModelDeck) */
constexpr bool IsPlaceholder(uint32_t deck) {
#define DUELIST_RANGE_TEST(first, last) if (deck >= first && deck <= last) return true;
    DUELIST_PLACEHOLDER_RANGES(DUELIST_RANGE_TEST)
#undef DUELIST_RANGE_TEST
    return false;
}

enum class DeckRole : uint8_t {
    kNone,         /* not a valid deck id */
    kPersonal,     /* RCP000 */
    kLadder,       /* level 1..6 of a character with a ladder (a real deck) */
    kSingle,       /* the one deck of a single-deck character */
    kPlaceholder,  /* same cards as the placeholder model */
    kSpecial,      /* named decks outside the rules (Colin's) */
};
struct DeckOwner {
    DeckRole role;
    uint16_t character;   /* by the rule (0 = none); see the exceptions above */
    uint8_t level;        /* 1..6 for a ladder, 1 for a single deck, else 0 */
};
constexpr DeckOwner OwnerOfDeck(uint32_t deck) {
    if (deck >= kDeckCount) return {DeckRole::kNone, 0, 0};
    if (deck == kPersonalModelDeck) return {DeckRole::kPersonal, 0, 0};
    if (deck == kColinDeckA || deck == kColinDeckB) return {DeckRole::kSpecial, kColin, 0};
    if (IsPlaceholder(deck)) {
        const uint32_t owner = deck <= 228 ? kFirstCharacterId + (deck - 1) / kDecksPerLadder : deck >= 240 && deck <= 341 ? deck - kSingleDeckOffset : 0;
        return {DeckRole::kPlaceholder, (uint16_t)owner, deck <= 228 ? (uint8_t)((deck - 1) % kDecksPerLadder + 1) : (uint8_t)0};
    }
    if (deck <= 228) return {DeckRole::kLadder, (uint16_t)(kFirstCharacterId + (deck - 1) / kDecksPerLadder), (uint8_t)((deck - 1) % kDecksPerLadder + 1)};
    if (deck >= 240 && deck <= 341) return {DeckRole::kSingle, (uint16_t)(deck - kSingleDeckOffset), 1};
    return {DeckRole::kSpecial, 0, 0};
}

static_assert(FirstDeck(kJaden) == 1 && FirstDeck(kZane) == 37, "RCP001 is Jaden's first deck and RCP037 is Zane's");
static_assert(FirstDeck(kSheppard) == 223 && LadderDeck(kSheppard, 6) == 228, "the ladders end at RCP228");
static_assert(SingleDeck(kMimicry) == 240 && SingleDeck(kSironos) == 341, "single decks are RCP240..341");
static_assert(IsDeckOf(kCrowler, 43) && IsDeckOf(kBanner, 49) && IsDeckOf(kRei, 260) && !IsDeckOf(kColin, 284), "deck rules");
static_assert(OwnerOfDeck(37).character == kZane && OwnerOfDeck(37).level == 1 && OwnerOfDeck(37).role == DeckRole::kLadder, "RCP037 is Zane's level 1");
static_assert(OwnerOfDeck(0).role == DeckRole::kPersonal && OwnerOfDeck(351).role == DeckRole::kNone, "deck id range");
static_assert(IsPlaceholder(kStandInDeck) && !IsPlaceholder(1) && !IsPlaceholder(37), "placeholders");
static_assert(LadderUsedMask(kZane) == 0x3F && LadderUsedMask(kCamula) == 0x01 && LadderUsedMask(kRei) == 0, "ladders in use");

}  // namespace duelist
