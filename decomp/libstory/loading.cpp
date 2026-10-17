/* libstory: reading the story's data files. The places' tables live in two archives, the "brain" archive of the story and one with the language's texts;
 * both are read into the first story heap, and their entries (named like "<prefix><number><suffix>.bin") are looked up by name. */
#include "../swap.h"
#include "../ehsys.h"
#include "story_data.h"
#include "story_functions.h"
#include <stdio.h>
#include <string.h>

#define MODULE "libstory"

namespace {
using namespace story;

constexpr uint32_t kDevice = 3;                                   /* the engine's file device */
constexpr uint32_t kPathFormat = 0x08F15D2C;                       /* the format of the path of a file */
constexpr uint32_t kArchiveEnding = 0x08F16218, kLanguageEnding = 0x08F16224;
constexpr uint32_t kScriptFolderDefault = 0x08F163B8, kScriptFolderSecond = 0x08F163CC, kScriptFolderThird = 0x08F163E0;   /* "script/st01_t01_" ... */
constexpr uint32_t kNumberFormat = 0x08F163F4, kTextEnding = 0x08F16238;
constexpr uint32_t kArchiveReadCallback = 0x08F053B4;
constexpr uint32_t kSceneWithThirdFolder = 4;

uint32_t firstHeap(SwapCtx &x) { return datum<uint32_t>(x, kStoryHeaps); }
char *hostString(SwapCtx &x, uint32_t guestAddress) { return (char *)x.mem(guestAddress); }

/* ---- FUN_08f053b4: the archive has been read: registers it and says so ---- */
void swap_ArchiveRead(SwapCtx &x) {
    ehsys::RegisterArchive(x, 0, datum<uint32_t>(x, kArchiveBuffer));
    datum<uint32_t>(x, kArchiveDone) = 1;
}
SwapReg reg_ArchiveRead({"libstory.ArchiveRead", MODULE, offsetOf(kArchiveReadCallback), swap_ArchiveRead});

/* ---- FUN_08f051fc(name): reads a file into the first heap and waits for it; returns the buffer, 0 on failure ---- */
uint32_t readArchive(SwapCtx &x, uint32_t name) {
    datum<uint32_t>(x, kArchiveBuffer) = 0;
    datum<uint32_t>(x, kArchiveSize) = 0;
    datum<uint32_t>(x, kArchiveDone) = 0;
    GuestStack path(x, 512);
    x.callImport("sprintf", path.addr(), guest(x, kPathFormat), name);
    const int32_t size = (int32_t)ehsys::FileSize(x, kDevice, path.addr());
    datum<uint32_t>(x, kArchiveSize) = (uint32_t)size;
    if (size < 1) return 0;
    const uint32_t buffer = x.callNid(ehsys::nid::Alloc, firstHeap(x), (uint32_t)size);
    datum<uint32_t>(x, kArchiveBuffer) = buffer;
    if (buffer == 0) return 0;
    ehsys::ReadFileAsync(x, kDevice, path.addr(), buffer, datum<uint32_t>(x, kArchiveSize), x.addr(offsetOf(kArchiveReadCallback)), buffer);
    while (datum<uint32_t>(x, kArchiveDone) == 0) x.callNid(ehsys::nid::WaitFrame);
    return datum<uint32_t>(x, kArchiveBuffer);
}
void swap_ReadArchive(SwapCtx &x) { x.ret(readArchive(x, x.arg(0))); }
SwapReg reg_ReadArchive({"libstory.ReadArchive", MODULE, offsetOf(0x08F051FC), swap_ReadArchive});

/* ---- historia_caminho_construir (FUN_08f0bb90)(buffer, prefix, number, suffix1, suffix2, ending): the name of an entry: the prefix, the number plus one,
 * the two optional suffixes and the ending, in a 32-byte buffer ---- */
void buildEntryName(SwapCtx &x, uint32_t buffer, uint32_t prefix, uint32_t number, uint32_t suffix1, uint32_t suffix2, uint32_t ending) {
    char *out = hostString(x, buffer);
    memset(out, 0, 0x20);
    strcpy(out, hostString(x, prefix));
    char digits[16] = {};
    snprintf(digits, sizeof digits, hostString(x, guest(x, kNumberFormat)), (int32_t)(number + 1));
    strcat(out, digits);
    if (suffix1 != 0) strcat(out, hostString(x, suffix1));
    if (suffix2 != 0) strcat(out, hostString(x, suffix2));
    strcat(out, hostString(x, ending));
}
void swap_BuildEntryName(SwapCtx &x) { buildEntryName(x, x.arg(0), x.arg(1), x.arg(2), x.arg(3), x.arg(4), x.arg(5)); }
void regions_BuildEntryName(SwapCtx &x, Regions &out) { out.push_back({x.arg(0), 0x20}); }
SwapReg reg_BuildEntryName({"libstory.BuildEntryName", MODULE, offsetOf(0x08F0BB90), swap_BuildEntryName, false, regions_BuildEntryName});

/* ---- FUN_08f0bb58(archive, prefix, number, suffix1, suffix2, ending): the entry of the archive with that name ---- */
uint32_t findEntry(SwapCtx &x, uint32_t archive, uint32_t prefix, uint32_t number, uint32_t suffix1, uint32_t suffix2, uint32_t ending) {
    GuestStack name(x, 48);
    buildEntryName(x, name.addr(), prefix, number, suffix1, suffix2, ending);
    return ehsys::FindTexture(x, archive, name.addr(), 0);
}
void swap_FindEntry(SwapCtx &x) { x.ret(findEntry(x, x.arg(0), x.arg(1), x.arg(2), x.arg(3), x.arg(4), x.arg(5))); }
SwapReg reg_FindEntry({"libstory.FindEntry", MODULE, offsetOf(0x08F0BB58), swap_FindEntry, true});

/* ---- FUN_08f0bc6c(name): a text entry of the language's archive: the name, the language's letters and the ending ---- */
uint32_t findTextEntry(SwapCtx &x, uint32_t name) {
    GuestStack buffer(x, 80);
    char *out = hostString(x, buffer.addr());
    memset(out, 0, 0x40);
    strcpy(out, hostString(x, name));
    strcat(out, hostString(x, x.at<uint32_t>(guest(x, kLanguageSuffixes) + 4 * datum<uint32_t>(x, kLanguageIndex))));
    strcat(out, hostString(x, guest(x, kTextEnding)));
    return ehsys::FindTexture(x, datum<uint32_t>(x, kTextArchive), buffer.addr(), 0);
}
void swap_FindTextEntry(SwapCtx &x) { x.ret(findTextEntry(x, x.arg(0))); }
SwapReg reg_FindTextEntry({"libstory.FindTextEntry", MODULE, offsetOf(0x08F0BC6C), swap_FindTextEntry, true});

/* ---- FUN_08f0babc(name, scene): adds the folder of the scene's scripts to the name ---- */
void appendScriptFolder(SwapCtx &x, uint32_t name, uint32_t scene) {
    const uint32_t folder = scene == kSceneWithThirdFolder ? kScriptFolderThird : (scene == 3 || scene == 2) ? kScriptFolderSecond : kScriptFolderDefault;
    strcat(hostString(x, name), hostString(x, guest(x, folder)));
}
void swap_AppendScriptFolder(SwapCtx &x) { appendScriptFolder(x, x.arg(0), x.arg(1)); }
void regions_AppendScriptFolder(SwapCtx &x, Regions &out) { out.push_back({x.arg(0), 0x20}); }
SwapReg reg_AppendScriptFolder({"libstory.AppendScriptFolder", MODULE, offsetOf(0x08F0BABC), swap_AppendScriptFolder, false, regions_AppendScriptFolder});

/* ---- FUN_08f0b170(scene): opens the two archives of the scene: the script one and the one with the language's texts. Returns 1 when both are open. ---- */
void swap_OpenArchives(SwapCtx &x) {
    const uint32_t scene = x.arg(0);
    GuestStack names(x, 80);
    const uint32_t scriptName = names.addr() + 32, textName = names.addr();
    memset(hostString(x, scriptName), 0, 32);
    memset(hostString(x, textName), 0, 32);
    const uint32_t language = ehsys::GetLanguage(x);
    datum<uint32_t>(x, kLanguageIndex) = x.callLocal(fn::LanguageIndex, language);
    appendScriptFolder(x, scriptName, scene);
    strcpy(hostString(x, textName), hostString(x, scriptName));
    strcat(hostString(x, scriptName), hostString(x, guest(x, kArchiveEnding)));
    strcat(hostString(x, textName), hostString(x, x.at<uint32_t>(guest(x, kLanguageSuffixes) + 4 * datum<uint32_t>(x, kLanguageIndex))));
    strcat(hostString(x, textName), hostString(x, guest(x, kLanguageEnding)));
    const uint32_t scripts = readArchive(x, scriptName);
    datum<uint32_t>(x, kScriptArchive) = scripts;
    if (scripts == 0) { x.ret(0); return; }
    const uint32_t texts = readArchive(x, textName);
    datum<uint32_t>(x, kTextArchive) = texts;
    x.ret(texts != 0 ? 1 : 0);
}
SwapReg reg_OpenArchives({"libstory.OpenArchives", MODULE, offsetOf(0x08F0B170), swap_OpenArchives});

/* ---- FUN_08f0b268: finds the story's tables in the two archives and binds them: five tables and four place tables per character, then the texts and the
 * tables of each part of the story. Returns 0 when an archive is missing. ---- */
constexpr uint32_t kEntryPrefix = 0x08F1622C;                                                            /* the prefix of the entries of the characters' tables */
constexpr uint32_t kCharacterTableSuffixes[5] = {0x08F16230, 0x08F16240, 0x08F16248, 0x08F1624C, 0x08F16250};
constexpr uint32_t kPlaceSuffix = 0x08F16258, kPlaceSuffixes[4] = {0x08F1625C, 0x08F16260, 0x08F16264, 0x08F16258};   /* (sic) the fourth repeats the first suffix */
constexpr uint32_t kSysMsgL = 0x08F16268, kSysMsgI = 0x08F16270, kImpChrMsgScrTag = 0x08F16278, kImpChrMsgL = 0x08F1628C, kImpChrMsgI = 0x08F16298;
constexpr uint32_t kImpMapMsgScrTag = 0x08F162A4, kImpMapMsgL = 0x08F162B8, kImpMapMsgI = 0x08F162C4, kFieldCon = 0x08F162D0, kFieldPos = 0x08F162E0;
constexpr uint32_t kItemCon = 0x08F162F0, kCardCon = 0x08F162FC, kMailESM = 0x08F16308, kMailEL = 0x08F16314, kMailEI = 0x08F1631C;
constexpr uint32_t kLessonLesPlan = 0x08F16324, kLessonESM = 0x08F16338, kLessonEL = 0x08F16348, kLessonEI = 0x08F16354;
constexpr uint32_t kMorningMorPlan = 0x08F16360, kMorningESM = 0x08F16374, kMorningEL = 0x08F16384, kMorningEI = 0x08F16390;
void swap_LoadTables(SwapCtx &x) {
    const uint32_t scripts = datum<uint32_t>(x, kScriptArchive);
    if (scripts == 0 || datum<uint32_t>(x, kTextArchive) == 0) { x.ret(0); return; }
    const uint32_t setters[5] = {fn::SetCharacterTable10, fn::SetCharacterRules14, fn::SetCharacterTable18, fn::SetCharacterTable1C, fn::SetCharacterSchedule20};
    for (uint32_t character = 0; character < (uint32_t)kCharacterCount; character++) {
        for (int32_t i = 0; i < 5; i++)
            x.callLocal(setters[i], character, findEntry(x, scripts, guest(x, kEntryPrefix), character, 0, guest(x, kCharacterTableSuffixes[i]), guest(x, kTextEnding)));
        uint32_t place[4];
        for (int32_t i = 0; i < 4; i++)
            place[i] = findEntry(x, scripts, guest(x, kEntryPrefix), character, guest(x, kPlaceSuffix), guest(x, kPlaceSuffixes[i]), guest(x, kTextEnding));
        x.callLocal(fn::SetCharacterTables, character, place[0], place[1], place[2], place[3]);
    }
    auto entry = [&](uint32_t name) { return ehsys::FindTexture(x, scripts, guest(x, name), 0); };
    auto text = [&](uint32_t name) { return findTextEntry(x, guest(x, name)); };
    uint32_t first = text(kSysMsgL), second = text(kSysMsgI);
    x.callLocal(fn::BindSystemMessages, first, second);
    first = entry(kImpChrMsgScrTag); second = text(kImpChrMsgL);
    uint32_t third = text(kImpChrMsgI);
    x.callLocal(fn::BindCharacterMessages, first, second, third);
    first = entry(kImpMapMsgScrTag); second = text(kImpMapMsgL); third = text(kImpMapMsgI);
    x.callLocal(fn::BindMapMessages, first, second, third);
    first = entry(kFieldCon); second = entry(kFieldPos);
    x.callLocal(fn::BindFieldTables, first, second);
    x.callLocal(fn::BindItemTable, entry(kItemCon));
    x.callLocal(fn::BindCardTable, entry(kCardCon));
    x.callLocal(fn::BindMailTable, entry(kMailESM));
    first = text(kMailEL); second = text(kMailEI);
    x.callLocal(fn::BindMailTexts, first, second);
    x.callLocal(fn::BindLessonPlan, entry(kLessonLesPlan));
    x.callLocal(fn::BindLessonScript, entry(kLessonESM));
    first = text(kLessonEL); second = text(kLessonEI);
    x.callLocal(fn::BindLessonTexts, first, second);
    x.callLocal(fn::BindMorningPlan, entry(kMorningMorPlan));
    x.callLocal(fn::BindMorningScript, entry(kMorningESM));
    first = text(kMorningEL); second = text(kMorningEI);
    x.callLocal(fn::BindMorningTexts, first, second);
    x.ret(1);
}
SwapReg reg_LoadTables({"libstory.LoadTables", MODULE, offsetOf(0x08F0B268), swap_LoadTables});

}  // namespace
