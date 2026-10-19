/* libfield: the place menu (the "world map"): fourteen places on a map of the island; the cursor moves from place to place with the pad (or the stick,
 * picking the nearest place in that direction), the confirm button chooses, a popup lists the shops of the main street. The story asks for it with
 * a handler (`StartWorldMap`) and gets the answer from `WorldMapPoll`. */
#include "../common/swap.h"
#include "../common/ehsys.h"
#include "../boot/window.h"
#include "field_data.h"
#include "field_engine.h"
#include "field_functions.h"
#include <stddef.h>
#include <string.h>

#define MODULE "libfield"

namespace {
using namespace field;
namespace gu = ehsys;

constexpr int32_t kPlaceCount = 14, kBackPlace = 0xD, kShopStreetPlace = 4;   /* place 13 is the "back" button, place 4 the main street with its popup */
enum MapPhase : int32_t { kPhaseInput = 1, kPhaseMove = 2, kPhasePopup = 3, kPhaseChosen = 4, kPhaseLeave = 5, kPhaseLoad = 6, kPhaseAsk = 7, kPhaseAskWait = 8, kPhaseAnswerLeave = 9 };

struct WorldMap {
    int32_t busy;                 /* +0x000 1 while the answer is not ready */
    int32_t answerKind;           /* +0x004 0 = a place was chosen, 1 = the player wants to leave */
    int32_t answerValue;          /* +0x008 the place (and sub-choice) */
    uint32_t archive;             /* +0x00C the map sprites archive (0 until it is loaded) */
    uint32_t textures[8];         /* +0x010 island, parts, whole effect, middle effect, arrows 11..14 */
    uint32_t request[8];          /* +0x030 the story's request: {places, place count, current place, extra places, extra count, first-time flag, callbackA, callbackB} */
    int32_t ready;                /* +0x050 */
    int32_t phase;                /* +0x054 MapPhase */
    uint8_t windowSet[0x4C];      /* +0x058 */
    boot::WindowStyle style;      /* +0x0A4 the window with the place's name */
    int32_t cursorState;          /* +0x11C 0 = the name window grows in, 1 = steady, 2 = shrinks out, 3 = gone */
    float cursorT;                /* +0x120 */
    float moveT;                  /* +0x124 */
    int32_t place;                /* +0x128 the place the cursor is on */
    int32_t previousPlace;        /* +0x12C */
    int32_t homePlace;            /* +0x130 */
    int32_t subChoice;            /* +0x134 */
    boot::WindowBlock popup;      /* +0x138 the popup list of the main street */
};
static_assert(offsetof(WorldMap, popup) == 0x138, "WorldMap layout");
constexpr uint32_t kWorldMap = 0x09993BC0, kPopupTarget = 0x09993E18, kPlaceMarks = 0x09993E1C, kSubMarks = 0x09993E54, kPlaceList = 0x09993E68;
constexpr uint32_t kPlaceCountAddress = 0x09993EA4, kExcludedPlaces = 0x09993EA8, kFirstTime = 0x09993EB4;
constexpr uint32_t kPlacePositions = 0x091FA2B8, kNeighbours = 0x091FA328;   /* {x, y} per place; 4 records of 8 bytes per place */
constexpr uint32_t kDirectionVectors = 0x091FE100, kCursorRoute = 0x091FE140;
constexpr uint32_t kMoveSound = 0x6212, kChooseSound = 0x6213, kFadeColour = 0xA0000000u;
constexpr float kGrowth = 0.1f;
constexpr uint32_t kPopupCallback = 0x091DBBB8;
WorldMap &map(SwapCtx &x) { return datum<WorldMap>(x, kWorldMap); }
uint32_t mapAddress(SwapCtx &x, size_t member) { return guest(x, kWorldMap) + (uint32_t)member; }
int32_t s32(SwapCtx &x, uint32_t address) { return datum<int32_t>(x, address); }
void playMoveSound(SwapCtx &x) { x.callLocal(fn::PlaySoundWhenReady, kMoveSound, 0); }
void restartArrow(SwapCtx &x) { x.callNid(engine::nid::RestartSprite, map(x).textures[7], 0); }

/* ---- FUN_091db508(place, &kind, &sub): which kind of place a story place number is (0..12), and for the shop street (kind 4) which shop (0..4) ---- */
void swap_PlaceInfo(SwapCtx &x) {
    const int32_t place = (int32_t)x.arg(0);
    int32_t *kind = view<int32_t>(x, x.arg(1)), *sub = view<int32_t>(x, x.arg(2));
    switch (place) {
        case 2: *kind = 4; *sub = 1; break;
        case 3: *kind = 4; *sub = 2; break;
        case 4: *kind = 4; *sub = 3; break;
        case 5: *kind = 4; *sub = 4; break;
        case 0xC: case 0xD: case 0xE: case 0xF: case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: *kind = 0; break;
        case 0x16: *kind = 1; break;
        case 0x17: *kind = 2; break;
        case 0x18: *kind = 3; break;
        case 0x19: *kind = 6; break;
        case 0x1A: *kind = 5; break;
        case 0x1B: *kind = 7; break;
        case 0x1D: *kind = 8; break;
        case 0x1E: *kind = 9; break;
        case 0x1F: *kind = 10; break;
        case 0x20: *kind = 0xB; break;
        case 0x24: *kind = 0xC; break;
        case 0x25: case 0x26: *kind = 0; break;
        case 0x27: *kind = 0xC; break;
        default: *kind = 4; *sub = 0; break;
    }
}
SwapReg reg_PlaceInfo({"libfield.PlaceInfo", MODULE, offsetOf(0x091DB508), swap_PlaceInfo});

/* ---- FUN_091db2d4(entry): which of the seven marks a kind of entry draws; -1 for none ---- */
void swap_PlaceKind(SwapCtx &x) {
    switch ((int32_t)x.arg(0)) {
        case 2: x.ret(0); break;
        case 3: x.ret(1); break;
        case 4: x.ret(2); break;
        case 5: x.ret(3); break;
        case 6: x.ret(4); break;
        case 7: x.ret(5); break;
        case 8: x.ret(6); break;
        default: x.ret(0xFFFFFFFFu); break;
    }
}
SwapReg reg_PlaceKind({"libfield.PlaceKind", MODULE, offsetOf(0x091DB2D4), swap_PlaceKind});

/* ---- FUN_091db344(request): the marks (one bit per kind of entry) each place shows, from the request's list of {entry, place} records ---- */
void swap_RegisterPlaces(SwapCtx &x) {
    const uint32_t *request = view<uint32_t>(x, x.arg(0));
    GuestStack out(x, 16);
    const uint32_t kindOut = out.addr(), subOut = out.addr() + 4;
    for (int32_t i = 0; i < (int32_t)request[1]; i++) {
        const uint32_t *record = view<uint32_t>(x, request[0] + i * 8);
        const uint32_t markBit = x.callLocal(fn::PlaceKind, record[0]);
        int32_t kind;
        if (record[1] == 0xFFFFFFFFu) { kind = -1; *(int32_t *)x.mem(kindOut) = -1; }
        else { x.callLocal(fn::PlaceInfo, record[1], kindOut, subOut); kind = *(int32_t *)x.mem(kindOut); }
        if (kind != -1 && markBit != 0xFFFFFFFFu) {
            const uint32_t bit = 1u << (markBit & 31);
            datum<uint32_t>(x, kPlaceMarks + kind * 4) |= bit;
            if (kind == 4) datum<uint32_t>(x, kSubMarks + *(int32_t *)x.mem(subOut) * 4) |= bit;
        }
    }
}
SwapReg reg_RegisterPlaces({"libfield.RegisterPlaces", MODULE, offsetOf(0x091DB344), swap_RegisterPlaces});

/* ---- FUN_091db680(kind, sub): the number of the text that names a kind of place; -1 for none ---- */
void swap_PlaceText(SwapCtx &x) {
    const int32_t kind = (int32_t)x.arg(0), sub = (int32_t)x.arg(1);
    uint32_t text = 0xFFFFFFFFu;
    switch (kind) {
        case 0: text = 1; break;
        case 1: text = 0xB; break;
        case 2: text = 0xC; break;
        case 3: text = 0xD; break;
        case 4:
            switch (sub) {
                case 0: text = 0xF; break;
                case 1: text = 0x10; break;
                case 2: text = 0x11; break;
                case 3: text = 0x12; break;
                case 4: text = 0x13; break;
                case -1: text = 0xE; break;
            }
            break;
        case 5: text = 0x16; break;
        case 6: text = 0x17; break;
        case 7: text = 0x18; break;
        case 8: text = 0x1A; break;
        case 9: text = 0x1B; break;
        case 10: text = 0x1C; break;
        case 0xB: text = 0x1D; break;
        case 0xC: text = 0x1E; break;
        case 0xD: text = 0x1F; break;
    }
    x.ret(text);
}
SwapReg reg_PlaceText({"libfield.PlaceText", MODULE, offsetOf(0x091DB680), swap_PlaceText});

/* ---- FUN_091db770(archive): the map sprites are loaded: finds each one and attaches the texture files to the effects ---- */
void swap_OnMapSprites(SwapCtx &x) {
    const uint32_t archive = x.arg(0);
    if (archive == 0) return;
    WorldMap &m = map(x);
    m.archive = *view<uint32_t>(x, archive);
    const uint32_t names[8] = {0x091FE150, 0x091FE160, 0x091FE170, 0x091FE184, 0x091FE194, 0x091FE1A4, 0x091FE1B4, 0x091FE1C4};
    for (int32_t i = 0; i < 8; i++) m.textures[i] = x.callNid(gu::nid::FindTexture, m.archive, guest(x, names[i]), 0);
    const struct { uint32_t name; int32_t target; } attached[4] = {{0x091FE1D4, 2}, {0x091FE1E8, 3}, {0x091FE1F8, 4}, {0x091FE208, 6}};
    for (const auto &a : attached) {
        const uint32_t file = x.callNid(gu::nid::FindTexture, m.archive, guest(x, a.name), 0);
        if (file != 0) x.callNid(engine::nid::AttachTextureFile, m.textures[a.target], file);
    }
    x.callNid(gu::nid::SetSpriteFrame, m.textures[4], 0, 0x36);
    x.callNid(gu::nid::SetSpriteFrame, m.textures[5], 0, 0x31);
    x.callNid(gu::nid::SetSpriteFrame, m.textures[6], 0, 0x10);
    x.callNid(gu::nid::SetSpriteFrame, m.textures[7], 0, 0x36);
    m.ready = 1;
}
SwapReg reg_OnMapSprites({"libfield.OnMapSprites", MODULE, offsetOf(0x091DB770), swap_OnMapSprites});

/* ---- FUN_091da278(request): the story opens the menu: takes the request, works out which places show which marks, starts loading the sprites ---- */
void swap_StartWorldMap(SwapCtx &x) {
    const uint32_t requestAddress = x.arg(0);
    WorldMap &m = map(x);
    GuestStack frame(x, 64);
    const uint32_t kindOut = frame.addr(), subOut = frame.addr() + 4, flagsAddress = frame.addr() + 8, kind2 = frame.addr() + 32, sub2 = frame.addr() + 36;
    uint32_t *request = view<uint32_t>(x, requestAddress);
    x.callLocal(fn::PlaceInfo, request[2], kindOut, subOut);
    memcpy(m.request, request, 0x20);
    const int32_t kind = *(int32_t *)x.mem(kindOut);
    m.place = kind; m.previousPlace = kind; m.homePlace = kind;
    m.phase = 0;
    datum<uint32_t>(x, kFirstTime) = request[5];
    x.callLocal(fn::RegisterPlaces, requestAddress);
    uint32_t *flags = (uint32_t *)x.mem(flagsAddress);
    for (int32_t i = 0; i < 5; i++) flags[i] = 0;
    datum<int32_t>(x, kPlaceCountAddress) = 0;
    datum<uint32_t>(x, kExcludedPlaces) = 0;
    for (int32_t i = 0; i < (int32_t)request[4]; i++) {
        const uint32_t entry = *view<uint32_t>(x, request[3] + i * 4);
        *(int16_t *)x.mem(guest(x, kPlaceList) + i * 2) = (int16_t)entry;
        datum<int32_t>(x, kPlaceCountAddress)++;
        x.callLocal(fn::PlaceInfo, entry, kind2, sub2);
        if (*(int32_t *)x.mem(kind2) == 4) flags[*(int32_t *)x.mem(sub2)] = 1;
        else datum<uint32_t>(x, kExcludedPlaces) |= 1u << (*(int32_t *)x.mem(kind2) & 31);
    }
    if ((flags[0] & 1 & flags[1] & flags[2] & flags[3] & flags[4]) != 0) datum<uint32_t>(x, kExcludedPlaces) |= 0x10;
    const uint32_t timeOfDay = x.callLocal(fn::TimeOfDay, (uint32_t)*view<int32_t>(x, fieldArguments(x).hour));
    if (timeOfDay == 2) x.callLocal(fn::RequestResource, 8, guest(x, 0x091DB770), 0);
    else if (timeOfDay == 1) x.callLocal(fn::RequestResource, 7, guest(x, 0x091DB770), 0);
    else if (timeOfDay == 0) x.callLocal(fn::RequestResource, 6, guest(x, 0x091DB770), 0);
}
SwapReg reg_StartWorldMap({"libfield.StartWorldMap", MODULE, offsetOf(0x091DA278), swap_StartWorldMap});

void swap_WorldMapDone(SwapCtx &x) { x.ret(map(x).archive != 0 ? 1 : 0); }   /* the map sprites are loaded */
SwapReg reg_WorldMapDone({"libfield.WorldMapDone", MODULE, offsetOf(0x091DA488), swap_WorldMapDone});

/* ---- FUN_091da498: the sprites are ready: opens the window with the place's name and starts the menu ---- */
void swap_OpenWorldMap(SwapCtx &x) {
    WorldMap &m = map(x);
    m.busy = 1;
    m.answerKind = 0; m.answerValue = 0;
    m.phase = kPhaseInput;
    x.callNid(gu::nid::InitWindowSet, mapAddress(x, offsetof(WorldMap, windowSet)), x.callLocal(fn::HeapHandle));
    memset(&m.style, 0, sizeof(m.style));
    m.style.visible = 1;
    m.style.fitToText = 1;
    m.style.uiFlag4 = 1;
    m.style.frameKind = 2;
    m.style.x = 4;
    m.style.y = 4;
    m.style.colour = 0xFFFFFFFFu;
    m.style.text = 0;
    const int32_t language = (int32_t)x.callNid(gu::nid::GetLanguage);
    m.style.wideScreenFlag = language == 0 ? 1 : 0;
    m.style.fontSize = 0xF;
    m.style.textColour = 0xFFFFFFFFu;
    m.style.textBottom = (int16_t)0xFFFF;
    m.style.textTop = 1;
    m.style.textLeft = 3;
    m.style.textRight = 2;
    x.callNid(gu::nid::AddWindow, mapAddress(x, offsetof(WorldMap, windowSet)), mapAddress(x, offsetof(WorldMap, style)));
}
SwapReg reg_OpenWorldMap({"libfield.OpenWorldMap", MODULE, offsetOf(0x091DA498), swap_OpenWorldMap});

/* ---- FUN_091da594(answer): when the menu is not busy, frees its window and gives the answer; returns whether it did ---- */
void swap_WorldMapPoll(SwapCtx &x) {
    const uint32_t answer = x.arg(0);
    WorldMap &m = map(x);
    if (m.busy == 0) {
        x.callNid(gu::nid::FreeWindowSet, mapAddress(x, offsetof(WorldMap, windowSet)));
        uint32_t *out = view<uint32_t>(x, answer);
        out[0] = (uint32_t)m.answerKind;
        out[1] = (uint32_t)m.answerValue;
    }
    x.ret(m.busy == 0 ? 1 : 0);
}
SwapReg reg_WorldMapPoll({"libfield.WorldMapPoll", MODULE, offsetOf(0x091DA594), swap_WorldMapPoll});

/* ---- FUN_091dc714(direction, colour, frames, kind) / FUN_091dc754: the screen fade of the menu ---- */
constexpr uint32_t kFade = 0x09993EB8;   /* {active, out, colour, current colour, frame, frames, kind} */
struct MapFade { int32_t active, out; uint32_t colour, current; int32_t frame, frames, kind; };
void swap_StartFade(SwapCtx &x) {
    const uint32_t out = x.arg(0), colour = x.arg(1), frames = x.arg(2), kind = x.arg(3);
    MapFade &f = datum<MapFade>(x, kFade);
    f.active = 1; f.out = (int32_t)out; f.frame = 0; f.frames = (int32_t)frames; f.colour = colour; f.current = 0; f.kind = (int32_t)kind;
}
SwapReg reg_StartFade({"libfield.StartFade", MODULE, offsetOf(0x091DC714), swap_StartFade});
void swap_UpdateFade(SwapCtx &x) {
    MapFade &f = datum<MapFade>(x, kFade);
    if (f.active == 0) return;
    float alpha;
    if (f.out == 0) alpha = (float)(f.colour >> 24) * ((float)f.frame / (float)f.frames);
    else alpha = (float)(f.colour >> 24) * (1.0f - (float)f.frame / (float)f.frames);
    if (2147483648.0f <= alpha) alpha = alpha - 2147483648.0f;
    f.current = (f.colour & 0xFFFFFF) | ((uint32_t)truncate(alpha) << 24);
    f.frame++;
    if (f.frames < f.frame) f.active = 0;
}
SwapReg reg_UpdateFade({"libfield.UpdateFade", MODULE, offsetOf(0x091DC754), swap_UpdateFade});

/* ---- FUN_091daecc(direction, slot): the neighbour of the current place in a direction (0 right, 2 down, 4 up, 6 left), from the table of linked records ---- */
void swap_MapNeighbour(SwapCtx &x) {
    const int32_t direction = (int32_t)x.arg(0), slot = (int32_t)x.arg(1);
    if (direction == -1) return;
    WorldMap &m = map(x);
    const uint32_t base = kNeighbours + (uint32_t)m.place * 0x20;
    uint32_t record = base + slot * 8;
    const uint32_t first = datum<uint32_t>(x, record);
    const uint32_t link = (uint32_t)((uint16_t)first & 0x1FF) >> 1;
    uint8_t kindByte;
    if (link == 0xFF) kindByte = (uint8_t)first;
    else { record = base + link * 8; kindByte = (uint8_t)datum<uint32_t>(x, record); }
    if ((kindByte & 1) != 1) return;
    const uint32_t word0 = datum<uint32_t>(x, record), word1 = datum<uint32_t>(x, record + 4);
    uint32_t next;
    if (direction == 6) next = (uint8_t)word1 & 0x3F;
    else if (direction == 2) next = ((uint16_t)(word0 >> 16) & 0x7FF) >> 5;
    else if (direction == 0) next = (word0 & 0x1FFFFF) >> 15;
    else next = (((uint8_t)(word0 >> 8)) & 0x7F) >> 1;
    if (next == 0x3F) return;
    playMoveSound(x);
    m.phase = kPhaseMove;
    m.previousPlace = m.place;
    m.place = (int32_t)next;
    if (next != kBackPlace) restartArrow(x);
}
SwapReg reg_MapNeighbour({"libfield.MapNeighbour", MODULE, offsetOf(0x091DAECC), swap_MapNeighbour});

/* ---- FUN_091da604: the menu waits for the pad: confirm chooses, cancel goes back to the home place, start to the back button, a direction moves the cursor ---- */
constexpr uint32_t kSearchAngles[3] = {0x3F490FDB, 0x3F860A92, 0x3FA78D37};   /* 0.785, 1.047, 1.309 radians: the nearest-place search accepts places within these cones, narrow first */
constexpr float kFarthest = 99999.0f, kStickDeadZone = 0.5f;
void swap_WorldMapInput(SwapCtx &x) {
    WorldMap &m = map(x);
    const uint32_t pad = gu::GetInput(x);
    const uint32_t held = view<uint32_t>(x, pad)[0], heldOrRepeating = view<uint32_t>(x, pad)[6];
    GuestStack frame(x, 0xE0);
    const uint32_t base = frame.addr();
    const uint32_t directionAddress = base, currentAddress = base + 8, candidateAddress = base + 16, differenceAddress = base + 24, normalAddress = base + 32, stickAddress = base + 40;
    float *direction = (float *)x.mem(directionAddress);
    direction[0] = 0; direction[1] = 0;
    if ((held & x.callNid(gu::nid::GetButtonMask, 0) & 0xFFFF) != 0) {
        x.callLocal(fn::PlaySoundWhenReady, kChooseSound, 0);
        if (m.place == kShopStreetPlace) {
            x.callLocal(fn::OpenPopup);
            m.phase = kPhasePopup;
            x.callLocal(fn::StartFade, 0, kFadeColour, 10, 3);
        } else if (m.place == kBackPlace) {
            m.phase = kPhaseLeave;
        } else {
            m.phase = kPhaseChosen;
            datum<int32_t>(x, kPopupTarget) = m.place;
        }
        return;
    }
    if ((held & x.callNid(gu::nid::GetButtonMask, 1) & 0xFFFF) != 0) {
        x.callLocal(fn::PlaySoundWhenReady, kMoveSound, 0);
        m.previousPlace = m.place;
        m.place = m.homePlace;
        m.phase = kPhaseMove;
        restartArrow(x);
        return;
    }
    if ((held & 0x1000) != 0) {
        x.callLocal(fn::PlaySoundWhenReady, kMoveSound, 0);
        m.previousPlace = m.place;
        m.place = kBackPlace;
        m.phase = kPhaseMove;
        return;
    }
    /* the direction pad (up 0x10, right 0x20, down 0x40, left 0x80); the directions are numbered 0 down, 2 left, 4 up, 6 right */
    const uint32_t up = held & 0x10, rightKey = held & 0x20, downKey = held & 0x40, leftKey = held & 0x80;
    int32_t pressed = -1;
    if (up != 0) pressed = 4;
    else if (downKey != 0) pressed = 0;
    else if (leftKey != 0) pressed = 2;
    else if (rightKey != 0) pressed = 6;
    else if ((heldOrRepeating & 0x10) != 0) pressed = 4;
    else if ((heldOrRepeating & 0x40) != 0) pressed = 0;
    else if ((heldOrRepeating & 0x80) != 0) pressed = 2;
    else if ((heldOrRepeating & 0x20) != 0) pressed = 6;
    const uint32_t excluded = datum<uint32_t>(x, kExcludedPlaces);
    if (excluded == 0) { x.callLocal(fn::MapNeighbour, (uint32_t)pressed, 3); return; }
    if (excluded == 0x1000) { x.callLocal(fn::MapNeighbour, (uint32_t)pressed, 1); return; }
    if (excluded == 0x800) { x.callLocal(fn::MapNeighbour, (uint32_t)pressed, 2); return; }
    if (excluded == 0x1800) { x.callLocal(fn::MapNeighbour, (uint32_t)pressed, 0); return; }
    /* free movement: a direction vector from the pad's combinations or from the stick, then the nearest place that lies that way */
    auto vector = [&](uint32_t index) { direction[0] = datum<float>(x, kDirectionVectors + index * 8); direction[1] = datum<float>(x, kDirectionVectors + index * 8 + 4); };
    if (rightKey != 0 && downKey != 0) vector(0);
    else if (rightKey != 0 && up != 0) vector(1);
    else if (leftKey != 0 && downKey != 0) vector(2);
    else if (leftKey != 0 && up != 0) vector(3);
    else if (up != 0) vector(4);
    else if (downKey != 0) vector(5);
    else if (leftKey != 0) vector(6);
    else if (rightKey != 0) vector(7);
    else {
        float *stick = (float *)x.mem(stickAddress);
        x.callLocal(fn::ConvertAxis, view<uint32_t>(x, gu::GetInput(x))[4]);
        stick[0] = engine::resultFloat(x);
        x.callLocal(fn::ConvertAxis, view<uint32_t>(x, gu::GetInput(x))[5]);
        stick[1] = engine::resultFloat(x);
        x.callLocal(fn::VectorLength2D, stickAddress);
        if (kStickDeadZone <= engine::resultFloat(x)) { direction[0] = stick[0]; direction[1] = stick[1]; }
    }
    if (direction[0] == 0.0f && direction[1] == 0.0f) return;
    float cosines[3];
    for (int32_t i = 0; i < 3; i++) {
        x.c->f[12].u = kSearchAngles[i];
        x.callNid(engine::nid::Cosine);
        cosines[i] = engine::resultFloat(x);
    }
    float *current = (float *)x.mem(currentAddress);
    current[0] = (float)s32(x, kPlacePositions + m.place * 8);
    current[1] = (float)s32(x, kPlacePositions + m.place * 8 + 4);
    x.callNid(engine::nid::Normalize2D, directionAddress, directionAddress);
    int32_t best = -1;
    float bestDistance = kFarthest;
    for (int32_t level = 0; level < 3 && best == -1; level++) {
        for (int32_t place = 0; place < kPlaceCount; place++) {
            if (place == m.place || (excluded & (1u << place)) != 0) continue;
            float *candidate = (float *)x.mem(candidateAddress);
            candidate[0] = (float)s32(x, kPlacePositions + place * 8);
            candidate[1] = (float)s32(x, kPlacePositions + place * 8 + 4);
            x.callNid(engine::nid::Sub2D, differenceAddress, candidateAddress, currentAddress);
            x.callNid(engine::nid::Normalize2D, normalAddress, differenceAddress);
            x.callNid(engine::nid::Dot2D, directionAddress, normalAddress);
            if (cosines[level] <= engine::resultFloat(x)) {
                const float *difference = (const float *)x.mem(differenceAddress);
                x.c->f[12].f = difference[0] * difference[0] + difference[1] * difference[1];
                x.callNid(engine::nid::Sqrt);
                x.c->f[12].f = engine::resultFloat(x);
                x.callNid(engine::nid::Abs);
                const float distance = engine::resultFloat(x);
                if (distance < bestDistance) { best = place; bestDistance = distance; }
            }
        }
    }
    if (best != -1) {
        x.callLocal(fn::PlaySoundWhenReady, kMoveSound, 0);
        m.phase = kPhaseMove;
        m.previousPlace = m.place;
        m.place = best;
        if (best != kBackPlace) restartArrow(x);
    }
}
SwapReg reg_WorldMapInput({"libfield.WorldMapInput", MODULE, offsetOf(0x091DA604), swap_WorldMapInput});

/* ---- FUN_091dbb1c: the width of the widest shop name of the popup (in pixels, +1) ---- */
void swap_PopupWidth(SwapCtx &x) {
    gu::SetFontSize(x, 0xF, 0xF);
    gu::SetUiFlag4(x, true);
    int32_t widest = 0;
    for (int32_t i = 0; i < 5; i++) {
        const uint32_t number = x.callLocal(fn::PlaceText, kShopStreetPlace, (uint32_t)i);
        const uint32_t text = x.callNid(gu::nid::GetText, x.callLocal(fn::SystemSprite, 8), number);
        const int32_t width = (int32_t)x.callNid(gu::nid::MeasureTextWidth, text) >> 6;
        widest = width <= widest ? widest : width;
    }
    x.ret((uint32_t)(widest + 1));
}
SwapReg reg_PopupWidth({"libfield.PopupWidth", MODULE, offsetOf(0x091DBB1C), swap_PopupWidth});

/* ---- FUN_091db944: the popup list of the main street: five rows, one per shop, drawn by `DrawPopupRow` ---- */
void swap_OpenPopup(SwapCtx &x) {
    WorldMap &m = map(x);
    boot::WindowBlock &popup = m.popup;
    memset(&popup, 0, sizeof(popup));
    boot::WindowStyle &style = popup.style;
    style.visible = 1;
    style.fitToText = 1;
    style.textTop = 1; style.textBottom = 1; style.textLeft = 1; style.textRight = 1;
    style.x = 4;
    style.y = 0x26;
    style.colour = 0xFFFFFFFFu;
    style.openAnimation = 2;
    style.closeAnimation = 2;
    popup.flags = (uint8_t)((popup.flags & 0xFD) | 2);
    popup.layoutFlags = (uint8_t)((popup.layoutFlags & 0x87) | 0x10);
    popup.buttonFlags = (uint8_t)(popup.buttonFlags & 0xF0);
    popup.cellCount = 5;
    popup.selection = (uint8_t)(popup.selection & 0xE0);
    style.frameKind = 0;
    popup.firstCell = 0;
    popup.selectedButton = (uint8_t)(popup.selectedButton & 0xF0);
    popup.cellMask = 0;
    const int16_t extra = x.callNid(gu::nid::GetLanguage) == 0 ? 5 : 0;
    const uint32_t width = x.callLocal(fn::PopupWidth);
    popup.cellStrideX = (uint16_t)(width + 8 + extra);
    popup.cellStrideY = 0x19;
    popup.cellHeight = 0x17;
    popup.cellCallback = guest(x, kPopupCallback);
    popup.heap = x.callLocal(fn::HeapHandle);
    x.callNid(gu::nid::LayoutWindow, mapAddress(x, offsetof(WorldMap, popup)));
    m.subChoice = (int32_t)popup.firstCell + (popup.selection & 0x1F);
    GuestStack out(x, 16);
    const uint32_t kindOut = out.addr(), subOut = out.addr() + 4;
    for (int32_t i = 0; i < s32(x, kPlaceCountAddress); i++) {
        x.callLocal(fn::PlaceInfo, (uint32_t)(int32_t)datum<int16_t>(x, kPlaceList + i * 2), kindOut, subOut);
        if (*(int32_t *)x.mem(kindOut) == 4) popup.disabledCells |= 1u << (*(uint32_t *)x.mem(subOut) & 31);
    }
}
SwapReg reg_OpenPopup({"libfield.OpenPopup", MODULE, offsetOf(0x091DB944), swap_OpenPopup});

/* ---- FUN_091db064: waits for the popup's answer; -2 = cancelled (back to the menu) ---- */
void swap_WorldMapWait(SwapCtx &x) {
    WorldMap &m = map(x);
    const int32_t answer = (int32_t)x.callNid(gu::nid::UpdateWindow, mapAddress(x, offsetof(WorldMap, popup)));
    m.subChoice = (int32_t)m.popup.firstCell + (m.popup.selection & 0x1F);
    if (answer != -1) {
        x.callNid(gu::nid::ClearWindow, mapAddress(x, offsetof(WorldMap, popup)));
        if (answer == -2) {
            m.phase = kPhaseInput;
            x.callLocal(fn::StartFade, 1, kFadeColour, 10, 3);
        } else {
            m.phase = kPhaseChosen;
            datum<int32_t>(x, kPopupTarget) = m.subChoice + 0xE;
        }
    }
}
SwapReg reg_WorldMapWait({"libfield.WorldMapWait", MODULE, offsetOf(0x091DB064), swap_WorldMapWait});

/* ---- FUN_091db118: the name window of the place the cursor is on grows in / shrinks out, with the name of the place ---- */
void swap_AnimateCursor(SwapCtx &x) {
    WorldMap &m = map(x);
    GuestStack points(x, 32);
    const uint32_t from = points.addr(), to = points.addr() + 8, out = points.addr() + 16;
    memcpy(x.mem(from), x.mem(guest(x, kCursorRoute)), 16);
    if (m.cursorState == 2) {
        x.c->f[12].f = m.cursorT;
        x.callNid(engine::nid::Lerp2D, out, from, to);
        m.style.x = (int16_t)truncate(((float *)x.mem(out))[0]);
        m.style.y = (int16_t)truncate(((float *)x.mem(out))[1]);
        if (m.cursorT <= 0.0f) m.cursorState = 3;
        else { m.cursorT = m.cursorT - (m.cursorT * kGrowth + kGrowth); if (m.cursorT < 0.0f) m.cursorT = 0.0f; }
    } else if (m.cursorState != 1 && m.cursorState == 0) {
        x.c->f[12].f = m.cursorT;
        x.callNid(engine::nid::Lerp2D, out, from, to);
        m.style.x = (int16_t)truncate(((float *)x.mem(out))[0]);
        m.style.y = (int16_t)truncate(((float *)x.mem(out))[1]);
        if (1.0f <= m.cursorT) m.cursorState = 1;
        else { m.cursorT = m.cursorT + (kGrowth + m.cursorT * kGrowth); if (1.0f < m.cursorT) m.cursorT = 1.0f; }
    }
    const uint32_t number = x.callLocal(fn::PlaceText, (uint32_t)m.place, 0xFFFFFFFFu);
    m.style.text = x.callNid(gu::nid::GetText, x.callLocal(fn::SystemSprite, 8), number);
}
SwapReg reg_AnimateCursor({"libfield.AnimateCursor", MODULE, offsetOf(0x091DB118), swap_AnimateCursor});

/* ---- FUN_091d9e90: one frame of the place menu, by phase ---- */
void swap_UpdateWorldMap(SwapCtx &x) {
    WorldMap &m = map(x);
    if (m.ready == 0) return;
    switch (m.phase) {
        case kPhaseInput: x.callLocal(fn::WorldMapInput); break;
        case kPhaseMove: x.callLocal(fn::WorldMapMove); break;
        case kPhasePopup: x.callLocal(fn::WorldMapWait); break;
        case kPhaseChosen:
            m.answerKind = 0;
            m.answerValue = (int32_t)x.callLocal(fn::PlaceAnswer, (uint32_t)m.place, (uint32_t)m.subChoice);
            m.busy = 0;
            break;
        case kPhaseLeave:
            if (m.request[6] != 0) x.callAddr(m.request[6]);
            x.callNid(gu::nid::StartDataLoad, 5, x.callLocal(fn::HeapHandle));
            m.phase = kPhaseLoad;
            x.callLocal(fn::StartFade, 0, kFadeColour, 10, 4);
            break;
        case kPhaseLoad: {
            const int32_t loaded = (int32_t)x.callNid(gu::nid::PollDataLoad);
            if (loaded == 3 || loaded == 2) m.phase = kPhaseAsk;
            break;
        }
        case kPhaseAsk:
            x.callLocal(fn::OpenAsk, x.callNid(gu::nid::GetText, x.callLocal(fn::SystemSprite, 5), 0x65), 3, 3);
            m.phase = kPhaseAskWait;
            break;
        case kPhaseAskWait: {
            bool answered = false;
            uint32_t yes = 0;
            const int32_t reply = (int32_t)x.callLocal(fn::AskReply);
            if (reply == 2) { answered = true; m.phase = kPhaseAnswerLeave; yes = 1; }
            else if (reply == 3 || reply == -2) {
                answered = true; m.phase = kPhaseInput; yes = 0;
                x.callLocal(fn::StartFade, 1, kFadeColour, 10, 4);
            }
            if (answered && m.request[7] != 0) x.callAddr(m.request[7], yes);
            break;
        }
        case kPhaseAnswerLeave:
            m.answerKind = 1;
            m.answerValue = (int32_t)x.callLocal(fn::PlaceAnswer, (uint32_t)m.homePlace, (uint32_t)m.subChoice);
            m.busy = 0;
            break;
    }
    x.callLocal(fn::UpdateFade);
    x.callLocal(fn::AnimateCursor);
}
SwapReg reg_UpdateWorldMap({"libfield.UpdateWorldMap", MODULE, offsetOf(0x091D9E90), swap_UpdateWorldMap});

}  // namespace
