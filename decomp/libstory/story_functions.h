/* libstory: the functions of the module that other functions call, by offset from the module base. Those listed under "not decompiled yet" are still
 * the generated code; calling them through `callLocal` runs it (and the swaps it contains). */
#pragma once
#include "story_data.h"

namespace story {
namespace fn {

/* calendar and flags */
constexpr uint32_t CurrentTick = offsetOf(0x08F03F48), CurrentMonth = offsetOf(0x08F03F58), CurrentDay = offsetOf(0x08F03F78);
constexpr uint32_t CurrentDayOfYear = offsetOf(0x08F04988), TodayInRange = offsetOf(0x08F04994);
constexpr uint32_t OutsideFirstBreak = offsetOf(0x08F0405C), AfterFirstBreak = offsetOf(0x08F040B4);
constexpr uint32_t PhaseIsOne = offsetOf(0x08F0FA34), StoryMode = offsetOf(0x08F0FAA8), SetStoryMode = offsetOf(0x08F0FA98);
constexpr uint32_t FieldStateMode = offsetOf(0x08F09688);

/* characters */
constexpr uint32_t SeenBit = offsetOf(0x08EFCF70), SetHidden = offsetOf(0x08EFDFDC), IsHidden = offsetOf(0x08EFE024), ProgressBit = offsetOf(0x08EFDE58);
constexpr uint32_t LoadCharacterState = offsetOf(0x08EFE5D0), LoadAllCharacters = offsetOf(0x08EFD058), FindEventValues = offsetOf(0x08EFE824);
constexpr uint32_t UpdateImageEvent = offsetOf(0x08EFEB70), NameOfCharacter = offsetOf(0x08EFDEF0), NoName = offsetOf(0x08EFDE98);
constexpr uint32_t NameOfImageEvent = offsetOf(0x08EFEC24), NoImageEventName = offsetOf(0x08EFECA4);
constexpr uint32_t PlaceTextNumber = offsetOf(0x08F0ABEC), ObjectIdOfCharacter = offsetOf(0x08F0A108), OddNumberOfCode = offsetOf(0x08F0AE50);
constexpr uint32_t LanguageIndex = offsetOf(0x08F0ADF4), SystemMessage = offsetOf(0x08F046B4);

/* binding the story's tables */
constexpr uint32_t SetCharacterTables = offsetOf(0x08EFCD04), SetCharacterTable10 = offsetOf(0x08EFCD30), SetCharacterRules14 = offsetOf(0x08EFCD50);
constexpr uint32_t SetCharacterTable18 = offsetOf(0x08EFCD70), SetCharacterTable1C = offsetOf(0x08EFCD90), SetCharacterSchedule20 = offsetOf(0x08EFCDB0);
constexpr uint32_t BindSystemMessages = offsetOf(0x08F046A0), BindCharacterMessages = offsetOf(0x08F046DC), BindMapMessages = offsetOf(0x08F0472C);
constexpr uint32_t BindFieldTables = offsetOf(0x08F0477C), BindItemTable = offsetOf(0x08F0C2E8), BindCardTable = offsetOf(0x08F0E0CC);
constexpr uint32_t BindMailTable = offsetOf(0x08F0BD48), BindMailTexts = offsetOf(0x08F0BD54);
constexpr uint32_t BindLessonPlan = offsetOf(0x08F0F034), BindLessonScript = offsetOf(0x08F0F040), BindLessonTexts = offsetOf(0x08F0F04C);
constexpr uint32_t BindMorningPlan = offsetOf(0x08F0FC30), BindMorningScript = offsetOf(0x08F0FC3C), BindMorningTexts = offsetOf(0x08F0FC48);

/* the story's flow */
constexpr uint32_t PlayerCharacter = offsetOf(0x08EFF2A0), PlayerValue = offsetOf(0x08EFED70), FlagBit = offsetOf(0x08F043C0), HolidayValueAfter = offsetOf(0x08F03FB8);
constexpr uint32_t EventIdForMode = offsetOf(0x08F0F2CC), DateFieldPointers = offsetOf(0x08F0494C), PlayerField30 = offsetOf(0x08EFEDF8);
constexpr uint32_t PlayerField84 = offsetOf(0x08EFF008), PlayerField2C = offsetOf(0x08EFF17C), SetFlag44D4 = offsetOf(0x08F0FA7C);
constexpr uint32_t SwitchModule = offsetOf(0x08F085E4), ModuleLoaded = offsetOf(0x08F10154), StartMapSoundScene = offsetOf(0x08F10634);
constexpr uint32_t FinishSoundLoads = offsetOf(0x08F104D8), StartVoiceScenes = offsetOf(0x08F10398);
constexpr uint32_t FieldSetup = offsetOf(0x08F06118), PlaceMenu = offsetOf(0x08F07250), StartPlaceChoice = offsetOf(0x08F0751C), PlaceChoiceDone = offsetOf(0x08F0757C);
constexpr uint32_t SetOverlayState = offsetOf(0x08F0FAF8), FinishVoiceLoads = offsetOf(0x08F07638);
constexpr uint32_t EnterMap = offsetOf(0x08F0769C), PlaceChosen = offsetOf(0x08F07768), ResetAll = offsetOf(0x08EFCAF0), Start = offsetOf(0x08F0540C);
constexpr uint32_t StepDone = offsetOf(0x08F05F00), Step = offsetOf(0x08F05AEC), DayChanged = offsetOf(0x08F04A7C);

/* not decompiled yet */
constexpr uint32_t CharacterPlace = offsetOf(0x08EFCE9C);    /* (character) the character's place on the map */
constexpr uint32_t PlaceMatches = offsetOf(0x08EFE054);      /* (character, place): 0 matches any */
constexpr uint32_t StoryFlag = offsetOf(0x08F042A0);         /* (flag) a bit of the story's flags */
constexpr uint32_t FirstScheduleIndex = offsetOf(0x08EFE568);
constexpr uint32_t Fun08EFED34 = offsetOf(0x08EFED34);       /* (place) the player's place (guess) */
constexpr uint32_t Fun08F096D8 = offsetOf(0x08F096D8);       /* () the place of the field state (guess) */
constexpr uint32_t Fun08F0AAD0 = offsetOf(0x08F0AAD0);       /* (choice) the place of a menu choice (guess) */
constexpr uint32_t Fun08F0FAE8 = offsetOf(0x08F0FAE8), Fun08F0FAD8 = offsetOf(0x08F0FAD8), Fun08F04050 = offsetOf(0x08F04050);
constexpr uint32_t AdvanceTime = offsetOf(0x08F05F50);       /* (kind, value) moves the story's clock on and reloads the characters */
constexpr uint32_t UnloadModule = offsetOf(0x08F100A4);
constexpr uint32_t Fun08F03F88 = offsetOf(0x08F03F88), Fun08F03FA0 = offsetOf(0x08F03FA0), Fun08F044AC = offsetOf(0x08F044AC), Fun08F04544 = offsetOf(0x08F04544);
constexpr uint32_t Fun08F04418 = offsetOf(0x08F04418), Fun08F044D8 = offsetOf(0x08F044D8), Fun08F04404 = offsetOf(0x08F04404);   /* the clock's moves, see AdvanceTime */
constexpr uint32_t Fun08F040FC = offsetOf(0x08F040FC), Fun08F0FC94 = offsetOf(0x08F0FC94), Fun08F0FD58 = offsetOf(0x08F0FD58), Fun08EFF114 = offsetOf(0x08EFF114);
constexpr uint32_t Fun08EFF290 = offsetOf(0x08EFF290);
constexpr uint32_t Fun08EFCBA4 = offsetOf(0x08EFCBA4), Fun08F05A4C = offsetOf(0x08F05A4C);   /* what the story's main runs when it ends */
constexpr uint32_t StepRunner03 = offsetOf(0x08F07824), StepRunner04 = offsetOf(0x08F07854), StepRunner05 = offsetOf(0x08F07994), StepRunner06 = offsetOf(0x08F079C8);
constexpr uint32_t StepRunner07 = offsetOf(0x08F0810C), StepRunner0B = offsetOf(0x08F08188), StepRunner0C = offsetOf(0x08F05E64);
constexpr uint32_t StepRunner2E = offsetOf(0x08F09144), StepRunner30 = offsetOf(0x08F09274), StepRunner32 = offsetOf(0x08F09364), StepRunner33 = offsetOf(0x08F09440);
constexpr uint32_t StepRunner34 = offsetOf(0x08F094A4);

/* the parts of the story's start that are not decompiled yet */
constexpr uint32_t Fun08EFCCAC = offsetOf(0x08EFCCAC), Fun08EFD3EC = offsetOf(0x08EFD3EC), Fun08F0BD24 = offsetOf(0x08F0BD24), Fun08F0F2B8 = offsetOf(0x08F0F2B8);
constexpr uint32_t Fun08EFF468 = offsetOf(0x08EFF468), Fun08EFED20 = offsetOf(0x08EFED20), Fun08F03E28 = offsetOf(0x08F03E28), Fun08F0950C = offsetOf(0x08F0950C);
constexpr uint32_t Fun08F0C2D4 = offsetOf(0x08F0C2D4), Fun08F0E57C = offsetOf(0x08F0E57C), Fun08F0F020 = offsetOf(0x08F0F020), Fun08F0FC1C = offsetOf(0x08F0FC1C);
constexpr uint32_t Fun08F10358 = offsetOf(0x08F10358), Fun08EFD3B0 = offsetOf(0x08EFD3B0), Fun08EFEF0C = offsetOf(0x08EFEF0C), Fun08F04360 = offsetOf(0x08F04360);
constexpr uint32_t Fun08F03E70 = offsetOf(0x08F03E70), Fun08F0A5EC = offsetOf(0x08F0A5EC), Fun08EFCFCC = offsetOf(0x08EFCFCC), Fun08F041CC = offsetOf(0x08F041CC);
constexpr uint32_t Fun08EFDE18 = offsetOf(0x08EFDE18), Fun08F0FAB8 = offsetOf(0x08F0FAB8), Fun08F0B820 = offsetOf(0x08F0B820), Fun08F0964C = offsetOf(0x08F0964C);
constexpr uint32_t Fun08F0ECEC = offsetOf(0x08F0ECEC), Fun08F0ECDC = offsetOf(0x08F0ECDC), Fun08F0ECCC = offsetOf(0x08F0ECCC), Fun08F0ECAC = offsetOf(0x08F0ECAC);
constexpr uint32_t Fun08F0ECBC = offsetOf(0x08F0ECBC), Fun08EFF2E8 = offsetOf(0x08EFF2E8), Fun08EFF2C8 = offsetOf(0x08EFF2C8), Fun08EFF2D8 = offsetOf(0x08EFF2D8);
constexpr uint32_t Fun08F0B6E0 = offsetOf(0x08F0B6E0), Fun08F0F62C = offsetOf(0x08F0F62C), Fun08F0E590 = offsetOf(0x08F0E590);
constexpr uint32_t SetStoryPhase = offsetOf(0x08F0F9B8), StoryPhase = offsetOf(0x08F0FA28), OpenArchives = offsetOf(0x08F0B170), LoadTables = offsetOf(0x08F0B268);
constexpr uint32_t BindCharacters = offsetOf(0x08EFCC98), BindMail = offsetOf(0x08F0BD18), BindMode = offsetOf(0x08F0F2AC), BindRegisters = offsetOf(0x08EFF45C);
constexpr uint32_t BindPlayer = offsetOf(0x08EFED0C), BindCalendar = offsetOf(0x08F03E0C), BindFieldState = offsetOf(0x08F09500), BindItems = offsetOf(0x08F0C2C8);
constexpr uint32_t BindCards = offsetOf(0x08F0E0C0), BindShop = offsetOf(0x08F0E570), BindLessons = offsetOf(0x08F0F014), BindMorning = offsetOf(0x08F0FC10);
constexpr uint32_t BindModuleList = offsetOf(0x08F1034C), SetFlag44D8 = offsetOf(0x08F0FBDC);


}  // namespace fn
}  // namespace story
