/* libfield: the DP counter: the player's Duel Points shown on the map. When the DP change, the counter slides in, a number flies up from it (the change,
 * red when negative) and the shown value counts up or down to the new one, its last digits flickering. */
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

enum DpState : int32_t { kDpAway = 0, kDpSliding = 1, kDpShown = 2, kDpWaiting = 3, kDpFloating = 4, kDpCounting = 5 };
struct DpCounter {
    int32_t wait;            /* +0x00 frames waited before the number floats */
    int32_t state;           /* +0x04 DpState */
    int32_t shown;           /* +0x08 the value on screen */
    int32_t target;          /* +0x0C the value it counts to */
    int32_t delta;           /* +0x10 the change of the DP that is being shown */
    int32_t step;            /* +0x14 how much `shown` moves each frame */
    float from[2];           /* +0x18 where the counter slides from */
    float to[2];             /* +0x20 where it rests */
    float floatFrom[2];      /* +0x28 where the floating number starts */
    int32_t blinking;        /* +0x30 */
    int32_t blinkPeriod;     /* +0x34 */
    int32_t blinkLimit;      /* +0x38 */
    uint32_t texture;        /* +0x3C */
    float position[2];       /* +0x40 */
    float t;                 /* +0x48 */
    float floatPosition[2];  /* +0x4C */
    float floatT;            /* +0x54 */
    float floatScale;        /* +0x58 */
};
static_assert(sizeof(DpCounter) == 0x5C, "DpCounter layout");
constexpr uint32_t kDpCounter = 0x09993AE0, kDpFormat = 0x091FE0D8;   /* "%d" */
constexpr float kGrowth = 0.1f;
DpCounter &dp(SwapCtx &x) { return datum<DpCounter>(x, kDpCounter); }
uint32_t dpAddress(SwapCtx &x, size_t member) { return guest(x, kDpCounter) + (uint32_t)member; }
constexpr int32_t kWaitFrames = 10;
constexpr uint32_t kCountSound = 0x621E;
constexpr uint32_t kColourBlueRed = 0xFF0000FF, kColourCyan = 0xFF00FFFF, kWhite = 0xFFFFFFFF;
int16_t s16(SwapCtx &x, uint32_t address) { return datum<int16_t>(x, address); }
int32_t s32(SwapCtx &x, uint32_t address) { return datum<int32_t>(x, address); }

/* ---- FUN_091d88f8(x, y, endX, endY): sets the counter up; the shown value starts at the player's DP ---- */
void swap_InitDp(SwapCtx &x) {
    const int32_t a = (int32_t)x.arg(0), b = (int32_t)x.arg(1), c = (int32_t)x.arg(2), d = (int32_t)x.arg(3);
    memset(&dp(x), 0, sizeof(DpCounter));
    dp(x).from[0] = (float)a; dp(x).from[1] = (float)b; dp(x).to[0] = (float)c; dp(x).to[1] = (float)d;
    dp(x).shown = (int32_t)x.callNid(engine::nid::PlayerDp, 0);
    dp(x).target = (int32_t)x.callNid(engine::nid::PlayerDp, 0);
}
SwapReg reg_InitDp({"libfield.InitDp", MODULE, offsetOf(0x091D88F8), swap_InitDp});

void swap_SetDpTexture(SwapCtx &x) { dp(x).texture = x.arg(0); }
SwapReg reg_SetDpTexture({"libfield.SetDpTexture", MODULE, offsetOf(0x091D8AD4), swap_SetDpTexture});
void swap_DpDone(SwapCtx &x) { x.ret(dp(x).state == kDpShown ? 1 : 0); }
SwapReg reg_DpDone({"libfield.DpDone", MODULE, offsetOf(0x091D8BF4), swap_DpDone});
void swap_StartDpAnimation(SwapCtx &x) { dp(x).state = kDpSliding; }
SwapReg reg_StartDpAnimation({"libfield.StartDpAnimation", MODULE, offsetOf(0x091D8C08), swap_StartDpAnimation});

/* ---- FUN_091d89d4 ---- */
void swap_UpdateDp(SwapCtx &x) {
    x.callLocal(fn::UpdateDpCounter);
    x.callLocal(fn::UpdateDpFloat);
}
SwapReg reg_UpdateDp({"libfield.UpdateDp", MODULE, offsetOf(0x091D89D4), swap_UpdateDp});

/* ---- campo_dp_animacao_atualizar (FUN_091d8c50): the counter slides in, then the shown value moves to the target ---- */
void swap_UpdateDpCounter(SwapCtx &x) {
    DpCounter &c = dp(x);
    if (c.state == kDpCounting) {
        if (c.shown == c.target) {
            c.state = kDpShown;
        } else {
            if (*view<uint32_t>(x, gu::FrameCounter(x)) % 3 == 0) gu::PlaySound(x, kCountSound);
            c.shown = c.shown + c.step;
            if (c.step < 1 || c.shown <= c.target) {
                if (c.step < 0 && c.shown < c.target) { c.shown = c.target; c.state = kDpShown; }
            } else {
                c.shown = c.target;
                c.state = kDpShown;
            }
        }
    } else if (c.state == kDpSliding) {
        x.c->f[12].f = c.t;
        x.callNid(engine::nid::Lerp2D, dpAddress(x, offsetof(DpCounter, position)), dpAddress(x, offsetof(DpCounter, from)), dpAddress(x, offsetof(DpCounter, to)));
        if (1.0f <= c.t) c.state = kDpShown;
        else { c.t = c.t + (kGrowth + c.t * kGrowth); if (1.0f < c.t) c.t = 1.0f; }
    }
}
SwapReg reg_UpdateDpCounter({"libfield.UpdateDpCounter", MODULE, offsetOf(0x091D8C50), swap_UpdateDpCounter});

/* ---- FUN_091d903c: the change flies from where it appeared to the counter, shrinking ---- */
void swap_UpdateDpFloat(SwapCtx &x) {
    DpCounter &c = dp(x);
    if (c.state == kDpFloating) {
        x.c->f[12].f = c.floatT;
        x.callNid(engine::nid::Lerp2D, dpAddress(x, offsetof(DpCounter, floatPosition)), dpAddress(x, offsetof(DpCounter, floatFrom)), dpAddress(x, offsetof(DpCounter, to)));
        c.floatScale = c.floatT * -0.75f + 1.0f;
        if (1.0f <= c.floatT) c.state = kDpCounting;
        else { c.floatT = c.floatT + (kGrowth + c.floatT * kGrowth); if (1.0f < c.floatT) c.floatT = 1.0f; }
    } else if (c.state == kDpWaiting) {
        if (c.wait < kWaitFrames) {
            c.wait++;
            c.floatPosition[0] = c.floatFrom[0];
            c.floatPosition[1] = c.floatFrom[1];
            c.floatScale = 1.0f;
        } else {
            c.state = kDpFloating;
        }
    }
}
SwapReg reg_UpdateDpFloat({"libfield.UpdateDpFloat", MODULE, offsetOf(0x091D903C), swap_UpdateDpFloat});

/* ---- FUN_091d953c(n): how many decimal digits the number has ---- */
void swap_DigitCount(SwapCtx &x) {
    int32_t n = (int32_t)x.arg(0);
    uint32_t digits = 0;
    while (n > 0) { digits++; n = truncate((float)n * kGrowth); }
    x.ret(digits);
}
SwapReg reg_DigitCount({"libfield.DigitCount", MODULE, offsetOf(0x091D953C), swap_DigitCount});

/* ---- FUN_091d92d4(scale, list, x, y, z, u, v, w, h): one sprite, scaled, with its texture wrapped ---- */
void swap_DrawBigDigit(SwapCtx &x) {
    const float scale = x.c->f[12].f;
    const uint32_t list = x.arg(0), px = x.arg(1), py = x.arg(2), z = x.arg(3), u = x.arg(4), v = x.arg(5), w = x.arg(6), h = x.arg(7);
    uint32_t drawn = gu::SetTextureWrap(x, list, 1, 1);
    drawn = gu::SetSpriteSize(x, drawn, (int32_t)w, (int32_t)h, truncate((float)(int32_t)w * scale), truncate((float)(int32_t)h * scale));
    x.ret(gu::DrawSprite(x, drawn, (int32_t)px, (int32_t)py, (int32_t)z, (int32_t)u, (int32_t)v, 0, 0));
}
SwapReg reg_DrawBigDigit({"libfield.DrawBigDigit", MODULE, offsetOf(0x091D92D4), swap_DrawBigDigit});

/* ---- FUN_091d93b0(scale, list, text, x, y, z, spacing, kind): the digits of a text, right to left; kind 0 = the digits of one strip, kind 1 = the table of the floating digits ---- */
void swap_DrawDigits(SwapCtx &x) {
    const float scale = x.c->f[12].f;
    uint32_t list = x.arg(0);
    const uint32_t text = x.arg(1), z = x.arg(4);
    const int32_t px = (int32_t)x.arg(2), py = (int32_t)x.arg(3), spacing = (int32_t)x.arg(5), kind = (int32_t)x.arg(6);
    const int32_t last = (int32_t)x.callImport("strlen", text) - 1;
    if (last >= 0) {
        const int32_t stripW = s16(x, 0x091FA234), stripH = s16(x, 0x091FA236), stripU = s16(x, 0x091FA230), stripV = s16(x, 0x091FA232);
        int32_t u = 0, v = 0, w = 0, h = 0;
        for (int32_t i = last; i >= 0; i--) {
            int32_t digit = (int32_t)(int8_t)*x.mem(text + i) - 0x30;
            if (kind == 1) {
                digit *= 8;
                u = s32(x, 0x091FA250 + digit); v = s32(x, 0x091FA254 + digit); w = 0x28; h = 0x30;
            } else if (kind == 0) {
                u = stripU + stripW * digit; w = stripW; v = stripV; h = stripH;
            }
            x.c->f[12].f = scale;
            list = x.callLocal(fn::DrawBigDigit, list, (uint32_t)(px - truncate(scale * (float)(w + spacing)) * (last - i)), (uint32_t)py, z, (uint32_t)u, (uint32_t)v, (uint32_t)w, (uint32_t)h);
        }
    }
    x.ret(list);
}
SwapReg reg_DrawDigits({"libfield.DrawDigits", MODULE, offsetOf(0x091D93B0), swap_DrawDigits});

/* ---- campo_dp_animacao_desenhar (FUN_091d8da8): the counter's frame and its number (the last digits flicker while it counts) ---- */
void swap_DrawDpCounter(SwapCtx &x) {
    DpCounter &c = dp(x);
    uint32_t list = x.arg(0);
    if (c.state == kDpAway) { x.ret(list); return; }
    if (c.blinking != 0 && c.blinkPeriod != 0 && !((int32_t)(*view<uint32_t>(x, gu::FrameCounter(x)) % (uint32_t)c.blinkPeriod) >= c.blinkLimit)) { x.ret(list); return; }   /* nothing in the module sets `blinking`; the period guard only avoids a host division by zero */
    list = gu::SetColor(x, list, kWhite);
    list = gu::BindTexture(x, list, c.texture);
    list = gu::SetColor(x, list, kColourCyan);
    list = gu::SetSpriteSize(x, list, s16(x, 0x091FA22C), s16(x, 0x091FA22E), s16(x, 0x091FA22C), s16(x, 0x091FA22E));
    list = gu::DrawSprite(x, list, truncate(c.position[0] + (float)s32(x, 0x091FA240)), truncate(c.position[1] + (float)s32(x, 0x091FA244)), 0, s16(x, 0x091FA228), s16(x, 0x091FA22A), 0, 0);
    list = gu::SetColor(x, list, kWhite);
    GuestStack text(x, 256);
    const uint32_t textAddress = text.addr();
    int32_t value;
    if (c.state == kDpFloating || c.state == kDpWaiting) {
        value = c.shown;
    } else if (c.state == kDpCounting) {
        x.c->f[12].f = (float)c.delta * kGrowth;
        x.callNid(engine::nid::Abs);
        const uint32_t digits = x.callLocal(fn::DigitCount, (uint32_t)truncate(engine::resultFloat(x)));
        x.c->f[12].f = 10.0f; x.c->f[13].f = (float)(int32_t)digits;
        x.callNid(engine::nid::Power);
        x.c->f[12].f = engine::resultFloat(x);
        x.callNid(engine::nid::Round);
        const int32_t modulus = truncate(engine::resultFloat(x));
        const int32_t base = c.shown - c.shown % modulus;
        const uint32_t random = x.callNid(ehsys::nid::NextRandom) & 0xFFFF;
        value = base + (int32_t)(random % (uint32_t)modulus);
    } else {
        value = (int32_t)x.callNid(engine::nid::PlayerDp, 0);
    }
    x.callImport("sprintf", textAddress, guest(x, kDpFormat), (uint32_t)value);
    x.c->f[12].f = 1.0f;
    x.ret(x.callLocal(fn::DrawDigits, list, textAddress, (uint32_t)truncate(c.position[0] + (float)s32(x, 0x091FA248)), (uint32_t)truncate(c.position[1] + (float)s32(x, 0x091FA24C)), 0, 0xFFFFFFFEu, 0));
}
SwapReg reg_DrawDpCounter({"libfield.DrawDpCounter", MODULE, offsetOf(0x091D8DA8), swap_DrawDpCounter});

/* ---- FUN_091d913c: the number that floats up from the counter: the change, with a minus sign when it is negative ---- */
void swap_DrawDpFloat(SwapCtx &x) {
    DpCounter &c = dp(x);
    uint32_t list = x.arg(0);
    if (c.state != kDpFloating && c.state != kDpWaiting) { x.ret(list); return; }
    list = gu::SetColor(x, list, kColourBlueRed);
    list = gu::BindTexture(x, list, c.texture);
    GuestStack text(x, 256);
    const uint32_t textAddress = text.addr();
    if (c.delta < 0) {
        x.callImport("sprintf", textAddress, guest(x, kDpFormat), (uint32_t)(0 - c.delta));
        const uint32_t length = x.callImport("strlen", textAddress);
        x.c->f[12].f = c.floatScale;
        const float minusX = (c.floatPosition[0] + (float)s32(x, 0x091FA248)) - (38.0f * c.floatScale) * (float)(int32_t)length;
        list = x.callLocal(fn::DrawBigDigit, list, (uint32_t)truncate(minusX), (uint32_t)truncate(c.floatPosition[1] + (float)s32(x, 0x091FA24C)), 0,
                           (uint32_t)s16(x, 0x091FA238), (uint32_t)s16(x, 0x091FA23A), (uint32_t)s16(x, 0x091FA23C), (uint32_t)s16(x, 0x091FA23E));
    } else {
        x.callImport("sprintf", textAddress, guest(x, kDpFormat), (uint32_t)c.delta);
    }
    x.c->f[12].f = c.floatScale;
    x.ret(x.callLocal(fn::DrawDigits, list, textAddress, (uint32_t)truncate(c.floatPosition[0] + (float)s32(x, 0x091FA248)), (uint32_t)truncate(c.floatPosition[1] + (float)s32(x, 0x091FA24C)), 0, 0xFFFFFFFEu, 1));
}
SwapReg reg_DrawDpFloat({"libfield.DrawDpFloat", MODULE, offsetOf(0x091D913C), swap_DrawDpFloat});

/* ---- FUN_091d89f8(list): the drawing state for the DP counter, then the counter and the floating number ---- */
void swap_DrawDp(SwapCtx &x) {
    uint32_t list = x.arg(0);
    if (dp(x).texture == 0) { x.ret(list); return; }
    list = gu::SetBlend(x, list, 0, 2, 3, 0, 0);
    list = gu::EnableState(x, list, gu::kBlend);
    list = gu::AlphaFunc(x, list, 6, 0, 0xFF);
    list = gu::EnableState(x, list, gu::kAlphaTest);
    list = gu::DisableState(x, list, gu::kDepthTest);
    list = gu::EnableState(x, list, gu::kTexture2D);
    list = gu::SetTextureMode(x, list, 3, 0, 0, 0);
    list = gu::SetTextureFunction(x, list, 0, 1);
    list = gu::SetTextureWrap(x, list, 0, 0);
    list = gu::SetTextureFilter(x, list, 0, 0);
    list = x.callLocal(fn::DrawDpCounter, list);
    x.ret(x.callLocal(fn::DrawDpFloat, list));
}
SwapReg reg_DrawDp({"libfield.DrawDp", MODULE, offsetOf(0x091D89F8), swap_DrawDp});

}  // namespace
