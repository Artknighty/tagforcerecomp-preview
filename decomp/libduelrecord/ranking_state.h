/* Layout of the Duel Ranking screen's state, as the game keeps it in the module's data (guest memory), and the tab/row types.
 * Offsets are relative to the module base; names come from how the code uses each field. */
#pragma once
#include <stdint.h>

enum RankingTab { kTabHighScore = 0, kTabBonus = 1, kTabExtra = 2, kTabCount = 3 };   /* extra: shown only when save flag 7 is set */

#pragma pack(push, 1)
struct RankingRow {          /* one line of a tab; 16 bytes */
    uint16_t item;           /* item number inside the tab's static table */
    uint16_t order;          /* position in the tab */
    uint8_t flags;           /* bit0: new (not seen yet), bit1: has a numeric value */
    uint8_t pad0[3];
    uint32_t value;
    uint8_t heightBits;      /* low 6 bits: height of the row in text lines */
    uint8_t pad1[3];
};
struct RankingState {        /* module base + 0x6800 */
    uint16_t phase;          /* 0 load, 1 fade in, 2 run, 3 fade out, 4 unload */
    uint16_t unk02;
    uint8_t tabInfo;         /* low nibble: current tab, high nibble: number of tabs */
    uint8_t completionPercent;   /* "Rate of Accomplishment" */
    uint16_t scroll[kTabCount];      /* first visible row of each tab */
    uint16_t rowCount[kTabCount];
    uint8_t pad0[2];
    uint32_t rows[kTabCount];        /* guest address of each tab's RankingRow array */
    uint32_t saveData;       /* guest address of the save block this screen reads */
    uint32_t heap;           /* memory pool for the rows */
    int32_t poolId;          /* kernel fixed-size pool, when the screen owns its heap */
    uint32_t poolMem;
    uint32_t unk30;
    uint32_t ehp;            /* database/duelrecord.ehp, once loaded into the heap */
    uint32_t unk38[2];
    uint8_t loadFlags;       /* bit0: ready to start loading; bits 1..7: load progress counter */
};
struct TextureSlot {         /* module base + 0x65bc: three entries sorted by key */
    uint16_t key;
    uint16_t pad;
    uint32_t texture;        /* handle, 0 until first used */
    uint32_t nameFormat;     /* guest address of the file name pattern ("tab.gim", "parts_%c.gim"...) */
};
#pragma pack(pop)
static_assert(sizeof(RankingRow) == 16, "RankingRow");
static_assert(sizeof(RankingState) == 0x41, "RankingState");
static_assert(sizeof(TextureSlot) == 12, "TextureSlot");

/* per tab: the static table in the module data (u16 per item: bit0 = item exists, the rest = label index), how many items it holds and
 * how many labels LabelIndex accepts (the original loops one item further than it looks labels up for the last tab) */
struct TabTable { uint32_t offset, items, labels; };
constexpr TabTable kLabelTables[kTabCount] = {{0x6608, 40, 0x28}, {0x6658, 83, 0x53}, {0x6700, 118, 0x75}};

constexpr uint32_t kRankingStateOffset = 0x6800;
constexpr uint32_t kTextureSlotsOffset = 0x65bc;
constexpr uint32_t kLanguageLettersOffset = 0x65f8;   /* one file-name letter per language (e, f, g, i, ...) */
constexpr uint32_t kDatabasePathOffset = 0x65e0;     /* "database/duelrecord.ehp" */
constexpr uint32_t kPoolNameOffset = 0x6590;
