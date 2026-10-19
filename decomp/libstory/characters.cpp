/* libstory: the 150 characters of the story. Each one has a state in the save data (CharacterState) and a set of script tables (CharacterScript). Every
 * step of the story reloads the state of each character from the schedule record of the current tick (`LoadCharacterState`), works out which event
 * values apply (`FindEventValues`) and which picture to show (`FindImageEvent`), and prepares the names the screens print. */
#include "../common/swap.h"
#include "../common/ehsys.h"
#include "story_data.h"

#define MODULE "libstory"

namespace {
using namespace story;

constexpr uint32_t kPhaseIsOne = offsetOf(0x08F0FA34), kOutsideFirstBreak = offsetOf(0x08F0405C), kCurrentTick = offsetOf(0x08F03F48);
constexpr uint32_t kCurrentDayOfYear = offsetOf(0x08F04988), kTodayInRange = offsetOf(0x08F04994);
constexpr uint32_t kCharacterPlace = offsetOf(0x08EFCE9C);      /* the original, not decompiled yet: the character's place */
constexpr uint32_t kPlaceMatches = offsetOf(0x08EFE054);        /* (character, place): 0 matches any */
constexpr uint32_t kStoryFlag = offsetOf(0x08F042A0);           /* (flag): the story flag bits */
constexpr uint32_t kFirstScheduleIndex = offsetOf(0x08EFE568);
constexpr int32_t kRecordHalfwords = 25;                          /* a schedule record: the index, the tick, then 23 values */
constexpr uint32_t kNameBytes = 0x80, kCharacterStateBytes = sizeof(CharacterState), kProgressBytesPerCharacter = 12, kImageEventsOffset = 0x20;
constexpr int32_t kImageEventsPerCharacter = 6;
constexpr uint32_t kSystemMessageNoName = 1;                      /* the system text shown when a character has no name */

CharacterState &characterState(SwapCtx &x, uint32_t character) { return x.at<CharacterState>(datum<uint32_t>(x, kCharacterStatesPointer) + character * kCharacterStateBytes); }
CharacterScript &characterScript(SwapCtx &x, uint32_t character) { return x.at<CharacterScript>(guest(x, kCharacterScripts) + character * (uint32_t)sizeof(CharacterScript)); }

/* what a function that touches a character may write (for TF_DECOMP_VERIFY) */
uint32_t stateAddress(SwapCtx &x, uint32_t character) { return datum<uint32_t>(x, kCharacterStatesPointer) + character * kCharacterStateBytes; }
uint32_t scriptAddress(SwapCtx &x, uint32_t character) { return guest(x, kCharacterScripts) + character * (uint32_t)sizeof(CharacterScript); }
void regions_Names(SwapCtx &x, Regions &out) { out.push_back({scriptAddress(x, x.arg(0)) + offsetof(CharacterScript, name), 2 * kNameBytes}); }
void regions_CharacterEverything(SwapCtx &x, Regions &out) {
    out.push_back({stateAddress(x, x.arg(0)), kCharacterStateBytes});
    regions_Names(x, out);
}

/* FUN_08efd0d8(character): the event the character is in */
void swap_CharacterEventId(SwapCtx &x) { x.ret(characterState(x, x.arg(0)).eventId); }
SwapReg reg_CharacterEventId({"libstory.CharacterEventId", MODULE, offsetOf(0x08EFD0D8), swap_CharacterEventId, true});

/* FUN_08f0f2cc(character): the event id of the story's current mode: the main one, or (when the field state's mode is not 0) the alternative one */
void swap_EventIdForMode(SwapCtx &x) {
    const uint32_t mode = x.at<uint32_t>(datum<uint32_t>(x, 0x091C1D44) + 4);
    const CharacterState &state = characterState(x, x.arg(0));
    x.ret(mode == 0 ? state.eventId : state.alternativeEventId);
}
SwapReg reg_EventIdForMode({"libstory.EventIdForMode", MODULE, offsetOf(0x08F0F2CC), swap_EventIdForMode, true});

/* FUN_08efde58(bit): a bit of the progress flags (bit 0 is the least significant of the first word) */
void swap_ProgressBit(SwapCtx &x) {
    int32_t bit = (int32_t)x.arg(0), word = 0;
    for (; 0x1F < bit; bit -= 0x20) word++;
    const uint32_t bits = x.at<uint32_t>(datum<uint32_t>(x, kProgressBitsPointer) + word * 4);
    x.ret((bits & (1u << (bit & 0x1F))) != 0 ? 1 : 0);
}
SwapReg reg_ProgressBit({"libstory.ProgressBit", MODULE, offsetOf(0x08EFDE58), swap_ProgressBit, true});

/* FUN_08efcf70(character, n): n = 0 or above 32 is always true; otherwise the nth bit of the character's "seen" mask */
void swap_SeenBit(SwapCtx &x) {
    const uint32_t n = x.arg(1);
    if (n == 0 || n >= 0x21) { x.ret(1); return; }
    x.ret((characterState(x, x.arg(0)).seenMask & (1u << ((n - 1) & 0x1F))) != 0 ? 1 : 0);
}
SwapReg reg_SeenBit({"libstory.SeenBit", MODULE, offsetOf(0x08EFCF70), swap_SeenBit, true});

/* FUN_08efe024(character) / FUN_08efdfdc(character, on): the "hidden" flag */
void swap_IsHidden(SwapCtx &x) { x.ret((characterState(x, x.arg(0)).flags & 1) != 0 ? 1 : 0); }
SwapReg reg_IsHidden({"libstory.IsHidden", MODULE, offsetOf(0x08EFE024), swap_IsHidden, true});
void swap_SetHidden(SwapCtx &x) {
    uint8_t &flags = characterState(x, x.arg(0)).flags;
    flags = x.arg(1) == 0 ? (uint8_t)(flags & 0xFE) : (uint8_t)(flags | 1);
}
void regions_SetHidden(SwapCtx &x, Regions &out) { out.push_back({datum<uint32_t>(x, kCharacterStatesPointer) + x.arg(0) * kCharacterStateBytes + 0x8C, 1}); }
SwapReg reg_SetHidden({"libstory.SetHidden", MODULE, offsetOf(0x08EFDFDC), swap_SetHidden, false, regions_SetHidden});

/* FUN_08f046b4(n): the address of the nth system text (the texts of one file, an index of offsets in halfwords at kSystemMessages+4) */
uint32_t systemMessage(SwapCtx &x, uint32_t n) {
    const uint32_t texts = datum<uint32_t>(x, 0x08F21B64), offsets = datum<uint32_t>(x, 0x08F21B68);
    return texts + x.at<uint32_t>(offsets + n * 4 - 4) * 2;
}
void swap_SystemMessage(SwapCtx &x) { x.ret(systemMessage(x, x.arg(0))); }
SwapReg reg_SystemMessage({"libstory.SystemMessage", MODULE, offsetOf(0x08F046B4), swap_SystemMessage, true});

/* FUN_08f0ae50 / historia_objeto_id_por_estado: the maps from a code to another (see maps.cpp) */

/* ---- the names: the character's name for this event, and the name of the picture event ---- */
void copyName(SwapCtx &x, uint32_t destination, uint32_t text) {
    x.callNid(ehsys::nid::MemSet, destination, 0, kNameBytes);
    x.callNid(ehsys::nid::CopyText, destination, text);
}
uint32_t objectIdForState(uint32_t character) { return character <= 0x95 ? character + 2 : 0; }   /* the state index 0..149 is the object id 2..151 */
void swap_NameOfCharacter(SwapCtx &x) {   /* FUN_08efdef0(character): the character's own name */
    const uint32_t character = x.arg(0);
    copyName(x, guest(x, kCharacterScripts) + character * (uint32_t)sizeof(CharacterScript) + offsetof(CharacterScript, name),
             x.callNid(ehsys::nid::GetLanguageText, 0, objectIdForState(character)));
}
SwapReg reg_NameOfCharacter({"libstory.NameOfCharacter", MODULE, offsetOf(0x08EFDEF0), swap_NameOfCharacter, false, regions_Names});
void swap_NoName(SwapCtx &x) {   /* FUN_08efde98(character): the "no name" system text */
    const uint32_t character = x.arg(0);
    copyName(x, guest(x, kCharacterScripts) + character * (uint32_t)sizeof(CharacterScript) + offsetof(CharacterScript, name), systemMessage(x, kSystemMessageNoName));
}
SwapReg reg_NoName({"libstory.NoName", MODULE, offsetOf(0x08EFDE98), swap_NoName, false, regions_Names});
void swap_NameOfImageEvent(SwapCtx &x) {   /* FUN_08efec24(character): the name of the event whose picture is shown */
    const uint32_t character = x.arg(0);
    copyName(x, guest(x, kCharacterScripts) + character * (uint32_t)sizeof(CharacterScript) + offsetof(CharacterScript, imageName),
             x.callNid(ehsys::nid::GetCardRecordData, characterState(x, character).imageEvent));
}
SwapReg reg_NameOfImageEvent({"libstory.NameOfImageEvent", MODULE, offsetOf(0x08EFEC24), swap_NameOfImageEvent, false, regions_Names});
void swap_NoImageEventName(SwapCtx &x) {   /* FUN_08efeca4(character) */
    const uint32_t character = x.arg(0);
    copyName(x, guest(x, kCharacterScripts) + character * (uint32_t)sizeof(CharacterScript) + offsetof(CharacterScript, imageName), systemMessage(x, kSystemMessageNoName));
}
SwapReg reg_NoImageEventName({"libstory.NoImageEventName", MODULE, offsetOf(0x08EFECA4), swap_NoImageEventName, false, regions_Names});

/* ---- FUN_08efea74(character): the event whose picture to show: the first image rule that holds today, or 0 ---- */
uint32_t findImageEvent(SwapCtx &x, uint32_t character) {
    const uint16_t *rules = (const uint16_t *)x.mem(characterScript(x, character).imageRules);
    const uint16_t *rule = rules;
    for (int32_t i = 0;; i++, rule += 8) {
        if (rule[7] == 0) return 0;
        if (x.callLocal(kTodayInRange, rule[0], rule[1], rule[2], rule[3]) == 0) continue;
        uint32_t second = rule[5];
        if (rule[4] != 0 && x.callLocal(kPlaceMatches, character, rule[4]) == 0) continue;
        if (second != 0 && x.callLocal(offsetOf(0x08EFCF70), character, second) == 0) continue;
        const uint32_t third = rule[6];
        if (third == 0 || x.callLocal(kStoryFlag, third) != 0) return rules[i * 8 + 7];
    }
}
void swap_FindImageEvent(SwapCtx &x) { x.ret(findImageEvent(x, x.arg(0))); }
SwapReg reg_FindImageEvent({"libstory.FindImageEvent", MODULE, offsetOf(0x08EFEA74), swap_FindImageEvent, true});

/* ---- FUN_08efeb70(character): picks the picture event and the names that go with it ---- */
void swap_UpdateImageEvent(SwapCtx &x) {
    const uint32_t character = x.arg(0);
    CharacterState &state = characterState(x, character);
    const uint32_t bits = datum<uint32_t>(x, kProgressBitsPointer);
    state.imageEvent = findImageEvent(x, character);
    const uint16_t *listed = (const uint16_t *)x.mem(bits + character * kProgressBytesPerCharacter + kImageEventsOffset);
    for (int32_t i = 0; i < kImageEventsPerCharacter; i++) {
        if (listed[i] == 0) break;
        if (listed[i] == state.imageEvent) { x.callLocal(offsetOf(0x08EFEC24), character); return; }
    }
    x.callLocal(offsetOf(0x08EFECA4), character);
}
SwapReg reg_UpdateImageEvent({"libstory.UpdateImageEvent", MODULE, offsetOf(0x08EFEB70), swap_UpdateImageEvent, false, regions_CharacterEverything});

/* ---- FUN_08efe824(character): the event values that apply to the character now, from the four event tables in order (a dated table, then the places, then
 * the "seen" numbers, then the story flags); stored in the state. Returns 1 when one applied, else clears the values and returns 0. ---- */
uint32_t findEventValues(SwapCtx &x, uint32_t character) {
    const CharacterScript &script = characterScript(x, character);
    CharacterState &state = characterState(x, character);
    const uint16_t *a = (const uint16_t *)x.mem(script.eventTableA), *b = (const uint16_t *)x.mem(script.eventTableB);
    const uint16_t *c = (const uint16_t *)x.mem(script.eventTableC), *d = (const uint16_t *)x.mem(script.eventTableD);
    auto found = [&](const uint16_t *table, int32_t i) { state.eventKind = table[i * 3 + 1]; state.eventValue = table[i * 3 + 2]; return 1u; };
    for (int32_t i = 0;; i++) {
        const uint16_t *record = a + i * 6;
        if (record[4] == 0 && record[5] == 0) break;
        if (x.callLocal(kTodayInRange, record[0], record[1], record[2], record[3]) != 0) { state.eventKind = record[4]; state.eventValue = record[5]; return 1; }
    }
    for (int32_t i = 0;; i++) {
        if (b[i * 3 + 1] == 0 && b[i * 3 + 2] == 0) break;
        const uint32_t place = x.callLocal(kCharacterPlace, character) & 0xFF;
        if (place == b[0] || place == 0) return found(b, i);   /* (sic) the original compares with the FIRST entry's place on every turn */
    }
    for (int32_t i = 0;; i++) {
        if (c[i * 3 + 1] == 0 && c[i * 3 + 2] == 0) break;
        if (c[i * 3] == 0 || x.callLocal(offsetOf(0x08EFCF70), character, c[i * 3]) != 0) return found(c, i);
    }
    for (int32_t i = 0;; i++) {
        if (d[i * 3 + 1] == 0 && d[i * 3 + 2] == 0) { state.eventKind = 0; state.eventValue = 0; return 0; }
        if (d[i * 3] == 0 || x.callLocal(kStoryFlag, d[i * 3]) != 0) return found(d, i);
    }
}
void swap_FindEventValues(SwapCtx &x) { x.ret(findEventValues(x, x.arg(0))); }
SwapReg reg_FindEventValues({"libstory.FindEventValues", MODULE, offsetOf(0x08EFE824), swap_FindEventValues, false, regions_CharacterEverything});

/* ---- historia_estado_slot_carregar (FUN_08efe5d0)(character, mode): reloads the character's state from its schedule. Mode 0 first works out which
 * schedule to follow; the record of the schedule that is current at this tick is then copied into the state. ---- */
struct RecordCopy { uint8_t record, stateOffset; };
constexpr RecordCopy kRecordCopies[] = {{8, 0x3A}, {10, 0x3C}, {12, 0x3E}, {14, 0x40}, {16, 0x42}, {18, 0x44}, {9, 0x46}, {11, 0x48}, {13, 0x4A}, {15, 0x4C},
                                        {17, 0x4E}, {19, 0x50}, {20, 0x30}, {21, 0x32}, {22, 0x38}, {23, 0x34}, {24, 0x36}};
constexpr uint8_t kChangedFlag = 4;
void swap_LoadCharacterState(SwapCtx &x) {
    const uint32_t character = x.arg(0), mode = x.arg(1);
    CharacterState &state = characterState(x, character);
    if (mode == 0) state.scheduleIndex = x.callLocal(kFirstScheduleIndex, character);   /* the original leaves the character in a0 */
    const uint16_t *schedule = (const uint16_t *)x.mem(characterScript(x, character).schedule);
    while ((uint32_t)schedule[0] != state.scheduleIndex) schedule += kRecordHalfwords;
    const int32_t tick = (int32_t)x.callLocal(kCurrentTick);
    int32_t index = 0;
    for (const uint16_t *probe = schedule;; probe += kRecordHalfwords, index++) {
        const int32_t recordTick = probe[1];
        if (tick == recordTick) break;
        if (tick < recordTick || recordTick == 0) { index--; break; }
    }
    const uint16_t *record = schedule + index * kRecordHalfwords;
    const uint32_t eventId = record[2];
    const uint32_t day = x.callLocal(kCurrentDayOfYear);
    if (state.eventId != eventId) state.flags |= kChangedFlag;
    if (state.dayStamp != day) { state.flags |= kChangedFlag; state.dayStamp = day; }
    state.eventId = (uint16_t)eventId;
    state.field0C = record[3];
    state.field10 = record[4];
    state.field15 = (uint8_t)x.callLocal(offsetOf(0x08F0AE50), record[5]);
    state.field16 = (uint8_t)record[6];
    state.field17 = (uint8_t)record[7];
    for (const RecordCopy &copy : kRecordCopies) *(uint16_t *)((uint8_t *)&state + copy.stateOffset) = record[copy.record];
    findEventValues(x, character);
    if (x.callLocal(offsetOf(0x08EFDE58), character) == 0) x.callLocal(offsetOf(0x08EFDE98), character);
    else x.callLocal(offsetOf(0x08EFDEF0), character);
    x.callLocal(offsetOf(0x08EFEB70), character);
}
SwapReg reg_LoadCharacterState({"libstory.LoadCharacterState", MODULE, offsetOf(0x08EFE5D0), swap_LoadCharacterState, false, regions_CharacterEverything});

/* ---- FUN_08efd058(mode): reloads every character; before that, in the story's first phase, the "hidden" flag of the character 4 follows the first break ---- */
void swap_LoadAllCharacters(SwapCtx &x) {
    const uint32_t mode = x.arg(0);
    if (x.callLocal(kPhaseIsOne) != 0) {
        if (x.callLocal(kOutsideFirstBreak) == 0) x.callLocal(offsetOf(0x08EFDFDC), 4, 1);
        else x.callLocal(offsetOf(0x08EFDFDC), 4, 0);
    }
    for (int32_t character = 0; character < kCharacterCount; character++) x.callLocal(offsetOf(0x08EFE5D0), (uint32_t)character, mode);
}
void regions_AllCharacters(SwapCtx &x, Regions &out) {
    out.push_back({datum<uint32_t>(x, kCharacterStatesPointer), kCharacterCount * kCharacterStateBytes});
    out.push_back({guest(x, kCharacterScripts), (uint32_t)kCharacterCount * (uint32_t)sizeof(CharacterScript)});
}
SwapReg reg_LoadAllCharacters({"libstory.LoadAllCharacters", MODULE, offsetOf(0x08EFD058), swap_LoadAllCharacters, false, regions_AllCharacters});

}  // namespace
