/* libcharalist: the character list library (see charalist.h). Every function the story runs is replaced here; the rest of the module (the picture
 * drawing, FUN_00000140...) is still generated code. */
#include "../common/swap.h"
#include "../common/ehsys.h"
#include "../libduelrecord/records.h"
#include "charalist.h"
#include <stddef.h>

#define MODULE "libcharalist_08eeb400"

namespace {
using namespace charalist;

constexpr uint32_t kSceneDevice = 3;                  /* the device number the engine uses for its archive files */
constexpr uint32_t kDatabaseTextKey = 0x1020;         /* the key of the table that holds the character names */
constexpr uint32_t kEntryBytes = sizeof(ListEntry);
constexpr uint32_t kSaveFlagFirstSpecial = 4, kSaveFlagSecondSpecial = 5, kSaveFlagThirdSpecial = 6;   /* save flags that reveal the three special characters */
constexpr uint32_t kIdFirstSpecial = 6, kIdSecondSpecial = 0x18, kIdThirdSpecial = 0x10, kIdSharedCategory = 0xE;   /* the characters those flags are about */
constexpr uint32_t kCategoryFirstSpecial = 3, kCategoryThirdSpecial = 6;
constexpr uint32_t kListBufferEntries = 152;           /* the temporary lists hold one 4-byte entry per id */
constexpr uint32_t kDetailBytes = sizeof(DetailRecord);

State &state(SwapCtx &x) { return x.at<State>(x.addr(kState)); }
uint32_t heap(SwapCtx &x) { return state(x).heap; }
uint32_t alloc(SwapCtx &x, uint32_t bytes) { return x.callNid(ehsys::nid::Alloc, heap(x), bytes); }
void release(SwapCtx &x, uint32_t block) { x.callNid(ehsys::nid::Free, heap(x), block); }
DetailRecord &detail(SwapCtx &x, uint32_t address) { return x.at<DetailRecord>(address); }

/* ---- FUN_000062d8(id): the table entry of a character, 0 when it is not in the table (a binary search over 41 entries sorted by id) ---- */
const CharacterEntry *findEntry(const CharacterEntry *table, uint32_t id) {
    int32_t low = 0, high = kCharacterCount - 1;
    while (low <= high) {
        const int32_t middle = (low + high) / 2;
        const uint32_t found = table[middle].id();
        if (found == id) return &table[middle];
        if (found < id) low = middle + 1; else high = middle - 1;
    }
    return nullptr;
}
uint32_t findEntryAddress(SwapCtx &x, uint32_t id) {
    const CharacterEntry *table = (const CharacterEntry *)x.mem(x.addr(kCharacterTable));
    const CharacterEntry *entry = findEntry(table, id & 0xFFFF);
    return entry ? x.addr(kCharacterTable) + (uint32_t)(entry - table) * sizeof(CharacterEntry) : 0;
}
void swap_FindCharacterEntry(SwapCtx &x) { x.ret(findEntryAddress(x, x.arg(0))); }
SwapReg reg_FindCharacterEntry({"libcharalist.FindCharacterEntry", MODULE, 0x62D8, swap_FindCharacterEntry, true});

/* ---- FUN_00000794(key, table, count, stride): the index of the entry whose first halfword is `key`, -1 when none (the same search as the Duel Ranking's) ---- */
void swap_FindEntryIndex(SwapCtx &x) {
    x.ret((uint32_t)FindEntryIndex((uint16_t)x.arg(0), x.mem(x.arg(1)), (int32_t)x.arg(2), (int32_t)x.arg(3)));
}
SwapReg reg_FindEntryIndex({"libcharalist.FindEntryIndex", MODULE, 0x794, swap_FindEntryIndex, true});

/* ---- FUN_00000244: the handle of the character database, 0 while it is not loaded. The first call starts reading the file. ---- */
uint32_t loadDatabase(SwapCtx &x) {
    uint32_t &database = x.at<uint32_t>(x.addr(kDatabase));
    if (database != 0) return database;
    uint8_t &loadFlags = x.at<uint8_t>(x.addr(kLoadFlags));
    if (loadFlags & 1) {
        loadFlags &= 0xFE;
        const uint32_t path = x.addr(kDatabasePath);
        const uint32_t size = ehsys::FileSize(x, kSceneDevice, path);
        database = alloc(x, size);
        ehsys::ReadFileAsync(x, kSceneDevice, path, database, size, x.addr(kDatabaseLoaded), database);
    }
    return 0;
}
void swap_LoadDatabase(SwapCtx &x) { x.ret(loadDatabase(x)); }
SwapReg reg_LoadDatabase({"libcharalist.LoadDatabase", MODULE, 0x244, swap_LoadDatabase});

/* ---- FUN_000002f4(key): a named picture or table of the database, loaded the first time it is asked for. Its file name has the language's letter in it. ---- */
uint32_t databaseResource(SwapCtx &x, uint32_t key) {
    DatabaseResource *table = (DatabaseResource *)x.mem(x.addr(kResourceTable));
    const int32_t index = FindEntryIndex((uint16_t)key, (const uint8_t *)table, kResourceCount, sizeof(DatabaseResource));
    if (index < 0) return 0;
    DatabaseResource &resource = table[index];
    if (resource.handle != 0) return resource.handle;
    const uint32_t database = loadDatabase(x);
    if (database == 0) return 0;
    const uint32_t language = ehsys::GetLanguage(x);
    const uint32_t letter = (uint32_t)(int32_t)(int8_t)x.at<uint8_t>(x.addr(kLocaleLetters) + language);
    GuestStack name(x, 256);
    x.callImport("sprintf", name.addr(), resource.nameFormat, letter);
    resource.handle = ehsys::FindTexture(x, database, name.addr(), 0);
    return resource.handle;
}
void swap_DatabaseResource(SwapCtx &x) { x.ret(databaseResource(x, x.arg(0))); }
SwapReg reg_DatabaseResource({"libcharalist.DatabaseResource", MODULE, 0x2F4, swap_DatabaseResource});

/* ---- FUN_00000810: seeds the random generator (the story picks a random character) ---- */
void swap_SeedGenerator(SwapCtx &x) { x.callNid(ehsys::nid::SeedRandom); x.ret(0); }
SwapReg reg_SeedGenerator({"libcharalist.SeedGenerator", MODULE, 0x810, swap_SeedGenerator});

/* ---- B80F59AD: gives back everything the module loaded (the database and the pictures) ---- */
void swap_Unload(SwapCtx &x) {
    uint32_t &database = x.at<uint32_t>(x.addr(kDatabase));
    if (database != 0) { release(x, database); database = 0; }
    uint32_t *pictures = (uint32_t *)x.mem(x.addr(kPictures));
    for (int32_t character = 0; character < kCharacterCount; character++) {
        for (int32_t picture = 0; picture < 3; picture++) {
            uint32_t &handle = pictures[character * 3 + picture];
            if (handle != 0) { release(x, handle); handle = 0; }
        }
    }
    uint32_t &background = x.at<uint32_t>(x.addr(kBackground));
    if (background != 0) { release(x, background); background = 0; }
}
SwapReg reg_Unload({"libcharalist.Unload", MODULE, 0x178, swap_Unload});

/* ---- FUN_0000581c(list, source, count): the third list, one entry per source pair (order, id): the character and its category. Returns the entry count. ---- */
uint32_t buildSimpleList(SwapCtx &x, uint32_t list, uint32_t source, uint32_t count) {
    uint32_t built = 0;
    GuestStack category(x, 8);
    for (uint32_t i = 0; i < (count & 0xFFFF); i++) {
        const uint16_t *pair = (const uint16_t *)x.mem(source + i * 4);
        ListEntry &entry = x.at<ListEntry>(list + built * kEntryBytes);
        entry.id = (uint16_t)((entry.id & 0xFC00) | (pair[1] & 0x3FF));
        entry.order = (uint16_t)((entry.order & 0xFC00) | (pair[0] & 0x3FF) | kNoDetailFlag);
        const uint32_t sixteenBit = x.callNid(ehsys::nid::CharacterCategory, pair[1], category.addr());
        uint8_t &valueByte = x.at<uint8_t>(list + built * kEntryBytes + 8);
        valueByte = (uint8_t)((valueByte & 0xF8) | (x.at<uint8_t>(category.addr()) & 7));
        x.at<uint16_t>(list + built * kEntryBytes + 10) = (uint16_t)sixteenBit;
        built = (built + 1) & 0xFFFF;
    }
    return built;
}
void swap_BuildSimpleList(SwapCtx &x) { x.ret(buildSimpleList(x, x.arg(0), x.arg(1), x.arg(2))); }
void regions_BuildSimpleList(SwapCtx &x, Regions &out) { out.push_back({x.arg(0), (x.arg(2) & 0xFFFF) * kEntryBytes}); }
SwapReg reg_BuildSimpleList({"libcharalist.BuildSimpleList", MODULE, 0x581C, swap_BuildSimpleList, false, regions_BuildSimpleList});

/* ---- FUN_0000519c(list, source, count): the first and second lists, one entry per source pair (order, id) whose character is in the table, each with a
 * detail record: how many pictures it has in this mode, their values and names. A few characters get special treatment until a save flag reveals them
 * (their second picture stands in for the first, and they show one picture only). Returns the entry count. ---- */
void showAlternatePictureOnly(SwapCtx &x, uint32_t special, uint32_t idOfStandIn, uint32_t flag, uint32_t forcedCategory, bool copyExtra) {
    if (special == 0) return;
    DetailRecord &d = detail(x, special);
    if (!ehsys::SaveFlag(x, flag)) {
        const CharacterEntry &standIn = *(const CharacterEntry *)x.mem(findEntryAddress(x, idOfStandIn));
        d.picture[0] = standIn.picture[1];
        if (forcedCategory) d.category[0] = (uint8_t)forcedCategory;
        d.text[0] = d.text[1];
        if (copyExtra && (d.flags & 0x40)) d.extra[0] = d.extra[1];
        d.flags = (uint16_t)((d.flags & 0x807F) | ((((d.flags & 0x7FFF) >> 7) + 1) & 0xFF) << 7);
        d.flags = (uint16_t)((d.flags & 0xFFCF) | 0x10);
        if (d.pictureCount() > 1) { d.picture[1] = 0; d.text[1] = 0; }
        d.flags = (uint16_t)((d.flags & 0xFFFC) | 1);
    } else if (forcedCategory && d.pictureCount() > 1) {
        d.category[1] = (uint8_t)forcedCategory;
    }
}
uint32_t buildDetailList(SwapCtx &x, uint32_t list, uint32_t source, uint32_t count) {
    uint32_t firstSpecial = 0, secondSpecial = 0, thirdSpecial = 0;
    uint32_t built = 0;
    for (uint32_t i = 0; i < (count & 0xFFFF); i++) {
        const uint16_t *pair = (const uint16_t *)x.mem(source + i * 4);
        const uint32_t id = pair[1];
        const uint32_t entryAddress = findEntryAddress(x, id);
        if (entryAddress == 0) continue;
        const CharacterEntry &entry = *(const CharacterEntry *)x.mem(entryAddress);
        const uint32_t block = alloc(x, kDetailBytes);
        x.callNid(ehsys::nid::MemSet, block, 0, kDetailBytes);
        DetailRecord &d = detail(x, block);
        uint32_t pictures = entry.pictures();
        const uint32_t mode = state(x).modeFlags & 0xF;
        if (mode == kModeStory) {
            if (entry.limitedToTwo()) { if (pictures > 2) pictures = 2; } else pictures = 1;
        } else if (mode == kModeCards) {
            pictures = 1;
        } else if ((mode == kModeCharactersMetToo || mode == kModeCharactersMet) && x.callNid(ehsys::nid::GetUiMode) == 0 && ehsys::CardState(x, id) == 3) {
            d.flags = (uint16_t)((d.flags & 0xFFBF) | 0x40);   /* the character's card is fully owned */
        }
        if (pictures != 0) d.picture[0] = entry.picture[0];
        if (pictures > 1) d.picture[1] = entry.picture[1];
        if (pictures >= 3) d.picture[2] = entry.picture[2];
        d.flags = (uint16_t)((d.flags & 0xFFFC) | pictures);
        const uint16_t *textIndex = (const uint16_t *)x.mem(x.addr(kTextIndexes) + id * 4);
        const uint16_t *progress = (const uint16_t *)x.mem(x.addr(kProgressNames) + id * 6);
        for (uint32_t picture = 0; picture < pictures; picture++) {
            d.category[picture] = (uint8_t)entry.category();
            d.text[picture] = ehsys::GetText(x, databaseResource(x, kDatabaseTextKey), textIndex[0]);
            if (d.flags & 0x40) d.extra[picture] = (uint8_t)progress[picture];
        }
        if (textIndex[1] != 0) d.text[1] = ehsys::GetText(x, databaseResource(x, kDatabaseTextKey), textIndex[1]);
        d.flags = (uint16_t)((d.flags & 0x807F) | (uint16_t)(entry.countdown << 7));
        if (id == kIdSharedCategory) d.category[1] = 6;
        else if (id == kIdThirdSpecial) thirdSpecial = block;
        else if (id == kIdSecondSpecial) secondSpecial = block;
        else if (id == kIdFirstSpecial) firstSpecial = block;
        ListEntry &out = x.at<ListEntry>(list + built * kEntryBytes);
        out.id = (uint16_t)((out.id & 0xFC00) | (id & 0x3FF));
        out.order = (uint16_t)(((out.order & 0xFC00) | (pair[0] & 0x3FF)) & ~kNoDetailFlag);
        out.detail = block;
        built = (built + 1) & 0xFFFF;
    }
    showAlternatePictureOnly(x, firstSpecial, kIdFirstSpecial, kSaveFlagFirstSpecial, kCategoryFirstSpecial, true);
    showAlternatePictureOnly(x, secondSpecial, kIdSecondSpecial, kSaveFlagSecondSpecial, 0, true);
    showAlternatePictureOnly(x, thirdSpecial, kIdThirdSpecial, kSaveFlagThirdSpecial, kCategoryThirdSpecial, false);
    return built;
}
void swap_BuildDetailList(SwapCtx &x) { x.ret(buildDetailList(x, x.arg(0), x.arg(1), x.arg(2))); }
SwapReg reg_BuildDetailList({"libcharalist.BuildDetailList", MODULE, 0x519C, swap_BuildDetailList});   /* not verifiable: it takes heap blocks */

/* ---- FUN_00003f68: builds the three lists from the characters the player can see in the current mode. The ids 2..151 are walked in order; each one with a
 * category from 1 to 6 takes the next position of the roster, and goes into the list of its kind when it qualifies. ---- */
bool qualifies(SwapCtx &x, uint32_t id, uint32_t mode) {
    if (mode == kModeStory || mode == kModeCharactersMetToo || mode == kModeCharactersMet) {
        const uint32_t record = x.callNid(ehsys::nid::GetProgressRecord, id);
        return x.at<uint16_t>(record + 2) != 0;
    }
    if (mode == kModeCards) {
        const uint32_t cardState = ehsys::CardState(x, id);
        if (cardState == 1) return ((state(x).modeFlags >> 4) & 3) == 2;
        return cardState == 2 || cardState == 3;
    }
    return false;
}
void buildLists(SwapCtx &x) {
    uint32_t temporary[3], count[3] = {0, 0, 0};
    for (int32_t i = 0; i < 3; i++) temporary[i] = alloc(x, kListBufferEntries * 4);
    GuestStack scratch(x, 8);
    uint32_t order = 1;
    for (uint32_t id = kFirstCharacterId; id < kMaxCharacterId; id++) {
        uint32_t &category = x.at<uint32_t>(scratch.addr());
        category = 0;
        const uint32_t kind = x.callNid(ehsys::nid::CharacterKind, id);
        if (kind == 2 || kind == 1) {
            const CharacterEntry *entry = (const CharacterEntry *)x.mem(findEntryAddress(x, id & 0xFFFF));
            category = entry->category();
        } else {
            x.callNid(ehsys::nid::CharacterCategory, id, scratch.addr());
        }
        if ((int32_t)category <= 0 || (int32_t)category >= 7) continue;
        const uint32_t mode = state(x).modeFlags & 0xF;
        if (qualifies(x, id, mode)) {
            const uint32_t listKind = x.callNid(ehsys::nid::CharacterKind, id);
            const uint32_t which = listKind == 2 ? 1 : listKind == 1 ? 0 : 2;
            uint16_t *slot = (uint16_t *)x.mem(temporary[which] + count[which] * 4);
            slot[0] = (uint16_t)order;
            slot[1] = (uint16_t)id;
            count[which]++;
        }
        order++;
    }
    State &s = state(x);
    for (int32_t i = 0; i < 3; i++) s.list[i] = count[i] != 0 ? alloc(x, count[i] << 4) : 0;
    s.count[0] = (uint16_t)buildDetailList(x, s.list[0], temporary[0], count[0] & 0xFFFF);
    s.count[1] = (uint16_t)buildDetailList(x, s.list[1], temporary[1], count[1] & 0xFFFF);
    s.count[2] = (uint16_t)buildSimpleList(x, s.list[2], temporary[2], count[2] & 0xFFFF);
    for (int32_t i = 0; i < 3; i++) release(x, temporary[i]);
}
void swap_BuildLists(SwapCtx &x) { buildLists(x); }
SwapReg reg_BuildLists({"libcharalist.BuildLists", MODULE, 0x3F68, swap_BuildLists});   /* not verifiable: it takes heap blocks */

/* ---- FUN_00000bec: frees the lists and their detail records (and the kernel pool, if any). Returns 1 once done. ---- */
void swap_FreeLists(SwapCtx &x) {
    State &s = state(x);
    if ((s.modeFlags & 0xF) != kModeStory && x.callLocal(0x140) == 0) { x.ret(0); return; }
    for (int32_t list = 0; list < 3; list++) {
        for (uint32_t i = 0; i < s.count[list]; i++) {
            const ListEntry &entry = x.at<ListEntry>(s.list[list] + i * kEntryBytes);
            if (!(entry.order & kNoDetailFlag)) release(x, entry.detail);
        }
        if (s.list[list] != 0) release(x, s.list[list]);
    }
    if (s.fplPool >= 1) {
        x.callImport("sceKernelFreeFpl", (uint32_t)s.fplPool, s.fplBlock);
        x.callImport("sceKernelDeleteFpl", (uint32_t)s.fplPool);
    }
    x.ret(1);
}
SwapReg reg_FreeLists({"libcharalist.FreeLists", MODULE, 0xBEC, swap_FreeLists});

/* ---- E11636EC(heap): starts the story's pick: builds the lists, chooses one entry at random (all the entries of the first two lists count; the third list
 * only when those are empty) and remembers its character and picture, then frees the lists. ---- */
void swap_Start(SwapCtx &x) {
    const uint32_t heapArgument = x.arg(0);
    x.callNid(ehsys::nid::MemSet, x.addr(kState), 0, sizeof(State));
    State &s = state(x);
    s.modeFlags = (uint8_t)((s.modeFlags & 0xF0) | kModeStory);
    s.chosenCharacter = -1;
    s.chosenPicture = -1;
    s.heap = heapArgument;
    buildLists(x);
    int32_t total = 0;
    for (int32_t list = 0; list < 3; list++) {
        total += s.count[list];
        if (list == 1 && total > 0) break;
    }
    if (total < 1) { s.chosenCharacter = 0; s.chosenPicture = 0; return; }
    int32_t pick = (int32_t)x.callNid(ehsys::nid::RandomNumber) % total;
    int32_t list = 0;
    for (; list < 3; list++) {
        if (pick < (int32_t)s.count[list]) break;
        pick -= s.count[list];
    }
    const uint32_t entryAddress = pick < (int32_t)s.count[list] ? s.list[list] + (uint32_t)pick * kEntryBytes : 0;
    const ListEntry &entry = x.at<ListEntry>(entryAddress);
    s.chosenPicture = 0;
    s.chosenCharacter = entry.id & 0x3FF;
    if (!(entry.order & kNoDetailFlag)) {
        const uint8_t detailByte = x.at<uint8_t>(entry.detail);
        if ((detailByte & 3) < 2) {
            const uint32_t pictureState = (detailByte & 0x3F) >> 4;
            if (pictureState != 0) s.chosenPicture = (int32_t)pictureState;
        } else {
            s.chosenPicture = (int32_t)x.callNid(ehsys::nid::RandomNumber) % (int32_t)(detailByte & 3);
        }
    }
    x.callLocal(0xBEC);
}
SwapReg reg_Start({"libcharalist.Start", MODULE, 0xD20, swap_Start});   /* not verifiable: heap and random numbers */

/* ---- 342A4009 / BAC2BA34: the chosen character and picture, -1 while the choice is hidden ---- */
void swap_ChosenCharacter(SwapCtx &x) { const State &s = state(x); x.ret((s.disabled & 1) ? 0xFFFFFFFFu : (uint32_t)s.chosenCharacter); }
SwapReg reg_ChosenCharacter({"libcharalist.ChosenCharacter", MODULE, 0xEF8, swap_ChosenCharacter, true});
void swap_ChosenPicture(SwapCtx &x) { const State &s = state(x); x.ret((s.disabled & 1) ? 0xFFFFFFFFu : (uint32_t)s.chosenPicture); }
SwapReg reg_ChosenPicture({"libcharalist.ChosenPicture", MODULE, 0xF20, swap_ChosenPicture, true});

}  // namespace
