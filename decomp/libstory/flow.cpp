/* libstory: the flow of the story's map. The story is a state machine (`Step`, run every frame); the steps that matter here put the map up: the clock
 * and the calendar fields for the field module (`FieldSetup`), the list of places the player can walk to (`PlaceMenu`), and the choice of a place
 * (`StartPlaceChoice`, `PlaceChosen`). The field module draws all of these; they are "handlers" it runs when the story invokes them by number. */
#include "../swap.h"
#include "../ehsys.h"
#include "story_data.h"
#include "story_functions.h"
#include <string.h>

#define MODULE "libstory"

namespace {
using namespace story;

/* the field module's handler interface: invoke a handler with an argument block, then wait until it reports it is done */
constexpr uint32_t kInvokeHandler = 0x86220A06, kHandlerDone = 0x79E5F8B7;   /* libfield exports (guess) */
enum Handler : uint32_t { kMapHandler = 0, kPlaceMenuHandler = 2, kOverlayHandler = 0x18, kPlaceChoiceHandler = 0x1E, kPlaceChoiceStartHandler = 0x1F };

void invokeHandler(SwapCtx &x, uint32_t handler, uint32_t arguments) { x.callNid(kInvokeHandler, handler, arguments); }
bool handlerDone(SwapCtx &x, uint32_t handler, uint32_t result) { return x.callNid(kHandlerDone, handler, result) != 0; }
void waitForHandler(SwapCtx &x, uint32_t handler, uint32_t result) { while (!handlerDone(x, handler, result)) x.callNid(ehsys::nid::WaitFrame); }

constexpr uint32_t kStepState = 0x091C1D28;   /* StepState */
struct StepState {
    uint32_t done;           /* +0x00 nonzero when the story is over */
    uint32_t scene;          /* +0x04 which scene the story is in (12 = the map) */
    uint32_t step;           /* +0x08 the step Step runs next */
    uint32_t unknown0C[2];
    uint32_t callback;       /* +0x14 */
};
StepState &stepState(SwapCtx &x) { return datum<StepState>(x, kStepState); }

constexpr uint32_t kSceneMap = 12, kStepAfterMap = 2, kStepPlaceChosen = 3, kStepMapIdle = 12, kStoryModeLeave = 9;
constexpr uint32_t kFinishVoiceScenes[] = {7, 9, 8, 2};   /* the scenes that start the voices when the loads end */

/* ---- FUN_08f07638: in the scenes that play voices, waits for the loads and starts the voice sound scenes ---- */
void swap_FinishVoiceLoads(SwapCtx &x) {
    const uint32_t scene = stepState(x).scene;
    for (uint32_t voiced : kFinishVoiceScenes) {
        if (scene == voiced) { x.callLocal(fn::FinishSoundLoads); x.callLocal(fn::StartVoiceScenes); return; }
    }
}
SwapReg reg_FinishVoiceLoads({"libstory.FinishVoiceLoads", MODULE, offsetOf(0x08F07638), swap_FinishVoiceLoads});

/* ---- FUN_08f06118: hands the field module the clock's numbers and the story's callbacks, and waits for it ---- */
constexpr uint32_t kFieldCallbacks[6] = {0x08F08F20, 0x08EFF060, 0x08F04018, 0x08F0BEB0, 0x08F0C0DC, 0x08EFF1EC};   /* functions the field module calls back */
void swap_FieldSetup(SwapCtx &x) {
    if (x.callLocal(fn::FieldStateMode) != 0) x.callLocal(fn::Fun08EFED34, x.callLocal(fn::Fun08F096D8));
    const uint32_t flag = x.callLocal(fn::StoryMode) == 2 || stepState(x).scene == 2 ? 1 : 0;
    GuestStack block(x, 96);
    uint32_t *arguments = (uint32_t *)x.mem(block.addr());
    x.callLocal(fn::DateFieldPointers, block.addr());
    arguments[6] = x.callLocal(fn::PlayerField30);
    arguments[7] = x.callLocal(fn::PlayerField84);
    arguments[8] = x.callLocal(fn::PlayerField2C);
    arguments[9] = flag;
    for (int32_t i = 0; i < 6; i++) arguments[10 + i] = x.addr(offsetOf(kFieldCallbacks[i]));
    invokeHandler(x, kMapHandler, block.addr());
    waitForHandler(x, kMapHandler, block.addr() + 64);
}
SwapReg reg_FieldSetup({"libstory.FieldSetup", MODULE, offsetOf(0x08F06118), swap_FieldSetup});

/* ---- FUN_08f07250: the list of places the map offers. The first nine characters get an entry (the object id and the number of the text of the place they
 * are at, -1 when hidden, the player or without an event); the ids of the places that can be chosen follow: some always, some by the story's phase. ---- */
constexpr int32_t kMenuCharacters = 9, kMenuPlaceSlots = 20;
constexpr uint32_t kPlaceMenuCallbacks[2] = {0x08F0911C, 0x08F09124};
constexpr uint32_t kPlaceBeforeBreak = 0x20, kPlaceInStoryPhase = 0x24;     /* places added by the flags */
constexpr uint32_t kHolidayPlaces[13] = {1, 3, 4, 0xC, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1D, 0x1E, 0x1F};   /* then 0x24 twice */
constexpr int32_t kHolidayPlaceCount = 15;
void swap_PlaceMenu(SwapCtx &x) {
    const int32_t player = (int32_t)x.callLocal(fn::PlayerCharacter);
    GuestStack block(x, 272);
    int32_t *entries = (int32_t *)x.mem(block.addr());                         /* kMenuCharacters pairs */
    uint32_t *places = (uint32_t *)x.mem(block.addr() + 72);                   /* 38 words: room for the holiday list past the 20 slots */
    uint32_t *arguments = (uint32_t *)x.mem(block.addr() + 224);
    for (int32_t i = 0; i < kMenuCharacters; i++) {
        const uint32_t eventId = x.callLocal(fn::EventIdForMode, (uint32_t)i);
        entries[i * 2] = (int32_t)x.callLocal(fn::ObjectIdOfCharacter, (uint32_t)i);
        const bool hidden = x.callLocal(fn::IsHidden, (uint32_t)i) != 0;
        entries[i * 2 + 1] = (!hidden && player != i && eventId != 0) ? (int32_t)x.callLocal(fn::PlaceTextNumber, eventId) : -1;
    }
    arguments[0] = block.addr();
    arguments[1] = kMenuCharacters;
    arguments[2] = x.callLocal(fn::PlaceTextNumber, x.callLocal(fn::PlayerValue));
    arguments[3] = block.addr() + 72;
    arguments[5] = x.callLocal(fn::AfterFirstBreak);
    arguments[6] = x.addr(offsetOf(kPlaceMenuCallbacks[0]));
    arguments[7] = x.addr(offsetOf(kPlaceMenuCallbacks[1]));
    memset(places, 0, kMenuPlaceSlots * 4);
    int32_t count = 0;
    auto firstFreeSlot = [&]() { for (int32_t i = 0; i < kMenuPlaceSlots; i++) if (places[i] == 0) return i; return -1; };
    if (x.callLocal(fn::FlagBit, 1) == 0) { const int32_t slot = firstFreeSlot(); if (slot >= 0) { places[slot] = kPlaceBeforeBreak; count++; } }
    if (x.callLocal(fn::PhaseIsOne) != 0) { const int32_t slot = firstFreeSlot(); if (slot >= 0) { places[slot] = kPlaceInStoryPhase; count++; } }
    if (x.callLocal(fn::HolidayValueAfter, 0) == 1) {
        const int32_t slot = firstFreeSlot();
        if (slot >= 0) {
            for (int32_t i = 0; i < 13; i++) places[slot + i] = kHolidayPlaces[i];
            places[slot + 13] = kPlaceInStoryPhase;
            places[slot + 14] = kPlaceInStoryPhase;
            count += kHolidayPlaceCount;
        }
    }
    arguments[4] = count > kMenuPlaceSlots ? kMenuPlaceSlots : count;
    invokeHandler(x, kPlaceMenuHandler, block.addr() + 224);
    waitForHandler(x, kPlaceMenuHandler, block.addr() + 220);
}
SwapReg reg_PlaceMenu({"libstory.PlaceMenu", MODULE, offsetOf(0x08F07250), swap_PlaceMenu});

/* ---- FUN_08f0751c(restore) / FUN_08f0757c: starts the choice of a place on the map (restore = 0 starts with nothing chosen), and asks whether it is done ---- */
void swap_StartPlaceChoice(SwapCtx &x) {
    GuestStack block(x, 16);
    int32_t *arguments = (int32_t *)x.mem(block.addr());
    if (x.arg(0) == 0) { arguments[0] = 0; arguments[1] = -1; }
    else {
        arguments[0] = (int32_t)x.callLocal(fn::Fun08F0FAE8);
        x.callLocal(fn::Fun08F0FAD8, 0);
        arguments[1] = (int32_t)x.callLocal(fn::Fun08F04050);
        if (arguments[1] == 0) arguments[1] = -1;
    }
    invokeHandler(x, kPlaceChoiceStartHandler, block.addr());
}
SwapReg reg_StartPlaceChoice({"libstory.StartPlaceChoice", MODULE, offsetOf(0x08F0751C), swap_StartPlaceChoice});
void swap_PlaceChoiceDone(SwapCtx &x) { GuestStack result(x, 16); x.ret(handlerDone(x, kPlaceChoiceStartHandler, result.addr()) ? 1 : 0); }
SwapReg reg_PlaceChoiceDone({"libstory.PlaceChoiceDone", MODULE, offsetOf(0x08F0757C), swap_PlaceChoiceDone});

/* ---- FUN_08f0faf8(on, wait, time): switches the field module's dark overlay on or off (when that changes anything); `wait` waits for it ---- */
constexpr uint32_t kOverlayFlag = 0x091C44D8, kOverlayDefaultTime = 15;
void swap_SetOverlayState(SwapCtx &x) {
    const uint32_t on = x.arg(0), wait = x.arg(1), time = x.arg(2);
    GuestStack block(x, 32);
    uint32_t *arguments = (uint32_t *)x.mem(block.addr());
    arguments[0] = on;
    arguments[1] = time == 0 ? kOverlayDefaultTime : time;
    arguments[2] = 0;
    uint32_t &current = datum<uint32_t>(x, kOverlayFlag);
    if ((current != 0) == (on != 0)) return;
    if (x.callLocal(fn::ModuleLoaded, 0) == 0) return;
    current = on;
    invokeHandler(x, kOverlayHandler, block.addr());
    if (wait != 0) waitForHandler(x, kOverlayHandler, block.addr() + 16);
}
SwapReg reg_SetOverlayState({"libstory.SetOverlayState", MODULE, offsetOf(0x08F0FAF8), swap_SetOverlayState});

/* ---- FUN_08f0769c: the step that puts the map up: the field module, the clock, the places, the sound, the choice of a place ---- */
void swap_EnterMap(SwapCtx &x) {
    x.callLocal(fn::FinishVoiceLoads);
    x.callLocal(fn::SwitchModule, (uint32_t)kMapHandler, 1);
    x.callLocal(fn::AdvanceTime, 1, 0);
    x.callLocal(fn::FieldSetup);
    x.callLocal(fn::PlaceMenu);
    x.callLocal(fn::StartMapSoundScene);
    x.callLocal(fn::StartPlaceChoice, 0);
    ehsys::SetScreenOverlay(x, false);
    x.callLocal(fn::SetOverlayState, 1, 1, 0);
    while (x.callLocal(fn::PlaceChoiceDone) == 0) x.callNid(ehsys::nid::WaitFrame);
    x.callLocal(fn::SetFlag44D4, 0);
    StepState &state = stepState(x);
    state.scene = kSceneMap;
    state.step = kStepAfterMap;
}
SwapReg reg_EnterMap({"libstory.EnterMap", MODULE, offsetOf(0x08F0769C), swap_EnterMap});

/* ---- FUN_08f07768: the player has picked something on the map: either leaves the story's map for another mode, or goes to the chosen place ---- */
void swap_PlaceChosen(SwapCtx &x) {
    GuestStack block(x, 48);
    uint32_t *result = (uint32_t *)x.mem(block.addr());
    invokeHandler(x, kPlaceChoiceHandler, block.addr() + 24);
    waitForHandler(x, kPlaceChoiceHandler, block.addr());
    const uint32_t place = x.callLocal(fn::Fun08F0AAD0, result[1]);
    StepState &state = stepState(x);
    if (result[0] == 1) { state.step = kStepMapIdle; x.callLocal(fn::SetStoryMode, kStoryModeLeave); return; }
    x.callLocal(fn::Fun08EFED34, place);
    x.callLocal(fn::SetOverlayState, 0, 1, 0);
    state.step = kStepPlaceChosen;
}
SwapReg reg_PlaceChosen({"libstory.PlaceChosen", MODULE, offsetOf(0x08F07768), swap_PlaceChosen});

/* ---- FUN_08f05f50(kind, value): moves the story's clock on by the kind of action the player took (time passes in the places: a lesson, a duel, a walk...),
 * then, if the day changed, applies what a new day brings, and reloads the characters. Kind 1 only reloads. ---- */
constexpr uint32_t kSaveValueClock = 2, kSaveFlagAfterBreak = 4;
void swap_AdvanceTime(SwapCtx &x) {
    const uint32_t kind = x.arg(0), value = x.arg(1);
    switch (kind) {
    case 2: x.callLocal(fn::Fun08F03F88); break;
    case 3: x.callLocal(fn::Fun08F03FA0, value); break;
    case 4: x.callLocal(fn::Fun08F044AC, value); break;
    case 5: x.callLocal(fn::Fun08F04544); break;
    case 6: x.callLocal(fn::Fun08F04418, value); break;
    case 7: x.callLocal(fn::Fun08EFED34, 0xE); x.callLocal(fn::Fun08F04544); x.callLocal(fn::Fun08F044AC, 1); break;
    case 8: x.callLocal(fn::Fun08F044D8); break;
    case 9: x.callLocal(fn::Fun08F04404, value); break;
    case 10: x.callLocal(fn::Fun08F044AC, 10); break;
    case 11: x.callLocal(fn::Fun08F04404, 3); x.callLocal(fn::Fun08F04418, 0x1F); x.callLocal(fn::Fun08F044AC, 1); break;
    default: break;
    }
    uint32_t mode = 1;
    if (x.callLocal(fn::DayChanged) != 0) {
        if (x.callLocal(fn::Fun08F040FC) != 0) x.callNid(ehsys::nid::RaiseSaveValue, kSaveValueClock, 1);
        mode = 0;
        x.callLocal(fn::Fun08F0FAD8, 1);
        x.callLocal(fn::Fun08F0FD58, x.callLocal(fn::Fun08F0FC94) == 0 ? 0 : 1);
        x.callLocal(fn::Fun08EFF114, 0);
        if (x.callLocal(fn::AfterFirstBreak) != 0) x.callNid(ehsys::nid::SetSaveFlag, kSaveFlagAfterBreak);
        if (x.callLocal(fn::PhaseIsOne) != 0) {
            const int32_t player = (int32_t)x.callLocal(fn::PlayerCharacter);
            if (player >= 0) { x.callLocal(fn::Fun08EFF290, 0xFFFFFFFFu); x.callLocal(fn::SetHidden, (uint32_t)player, 0); }
        }
    }
    x.callLocal(fn::LoadAllCharacters, mode);
}
SwapReg reg_AdvanceTime({"libstory.AdvanceTime", MODULE, offsetOf(0x08F05F50), swap_AdvanceTime});

/* ---- FUN_08f05aec: one step of the story's state machine. Most steps run a function and leave the step as it is; the module steps (0xD .. 0x2D, three per
 * place module) load a module and move on, or unload it and go back to step 4. ---- */
constexpr uint32_t kFirstModuleStep = 0xD, kLastModuleStep = 0x2D, kStepsPerModule = 3, kIdleStep = 4, kMapStepAfterAnswer = 0x33, kMapStepEnd = 0xC;
void swap_Step(SwapCtx &x) {
    StepState &state = stepState(x);
    const uint32_t step = state.step;
    if (step >= kFirstModuleStep && step <= kLastModuleStep && (step - kFirstModuleStep) % kStepsPerModule != 1) {
        const uint32_t module = (step - kFirstModuleStep) / kStepsPerModule + 1;
        const bool loading = (step - kFirstModuleStep) % kStepsPerModule == 0;
        x.callLocal(fn::SwitchModule, module, loading ? 1 : 0);
        state.step = loading ? step + 1 : kIdleStep;
        return;
    }
    switch (step) {
    case 1: x.callLocal(fn::EnterMap); break;
    case 2: x.callLocal(fn::PlaceChosen); break;
    case 3: x.callLocal(fn::StepRunner03); break;
    case 4: x.callLocal(fn::StepRunner04); break;
    case 5: x.callLocal(fn::StepRunner05); break;
    case 6: x.callLocal(fn::StepRunner06); break;
    case 7: x.callLocal(fn::StepRunner07); break;
    case 8: x.callLocal(fn::SwitchModule, 0, 1); break;
    case 10: x.callLocal(fn::SwitchModule, 0, 0); break;
    case 0xB: x.callLocal(fn::StepRunner0B); break;
    case 0xC: x.callLocal(fn::StepRunner0C, 1); break;
    case 0x2E: x.callLocal(fn::StepRunner2E); break;
    case 0x30: x.callLocal(fn::StepRunner30); break;
    case 0x32: x.callLocal(fn::StepRunner32); state.step = kMapStepAfterAnswer; break;
    case 0x33: x.callLocal(fn::StepRunner33); break;
    case 0x34: x.callLocal(fn::StepRunner34); state.step = kMapStepEnd; break;
    case 9: case 0xE: case 0x11: case 0x14: case 0x17: case 0x1A: case 0x1D: case 0x20: case 0x23: case 0x26: case 0x29: case 0x2C: case 0x2F: case 0x31: break;
    default: state.step = kIdleStep; break;
    }
}
SwapReg reg_Step({"libstory.Step", MODULE, offsetOf(0x08F05AEC), swap_Step});

/* ---- FUN_08efcaf0: zeroes every part of the story: the heaps, the characters' scripts, the registers, the mail, the lessons... ---- */
void swap_ResetAll(SwapCtx &x) {
    static const uint32_t kResets[] = {0x08F05128, 0x08F0B11C, 0x08F053E4, 0x08F0F284, 0x08EFECFC, 0x08EFCC70, 0x08F0BCF0, 0x08F03DE4, 0x08EFF418, 0x08F094D8,
                                       0x08F0C2A0, 0x08F0E098, 0x08F0C678, 0x08F0E548, 0x08F0EFEC, 0x08F0FBE8, 0x08F09718, 0x08F0FEF4, 0x08F101B8, 0x08F10324};
    for (uint32_t reset : kResets) x.callLocal(offsetOf(reset));
}
SwapReg reg_ResetAll({"libstory.ResetAll", MODULE, offsetOf(0x08EFCAF0), swap_ResetAll});

/* ---- FUN_08f0540c: starts the story: binds every part to the save data, opens the archives and tables, then either sets up a new story for the phase the
 * save says (the player's place, who is shown, the hidden characters) or picks up where the story was left (by the mode it was left in). Returns 1 when ready. ---- */
constexpr uint32_t kProgressNewStoryBit = 1, kProgressPhaseMask = 0xF, kProgressPhaseShift = 1, kProgressPlaceMask = 0xFFF, kProgressPlaceShift = 4;
constexpr uint32_t kStepNewStory = 4, kStepContinue = 5, kStepAfterAnswer = 0x22, kStepLoaded = 0xB, kStepVoices = 0x25, kStepMap = 1;
constexpr uint32_t kRegisterKindGuide = 6, kRegisterKindGuideTwo = 7, kRegisterKindOther = 13;   /* the first argument of Fun08F0F62C */
constexpr int32_t kOtherCharacterCount = 100;
constexpr uint32_t kSceneAfterStart = 12, kPlayerPlaceNewStory = 5, kPlayerPlaceSecondPhase = 14, kPlayerPlaceLastPhase = 6;
constexpr uint32_t kGuideFlag = 0x35, kThirdPhaseFlag = 0x34, kBreakFlag = 0x15;
constexpr int32_t kCharacterWithBreakFlag1 = 28, kCharacterWithBreakFlag2 = 29;
void swap_Start(SwapCtx &x) {
    const uint32_t save = x.callNid(ehsys::nid::GetStorySaveBlock);
    x.callLocal(fn::BindCharacters, save, save + 0x5D80);
    x.callLocal(fn::BindMail, save + 0x5910);
    x.callLocal(fn::BindMode, save + 0x5B68);
    x.callLocal(fn::BindRegisters, save + 0x5B78);
    x.callLocal(fn::BindPlayer, save + 0x5B7C, save + 0x5D78);
    x.callLocal(fn::BindCalendar, save + 0x5CD8, save + 0x5CFC, save + 0x64A8);
    x.callLocal(fn::BindFieldState, save + 0x5D08);
    x.callLocal(fn::BindItems, save + 0x5D20);
    x.callLocal(fn::BindCards, save + 0x5D70);
    x.callLocal(fn::BindShop, save + 0x5D30);
    x.callLocal(fn::BindLessons, save + 0x5D50);
    x.callLocal(fn::BindMorning, save + 0x5D5C);
    x.callLocal(fn::BindModuleList, save + 0x5D64);
    GuestStack local(x, 16);
    uint16_t &progress = x.at<uint16_t>(local.addr());
    x.callNid(ehsys::nid::GetProgressFlags, local.addr());
    if (x.callLocal(fn::SetStoryPhase, (progress & kProgressPhaseMask) >> kProgressPhaseShift) == 0) x.callNid(ehsys::nid::GetProgressFlags, local.addr());
    if (x.callLocal(fn::OpenArchives, x.callLocal(fn::StoryPhase)) == 0) { x.ret(0); return; }
    if (x.callLocal(fn::LoadTables) == 0) { x.ret(0); return; }
    if (x.callLocal(fn::StoryMode) != 3) { x.callLocal(fn::FinishSoundLoads); x.callLocal(fn::StartVoiceScenes); }
    StepState &state = stepState(x);
    state.scene = kSceneAfterStart;
    x.callLocal(fn::SetFlag44D8, 0);
    if ((progress & kProgressNewStoryBit) == 0) {
        for (int32_t character = 0; character < kCharacterCount; character++) { x.callLocal(fn::Fun08EFCCAC, (uint32_t)character); x.callLocal(fn::Fun08EFD3EC, (uint32_t)character, 0); }
        for (int32_t i = 0; i < kOtherCharacterCount; i++) x.callLocal(fn::Fun08F0BD24, (uint32_t)i);
        for (uint32_t reset : {fn::Fun08F0F2B8, fn::Fun08EFF468, fn::Fun08EFED20, fn::Fun08F03E28, fn::Fun08F0950C, fn::Fun08F0C2D4, fn::Fun08F0E57C, fn::Fun08F0F020,
                               fn::Fun08F0FC1C, fn::Fun08F10358, fn::Fun08EFD3B0}) x.callLocal(reset);
        x.callLocal(fn::Fun08EFEF0C, 0xFFFFFFFFu);
        const uint32_t phase = x.callLocal(fn::StoryPhase);
        const uint32_t place = (progress & kProgressPlaceMask) >> kProgressPlaceShift;
        if (phase == 4) {
            x.callLocal(fn::Fun08F03E70, 4);
            x.callLocal(fn::Fun08EFF290, 0xFFFFFFFFu);
            x.callLocal(fn::Fun08EFED34, kPlayerPlaceLastPhase);
            for (int32_t character = 0; character < kCharacterCount; character++) x.callLocal(fn::Fun08EFDE18, (uint32_t)character);
        } else if (phase == 3 || phase == 2) {
            x.callLocal(fn::Fun08EFED34, phase == 3 ? kPlayerPlaceNewStory : kPlayerPlaceSecondPhase);
            if (phase == 3) x.callLocal(fn::Fun08F04360, 2, 1);
            x.callLocal(fn::Fun08F03E70, phase);
            const uint32_t chosen = x.callLocal(fn::Fun08F0A5EC, place);
            x.callLocal(fn::Fun08EFF290, chosen);
            x.callLocal(fn::SetHidden, chosen, 1);
            if (phase == 3) x.callLocal(fn::Fun08F041CC, kThirdPhaseFlag, 1);
            x.callLocal(fn::Fun08EFCFCC, chosen, 1, 1);
        } else {
            x.callLocal(fn::Fun08EFED34, kPlayerPlaceNewStory);
            x.callLocal(fn::Fun08F04360, 2, 1);
            x.callLocal(fn::Fun08F03E70, 1);
            x.callLocal(fn::Fun08EFF290, 0xFFFFFFFFu);
        }
        state.step = kStepNewStory;
        x.callLocal(fn::Fun08F041CC, kGuideFlag, 1);
        const uint32_t saveState = x.callNid(ehsys::nid::GetSaveState);
        uint32_t breakFlag = x.at<uint32_t>(saveState + 8) & 1;
        const int32_t player = (int32_t)x.callLocal(fn::PlayerCharacter);
        if (player == kCharacterWithBreakFlag2 || player == kCharacterWithBreakFlag1) breakFlag = 1;
        x.callLocal(fn::Fun08F041CC, kBreakFlag, breakFlag);
        x.callNid(ehsys::nid::FinishStoryStart);
        x.callLocal(fn::Fun08F0FAB8, 0);
        x.callLocal(fn::AdvanceTime, 1, 0);
        x.ret(1);
        return;
    }
    /* an existing story: picks up by the mode it was left in */
    x.callLocal(fn::SetFlag44D4, 1);
    x.callLocal(fn::AdvanceTime, 1, 0);
    if (x.callLocal(fn::FieldStateMode) != 0) x.callLocal(fn::Fun08F0B820, x.callLocal(fn::Fun08F0964C));
    const uint32_t mode = x.callLocal(fn::StoryMode);
    auto prepareStartRecord = [&]() { x.callLocal(fn::StepRunner04); x.callNid(ehsys::nid::WaitFrame); };
    auto placeForKind = [&](uint32_t kind) {
        return kind == 2 ? x.callLocal(fn::Fun08F0ECDC) : kind == 1 ? x.callLocal(fn::Fun08F0ECCC) : x.callLocal(fn::Fun08F0ECEC);
    };
    switch (mode) {
    case 3: state.step = kStepAfterAnswer; break;
    case 4: case 5: {
        prepareStartRecord();
        const uint32_t record = x.callNid(ehsys::nid::GetStartRequest);
        const uint32_t place = placeForKind(x.at<uint32_t>(record + 0x20));
        x.callLocal(fn::Fun08EFF2E8, x.at<uint32_t>(record + 0x24));
        x.callLocal(fn::Fun08EFF2C8, x.at<uint32_t>(record + 0x28));
        x.callLocal(fn::Fun08EFF2D8, x.at<uint32_t>(record + 0x2C));
        x.callLocal(fn::Fun08F0B6E0, x.callLocal(fn::Fun08F0ECAC));
        x.callLocal(fn::Fun08F0F62C, kRegisterKindGuide, 0xFFFFFFFFu, place);
        x.callLocal(fn::Fun08F0E590);
        state.step = kStepLoaded;
        break; }
    case 6:
        prepareStartRecord();
        x.callLocal(fn::Fun08F0F62C, kRegisterKindOther, 0xFFFFFFFFu, 0xFFFFFFFFu);
        x.callLocal(fn::Fun08F0E590);
        state.step = kStepLoaded;
        break;
    case 7: x.callLocal(fn::Fun08F0E590); state.step = kStepVoices; break;
    case 8: {
        prepareStartRecord();
        x.callLocal(fn::Fun08F0ECBC);
        const uint32_t record = x.callNid(ehsys::nid::GetStartRequest);
        const uint32_t place = x.at<uint32_t>(record + 0x20) == 1 ? x.callLocal(fn::Fun08F0ECCC) : x.callLocal(fn::Fun08F0ECDC);
        x.callLocal(fn::Fun08EFF2E8, x.at<uint32_t>(record + 0x24));
        x.callLocal(fn::Fun08F0B6E0, x.callLocal(fn::Fun08F0ECAC));
        x.callLocal(fn::Fun08F0F62C, kRegisterKindGuideTwo, 0xFFFFFFFFu, place);
        x.callLocal(fn::Fun08F0E590);
        state.step = kStepLoaded;
        break; }
    case 9: state.step = kStepMap; break;
    default: prepareStartRecord(); state.step = kStepContinue; break;   /* modes 0, 1, 2 and above 9 */
    }
    x.callLocal(fn::SetStoryMode, 0);
    x.ret(1);
}
SwapReg reg_Start({"libstory.Start", MODULE, offsetOf(0x08F0540C), swap_Start});

/* ---- FUN_08efca38: the story's main: sets the thread up, resets the story, starts it and runs its steps one frame at a time until it is over ---- */
constexpr uint32_t kThreadAttributeUser = 0x4000;
constexpr uint32_t kSceneCallbacks[2] = {0x08EFCC60, 0x08EFCC68};   /* what the engine runs when the scene starts and ends */
constexpr uint32_t kStoryScene = 2;
void swap_Main(SwapCtx &x) {
    x.callImport("sceKernelChangeCurrentThreadAttr", 0, kThreadAttributeUser);
    x.callNid(ehsys::nid::SeedRandom);
    x.callLocal(fn::ResetAll);
    x.callNid(ehsys::nid::WaitFrame);
    x.callNid(ehsys::nid::SetSceneCallbacks, kStoryScene, x.addr(offsetOf(kSceneCallbacks[0])), x.addr(offsetOf(kSceneCallbacks[1])));
    if (x.callLocal(fn::Start) != 0) {
        do {
            x.callLocal(fn::Step);
            x.callNid(ehsys::nid::WaitFrame);
        } while (x.callLocal(fn::StepDone) == 0);
    }
    x.callNid(ehsys::nid::SetSceneCallbacks, 0, 0, 0);
    ehsys::SetScreenOverlay(x, true);
    x.callNid(ehsys::nid::WaitFrame);
    x.callLocal(fn::Fun08EFCBA4);
    x.callLocal(fn::Fun08F05A4C);
}
SwapReg reg_Main({"libstory.Main", MODULE, offsetOf(0x08EFCA38), swap_Main});

}  // namespace
