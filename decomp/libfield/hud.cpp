/* libfield: the HUD of the map: the calendar with the date and weekday, the analogue clock, the "days left" and the three counters. Its parts slide in
 * from the edge of the screen (a `Slide`: state, position, and a fraction t that grows by t * 0.1 + 0.1 each frame) and are drawn from sprite tables
 * in the module's data. */
#include "../swap.h"
#include "../ehsys.h"
#include "field_data.h"
#include "field_engine.h"
#include "field_functions.h"
#include <stddef.h>
#include <string.h>

#define MODULE "libfield"

namespace {
using namespace field;
namespace gu = ehsys;

/* a part of the HUD that slides: 0 = away, 1 = coming, 2 = there, 3 = leaving */
struct Slide { int32_t state; float x, y; float t; };
static_assert(sizeof(Slide) == 16, "Slide layout");
struct SlideRoute { float from[2], to[2]; };   /* where a slide starts and ends (a table in the module's data) */

constexpr uint32_t kHud = 0x0999360C;
constexpr uint32_t kHudSprites = 0x09993610;       /* three system sprites: 0, 1 and 0x13 */
constexpr uint32_t kHudTitle = 0x0999361C;         /* -> UTF-16 text above the counters; 0 = none */
constexpr uint32_t kCounters = 0x09993620;         /* three numbers; -1 = not shown */
constexpr uint32_t kCounterBar = 0x0999362C;
constexpr uint32_t kHudWindows = 0x09993630;       /* the window set the HUD draws */
constexpr uint32_t kSlideCalendar = 0x0999367C, kCalendarPosition = 0x09993698;
constexpr uint32_t kSlideDate = 0x09993704, kSlideWeekday = 0x09993714, kSlideD = 0x09993724;
constexpr uint32_t kSlideCounters = 0x09993758, kSlideBadge = 0x09993768, kBadgePosition = 0x09993784;
constexpr uint32_t kDpFly = 0x09993734;
constexpr uint32_t kBlink = 0x099937F0;            /* {enabled, on, kind, period, limit}: a part of the HUD blinks while "on" */
constexpr uint32_t kBadgeFlag = 0x09993808;
constexpr uint32_t kRouteCalendar = 0x091FE04C, kRouteDate = 0x091FE05C, kRouteDateExtra = 0x091FE06C, kRouteWeekday = 0x091FE080, kRouteD = 0x091FE090;
constexpr uint32_t kRouteCounters = 0x091FE0A0, kRouteBadge = 0x091FE0B8;
constexpr uint32_t kRouteBadge2 = 0x091FE0A8;
constexpr int32_t kStageWithExtraOffset = 2;
constexpr float kSlideGrowth = 0.1f;

struct Blink { int32_t enabled, on, kind, period, limit; };
Blink &blink(SwapCtx &x) { return datum<Blink>(x, kBlink); }
Slide &slide(SwapCtx &x, uint32_t address) { return datum<Slide>(x, address); }
/* is the part with this kind number hidden for the moment of its blink? */
bool blinkHides(SwapCtx &x, int32_t kind) { const Blink &b = blink(x); return b.kind == kind && b.enabled != 0 && b.on != 0; }

/* the position of a sliding part: lerp(t) between the two points of its route */
void moveSlide(SwapCtx &x, uint32_t slideAddress, uint32_t routeAddress) {
    GuestStack points(x, 16);
    memcpy(x.mem(points.addr()), x.mem(guest(x, routeAddress)), sizeof(SlideRoute));
    x.c->f[12].f = slide(x, slideAddress).t;
    x.callNid(engine::nid::Lerp2D, guest(x, slideAddress) + offsetof(Slide, x), points.addr(), points.addr() + 8);
}
void growSlide(Slide &s) { s.t = s.t + (kSlideGrowth + s.t * kSlideGrowth);   /* this grouping: the game adds 0.1 to the product first */
    if (1.0f < s.t) s.t = 1.0f; }
void shrinkSlide(Slide &s) { s.t = s.t - (s.t * kSlideGrowth + kSlideGrowth); if (s.t < 0.0f) s.t = 0.0f; }
/* parts that only come in */
void updateComingSlide(SwapCtx &x, uint32_t slideAddress, uint32_t routeAddress, bool extraOffset = false) {
    Slide &s = slide(x, slideAddress);
    if (s.state == 2 || s.state != 1) return;
    moveSlide(x, slideAddress, routeAddress);
    if (extraOffset) {
        const int32_t stage = (int32_t)x.callLocal(fn::ProgressStage);
        if (stage == 3 || stage == kStageWithExtraOffset) {
            GuestStack offset(x, 16);
            memcpy(x.mem(offset.addr()), x.mem(guest(x, kRouteDateExtra)), 8);
            x.callNid(engine::nid::Vec2Add, guest(x, slideAddress) + offsetof(Slide, x), guest(x, slideAddress) + offsetof(Slide, x), offset.addr());
        }
    }
    if (1.0f <= s.t) s.state = 2;
    else growSlide(s);
}
/* parts that come in and leave */
void updateComingLeavingSlide(SwapCtx &x, uint32_t slideAddress, uint32_t routeAddress) {
    Slide &s = slide(x, slideAddress);
    if (s.state == 3) {
        moveSlide(x, slideAddress, routeAddress);
        if (s.t <= 0.0f) s.state = 0;
        else shrinkSlide(s);
    } else if (s.state != 2 && s.state == 1) {
        moveSlide(x, slideAddress, routeAddress);
        if (1.0f <= s.t) s.state = 2;
        else growSlide(s);
    }
}

/* ---- FUN_091d3600: the calendar slides in; its integer position is kept for the drawing ---- */
void swap_UpdateSlideCalendar(SwapCtx &x) {
    updateComingSlide(x, kSlideCalendar, kRouteCalendar);
    datum<int16_t>(x, kCalendarPosition) = (int16_t)truncate(slide(x, kSlideCalendar).x);
    datum<int16_t>(x, kCalendarPosition + 2) = (int16_t)truncate(slide(x, kSlideCalendar).y);
}
SwapReg reg_UpdateSlideCalendar({"libfield.UpdateSlideCalendar", MODULE, offsetOf(0x091D3600), swap_UpdateSlideCalendar});
void swap_UpdateSlideDate(SwapCtx &x) { updateComingSlide(x, kSlideDate, kRouteDate, true); }
SwapReg reg_UpdateSlideDate({"libfield.UpdateSlideDate", MODULE, offsetOf(0x091D3714), swap_UpdateSlideDate});
void swap_UpdateSlideWeekday(SwapCtx &x) { updateComingSlide(x, kSlideWeekday, kRouteWeekday); }
SwapReg reg_UpdateSlideWeekday({"libfield.UpdateSlideWeekday", MODULE, offsetOf(0x091D4C98), swap_UpdateSlideWeekday});
void swap_UpdateSlideD(SwapCtx &x) { updateComingSlide(x, kSlideD, kRouteD); }
SwapReg reg_UpdateSlideD({"libfield.UpdateSlideD", MODULE, offsetOf(0x091D4F44), swap_UpdateSlideD});
void swap_UpdateSlideCounters(SwapCtx &x) { updateComingLeavingSlide(x, kSlideCounters, kRouteCounters); }
SwapReg reg_UpdateSlideCounters({"libfield.UpdateSlideCounters", MODULE, offsetOf(0x091D564C), swap_UpdateSlideCounters});
void swap_UpdateSlideBadge(SwapCtx &x) {
    updateComingLeavingSlide(x, kSlideBadge, kRouteBadge);
    datum<int16_t>(x, kBadgePosition) = (int16_t)truncate(slide(x, kSlideBadge).x);
    datum<int16_t>(x, kBadgePosition + 2) = (int16_t)truncate(slide(x, kSlideBadge).y);
}
SwapReg reg_UpdateSlideBadge({"libfield.UpdateSlideBadge", MODULE, offsetOf(0x091D5A90), swap_UpdateSlideBadge});

/* ---- FUN_091d3344: the badge leaves ---- */
void swap_HideBadge(SwapCtx &x) { if (slide(x, kSlideBadge).state != 3) slide(x, kSlideBadge).state = 3; }
SwapReg reg_HideBadge({"libfield.HideBadge", MODULE, offsetOf(0x091D3344), swap_HideBadge});

/* ---- FUN_091d36f4: does nothing (returns the list it was given) ---- */
void swap_PassList(SwapCtx &x) { x.ret(x.arg(0)); }
SwapReg reg_PassList({"libfield.PassList", MODULE, offsetOf(0x091D36F4), swap_PassList});

/* ---- the parts start to slide in ---- */
void swap_ShowDate(SwapCtx &x) { datum<int32_t>(x, kSlideDate) = 1; }
SwapReg reg_ShowDate({"libfield.ShowDate", MODULE, offsetOf(0x091D3704), swap_ShowDate});
void swap_ShowWeekday(SwapCtx &x) { datum<int32_t>(x, kSlideWeekday) = 1; }
SwapReg reg_ShowWeekday({"libfield.ShowWeekday", MODULE, offsetOf(0x091D4C88), swap_ShowWeekday});
void swap_ShowSlideD(SwapCtx &x) { datum<int32_t>(x, kSlideD) = 1; }
SwapReg reg_ShowSlideD({"libfield.ShowSlideD", MODULE, offsetOf(0x091D4F34), swap_ShowSlideD});

/* ---- FUN_091d3214(mapOnly): sets the HUD up: its windows and sprites, and which parts slide in ---- */
void swap_StartHud(SwapCtx &x) {
    const uint32_t mapOnly = x.arg(0);
    x.callNid(gu::nid::InitWindowSet, guest(x, kHudWindows), x.callLocal(fn::HeapHandle));
    datum<uint32_t>(x, kHudSprites) = x.callLocal(fn::SystemSprite, 0);
    datum<uint32_t>(x, kHudSprites + 4) = x.callLocal(fn::SystemSprite, 1);
    datum<uint32_t>(x, kHudSprites + 8) = x.callLocal(fn::SystemSprite, 0x13);
    if (mapOnly == 1) {
        x.callLocal(fn::ShowDate);
        x.callLocal(fn::ShowWeekday);
        x.callLocal(fn::ShowSlideD);
    } else if (mapOnly == 0) {
        x.callLocal(fn::ShowCalendar);
        x.callLocal(fn::ShowDate);
        x.callLocal(fn::ShowWeekday);
        x.callLocal(fn::ShowSlideD);
        x.callLocal(fn::ShowCounters);
        x.callLocal(fn::ShowBadge);
    }
    datum<int32_t>(x, kHud) = 1;
}
SwapReg reg_StartHud({"libfield.StartHud", MODULE, offsetOf(0x091D3214), swap_StartHud});

/* ---- FUN_091d2f4c: one frame of the HUD ---- */
void swap_UpdateHud(SwapCtx &x) {
    if (datum<int32_t>(x, kHud) == 0) return;
    x.callLocal(fn::UpdateSlideCalendar);
    x.callLocal(fn::UpdateSlideDate);
    x.callLocal(fn::UpdateSlideWeekday);
    x.callLocal(fn::UpdateSlideD);
    x.callLocal(fn::UpdateDpFly);
    x.callLocal(fn::UpdateSlideCounters);
    x.callLocal(fn::UpdateSlideBadge);
    Blink &b = blink(x);
    if (b.enabled != 0) {
        const uint32_t frame = *view<uint32_t>(x, ehsys::FrameCounter(x));
        b.on = b.period != 0 && (int32_t)(frame % (uint32_t)b.period) < b.limit ? 1 : 0;   /* a zero period would divide by zero on the host */
    }
}
SwapReg reg_UpdateHud({"libfield.UpdateHud", MODULE, offsetOf(0x091D2F4C), swap_UpdateHud});

/* ---- FUN_091d5258: the DP icon waits 10 frames (state 1), then flies (state 2) to its counter, shrinking ---- */
void swap_UpdateDpFly(SwapCtx &x) {
    const uint32_t state = kDpFly;
    int32_t &flyState = datum<int32_t>(x, state);
    float &t = datum<float>(x, state + 0x18), &scale = datum<float>(x, state + 0x1C);
    int32_t &wait = datum<int32_t>(x, state + 0x20);
    if (flyState == 2) {
        GuestStack target(x, 16);
        float *to = (float *)x.mem(target.addr());
        to[0] = datum<float>(x, kSlideD + 4) + 20.0f;   /* where the "D" part of the HUD rests */
        to[1] = datum<float>(x, kSlideD + 8) + 5.0f;
        x.c->f[12].f = t;
        x.callNid(engine::nid::Lerp2D, guest(x, state + 0x0C), guest(x, state + 4), target.addr());
        scale = t * -0.75f + 1.0f;
        if (1.0f <= t) flyState = 0;
        else { t = t + (kSlideGrowth + t * kSlideGrowth); if (1.0f < t) t = 1.0f; }
    } else if (flyState == 1) {
        if (wait < 10) wait++;
        else flyState = 2;
    }
}
SwapReg reg_UpdateDpFly({"libfield.UpdateDpFly", MODULE, offsetOf(0x091D5258), swap_UpdateDpFly});

}  // namespace
