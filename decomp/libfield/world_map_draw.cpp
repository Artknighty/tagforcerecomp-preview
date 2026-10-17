/* libfield: drawing of the place menu: the island map, the marks of each place, the arrows, the cursor and the picture of the chosen place. */
#include "../swap.h"
#include "../ehsys.h"
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

constexpr uint32_t kWorldMap = 0x09993BC0;
constexpr uint32_t kTextures = kWorldMap + 0x10;     /* island, parts, whole effect, middle effect, arrows 11..14 */
constexpr uint32_t kPhaseAddress = kWorldMap + 0x54, kPlaceAddress = kWorldMap + 0x128, kPreviousAddress = kWorldMap + 0x12C, kSubChoiceAddress = kWorldMap + 0x134;
constexpr uint32_t kPopupAddress = 0x09993CF8, kWindowSet = 0x09993C18, kPopupTarget = 0x09993E18, kPlaceMarks = 0x09993E1C, kSubMarks = 0x09993E54;
constexpr uint32_t kExcludedPlaces = 0x09993EA8, kFirstTime = 0x09993EB4, kDisabledCells = 0x09993DD4;
constexpr uint32_t kFadeKind = 0x09993ED0, kFadeCurrent = 0x09993EC4, kMoveT = 0x09993CE4;
constexpr uint32_t kPlacePositions = 0x091FA2B8, kPlacePictures = 0x091FA4E8, kPictureFirstTime = 0x091FA580, kIconTable = 0x091FA588, kIconFirstTime = 0x091FA5C0;
constexpr int32_t kPlaceCount = 14, kBackPlace = 0xD, kShopStreetPlace = 4, kScreenWidth = 0x1E0;
constexpr uint32_t kWhite = 0xFFFFFFFF;
int32_t s32(SwapCtx &x, uint32_t address) { return datum<int32_t>(x, address); }
uint32_t texture(SwapCtx &x, uint32_t index) { return datum<uint32_t>(x, kTextures + index * 4); }
enum Texture : uint32_t { kIsland = 0, kParts = 1, kWholeEffect = 2, kMiddleEffect = 3, kArrow11 = 4, kArrow12 = 5, kArrow13 = 6, kArrow14 = 7 };

/* a sprite resource drawn with a state block of 0x60 bytes: [0] the frame, [3] flags, [5] x, [6] y (as floats) */
struct SpriteState { uint32_t words[24]; };
uint32_t drawResource(SwapCtx &x, uint32_t list, uint32_t sprite, uint32_t stateAddress) {
    list = gu::BeginSpriteDraw(x, list);
    list = gu::DrawSpriteResource(x, list, sprite, stateAddress);
    return gu::EndSpriteDraw(x, list);
}

/* ---- FUN_091dc3f0(list, kind, x, y, width, height): the icon of a kind of mark ---- */
void swap_DrawPlaceIcon(SwapCtx &x) {
    const uint32_t list = x.arg(0), px = x.arg(2), py = x.arg(3), w = x.arg(4), h = x.arg(5);
    const int32_t kind = (int32_t)x.arg(1);
    uint32_t entry = kIconTable + kind * 8;
    if (kind == 4 && s32(x, kFirstTime) != 0) entry = kIconFirstTime;
    GuestStack ninth(x, 16);
    *(uint32_t *)x.mem(ninth.addr()) = h;
    x.ret(x.callLocal(fn::DrawSpriteQuad, list, px, py, (uint32_t)s32(x, entry), (uint32_t)s32(x, entry + 4), 0x17, 0x17, w));
}
SwapReg reg_DrawPlaceIcon({"libfield.DrawPlaceIcon", MODULE, offsetOf(0x091DC3F0), swap_DrawPlaceIcon});

/* ---- FUN_091dbd88(list): the island map in strips of the texture, and the lit effects ---- */
void swap_DrawMapBase(SwapCtx &x) {
    uint32_t list = gu::SetColor(x, x.arg(0), kWhite);
    list = gu::BindTexture(x, list, texture(x, kIsland));
    list = gu::SetSpriteSize(x, list, 0x40, 0x100, 0x40, 0x100);
    for (int32_t i = 0; i < 7; i++) list = gu::DrawSprite(x, list, i * 0x40, 0, 0, i * 0x40, 0, 0, 0);
    list = gu::SetSpriteSize(x, list, 0x20, 0x100, 0x20, 0x100);
    list = gu::DrawSprite(x, list, 0x1C0, 0, 0, 0x1C0, 0, 0, 0);
    list = gu::SetSpriteSize(x, list, 0x10, 0x100, 0x100, 0x10);
    list = gu::DrawSprite(x, list, 0, 0x100, 0, 0x1E0, 0, 0, 3);
    list = gu::SetSpriteSize(x, list, 0x10, 0x100, 0x100, 0x10);
    list = gu::DrawSprite(x, list, 0x100, 0x100, 0, 0x1F0, 0, 0, 3);
    const bool noBack = (datum<uint32_t>(x, kExcludedPlaces) & 0x1000) != 0;
    if (!noBack) {
        list = gu::BindTexture(x, list, texture(x, kParts));
        list = x.callLocal(fn::DrawSpriteUnscaled, list, 0x2C, 0x24, 0x132, 0xF9, 0x50, 0x50);
    }
    GuestStack state(x, 0x60);
    const uint32_t stateAddress = state.addr();
    memset(x.mem(stateAddress), 0, 0x60);
    *(uint32_t *)x.mem(stateAddress) = *view<uint32_t>(x, gu::FrameCounter(x));
    list = drawResource(x, list, texture(x, kWholeEffect), stateAddress);
    if (!noBack) list = drawResource(x, list, texture(x, kMiddleEffect), stateAddress);
    x.ret(list);
}
SwapReg reg_DrawMapBase({"libfield.DrawMapBase", MODULE, offsetOf(0x091DBD88), swap_DrawMapBase});

/* ---- FUN_091dbf90(list): the picture and description frame of the place the cursor is on (not for the back button or the shop street) ---- */
void swap_DrawPlaceInfo(SwapCtx &x) {
    const uint32_t list = x.arg(0);
    const int32_t phase = s32(x, kPhaseAddress), place = s32(x, kPlaceAddress);
    uint32_t entry;
    if (phase == 4) entry = kPlacePictures + (uint32_t)s32(x, kPopupTarget) * 8;
    else if (phase == 3) entry = kPlacePictures + (uint32_t)(s32(x, kSubChoiceAddress) + 0xE) * 8;
    else {
        if (place == kBackPlace || place == kShopStreetPlace) { x.ret(list); return; }
        entry = kPlacePictures + (uint32_t)place * 8;
    }
    if (place == 0 && s32(x, kFirstTime) != 0) entry = kPictureFirstTime;
    uint32_t drawn = gu::SetColor(x, list, kWhite);
    drawn = gu::BindTexture(x, drawn, texture(x, kParts));
    drawn = x.callLocal(fn::DrawSpriteUnscaled, drawn, 0x170, 0xB3, 0x193, 0xF9, 0x6D, 0x5A);
    x.ret(x.callLocal(fn::DrawSpriteUnscaled, drawn, 0x173, 0xB6, (uint32_t)s32(x, entry), (uint32_t)s32(x, entry + 4), 0x66, 0x53));
}
SwapReg reg_DrawPlaceInfo({"libfield.DrawPlaceInfo", MODULE, offsetOf(0x091DBF90), swap_DrawPlaceInfo});

/* ---- FUN_091dc0bc(list): the marks over each place: one icon per kind of entry the place has, in one row or in two ---- */
void swap_DrawPlaceMarks(SwapCtx &x) {
    uint32_t list = gu::SetColor(x, x.arg(0), kWhite);
    list = gu::BindTexture(x, list, texture(x, kParts));
    list = gu::SetBlend(x, list, 0, 2, 3, 0, 0);
    list = gu::DisableState(x, list, gu::kBlend);
    const int32_t current = s32(x, kPlaceAddress);
    for (int32_t place = 0; place < kPlaceCount; place++) {
        if ((datum<uint32_t>(x, kExcludedPlaces) & (1u << place)) != 0) continue;
        const int32_t size = place == current ? 0x17 : 0x12;
        const uint32_t marks = datum<uint32_t>(x, kPlaceMarks + place * 4);
        int32_t count = 0;
        for (uint32_t kind = 0; kind < 7; kind++) if ((marks & (1u << kind)) != 0) count++;
        if (count == 0) continue;
        const int32_t px = s32(x, kPlacePositions + place * 8), py = s32(x, kPlacePositions + place * 8 + 4);
        if (count < 1 || count > 3) {
            x.c->f[12].f = (float)count * 0.5f;
            x.callNid(engine::nid::Round);
            const int32_t firstRow = truncate(engine::resultFloat(x));
            int32_t advance = 0, index = 0;
            const float half = (float)((firstRow - 1) * size) * 0.5f;
            const float start = -4.0f - half;
            for (uint32_t kind = 0; kind < 7; kind++) {
                if ((marks & (1u << (kind & 31))) == 0) continue;
                int32_t offsetY;
                float offsetX;
                if (index < firstRow) { offsetY = 8; offsetX = (float)(advance - 4) - half; }
                else { offsetY = size + 6; offsetX = start + ((float)size * 0.5f + (float)((size - 1) * (index - firstRow))); }
                list = x.callLocal(fn::DrawPlaceIcon, list, kind, (uint32_t)(px + truncate(offsetX)), (uint32_t)(py + offsetY), (uint32_t)size, (uint32_t)size);
                advance += size - 1;
                index++;
            }
        } else {
            int32_t advance = 0;
            for (uint32_t kind = 0; kind < 7; kind++) {
                if ((marks & (1u << (kind & 31))) == 0) continue;
                list = x.callLocal(fn::DrawPlaceIcon, list, kind, (uint32_t)(px + truncate((float)(advance - 4) - (float)((count - 1) * size) * 0.5f)), (uint32_t)(py + 8), (uint32_t)size, (uint32_t)size);
                advance += size - 1;
            }
        }
    }
    x.ret(list);
}
SwapReg reg_DrawPlaceMarks({"libfield.DrawPlaceMarks", MODULE, offsetOf(0x091DC0BC), swap_DrawPlaceMarks});

/* ---- FUN_091dc45c(list): the arrow over each place (the chosen one also gets its second arrow), and the back button's own arrow ---- */
void swap_DrawArrows(SwapCtx &x) {
    uint32_t list = x.arg(0);
    GuestStack frame(x, 0xE0);
    const uint32_t state = frame.addr(), backState = frame.addr() + 0x70;
    uint32_t *words = (uint32_t *)x.mem(state);
    memset(words, 0, 0x60);
    words[0] = *view<uint32_t>(x, gu::FrameCounter(x));
    words[3] |= 0x10000;
    for (int32_t place = 0; place < kPlaceCount; place++) {
        if ((datum<uint32_t>(x, kExcludedPlaces) & (1u << place)) != 0) continue;
        if (place == kBackPlace) {
            uint32_t *back = (uint32_t *)x.mem(backState);
            memset(back, 0, 0x60);
            if (s32(x, kPlaceAddress) == kBackPlace) back[0] = *view<uint32_t>(x, gu::FrameCounter(x));
            const float px = (float)s32(x, kPlacePositions + place * 8), py = (float)s32(x, kPlacePositions + place * 8 + 4);
            memcpy(&back[5], &px, 4);
            back[3] |= 0x10000;
            memcpy(&back[6], &py, 4);
            list = drawResource(x, list, texture(x, kArrow14), backState);
        }
        const float px = (float)s32(x, kPlacePositions + place * 8), py = (float)s32(x, kPlacePositions + place * 8 + 4);
        memcpy(&words[5], &px, 4);
        memcpy(&words[6], &py, 4);
        if (s32(x, kPhaseAddress) == 4 && place == s32(x, kPlaceAddress)) list = drawResource(x, list, texture(x, kArrow13), state);
        list = drawResource(x, list, texture(x, kArrow11), state);
    }
    x.ret(list);
}
SwapReg reg_DrawArrows({"libfield.DrawArrows", MODULE, offsetOf(0x091DC45C), swap_DrawArrows});

/* ---- FUN_091dc60c(list): the trail of the cursor: an arrow slides from the previous place to the current one ---- */
void swap_DrawCursorTrail(SwapCtx &x) {
    const uint32_t list = x.arg(0);
    GuestStack frame(x, 0xC0);
    const uint32_t state = frame.addr(), points = frame.addr() + 0x70;
    uint32_t *words = (uint32_t *)x.mem(state);
    memset(words, 0, 0x60);
    words[0] = *view<uint32_t>(x, gu::FrameCounter(x));
    words[3] |= 0x10000;
    float *p = (float *)x.mem(points);   /* to (2), from (2), out (2) */
    const int32_t place = s32(x, kPlaceAddress), previous = s32(x, kPreviousAddress);
    p[0] = (float)s32(x, kPlacePositions + place * 8);
    p[1] = (float)s32(x, kPlacePositions + place * 8 + 4);
    p[2] = (float)s32(x, kPlacePositions + previous * 8);
    p[3] = (float)s32(x, kPlacePositions + previous * 8 + 4);
    x.c->f[12].f = datum<float>(x, kMoveT);
    x.callNid(engine::nid::Lerp2D, points + 16, points + 8, points);
    words[5] = ((uint32_t *)p)[4];
    words[6] = ((uint32_t *)p)[5];
    x.ret(drawResource(x, list, texture(x, kArrow12), state));
}
SwapReg reg_DrawCursorTrail({"libfield.DrawCursorTrail", MODULE, offsetOf(0x091DC60C), swap_DrawCursorTrail});

/* ---- FUN_091dbbb8(list, row, x, y): one row of the popup: the shop's name (grey when it cannot be chosen) and the marks of its entries ---- */
void swap_DrawPopupRow(SwapCtx &x) {
    uint32_t list = x.arg(0);
    const uint32_t row = x.arg(1) & 0xFFFF;
    const int32_t px = (int32_t)x.arg(2), py = (int32_t)x.arg(3);
    const int32_t language = (int32_t)x.callNid(gu::nid::GetLanguage);
    const int32_t offset = language == 0 ? (int32_t)datum<int8_t>(x, 0x091FA2B0 + row) : 4;
    const uint32_t number = x.callLocal(fn::PlaceText, 4, row);
    const uint32_t colour = ((datum<uint32_t>(x, kDisabledCells) >> (row & 31)) & 1) == 0 ? 0xFF000000u : 0xFF808080u;
    gu::SetWideScreenFlag(x, x.callNid(gu::nid::GetLanguage) == 0);
    gu::BeginText(x, list);
    gu::SetFontSize(x, 0xF, 0xF);
    gu::SetUiFlag4(x, true);
    gu::SetTextColor(x, colour);
    const uint32_t text = x.callNid(gu::nid::GetText, x.callLocal(fn::SystemSprite, 8), number);
    const int32_t lineOffset = x.callNid(gu::nid::GetLanguage) == 0 ? 1 : 5;
    gu::DrawText(x, (px + offset) * 0x40, (py + lineOffset) * 0x40, (kScreenWidth - (px + offset)) * 0x40, text);
    list = gu::EndText(x);
    list = gu::SetColor(x, list, kWhite);
    list = gu::BindTexture(x, list, texture(x, kParts));
    const boot::WindowStyle &style = *view<boot::WindowStyle>(x, kPopupAddress + 0x4C);
    int32_t advance = 0;
    for (uint32_t kind = 0; kind < 7; kind++) {
        if ((datum<uint32_t>(x, kSubMarks + row * 4) & (1u << kind)) == 0) continue;
        list = x.callLocal(fn::DrawPlaceIcon, list, kind, (uint32_t)((int32_t)style.x + (int32_t)style.width + advance), (uint32_t)py, 0x17, 0x17);
        advance += 0x17;
    }
    x.ret(list);
}
SwapReg reg_DrawPopupRow({"libfield.DrawPopupRow", MODULE, offsetOf(0x091DBBB8), swap_DrawPopupRow});

/* ---- FUN_091da0d0: draws the place menu in two display lists (the map, then the dimming fade and the question) ---- */
void swap_DrawWorldMap(SwapCtx &x) {
    if (datum<int32_t>(x, kWorldMap + 0x50) == 0) return;
    const int32_t phase = s32(x, kPhaseAddress);
    const int32_t fadeKind = s32(x, kFadeKind);
    GuestStack cursor(x, 16);
    const uint32_t cursorAddress = cursor.addr();
    uint32_t list = gu::BeginList(x, 3, 0);
    list = x.callLocal(fn::DrawMapBase, list);
    list = x.callLocal(fn::DrawPlaceMarks, list);
    list = x.callLocal(fn::DrawArrows, list);
    list = x.callLocal(fn::DrawCursorTrail, list);
    if (fadeKind == 3 && (phase == 4 || phase == 3 || phase == 2 || phase == 1)) list = x.callLocal(fn::DrawFadeQuad, list, 0, datum<uint32_t>(x, kFadeCurrent));
    list = x.callLocal(fn::DrawPlaceInfo, list);
    *(uint32_t *)x.mem(cursorAddress) = list;
    x.callNid(engine::nid::DrawWindowSet, cursorAddress, guest(x, kWindowSet));
    if (phase == 3) x.callNid(gu::nid::DrawWindow, cursorAddress, guest(x, kPopupAddress));
    gu::EndList(x, *(uint32_t *)x.mem(cursorAddress));
    list = gu::BeginList(x, 4, 0);
    if (fadeKind == 4) {
        switch (phase) {
            case 0: case 3: case 4: break;
            case 1: case 2: case 5: case 6: case 7: case 8: case 9:
                list = x.callLocal(fn::DrawFadeQuad, list, 0, datum<uint32_t>(x, kFadeCurrent));
                break;
        }
    }
    if (phase == 8) list = x.callLocal(fn::DrawAsk, list);
    gu::EndList(x, list);
    if (phase == 6) x.callNid(gu::nid::AdvanceFlow);
}
SwapReg reg_DrawWorldMap({"libfield.DrawWorldMap", MODULE, offsetOf(0x091DA0D0), swap_DrawWorldMap});

}  // namespace
