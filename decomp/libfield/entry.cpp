/* libfield: the module's entry points. The story starts the field (`Start`), then every frame runs `FrameUpdate` and `FrameDraw`, and drives the field
 * through numbered handlers: `Invoke` starts one with an argument block, `HandlerDone` asks whether it has finished. */
#include "../swap.h"
#include "../ehsys.h"
#include "field_data.h"
#include "field_engine.h"
#include "field_functions.h"
#include <string.h>

#define MODULE "libfield"

namespace {
using namespace field;
namespace gu = ehsys;

constexpr int32_t kStageWithShortDpBar = 2;                 /* from the progress stage 2 on the DP bar is 16 px shorter */
constexpr uint32_t kDpBarX = 500, kDpBarWidth = 0x16C, kDpBarLength = 0x39, kDpBarShortLength = 0x29;
constexpr uint32_t kDialogX = 0x27, kDialogWidth = 400, kDialogLines = 14;
constexpr uint32_t kFadeList = 6, kMessageList = 4, kHudList = 3;
constexpr uint32_t kFadeColours = 0x091F9F1C;               /* the colours the story can fade to, by number */

/* ---- libfield_E92DC2F2 (the story's call that starts the field) ---- */
void swap_Start(SwapCtx &x) {
    memset(&fieldFlags(x), 0, sizeof(FieldFlags));
    uint32_t list = gu::BeginList(x, 0, 0);
    list = x.callNid(engine::nid::ClearScreenList, list);
    gu::EndList(x, list);
    x.callNid(engine::nid::SetAllocator, guest(x, 0x091CC4F0), guest(x, 0x091CC500));   /* the engine allocates through the field's heap */
    for (int32_t i = 0; i < kHandlerCount; i++) handlers(x)[i].active = 0;
    x.callLocal(fn::ResetFieldState);
    x.callLocal(fn::StartLoader);
    x.callNid(engine::nid::SetEngineHeap, x.callLocal(fn::HeapHandle));
    x.callLocal(fn::ResetAll);
    uint32_t barEnd = kDpBarLength, barStart = kDpBarLength;
    const int32_t stage = (int32_t)x.callLocal(fn::ProgressStage);
    if (stage == 3 || stage == kStageWithShortDpBar) { barEnd = kDpBarShortLength; barStart = kDpBarShortLength; }
    x.callLocal(fn::InitDp, kDpBarX, barStart, kDpBarWidth, barEnd);
    x.callLocal(fn::InitMessage, x.callLocal(fn::HeapHandle), kDialogX, kDialogWidth, kDialogLines, 1, 3);
}
SwapReg reg_Start({"libfield.Start", MODULE, offsetOf(0x091D01B4), swap_Start});

/* ---- FUN_091d0b64: clears every part of the field ---- */
void swap_ResetAll(SwapCtx &x) {
    x.callLocal(fn::ResetCameraShake);
    x.callLocal(fn::ResetEffects);
    x.callLocal(fn::ResetWorldMap);
    x.callLocal(fn::ResetMapScene);
    x.callLocal(fn::ResetBeacons);
    x.callLocal(fn::ResetShadowOffset);
    x.callLocal(fn::ResetWalkers);
    x.callLocal(fn::ResetScreens);
    x.callLocal(fn::ResetIndicators);
    x.callLocal(fn::ResetDialog);
    x.callLocal(fn::ResetBanner);
    x.callLocal(fn::ResetScreenFade);
}
SwapReg reg_ResetAll({"libfield.ResetAll", MODULE, offsetOf(0x091D0B64), swap_ResetAll});

/* ---- campo_atualizar_quadro: one frame of the field ---- */
void swap_FrameUpdate(SwapCtx &x) {
    if (fieldFlags(x).started == 0) return;
    x.callLocal(fn::UpdateDp);
    x.callLocal(fn::PumpLoader);
    x.callLocal(fn::UpdateMessage);
    x.callLocal(fn::UpdateShake);
    x.callLocal(fn::UpdateWorldMap);
    x.callLocal(fn::UpdateMapScene);
    if (fieldFlags(x).mapOnly == 0) x.callLocal(fn::PlayAmbient);
    x.callLocal(fn::UpdateBeacons);
    x.callLocal(fn::UpdateWalkers);
    x.callLocal(fn::UpdateFlow);
    x.callLocal(fn::UpdateIndicators);
    x.callLocal(fn::UpdateDialog);
    x.callLocal(fn::UpdateBanner);
    x.callLocal(fn::UpdateScreenFade);
    x.callLocal(fn::CheckHandlersDone);
    fieldState(x).frame++;
}
SwapReg reg_FrameUpdate({"libfield.FrameUpdate", MODULE, offsetOf(0x091D0304), swap_FrameUpdate});

/* ---- libfield_74B014A2: draws the field: a pending fade first, then everything in its order of layers ---- */
void swap_FrameDraw(SwapCtx &x) {
    FieldFlags &flags = fieldFlags(x);
    if (flags.started == 0) return;
    if (flags.fadePending != 0) {
        GuestStack cursor(x, 16);
        uint32_t &list = *(uint32_t *)x.mem(cursor.addr());
        list = gu::BeginList(x, kFadeList, 0);
        list = gu::DisableState(x, list, gu::kDepthTest);
        if (gu::DrawFade(x, cursor.addr()) != 0) {
            if (flags.fadeKind == 0) flags.drawing = 0;
            flags.fadePending = 0;
        }
        gu::EndList(x, list);
    }
    if (flags.drawing == 0) return;
    {
        GuestStack cursor(x, 16);
        *(uint32_t *)x.mem(cursor.addr()) = gu::BeginList(x, kMessageList, 0);
        x.callLocal(fn::DrawMessage, cursor.addr());
        gu::EndList(x, *(uint32_t *)x.mem(cursor.addr()));
    }
    x.callLocal(fn::DrawShakeScene);
    x.callLocal(fn::DrawWorldMap);
    x.callLocal(fn::DrawMapScene);
    x.callLocal(fn::DrawBeacons);
    gu::EndList(x, x.callLocal(fn::DrawDp, gu::BeginList(x, kHudList, 0)));
    x.callLocal(fn::DrawFlow);
    x.callLocal(fn::DrawScreenFade);
    x.callLocal(fn::DrawIndicators);
    x.callLocal(fn::DrawWalkers);
    x.callLocal(fn::DrawEffects);
    x.callLocal(fn::DrawDialog);
    x.callLocal(fn::DrawBanner);
}
SwapReg reg_FrameDraw({"libfield.FrameDraw", MODULE, offsetOf(0x091D03CC), swap_FrameDraw});

/* ---- libfield_86220A06(handler, argument): starts a handler; when it is still active, only once it has finished. 1 when started ---- */
void swap_Invoke(SwapCtx &x) {
    const int32_t handler = (int32_t)x.arg(0);
    const uint32_t argument = x.arg(1);
    fieldFlags(x).started = 1;
    HandlerEntry &entry = handlers(x)[handler];
    if (entry.active != 0 && x.callLocal(fn::HandlerDone, (uint32_t)handler, 0) == 0) { x.ret(0); return; }
    x.callAddr(handlers(x)[handler].start, argument);
    handlers(x)[handler].active = 1;
    x.ret(1);
}
SwapReg reg_Invoke({"libfield.Invoke", MODULE, offsetOf(0x091D0608), swap_Invoke});

/* ---- libfield_79E5F8B7(handler, result): asks the handler whether it is done; when it is, it is no longer active ---- */
void swap_HandlerDone(SwapCtx &x) {
    const int32_t handler = (int32_t)x.arg(0);
    const uint32_t done = x.callAddr(handlers(x)[handler].isDone, x.arg(1));
    if (done != 0) handlers(x)[handler].active = 0;
    x.ret(done != 0 ? 1 : 0);
}
SwapReg reg_HandlerDone({"libfield.HandlerDone", MODULE, offsetOf(0x091D0688), swap_HandlerDone});

/* ---- FUN_091d0d00: clears the "pending" flags of what has finished ---- */
void swap_CheckHandlersDone(SwapCtx &x) {
    FieldFlags &flags = fieldFlags(x);
    if (x.callLocal(fn::BannerDone) != 0) flags.bannerPending = 0;
    if (x.callLocal(fn::WorldMapDone) != 0) flags.worldMapPending = 0;
    if (x.callLocal(fn::DpDone) != 0) flags.dpPending = 0;
}
SwapReg reg_CheckHandlersDone({"libfield.CheckHandlersDone", MODULE, offsetOf(0x091D0D00), swap_CheckHandlersDone});

/* ---- the handlers the story invokes by number (those the field runs on the map) ---- */
/* FUN_091d0734(arguments): opens the place menu */
void swap_HandlerWorldMap(SwapCtx &x) {
    fieldFlags(x).worldMapPending = 1;
    x.callLocal(fn::StartWorldMap, x.arg(0));
    fieldFlags(x).mapOnly = 1;
}
SwapReg reg_HandlerWorldMap({"libfield.HandlerWorldMap", MODULE, offsetOf(0x091D0734), swap_HandlerWorldMap});

/* FUN_091d0778(arguments): {title shown, place number}: shows the clock, the calendar and (on the full map) the town menu */
void swap_HandlerMapHud(SwapCtx &x) {
    int32_t *arguments = view<int32_t>(x, x.arg(0));
    if (arguments[1] > 11) arguments[1] = 1;
    fieldFlags(x).bannerPending = 1;
    if (arguments[0] != 0) x.callLocal(fn::StartBanner, (uint32_t)arguments[1]);
    x.callLocal(fn::StartHud, fieldFlags(x).mapOnly == 1 ? 1 : 0);
    x.callLocal(fn::StartDpAnimation);
    fieldState(x).flag0C = 1;
    fieldState(x).flag10 = 1;
    if (fieldFlags(x).mapOnly == 0) x.callLocal(fn::StartTownMenu);
}
SwapReg reg_HandlerMapHud({"libfield.HandlerMapHud", MODULE, offsetOf(0x091D0778), swap_HandlerMapHud});

/* FUN_091d0934(arguments): {fade out (0) or in (1), frames, colour number}: starts a fade of the screen */
void swap_HandlerFade(SwapCtx &x) {
    FieldFlags &flags = fieldFlags(x);
    const uint32_t *arguments = view<uint32_t>(x, x.arg(0));
    flags.fadePending = 1;
    const uint32_t colour = datum<uint32_t>(x, kFadeColours + arguments[2] * 4);
    const uint32_t frames = *(const uint16_t *)&arguments[1];
    if (arguments[0] == 0) gu::FadeTo(x, 1, colour, frames);
    else gu::FadeFrom(x, 1, colour, frames);
    flags.fadeKind = (int32_t)arguments[0];
    if (flags.fadeKind != 0) flags.drawing = 1;
}
SwapReg reg_HandlerFade({"libfield.HandlerFade", MODULE, offsetOf(0x091D0934), swap_HandlerFade});

}  // namespace
