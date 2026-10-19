/* libfield: drawing of the HUD: sprites from the tables of the module's data, numbers made of digit sprites, the analogue clock. Every function takes the
 * display list cursor and returns the cursor after what it drew. */
#include "../common/swap.h"
#include "../common/ehsys.h"
#include "field_data.h"
#include "field_engine.h"
#include "field_functions.h"
#include <stddef.h>
#include <string.h>

#define MODULE "libfield"

namespace {
using namespace field;
namespace gu = ehsys;

struct Slide { int32_t state; float x, y; float t; };
constexpr uint32_t kHud = 0x0999360C, kHudSprites = 0x09993610, kHudTitle = 0x0999361C, kCounters = 0x09993620, kCounterBar = 0x0999362C, kHudWindows = 0x09993630;
constexpr uint32_t kSlideDate = 0x09993704, kSlideWeekday = 0x09993714, kSlideCounters = 0x09993758, kSlideBadge = 0x09993768;
constexpr uint32_t kBlink = 0x099937F0, kBadgeFlag = 0x09993808;
constexpr uint32_t kTextFormat = 0x091FE074, kTextFormatTwoDigits = 0x091FE078;   /* "%d" and "%02d" */
constexpr uint32_t kWhite = 0xFFFFFFFF;
constexpr float kPi = 3.1415927f;

struct Blink { int32_t enabled, on, kind, period, limit; };
Slide &slide(SwapCtx &x, uint32_t address) { return datum<Slide>(x, address); }
bool blinkHides(SwapCtx &x, int32_t kind) { const Blink &b = datum<Blink>(x, kBlink); return b.kind == kind && b.enabled != 0 && b.on != 0; }
int16_t s16(SwapCtx &x, uint32_t address) { return datum<int16_t>(x, address); }
int32_t s32(SwapCtx &x, uint32_t address) { return datum<int32_t>(x, address); }
int8_t s8(SwapCtx &x, uint32_t address) { return datum<int8_t>(x, address); }
uint8_t u8(SwapCtx &x, uint32_t address) { return datum<uint8_t>(x, address); }
int32_t valueAt(SwapCtx &x, uint32_t pointer) { return *view<int32_t>(x, pointer); }

/* a rectangle of the bound texture drawn at its natural size */
uint32_t drawRect(SwapCtx &x, uint32_t list, float px, float py, int32_t u, int32_t v, int32_t w, int32_t h) {
    return x.callLocal(fn::DrawSpriteUnscaled, list, (uint32_t)truncate(px), (uint32_t)truncate(py), (uint32_t)u, (uint32_t)v, (uint32_t)w, (uint32_t)h);
}
uint32_t drawTableRect(SwapCtx &x, uint32_t list, float px, float py, uint32_t table) {
    return drawRect(x, list, px, py, s16(x, table), s16(x, table + 2), s16(x, table + 4), s16(x, table + 6));
}
uint32_t bindHudSprite(SwapCtx &x, uint32_t list, uint32_t index) { return gu::BindTexture(x, list, datum<uint32_t>(x, kHudSprites + index * 4)); }
uint32_t plainColour(SwapCtx &x, uint32_t list) { return gu::SetTextureFilter(x, gu::SetColor(x, list, kWhite), 0, 0); }

/* a number made of digit sprites: formats it with the game's sprintf, then draws it right to left (FUN_091d67d8 has two arguments on the stack) */
uint32_t drawNumber(SwapCtx &x, float scale, uint32_t list, uint32_t format, int32_t value, float px, float py, uint32_t z, uint32_t u, uint32_t v, uint32_t w, uint32_t h, uint32_t spacing, uint32_t texture = 0) {
    GuestStack buffer(x, 256);
    const uint32_t bufferAddress = buffer.addr();   /* GuestStack::addr() is the stack pointer NOW: keep the address of each block when it is made */
    x.callImport("sprintf", bufferAddress, guest(x, format), (uint32_t)value);
    if (texture != 0) list = gu::BindTexture(x, list, texture);   /* the game formats the number first, then binds the sprite */
    GuestStack stackArguments(x, 16);   /* last: the callee reads its stack arguments at the stack pointer */
    uint32_t *stackWords = (uint32_t *)x.mem(stackArguments.addr());
    stackWords[0] = h;
    stackWords[1] = spacing;
    x.c->f[12].f = scale;
    return x.callLocal(fn::DrawNumberString, list, bufferAddress, (uint32_t)truncate(px), (uint32_t)truncate(py), z, u, v, w);
}

/* ---- FUN_091d6718(scale, list, x, y, z, u, v, w, h, index): one glyph of a digit strip, scaled ---- */
void swap_DrawGlyph(SwapCtx &x) {
    const float scale = x.c->f[12].f;
    const uint32_t list = x.arg(0), px = x.arg(1), py = x.arg(2), z = x.arg(3), u = x.arg(4), v = x.arg(5), w = x.arg(6), h = x.arg(7);
    const int32_t index = (int32_t)*view<uint32_t>(x, x.c->r[29]);
    const uint32_t sized = gu::SetSpriteSize(x, list, (int32_t)w, (int32_t)h, truncate((float)(int32_t)w * scale), truncate((float)(int32_t)h * scale));
    x.ret(gu::DrawSprite(x, sized, (int32_t)px, (int32_t)py, (int32_t)z, (int32_t)u + (int32_t)w * index, (int32_t)v, 0, 0));
}
SwapReg reg_DrawGlyph({"libfield.DrawGlyph", MODULE, offsetOf(0x091D6718), swap_DrawGlyph});

/* ---- FUN_091d67d8(scale, list, text, x, y, z, u, v, w, h, spacing): the digits of the text, right to left from x ---- */
void swap_DrawNumberString(SwapCtx &x) {
    const float scale = x.c->f[12].f;
    uint32_t list = x.arg(0);
    const uint32_t text = x.arg(1), z = x.arg(4), u = x.arg(5), v = x.arg(6), w = x.arg(7);
    const int32_t px = (int32_t)x.arg(2);
    const uint32_t py = x.arg(3);
    const uint32_t h = *view<uint32_t>(x, x.c->r[29]);
    const int32_t spacing = (int32_t)*view<uint32_t>(x, x.c->r[29] + 4);
    const int32_t last = (int32_t)x.callImport("strlen", text) - 1;
    if (last >= 0) {
        const float step = (float)spacing + (float)(int32_t)w * scale;
        for (int32_t i = last; i >= 0; i--) {
            GuestStack index(x, 16);
            *(int32_t *)x.mem(index.addr()) = (int32_t)(int8_t)*x.mem(text + i) - 0x30;
            x.c->f[12].f = scale;
            list = x.callLocal(fn::DrawGlyph, list, (uint32_t)truncate((float)px - step * (float)(last - i)), py, z, u, v, w, h);
        }
    }
    x.ret(list);
}
SwapReg reg_DrawNumberString({"libfield.DrawNumberString", MODULE, offsetOf(0x091D67D8), swap_DrawNumberString});

/* ---- FUN_091d68fc(list, index, x, y, z): a weekday name: the sprite strip has two rows (0..3 and 4..6) ---- */
void swap_DrawWeekdaySprite(SwapCtx &x) {
    int32_t index = (int32_t)x.arg(1);
    uint32_t v = x.arg(5), u = x.arg(6);
    if (index >= 0 && index <= 3) { u = 0; v = 0; }
    else if (index >= 4 && index <= 6) { u = 0; v = 0x10; index -= 4; }
    GuestStack stackArgument(x, 16);
    *(int32_t *)x.mem(stackArgument.addr()) = index;
    x.c->f[12].f = 1.0f;
    x.ret(x.callLocal(fn::DrawGlyph, x.arg(0), x.arg(2), x.arg(3), x.arg(4), u, v, 0x40, 0x10));
}
SwapReg reg_DrawWeekdaySprite({"libfield.DrawWeekdaySprite", MODULE, offsetOf(0x091D68FC), swap_DrawWeekdaySprite});

/* ---- FUN_091d6a78(angle, list, x, y, z, u, v, w, h): a sprite rotated about its centre (four vertices built by hand) ---- */
void swap_DrawRotatedSprite(SwapCtx &x) {
    const float angle = x.c->f[12].f;
    const uint32_t list = x.arg(0);
    const int32_t px = (int32_t)x.arg(1), py = (int32_t)x.arg(2);
    const int16_t z = (int16_t)x.arg(3), u = (int16_t)x.arg(4), v = (int16_t)x.arg(5);
    const int32_t w = (int32_t)x.arg(6), h = (int32_t)x.arg(7);
    GuestStack frame(x, 0xA0);
    const uint32_t cursor = frame.addr(), matrix = frame.addr() + 0x10, points = frame.addr() + 0x40;
    Vec3 *p = (Vec3 *)x.mem(points);            /* the four corners: (0,0) (w,0) (0,h) (w,h) */
    *(uint32_t *)x.mem(cursor) = list;
    const float fw = (float)w, fh = (float)h;
    const float corner[4][2] = {{0, 0}, {fw, 0}, {0, fh}, {fw, fh}};
    for (int32_t i = 0; i < 4; i++) {
        x.c->f[12].f = corner[i][0]; x.c->f[13].f = corner[i][1]; x.c->f[14].f = 0;
        x.callNid(engine::nid::MakeVector, points + i * 12);
    }
    x.callNid(engine::nid::MatrixIdentity, matrix);
    x.c->f[12].f = angle;
    x.callNid(engine::nid::MatrixRotate, matrix, matrix);
    const float halfW = fw * 0.5f, halfH = fh * 0.5f;
    for (int32_t i = 0; i < 4; i++) { p[i].x -= halfW; p[i].y -= halfH; }
    for (int32_t i = 0; i < 4; i++) x.callNid(engine::nid::TransformPoint, points + i * 12, matrix, points + i * 12);
    for (int32_t i = 0; i < 4; i++) { p[i].x += halfW; p[i].y += halfH; }
    const uint32_t verticesAddress = gu::ReserveListData(x, cursor, 0x40);
    int16_t *vertices = (int16_t *)x.mem(verticesAddress);
    const float fx = (float)px, fy = (float)py;
    const int16_t right = (int16_t)(u + (int16_t)w), bottom = (int16_t)(v + (int16_t)h);
    const struct { int16_t u, v; int32_t point; } layout[4] = {{right, bottom, 3}, {right, v, 1}, {u, bottom, 2}, {u, v, 0}};
    for (int32_t i = 0; i < 4; i++) {
        int16_t *vertex = vertices + i * 8;
        vertex[0] = layout[i].u; vertex[1] = layout[i].v; vertex[2] = -1; vertex[3] = -1;
        vertex[4] = (int16_t)truncate(p[layout[i].point].x + fx); vertex[5] = (int16_t)truncate(p[layout[i].point].y + fy); vertex[6] = z;
    }
    x.callNid(engine::nid::MatrixMode, 2);
    x.callNid(engine::nid::PushMatrix);
    x.callNid(engine::nid::LoadIdentity);
    uint32_t drawn = gu::SetTextureFilter(x, *(uint32_t *)x.mem(cursor), 1, 1);
    drawn = gu::SetTextureWrap(x, drawn, 1, 1);
    drawn = gu::DrawArray(x, drawn, 4, 0x80011E, 4, 0, verticesAddress);
    x.callNid(engine::nid::PopMatrix);
    drawn = gu::SetTextureFilter(x, drawn, 0, 0);
    x.ret(gu::SetTextureWrap(x, drawn, 0, 0));
}
SwapReg reg_DrawRotatedSprite({"libfield.DrawRotatedSprite", MODULE, offsetOf(0x091D6A78), swap_DrawRotatedSprite});

/* ---- FUN_091d6974(list, x, y, z, hour, minute): the hands of the analogue clock: the minute hand, then the hour hand (which moves with the minutes) ---- */
void swap_DrawClockHands(SwapCtx &x) {
    const uint32_t list = x.arg(0), px = x.arg(1), py = x.arg(2), pz = x.arg(3);
    const float hour = (float)(int32_t)x.arg(4), minute = (float)(int32_t)x.arg(5);
    const float minuteAngle = 360.0f * (minute / 60.0f);
    x.c->f[12].f = (kPi * minuteAngle) / 180.0f;
    const uint32_t afterMinute = x.callLocal(fn::DrawRotatedSprite, list, px, py, pz, 248, 32, 3, 39);
    x.c->f[12].f = (kPi * (360.0f * (hour / 12.0f) + minuteAngle / 12.0f)) / 180.0f;
    x.ret(x.callLocal(fn::DrawRotatedSprite, afterMinute, px, py, pz, 252, 32, 3, 39));
}
SwapReg reg_DrawClockHands({"libfield.DrawClockHands", MODULE, offsetOf(0x091D6974), swap_DrawClockHands});


/* ---- FUN_091d2ff0: draws the HUD ---- */
void swap_DrawHud(SwapCtx &x) {
    if (s32(x, kHud) == 0) return;
    GuestStack cursorCell(x, 16);
    uint32_t list = gu::BeginList(x, 3, 0);
    list = gu::SetBlend(x, list, 0, 2, 3, 0, 0);
    list = gu::EnableState(x, list, gu::kBlend);
    list = gu::AlphaFunc(x, list, 6, 0, 0xFF);
    list = gu::EnableState(x, list, gu::kAlphaTest);
    list = gu::DisableState(x, list, gu::kDepthTest);
    list = gu::EnableState(x, list, gu::kTexture2D);
    list = gu::SetTextureMode(x, list, 3, 0, 0, 0);
    list = gu::SetTextureFunction(x, list, 0, 1);
    list = gu::SetTextureWrap(x, list, 0, 0);
    list = gu::SetColor(x, list, kWhite);
    gu::SetWideScreenFlag(x, 0);
    const Blink &b = datum<Blink>(x, kBlink);
    if (b.kind != 0 || b.enabled == 0 || b.on == 0) {
        *(uint32_t *)x.mem(cursorCell.addr()) = list;
        x.callNid(engine::nid::DrawWindowSet, cursorCell.addr(), guest(x, kHudWindows));
        list = *(uint32_t *)x.mem(cursorCell.addr());
    }
    list = x.callLocal(fn::PassList, list);
    list = x.callLocal(fn::DrawCalendar, list);
    if ((int32_t)x.callLocal(fn::ProgressStage) < 2 && !blinkHides(x, 5)) list = x.callLocal(fn::DrawWeekday, list);
    if ((int32_t)x.callLocal(fn::ProgressStage) > 1 && !blinkHides(x, 6)) {
        list = x.callLocal(fn::DrawHudExtraA, list);
        list = x.callLocal(fn::DrawHudExtraB, list);
    }
    list = x.callLocal(fn::DrawBadgeIcons, list);
    list = x.callLocal(fn::DrawCounters, list);
    gu::EndList(x, list);
}
SwapReg reg_DrawHud({"libfield.DrawHud", MODULE, offsetOf(0x091D2FF0), swap_DrawHud});

/* ---- FUN_091d3838: the calendar, by language, then (on the first stages) the clock and the days left ---- */
void swap_DrawCalendar(SwapCtx &x) {
    uint32_t list = x.arg(0);
    if (slide(x, kSlideDate).state != 0) {
        switch (x.callNid(gu::nid::GetLanguage)) {
            case 0: list = x.callLocal(fn::DrawCalendarLanguage0, list); break;
            case 2: list = x.callLocal(fn::DrawCalendarLanguage2, list); break;
            case 3: list = x.callLocal(fn::DrawCalendarLanguage3, list); break;
            case 4: list = x.callLocal(fn::DrawCalendarLanguage4, list); break;
            case 5: list = x.callLocal(fn::DrawCalendarLanguage5, list); break;
            default: list = x.callLocal(fn::DrawCalendarEnglish, list); break;
        }
        const int32_t stage = (int32_t)x.callLocal(fn::ProgressStage);
        if (stage != 3 && stage != 2) {
            if (!blinkHides(x, 3)) list = x.callLocal(fn::DrawClock, list);
            if (!blinkHides(x, 4)) list = x.callLocal(fn::DrawDigitalClock, list);
        }
    }
    x.ret(list);
}
SwapReg reg_DrawCalendar({"libfield.DrawCalendar", MODULE, offsetOf(0x091D3838), swap_DrawCalendar});

/* ---- FUN_091d47e4(list, x1, x2, x3): the frame of the date box and the weekday name ---- */
void swap_DrawDateBox(SwapCtx &x) {
    const Slide &date = slide(x, kSlideDate);
    const float dx = (float)(int32_t)x.arg(1), dx2 = (float)(int32_t)x.arg(2), dx3 = (float)(int32_t)x.arg(3);
    uint32_t list = bindHudSprite(x, x.arg(0), 0);
    list = drawRect(x, list, date.x + dx, date.y, 0, 0x58, 0xC0, 0x16);
    list = drawRect(x, list, date.x + dx2, date.y + 21.0f, 0x1C, 0x6E, 0xA4, 0x12);
    const int32_t weekdayPointer = (int32_t)fieldArguments(x).weekday;
    const bool hidden = datum<Blink>(x, kBlink).kind == 2 && datum<Blink>(x, kBlink).enabled != 0 && datum<Blink>(x, kBlink).on != 0;
    if (!hidden && weekdayPointer != 0) {
        list = bindHudSprite(x, list, 1);
        list = x.callLocal(fn::DrawWeekdaySprite, list, (uint32_t)valueAt(x, weekdayPointer), (uint32_t)truncate(date.x + dx3), (uint32_t)truncate(date.y + 22.0f), 0);
    }
    x.ret(list);
}
SwapReg reg_DrawDateBox({"libfield.DrawDateBox", MODULE, offsetOf(0x091D47E4), swap_DrawDateBox});

/* ---- FUN_091d3c8c: the English calendar: the month name, the day number and its ordinal ending ("st", "nd", "rd", "th") ---- */
void swap_DrawCalendarEnglish(SwapCtx &x) {
    const FieldArguments &args = fieldArguments(x);
    const Slide &date = slide(x, kSlideDate);
    const int32_t month = valueAt(x, args.month);
    uint32_t list = gu::SetColor(x, x.arg(0), kWhite);
    list = gu::SetTextureFilter(x, list, 0, 0);
    list = x.callLocal(fn::DrawDateBox, list, 0xFFFFFFECu, 0x12, 0x18);
    if (!blinkHides(x, 1)) {
        list = bindHudSprite(x, list, 1);
        if (args.month != 0) {
            const uint32_t entry = 0x091FA17C + (uint32_t)((month - 5) * 12);
            list = drawRect(x, list, date.x + 31.0f, date.y + 3.0f, s16(x, entry), s16(x, entry + 2), s16(x, entry + 4), s16(x, entry + 6));
        }
        if (args.day != 0) {
            const int32_t day = valueAt(x, args.day);
            list = drawNumber(x, 1.0f, list, kTextFormat, day, date.x - 3.0f, (date.y + 3.0f) - 1.0f, 0, 0, 0x20, 0x10, 0x10, 0xFFFFFFFCu);
            uint32_t ending = 0x091FA170;
            if (day != 13 && day != 12 && day != 11) {
                const int32_t last = day % 10;
                ending = last == 3 ? 0x091FA164 : last == 2 ? 0x091FA158 : last == 1 ? 0x091FA14C : 0x091FA170;
            }
            list = drawTableRect(x, list, date.x + 11.0f, date.y + 3.0f, ending);
        }
    }
    x.ret(list);
}
SwapReg reg_DrawCalendarEnglish({"libfield.DrawCalendarEnglish", MODULE, offsetOf(0x091D3C8C), swap_DrawCalendarEnglish});

/* ---- FUN_091d4b88: the analogue clock ---- */
void swap_DrawClock(SwapCtx &x) {
    const FieldArguments &args = fieldArguments(x);
    const Slide &date = slide(x, kSlideDate);
    uint32_t list = gu::SetTextureFilter(x, gu::SetColor(x, x.arg(0), kWhite), 0, 0);
    list = bindHudSprite(x, list, 0);
    list = drawRect(x, list, date.x + 79.0f, date.y, 0, 0x20, 0x2A, 0x2A);
    x.ret(x.callLocal(fn::DrawClockHands, list, (uint32_t)truncate(date.x + 97.0f), (uint32_t)truncate(date.y + 1.0f), 0, (uint32_t)valueAt(x, args.hour), (uint32_t)valueAt(x, args.minute)));
}
SwapReg reg_DrawClock({"libfield.DrawClock", MODULE, offsetOf(0x091D4B88), swap_DrawClock});

/* ---- FUN_091d4978: the digital time: hour and minute (the minute is two digits) ---- */
void swap_DrawDigitalClock(SwapCtx &x) {
    const FieldArguments &args = fieldArguments(x);
    const Slide &date = slide(x, kSlideDate);
    uint32_t list = x.arg(0);   /* read before the call below clobbers the argument registers */
    const int32_t mode = (int32_t)x.callNid(gu::nid::GetUiMode);
    const float frameX = mode == 1 ? 66.0f : 90.0f, textX = mode == 1 ? 54.0f : 78.0f;
    list = bindHudSprite(x, list, 0);
    list = drawRect(x, list, date.x + frameX, date.y + 40.0f, 0x51, 0, 6, 0x10);
    if (args.hour != 0) list = drawNumber(x, 1.0f, list, kTextFormat, valueAt(x, args.hour), date.x + textX, date.y + 40.0f, 0, 0x57, 0, 0xC, 0x10, kWhite);
    if (args.minute != 0) list = drawNumber(x, 1.0f, list, kTextFormatTwoDigits, valueAt(x, args.minute), date.x + textX + 30.0f, date.y + 40.0f, 0, 0x57, 0, 0xC, 0x10, kWhite);
    x.ret(list);
}
SwapReg reg_DrawDigitalClock({"libfield.DrawDigitalClock", MODULE, offsetOf(0x091D4978), swap_DrawDigitalClock});

/* ---- FUN_091d4d6c: the weekday sprite and the number of days left (the first stages) ---- */
void swap_DrawWeekday(SwapCtx &x) {
    const FieldArguments &args = fieldArguments(x);
    const Slide &weekday = slide(x, kSlideWeekday);
    uint32_t list = x.arg(0);
    if (weekday.state != 0) {
        list = plainColour(x, list);
        list = bindHudSprite(x, list, 1);
        const uint32_t row = 0x091FA054 + x.callNid(gu::nid::GetLanguage) * 8;
        list = drawRect(x, list, weekday.x + (float)s8(x, row), weekday.y + (float)s8(x, row + 1), u8(x, row + 2), u8(x, row + 3), u8(x, row + 4), u8(x, row + 5));
        if (args.daysLeft != 0) {
            list = drawNumber(x, 1.0f, list, kTextFormat, valueAt(x, args.daysLeft), weekday.x + (float)s8(x, row + 6), weekday.y + (float)s8(x, row + 7), 0,
                              (uint32_t)s16(x, 0x091FA084), (uint32_t)s16(x, 0x091FA086), (uint32_t)s16(x, 0x091FA088), (uint32_t)s16(x, 0x091FA08A), kWhite,
                              datum<uint32_t>(x, kHudSprites + (uint32_t)s32(x, 0x091FA08C) * 4));
        }
    }
    x.ret(list);
}
SwapReg reg_DrawWeekdayBox({"libfield.DrawWeekdayBox", MODULE, offsetOf(0x091D4D6C), swap_DrawWeekday});

/* ---- FUN_091d5784: the two small icons beside the counters and the number next to the second ---- */
void swap_DrawBadgeIcons(SwapCtx &x) {
    const FieldArguments &args = fieldArguments(x);
    const Slide &counters = slide(x, kSlideCounters);
    uint32_t list = x.arg(0);
    if (counters.state == 0) { x.ret(list); return; }
    list = plainColour(x, list);
    const bool showSecond = s32(x, kBadgeFlag) == 0 && (args.playerValue2C == 0 || valueAt(x, args.playerValue2C) > 0);
    for (int32_t i = 0; i < 2; i++) {
        const uint32_t rect = 0x091FA0B0 + i * 12, offset = 0x091FA0D4 + i * 8;
        if (i == 1 && !showSecond) continue;
        list = bindHudSprite(x, list, (uint32_t)s32(x, rect + 8));
        list = drawRect(x, list, counters.x + (float)s32(x, offset), counters.y + (float)s32(x, offset + 4), s16(x, rect), s16(x, rect + 2), s16(x, rect + 4), s16(x, rect + 6));
    }
    if (s32(x, kBadgeFlag) == 0 && args.playerValue2C != 0 && valueAt(x, args.playerValue2C) > 0) {
        list = drawNumber(x, 1.0f, list, kTextFormat, valueAt(x, args.playerValue2C), counters.x + (float)s32(x, 0x091FA0E4), counters.y + (float)s32(x, 0x091FA0E8), 0,
                          (uint32_t)s16(x, 0x091FA0C8), (uint32_t)s16(x, 0x091FA0CA), (uint32_t)s16(x, 0x091FA0CC), (uint32_t)s16(x, 0x091FA0CE), kWhite,
                          datum<uint32_t>(x, kHudSprites + (uint32_t)s32(x, 0x091FA0D0) * 4));
    }
    x.ret(list);
}
SwapReg reg_DrawBadgeIcons({"libfield.DrawBadgeIcons", MODULE, offsetOf(0x091D5784), swap_DrawBadgeIcons});

/* ---- FUN_091d5be8: the three counters (up to 3 digits each) with their labels or icons, the title and the bar ---- */
struct CounterLayout { uint32_t digits, label, icon; };
constexpr CounterLayout kCounterLayout[3] = {{0x091FA11C, 0x091FA124, 0x091FA1B8}, {0x091FA12C, 0x091FA134, 0x091FA1C0}, {0x091FA13C, 0x091FA144, 0x091FA1C8}};
constexpr int32_t kCounterDigits = 3, kCounterMax = 999, kScreenWidth = 0x1E0;
constexpr uint32_t kCounterTextColour = 0xFF000000;
constexpr float kIconUnitsShift = 15.0f;

uint32_t drawCounterDigits(SwapCtx &x, uint32_t list, int32_t value, const CounterLayout &layout, float shift) {
    const Slide &badge = slide(x, kSlideBadge);
    const int32_t baseX = s32(x, layout.digits), baseY = s32(x, layout.digits + 4);
    value = value < 0 ? 0 : value;
    if (value > kCounterMax) value = kCounterMax;
    for (int32_t k = 0; k < kCounterDigits; k++) {
        GuestStack text(x, 16);
        int16_t *characters = (int16_t *)x.mem(text.addr());
        characters[1] = 0;
        characters[0] = (int16_t)(value % 10 + 0x30);
        value = value / 10;
        gu::BeginText(x, list);
        int32_t px;
        if (shift == 0.0f) px = truncate(badge.x + (float)baseX + (float)((2 - k) * 12));
        else px = truncate(badge.x + (float)baseX + (float)((2 - k) * 12) + shift);
        gu::DrawText(x, px << 6, truncate(badge.y + (float)baseY) << 6, (kScreenWidth - px) * 0x40, text.addr());
        list = gu::EndText(x);
    }
    return list;
}
void swap_DrawCounters(SwapCtx &x) {
    const Slide &badge = slide(x, kSlideBadge);
    uint32_t list = x.arg(0);
    if (badge.state == 0) { x.ret(list); return; }
    gu::SetFontSize(x, 0xF, 0xF);
    gu::SetUiFlag4(x, true);
    gu::SetTextColor(x, kCounterTextColour);
    const uint32_t title = datum<uint32_t>(x, kHudTitle);
    if (title != 0) {
        gu::BeginText(x, list);
        gu::DrawTextAligned(x, truncate(badge.x + (float)s32(x, 0x091FA114)) << 6, truncate(badge.y + (float)s32(x, 0x091FA118)) << 6, 0x2580, title);
        list = gu::EndText(x);
    }
    const int32_t mode = (int32_t)x.callNid(gu::nid::GetUiMode);
    int32_t counter[3];
    for (int32_t k = 0; k < 3; k++) counter[k] = s32(x, kCounters + k * 4);
    if (mode == 0) {
        for (int32_t k = 0; k < 3; k++) if (counter[k] > -1) list = drawCounterDigits(x, list, counter[k], kCounterLayout[k], 0.0f);
        for (int32_t k = 0; k < 3; k++) {
            if (counter[k] <= -1) continue;
            gu::BeginText(x, list);
            const uint32_t text = x.callNid(gu::nid::GetText, x.callLocal(fn::SystemSprite, 5), 0xF + k);
            const int32_t px = truncate(badge.x + (float)s32(x, kCounterLayout[k].label));
            gu::DrawText(x, px << 6, truncate(badge.y + (float)s32(x, kCounterLayout[k].label + 4)) << 6, (kScreenWidth - truncate(badge.x + (float)s32(x, kCounterLayout[k].label))) * 0x40, text);
            list = gu::EndText(x);
        }
    } else {
        for (int32_t k = 0; k < 3; k++) if (counter[k] > -1) list = drawCounterDigits(x, list, counter[k], kCounterLayout[k], kIconUnitsShift);
        list = bindHudSprite(x, list, 2);
        list = gu::SetTextureFilter(x, gu::SetColor(x, list, kWhite), 0, 0);
        for (int32_t k = 0; k < 3; k++) {
            if (counter[k] <= -1) continue;
            list = x.callLocal(fn::DrawSpriteUnscaled, list, (uint32_t)truncate(badge.x + (float)s32(x, kCounterLayout[k].digits)),
                               (uint32_t)truncate(badge.y + (float)s32(x, kCounterLayout[k].digits + 4) + 1.0f), (uint32_t)s32(x, kCounterLayout[k].icon),
                               (uint32_t)s32(x, kCounterLayout[k].icon + 4), 0xD, 0xD);
        }
    }
    list = gu::SetTextureFilter(x, gu::SetColor(x, list, kWhite), 0, 0);
    if (title != 0) {
        list = bindHudSprite(x, list, (uint32_t)s32(x, 0x091FA0F4));
        list = drawRect(x, list, badge.x + (float)s32(x, 0x091FA104), badge.y + (float)s32(x, 0x091FA108), s16(x, 0x091FA0EC), s16(x, 0x091FA0EE), s16(x, 0x091FA0F0), s16(x, 0x091FA0F2));
    }
    const int32_t bar = s32(x, kCounterBar);
    if (0 < bar) {
        list = bindHudSprite(x, list, (uint32_t)s32(x, 0x091FA100));
        list = drawRect(x, list, badge.x + (float)s32(x, 0x091FA10C), badge.y + (float)s32(x, 0x091FA110), s16(x, 0x091FA0F8), s16(x, 0x091FA0FA), bar * (int32_t)s16(x, 0x091FA0FC), s16(x, 0x091FA0FE));
    }
    x.ret(list);
}
SwapReg reg_DrawCounters({"libfield.DrawCounters", MODULE, offsetOf(0x091D5BE8), swap_DrawCounters});

}  // namespace
