/* libcharalist: the character list library. The story code asks it which character to show (a random one among the characters the player has met)
 * and which picture of that character. It builds three lists of characters from the module's own tables and the save data, picks one, and keeps the
 * choice until it is unloaded. The module image loads at 0x08EEB400 (the same base as libduelrecord, at other times); offsets below are from it. */
#pragma once
#include <stdint.h>

namespace charalist {

constexpr uint32_t kMaxCharacterId = 152;      /* the lists are built over the ids 2 .. 151 */
constexpr uint32_t kFirstCharacterId = 2;

/* One character of the module's own table (0x18 bytes, 41 entries sorted by id at kCharacterTable). */
struct CharacterEntry {
    uint16_t word;           /* +0x00 bits 0-9 the character id, bits 10-12 its category (1..6), bits 13-14 how many pictures it has (0..3), bit 15 limited to two */
    uint16_t unknown02;
    uint32_t picture[3];     /* +0x04 one value per picture (the first is used from one picture up, the second from two, the third from three) */
    uint8_t countdown;       /* +0x10 copied into bits 7-14 of the detail record's flags */
    uint8_t unknown11[7];
    uint32_t id() const { return word & 0x3FF; }
    uint32_t category() const { return (word >> 10) & 7; }
    uint32_t pictures() const { return (word >> 13) & 3; }
    bool limitedToTwo() const { return (word >> 15) != 0; }
};
static_assert(sizeof(CharacterEntry) == 0x18, "CharacterEntry layout");

/* The detail record of a list entry (0x28 bytes taken from the heap). */
struct DetailRecord {
    uint16_t flags;          /* +0x00 bits 0-1 pictures, bits 4-5 state, bit 6 the character's card is fully owned, bits 7-14 a counter (copied from CharacterEntry::countdown) */
    uint16_t unknown02;
    uint32_t picture[3];     /* +0x04 */
    uint8_t category[3];     /* +0x10 one per picture */
    uint8_t pad13;
    uint32_t text[3];        /* +0x14 the name text of each picture; the alias text (when there is one) replaces the second */
    uint8_t extra[3];        /* +0x20 one per picture, only when the card is fully owned */
    uint8_t unknown23[5];
    uint32_t pictureCount() const { return flags & 3; }
};
static_assert(sizeof(DetailRecord) == 0x28, "DetailRecord layout");

/* One entry of a list (0x10 bytes). Lists 0 and 1 point at a detail record; list 2 keeps a small value in place of the pointer (a byte at +0x08 with the
 * category in its low three bits, and a 16-bit id at +0x0A). */
struct ListEntry {
    uint16_t id;             /* +0x00 bits 0-9 the character id */
    uint16_t order;          /* +0x02 bits 0-9 the character's position in the roster, bit 10 = kNoDetailFlag */
    uint32_t unknown04;
    uint32_t detail;         /* +0x08 guest address of the DetailRecord (lists 0 and 1) */
    uint32_t unknown0C;
};
static_assert(sizeof(ListEntry) == 0x10, "ListEntry layout");
constexpr uint16_t kNoDetailFlag = 0x0400;   /* in ListEntry::order */

/* The module's working state (0x180 bytes at kState). */
enum ListMode : uint8_t { kModeCharactersMet = 0, kModeCharactersMetToo = 1, kModeStory = 2, kModeCards = 3 };   /* low nibble of State::modeFlags */
struct State {
    uint8_t modeFlags;       /* +0x00 low nibble: ListMode (the story picks one at random), bits 4-5: the answer of the card state check */
    uint8_t unknown01[0x15];
    uint16_t count[3];       /* +0x16 entries in each of the three lists */
    uint32_t list[3];        /* +0x1C guest address of each list (ListEntry[count]) */
    uint8_t unknown28[8];
    uint32_t disabled;       /* +0x30 bit 0: the choice is hidden (the accessors return -1) */
    int32_t chosenCharacter; /* +0x34 */
    int32_t chosenPicture;   /* +0x38 */
    uint32_t heap;           /* +0x3C the heap the lists and records come from */
    int32_t fplPool;         /* +0x40 a kernel pool (0 when none) */
    uint32_t fplBlock;       /* +0x44 */
    uint8_t unknown48[0x180 - 0x48];
};
static_assert(sizeof(State) == 0x180, "State layout");

/* Offsets of the module data, from its base. */
constexpr uint32_t kResourceTable = 0x9F48;      /* DatabaseResource[9] sorted by key */
constexpr uint32_t kLocaleLetters = 0x9FCC;      /* three pairs of letters, one pair per language (the file names contain one) */
constexpr uint32_t kDatabasePath = 0x9FB4;       /* "database/charalist.ehp" */
constexpr uint32_t kCharacterTable = 0xA6AC;     /* CharacterEntry[41] */
constexpr uint32_t kProgressNames = 0xA264;      /* u16[3] per character id: values copied into DetailRecord::extra */
constexpr uint32_t kTextIndexes = 0xA54C;        /* u16[2] per character id: the text index of its name and of its alias */
constexpr uint32_t kDatabase = 0xAAA0;           /* handle of the loaded database (0 = not loaded) */
constexpr uint32_t kPictures = 0xAAA8;           /* handles[41][3]: the pictures loaded for each table character */
constexpr uint32_t kBackground = 0xAE84;         /* handle of a loaded picture */
constexpr uint32_t kLoadFlags = 0xAE90;          /* bit 0: the database still has to be loaded */
constexpr uint32_t kState = 0xAEA0;
constexpr int32_t kCharacterCount = 41;
constexpr int32_t kResourceCount = 9;
constexpr uint32_t kDatabaseLoaded = 0x6CC;      /* the function that runs when the database file is read */

/* One of the nine named pictures/tables of the database (12 bytes at kResourceTable). */
struct DatabaseResource {
    uint16_t key;            /* +0x00 */
    uint16_t unknown02;
    uint32_t handle;         /* +0x04 loaded on first use (0 = not yet) */
    uint32_t nameFormat;     /* +0x08 guest address of the file name, with one %c for the language letter */
};
static_assert(sizeof(DatabaseResource) == 12, "DatabaseResource layout");

}  // namespace charalist
