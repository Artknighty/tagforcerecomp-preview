/* libstory: how the story code is wired to the save data and to its tables. At the start of the story `Start` (FUN_08f0540c) calls `ResetAll`, which zeroes the
 * tables of every part of the story (the characters, the mail, the lessons, the morning routine, the items, the cards, the field...), and then one small
 * function per part that stores the guest addresses of its save-data block and of its script tables in the module's data. The parts only differ by
 * their addresses, so the functions here are written once, over addresses. */
#include "../common/swap.h"
#include "../common/ehsys.h"
#include "story_data.h"
#include <utility>

#define MODULE "libstory"

namespace {
using namespace story;

/* a function that stores its arguments, in order, at the given module data words */
template <uint32_t... Addresses> void storeArguments(SwapCtx &x) {
    int32_t next = 0;
    ((datum<uint32_t>(x, Addresses) = x.arg(next++)), ...);
}
template <uint32_t... Addresses> void storeRegions(SwapCtx &x, Regions &out) { (out.push_back({guest(x, Addresses), 4}), ...); }
#define STORE(name, address, ...) \
    SwapReg reg_##name({"libstory." #name, MODULE, offsetOf(address), storeArguments<__VA_ARGS__>, false, storeRegions<__VA_ARGS__>})

/* a function that zeroes a block of module data */
template <uint32_t Address, uint32_t Bytes> void zeroBlock(SwapCtx &x) { x.callNid(ehsys::nid::MemSet, guest(x, Address), 0, Bytes); }
template <uint32_t Address, uint32_t Bytes> void zeroRegions(SwapCtx &x, Regions &out) { out.push_back({guest(x, Address), Bytes}); }
#define ZERO(name, address, block, bytes) \
    SwapReg reg_##name({"libstory." #name, MODULE, offsetOf(address), zeroBlock<block, bytes>, false, zeroRegions<block, bytes>})

/* ---- binding the parts to the save data (the arguments are addresses inside the save block) ---- */
STORE(BindCharacters, 0x08EFCC98, 0x08F16420, 0x08F1641C);        /* (the 150 character states, the progress flag bits) */
STORE(BindPlayer, 0x08EFED0C, 0x08F2189C, 0x08F218A0);
STORE(BindRegisters, 0x08EFF45C, 0x08F218A4);
STORE(BindCalendar, 0x08F03E0C, 0x08F21B44, 0x08F21B48, 0x08F21B40);   /* (the Date, the day seen last, the story's flags) */
STORE(BindSystemMessages, 0x08F046A0, 0x08F21B64, 0x08F21B68);
STORE(BindCharacterMessages, 0x08F046DC, 0x08F21B4C, 0x08F21B50, 0x08F21B54);
STORE(BindMapMessages, 0x08F0472C, 0x08F21B58, 0x08F21B5C, 0x08F21B60);
STORE(BindFieldTables, 0x08F0477C, 0x08F21B6C, 0x08F21B70);       /* FieldCon.bin and FieldPos.bin */
STORE(BindMode, 0x08F0F2AC, 0x091C44D0);                          /* the story mode block (its +4 is the current mode) */
STORE(BindFieldState, 0x08F09500, 0x091C1D44);
STORE(BindMail, 0x08F0BD18, 0x091C3A1C);
STORE(BindMailTable, 0x08F0BD48, 0x091C3A20);                     /* MailESM.bin */
STORE(BindMailTexts, 0x08F0BD54, 0x091C3A24, 0x091C3A28);         /* MailEL / MailEI */
STORE(BindItems, 0x08F0C2C8, 0x091C3A2C);
STORE(BindItemTable, 0x08F0C2E8, 0x091C3A30);                     /* ItemCon.bin */
STORE(BindCards, 0x08F0E0C0, 0x091C3CD8);
STORE(BindCardTable, 0x08F0E0CC, 0x091C3CDC);                     /* CardCon.bin */
STORE(BindShop, 0x08F0E570, 0x091C3D60);
STORE(BindLessons, 0x08F0F014, 0x091C44BC);
STORE(BindLessonPlan, 0x08F0F034, 0x091C44C0);                    /* LessonLesPlan.bin */
STORE(BindLessonScript, 0x08F0F040, 0x091C44C4);                  /* LessonESM.bin */
STORE(BindLessonTexts, 0x08F0F04C, 0x091C44C8, 0x091C44CC);       /* LessonEL / LessonEI */
STORE(SetFlag44D4, 0x08F0FA7C, 0x091C44D4);
STORE(SetFlag44D8, 0x08F0FBDC, 0x091C44D8);
STORE(BindMorning, 0x08F0FC10, 0x091C44E0);
STORE(BindMorningPlan, 0x08F0FC30, 0x091C44E4);                   /* MorningMorPlan.bin */
STORE(BindMorningScript, 0x08F0FC3C, 0x091C44E8);                 /* MorningESM.bin */
STORE(BindMorningTexts, 0x08F0FC48, 0x091C44EC, 0x091C44F0);      /* MorningEL / MorningEI */
STORE(BindModuleList, 0x08F1034C, 0x091C4530);

/* ---- zeroing the parts' tables ---- */
ZERO(ResetCharacterScripts, 0x08EFCC70, 0x08F16424, 0xB478);      /* the 150 CharacterScript records */
ZERO(ResetPlayer1D28, 0x08F053E4, 0x091C1D28, 0x1C);
ZERO(ResetMessages1D5C, 0x08F0B11C, 0x091C1D5C, 0x10);
ZERO(ResetMode44D4, 0x08F0F284, 0x091C44D4, 0xC);
ZERO(ResetMailTables, 0x08F0BCF0, 0x091C3A20, 0xC);
ZERO(ResetCalendarTables, 0x08F03DE4, 0x08F21B4C, 0x28);
ZERO(ResetFieldState, 0x08F094D8, 0x091C1D48, 0x10);
ZERO(ResetItemTables, 0x08F0C2A0, 0x091C3A30, 0x84);
ZERO(ResetCardTables, 0x08F0E098, 0x091C3CDC, 0x84);
ZERO(ResetShopTables, 0x08F0C678, 0x091C3AB4, 0x224);
ZERO(ResetShopFlags, 0x08F0E548, 0x091C3D64, 8);
ZERO(ResetLessonTables, 0x08F0EFEC, 0x091C44C0, 0x10);
ZERO(ResetMorningTables, 0x08F0FBE8, 0x091C44E4, 0x10);
ZERO(ResetField1D58, 0x08F09718, 0x091C1D58, 2);
ZERO(ResetModuleSlots, 0x08F0FEF4, 0x091C44F4, 0x38);
ZERO(ResetPreloadFlags, 0x08F101B8, 0x091C452C, 4);
ZERO(ResetModuleCursor, 0x08F10324, 0x091C4534, 4);

/* ---- the character script records (FUN_08efcd04 .. cdb0): one 0x134-byte record per character at 0x08F16424 ---- */
uint32_t scriptRecord(SwapCtx &x, uint32_t character) { return guest(x, kCharacterScripts) + character * (uint32_t)sizeof(CharacterScript); }
void swap_SetCharacterTables(SwapCtx &x) {   /* (character, a, b, c, d): the four tables at +0x00 .. +0x0C */
    const uint32_t record = scriptRecord(x, x.arg(0));
    for (int32_t i = 0; i < 4; i++) x.at<uint32_t>(record + 4 * i) = x.arg(1 + i);
}
void regions_SetCharacterTables(SwapCtx &x, Regions &out) { out.push_back({scriptRecord(x, x.arg(0)), 16}); }
SwapReg reg_SetCharacterTables({"libstory.SetCharacterTables", MODULE, offsetOf(0x08EFCD04), swap_SetCharacterTables, false, regions_SetCharacterTables});
template <uint32_t Field> void storeCharacterField(SwapCtx &x) { x.at<uint32_t>(scriptRecord(x, x.arg(0)) + Field) = x.arg(1); }
template <uint32_t Field> void regionsCharacterField(SwapCtx &x, Regions &out) { out.push_back({scriptRecord(x, x.arg(0)) + Field, 4}); }
#define CHARACTER_FIELD(name, address, field) \
    SwapReg reg_##name({"libstory." #name, MODULE, offsetOf(address), storeCharacterField<field>, false, regionsCharacterField<field>})
CHARACTER_FIELD(SetCharacterTable10, 0x08EFCD30, 0x10);
CHARACTER_FIELD(SetCharacterRules14, 0x08EFCD50, 0x14);
CHARACTER_FIELD(SetCharacterTable18, 0x08EFCD70, 0x18);
CHARACTER_FIELD(SetCharacterTable1C, 0x08EFCD90, 0x1C);
CHARACTER_FIELD(SetCharacterSchedule20, 0x08EFCDB0, 0x20);

/* ---- small reads and writes through those addresses ---- */
void swap_FieldStateMode(SwapCtx &x) { x.ret(x.at<uint32_t>(datum<uint32_t>(x, 0x091C1D44) + 4)); }
SwapReg reg_FieldStateMode({"libstory.FieldStateMode", MODULE, offsetOf(0x08F09688), swap_FieldStateMode, true});
void swap_SetFieldStateHalfword(SwapCtx &x) { x.at<uint16_t>(datum<uint32_t>(x, 0x091C1D44) + 0x10) = (uint16_t)x.arg(0); }
void regions_SetFieldStateHalfword(SwapCtx &x, Regions &out) { out.push_back({datum<uint32_t>(x, 0x091C1D44) + 0x10, 2}); }
SwapReg reg_SetFieldStateHalfword({"libstory.SetFieldStateHalfword", MODULE, offsetOf(0x08F096C8), swap_SetFieldStateHalfword, false, regions_SetFieldStateHalfword});
void swap_StoryMode(SwapCtx &x) { x.ret(x.at<uint32_t>(datum<uint32_t>(x, 0x091C44D0) + 4)); }
SwapReg reg_StoryMode({"libstory.StoryMode", MODULE, offsetOf(0x08F0FAA8), swap_StoryMode, true});
void swap_SetStoryMode(SwapCtx &x) { x.at<uint32_t>(datum<uint32_t>(x, 0x091C44D0) + 4) = x.arg(0); }
void regions_SetStoryMode(SwapCtx &x, Regions &out) { out.push_back({datum<uint32_t>(x, 0x091C44D0) + 4, 4}); }
SwapReg reg_SetStoryMode({"libstory.SetStoryMode", MODULE, offsetOf(0x08F0FA98), swap_SetStoryMode, false, regions_SetStoryMode});
void swap_StoryPhase(SwapCtx &x) { x.ret(datum<uint32_t>(x, 0x091C44DC)); }
SwapReg reg_StoryPhase({"libstory.StoryPhase", MODULE, offsetOf(0x08F0FA28), swap_StoryPhase, true});
void swap_StoryPhaseIsOne(SwapCtx &x) { x.ret(datum<uint32_t>(x, 0x091C44DC) == 1 ? 1 : 0); }
SwapReg reg_StoryPhaseIsOne({"libstory.StoryPhaseIsOne", MODULE, offsetOf(0x08F0FA34), swap_StoryPhaseIsOne, true});
void swap_StepState(SwapCtx &x) { x.ret(datum<uint32_t>(x, 0x091C1D28)); }
SwapReg reg_StepState({"libstory.StepState", MODULE, offsetOf(0x08F05F00), swap_StepState, true});

/* FUN_08f0f9b8(phase): accepts the phases 1..4 (the first one ever is turned into 1 by the save: any other value resets the progress); returns 1 when accepted */
void swap_SetStoryPhase(SwapCtx &x) {
    const int32_t phase = (int32_t)x.arg(0);
    if (phase == 4 || phase == 3 || phase == 2 || phase == 1) { datum<uint32_t>(x, 0x091C44DC) = (uint32_t)phase; x.ret(1); return; }
    x.callNid(ehsys::nid::RecordChapterCard, 1, 0);
    datum<uint32_t>(x, 0x091C44DC) = 1;
    x.ret(0);
}
SwapReg reg_SetStoryPhase({"libstory.SetStoryPhase", MODULE, offsetOf(0x08F0F9B8), swap_SetStoryPhase});

/* FUN_08f0fa34's neighbours: a callback slot the step functions run (FUN_08f10064, FUN_08f05f0c) */
void swap_RunStepCallback(SwapCtx &x) {
    const uint32_t callback = datum<uint32_t>(x, 0x091C4528);
    if (callback != 0) x.callAddr(callback);
}
SwapReg reg_RunStepCallback({"libstory.RunStepCallback", MODULE, offsetOf(0x08F10064), swap_RunStepCallback});
void swap_Step(SwapCtx &x) { x.callLocal(offsetOf(0x08F10064)); const uint32_t callback = datum<uint32_t>(x, 0x091C1D3C); if (callback != 0) x.callAddr(callback); }
SwapReg reg_Step({"libstory.RunCallbacks", MODULE, offsetOf(0x08F05F0C), swap_Step});

/* FUN_08f10154(index): is this one of the 12 loaded places' modules (a module id above 0)? */
void swap_ModuleLoaded(SwapCtx &x) {
    const int32_t index = (int32_t)x.arg(0);
    x.ret(index < 0 || 11 < index ? 0 : (int32_t)datum<uint32_t>(x, 0x091C44F8 + 4 * index) > 0 ? 1 : 0);
}
SwapReg reg_ModuleLoaded({"libstory.ModuleLoaded", MODULE, offsetOf(0x08F10154), swap_ModuleLoaded, true});

}  // namespace
