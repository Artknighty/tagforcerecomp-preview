/* libfield: the three indicators (markers that fade in next to a place, blink at random times, and fade out). */
#include "../swap.h"
#include "../ehsys.h"
#include "field_data.h"
#include "field_functions.h"
#include <stddef.h>
#include <string.h>

#define MODULE "libfield"

namespace {
using namespace field;

constexpr uint32_t kSelectedIndicator = 0x092061B8;   /* which indicator is drawn last (on top) */
constexpr int32_t kFadeStep = 0x11, kFullAlpha = 0xFF;
constexpr uint32_t kBlinkWaitMin = 0x5A, kBlinkWaitRange = 0xD2;   /* frames between blinks: 90 .. 299 */
constexpr uint32_t kBlinkExtraFrames = 0xE;

bool isLive(SwapCtx &x, const Indicator &indicator) {
    if (indicator.active == 0 || indicator.resource == 0) return false;
    const uint32_t *resource = view<uint32_t>(x, indicator.resource);
    return *resource != 0;
}

/* ---- FUN_091c4ad8: clears the indicators ---- */
void swap_ResetIndicators(SwapCtx &x) {
    memset(x.mem(guest(x, kIndicatorHeader)), 0, 0x340);
    x.callLocal(fn::ClearIndicators);
}
SwapReg reg_ResetIndicators({"libfield.ResetIndicators", MODULE, offsetOf(0x091C4AD8), swap_ResetIndicators});

/* ---- FUN_091c5564 / FUN_091c55ac: frees the resource block of each indicator and clears its 0x100 bytes ---- */
void clearIndicator(SwapCtx &x, uint32_t address) {
    Indicator &indicator = *view<Indicator>(x, address);
    if (indicator.block != 0) {
        x.callLocal(fn::HeapFree, indicator.block);
        indicator.block = 0;
    }
    memset(&indicator, 0, sizeof(Indicator));
}
void swap_ClearIndicators(SwapCtx &x) {
    for (int32_t i = 0; i < kIndicatorCount; i++) x.callLocal(fn::ClearIndicator, guest(x, kIndicatorObjects) + i * sizeof(Indicator));
}
SwapReg reg_ClearIndicators({"libfield.ClearIndicators", MODULE, offsetOf(0x091C5564), swap_ClearIndicators});
void swap_ClearIndicator(SwapCtx &x) { clearIndicator(x, x.arg(0)); }
SwapReg reg_ClearIndicator({"libfield.ClearIndicator", MODULE, offsetOf(0x091C55AC), swap_ClearIndicator});

/* ---- FUN_091c4b34: one frame of the indicators: fades, the highlighted one, and the random blink of the sprites ---- */
void swap_UpdateIndicators(SwapCtx &x) {
    FieldState &state = fieldState(x);
    IndicatorHeader &header = indicatorHeader(x);
    for (int32_t i = 0; i < kIndicatorCount; i++) {
        Indicator &indicator = indicators(x)[i];
        if (!isLive(x, indicator)) continue;
        if (indicator.state == kIndicatorFadeOut) {
            if (indicator.alpha < 1) {
                indicator.state = kIndicatorIdle;
                header.fadeOutRequest[i] = 0;
                clearIndicator(x, guest(x, kIndicatorObjects) + i * sizeof(Indicator));
            } else {
                indicator.alpha -= kFadeStep;
                indicator.alpha2 -= kFadeStep;
            }
        } else if (indicator.state == kIndicatorShown) {
            if (state.selected < 0) {
                x.callLocal(fn::SelectIndicator, 3, 0);
            } else {
                for (int32_t k = 0; k < kIndicatorCount; k++) x.callLocal(fn::SelectIndicator, (uint32_t)k, k == state.selected ? 1 : 0);
            }
        } else if (indicator.state == kIndicatorFadeIn) {
            if (header.fadeEnabled != 0) {
                if (indicator.alpha < kFullAlpha) {
                    indicator.alpha += kFadeStep;
                    indicator.alpha2 += kFadeStep;
                } else {
                    indicator.state = kIndicatorShown;
                    header.fadeInRequest[i] = 0;
                }
            }
        }
        if (indicator.blinking == 0) {
            if (indicator.blinkWait == 0) {
                indicator.blinking = 1;
                indicator.frame++;
            } else {
                indicator.blinkWait--;
            }
        } else {
            const uint32_t record = x.callLocal(fn::IndicatorRecord, (uint32_t)indicator.kind, (uint32_t)indicator.variant);
            const uint32_t frames = x.callNid(ehsys::nid::GetSpriteFrameCount, indicator.sprite);
            if (frames < view<uint32_t>(x, record)[1] + kBlinkExtraFrames) {
                indicator.frame++;
            } else {
                indicator.blinking = 0;
                indicator.blinkWait = (int32_t)((x.callNid(ehsys::nid::NextRandom) & 0xFFFF) % kBlinkWaitRange + kBlinkWaitMin);
                indicator.frame = 0;
            }
        }
        if (indicator.animate2 != 0) indicator.frame2++;
    }
    if (header.fadeInRequest[0] == 0 && header.fadeInRequest[1] == 0 && header.fadeInRequest[2] == 0) header.anyFadeInRequest = 0;
    if (header.fadeOutRequest[0] == 0 && header.fadeOutRequest[1] == 0 && header.fadeOutRequest[2] == 0) header.anyFadeOutRequest = 0;
}
SwapReg reg_UpdateIndicators({"libfield.UpdateIndicators", MODULE, offsetOf(0x091C4B34), swap_UpdateIndicators});

/* ---- FUN_091c4dc0: draws the indicators; the selected one last so that it lies on top ---- */
uint32_t drawIndicator(SwapCtx &x, uint32_t list, uint32_t objectAddress) {
    Indicator &indicator = *view<Indicator>(x, objectAddress);
    if (indicator.sprite2 != 0) {
        list = ehsys::BeginSpriteDraw(x, list);
        list = ehsys::DrawSpriteResource(x, list, indicator.sprite2, objectAddress + offsetof(Indicator, frame2));
        list = ehsys::EndSpriteDraw(x, list);
    }
    list = ehsys::BeginSpriteDraw(x, list);
    list = ehsys::DrawSpriteResource(x, list, indicator.sprite, objectAddress + offsetof(Indicator, frame));
    return ehsys::EndSpriteDraw(x, list);
}
void swap_DrawIndicators(SwapCtx &x) {
    const int32_t selected = datum<int32_t>(x, kSelectedIndicator);
    uint32_t list = ehsys::BeginList(x, 3, 0);
    for (int32_t i = 0; i < kIndicatorCount; i++) {
        if (!isLive(x, indicators(x)[i]) || i == selected) continue;
        list = drawIndicator(x, list, guest(x, kIndicatorObjects) + i * sizeof(Indicator));
    }
    const uint32_t chosen = guest(x, kIndicatorObjects) + (uint32_t)selected * sizeof(Indicator);   /* not range checked, like the original */
    if (isLive(x, *view<Indicator>(x, chosen))) list = drawIndicator(x, list, chosen);
    ehsys::EndList(x, list);
}
SwapReg reg_DrawIndicators({"libfield.DrawIndicators", MODULE, offsetOf(0x091C4DC0), swap_DrawIndicators});

}  // namespace
