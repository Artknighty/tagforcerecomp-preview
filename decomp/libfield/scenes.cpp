/* libfield: the smaller layers over the map: the question dialog, the info panel, the title banner, the screen fade and the ambient sound. */
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

int32_t s32(SwapCtx &x, uint32_t address) { return datum<int32_t>(x, address); }

/* ---- the question dialog (a window block with the answer to the story's question) ---- */
constexpr uint32_t kDialogOpen = 0x0999398C, kDialogBusy = 0x09993990, kDialogState = 0x09993994;
constexpr uint32_t kDialogCloseWhenDone = 0x09993998, kDialogClosing = 0x0999399C, kDialogAnswer = 0x099939A0;
constexpr uint32_t kDialogWindow = 0x099939A8, kDialogResult = 0x09993ADC;
constexpr uint32_t kDialogStateAsk = 1, kDialogStateWait = 2;

/* ---- FUN_091d80ac: one frame of the dialog: state 1 shows the question's text, state 2 waits for the window's answer ---- */
void swap_UpdateDialog(SwapCtx &x) {
    FieldState &field = fieldState(x);
    const int32_t state = s32(x, kDialogState);
    if (state == (int32_t)kDialogStateWait) {
        const int32_t answer = (int32_t)x.callNid(gu::nid::UpdateWindow, guest(x, kDialogWindow));
        if (answer != -1) {
            x.callNid(gu::nid::ClearWindow, guest(x, kDialogWindow));
            datum<int32_t>(x, kDialogBusy) = 0;
            datum<int32_t>(x, kDialogState) = 0;
            datum<int32_t>(x, kDialogResult) = answer;
        }
        return;
    }
    if (state != (int32_t)kDialogStateAsk) return;
    if (s32(x, kDialogClosing) == 0 && x.callLocal(fn::MessageDone) != 0) {
        if (s32(x, kDialogCloseWhenDone) == 0) {
            datum<int32_t>(x, kDialogOpen) = 0;
            datum<int32_t>(x, kDialogState) = 0;
        } else {
            x.callLocal(fn::CloseMessage);
            datum<int32_t>(x, kDialogClosing) = 1;
        }
        return;
    }
    if (s32(x, kDialogClosing) != 0 && x.callLocal(fn::MessageBusyB) == 0) {
        datum<int32_t>(x, kDialogOpen) = 0;
        datum<int32_t>(x, kDialogState) = 0;
        return;
    }
    field.selected = x.callLocal(fn::MessageBusyA) == 0 ? -1 : s32(x, kDialogAnswer);
    if (x.callLocal(fn::MessageBusyB) == 0) x.callLocal(fn::UpdateDialogText);
}
SwapReg reg_UpdateDialog({"libfield.UpdateDialog", MODULE, offsetOf(0x091D80AC), swap_UpdateDialog});

/* ---- FUN_091d81f8: draws the dialog on list 3 ---- */
void swap_DrawDialog(SwapCtx &x) {
    uint32_t list = gu::BeginList(x, 3, 0);
    const int32_t state = s32(x, kDialogState);
    if (state == (int32_t)kDialogStateWait) list = x.callLocal(fn::DialogWindowDraw, list);
    else if (state == (int32_t)kDialogStateAsk) list = x.callLocal(fn::DialogTextDraw, list);
    gu::EndList(x, list);
}
SwapReg reg_DrawDialog({"libfield.DrawDialog", MODULE, offsetOf(0x091D81F8), swap_DrawDialog});

/* ---- the info panel (two kinds, picked by the state) ---- */
constexpr uint32_t kPanelState = 0x099970DC, kPanelTexture = 0x099970E0, kPanelSprites = 0x09997140, kPanelDone = 0x09996FD4;

void swap_UpdatePanel(SwapCtx &x) {
    const int32_t state = s32(x, kPanelState);
    if (state == 2) x.callLocal(fn::PanelUpdateWindow);
    else if (state == 1) x.callLocal(fn::PanelUpdateText);
}
SwapReg reg_UpdatePanel({"libfield.UpdatePanel", MODULE, offsetOf(0x091E7150), swap_UpdatePanel});

void swap_DrawPanel(SwapCtx &x) {
    const int32_t state = s32(x, kPanelState);
    if (state == 2) x.callLocal(fn::PanelDrawWindow);
    else if (state == 1) x.callLocal(fn::PanelDrawText);
}
SwapReg reg_DrawPanel({"libfield.DrawPanel", MODULE, offsetOf(0x091E71A4), swap_DrawPanel});

/* ---- FUN_091e71f8: gathers the sprites of the panel and registers them ---- */
void swap_PreparePanel(SwapCtx &x) {
    datum<uint32_t>(x, kPanelTexture) = x.callNid(engine::nid::LoadPanelTexture, x.callLocal(fn::SystemSprite, 9), 0);
    memset(x.mem(guest(x, kPanelSprites)), 0, 0x24);
    datum<uint32_t>(x, kPanelSprites + 0x00) = x.callLocal(fn::SystemSprite, 10);
    datum<uint32_t>(x, kPanelSprites + 0x04) = x.callLocal(fn::SystemSprite, 11);
    datum<uint32_t>(x, kPanelSprites + 0x0C) = x.callLocal(fn::SystemSprite, 15);
    datum<uint32_t>(x, kPanelSprites + 0x10) = x.callLocal(fn::SystemSprite, 16);
    datum<uint32_t>(x, kPanelSprites + 0x14) = x.callLocal(fn::SystemSprite, 17);
    datum<uint32_t>(x, kPanelSprites + 0x1C) = x.callLocal(fn::PanelExtra);
    x.callNid(engine::nid::RegisterPanel, guest(x, kPanelSprites));
}
SwapReg reg_PreparePanel({"libfield.PreparePanel", MODULE, offsetOf(0x091E71F8), swap_PreparePanel});

/* ---- FUN_091e7534: 1 when the panel has finished (a window panel is closed first) ---- */
void swap_DialogDone(SwapCtx &x) {
    if (s32(x, kPanelState) == 2) {
        if (s32(x, kPanelDone) == 0) { x.ret(0); return; }
        datum<int32_t>(x, kPanelState) = 0;
    }
    x.ret(1);
}
SwapReg reg_DialogDone({"libfield.PanelDone", MODULE, offsetOf(0x091E7534), swap_DialogDone});

/* ---- the title banner: fades in over 30 frames, stays 30, fades out over 30 ---- */
constexpr uint32_t kBanner = 0x09997D70;
struct Banner {
    int32_t state;        /* +0x00 0 = none, 1 = in, 2 = out */
    uint32_t backdrop;    /* +0x04 colour of the dark backdrop */
    uint32_t shadow;      /* +0x08 */
    uint32_t text;        /* +0x0C colour of the text */
    uint32_t timer;       /* +0x10 */
    char title[0x80];     /* +0x14 */
    int32_t subtitle;     /* +0x94 line of the system texts below the title; -1 = none */
};
static_assert(offsetof(Banner, subtitle) == 0x94, "Banner layout");
constexpr uint32_t kBannerFrames = 30, kBannerSubtitle = 0x09997E04 - kBanner;
inline Banner &banner(SwapCtx &x) { return datum<Banner>(x, kBanner); }

void swap_UpdateBanner(SwapCtx &x) {
    Banner &b = banner(x);
    if (b.state == 2) {
        if (b.timer >= kBannerFrames) { b.timer = 0; b.state = 0; return; }
        const float t = (float)(int32_t)b.timer / (float)kBannerFrames;
        b.timer = b.timer + 1;
        x.c->f[12].f = t;
        b.backdrop = x.callLocal(fn::ColourLerp, 0xFF000000u, 0);
        x.c->f[12].f = t;
        b.shadow = x.callLocal(fn::ColourLerp, 0xFF000000u, 0);
        x.c->f[12].f = t;
        b.text = x.callLocal(fn::ColourLerp, 0xFFFFFFFFu, 0x00FFFFFFu);
    } else if (b.state == 1) {
        if (b.timer < kBannerFrames) b.timer = b.timer + 1;
        else { b.state = 2; b.timer = 0; }
    }
}
SwapReg reg_UpdateBanner({"libfield.UpdateBanner", MODULE, offsetOf(0x091E8740), swap_UpdateBanner});

void swap_DrawBanner(SwapCtx &x) {
    Banner &b = banner(x);
    if (b.state == 0) return;
    uint32_t list = gu::BeginList(x, 4, 0);
    list = x.callLocal(fn::DrawFadeQuad, list, 0, b.backdrop);
    gu::EndList(x, list);
    list = gu::BeginList(x, 4, 0);
    gu::SetUiFlag4(x, false);
    gu::SetFontSize(x, 22, 22);
    gu::SetWideScreenFlag(x, 0);
    const uint32_t title = guest(x, kBanner) + offsetof(Banner, title);
    int32_t width = gu::TextWidth(x, title) >> 6;
    list = x.callLocal(fn::DrawBannerLine, list, title, 22, (uint32_t)truncate(240.0f - 0.5f * (float)width), 110, b.text, b.shadow);
    gu::SetFontSize(x, 15, 15);
    gu::SetWideScreenFlag(x, gu::GetLanguage(x) == 0);
    const int32_t line = b.subtitle;
    if (line != -1) {
        const uint32_t text = gu::GetText(x, x.callLocal(fn::SystemSprite, 7), (uint32_t)line);
        width = gu::TextWidth(x, text) >> 6;
        list = x.callLocal(fn::DrawBannerLine, list, text, 15, (uint32_t)truncate(240.0f - 0.5f * (float)width), 150, b.text, b.shadow);
    }
    gu::EndList(x, list);
}
SwapReg reg_DrawBanner({"libfield.DrawBanner", MODULE, offsetOf(0x091E8848), swap_DrawBanner});

void swap_BannerDone(SwapCtx &x) { x.ret(banner(x).state == 0 ? 1 : 0); }
SwapReg reg_BannerDone({"libfield.BannerDone", MODULE, offsetOf(0x091E8AA8), swap_BannerDone});

/* ---- the screen fade (to black or white and back) ---- */
constexpr uint32_t kFade = 0x099982C0;
struct ScreenFade {
    int32_t active;       /* +0x00 */
    int32_t state;        /* +0x04 1 = going in, 2 = held, 3 = going out */
    uint32_t texture;     /* +0x08 the picture of the screen that is faded */
    uint32_t timer;       /* +0x0C */
    uint32_t colour;      /* +0x10 */
    int32_t plain;        /* +0x14 1 = a plain coloured rectangle instead of the picture */
};
static_assert(sizeof(ScreenFade) == 0x18, "ScreenFade layout");
constexpr uint32_t kFadeFrames = 15;
inline ScreenFade &fade(SwapCtx &x) { return datum<ScreenFade>(x, kFade); }

void swap_UpdateScreenFade(SwapCtx &x) {
    ScreenFade &f = fade(x);
    if (f.state == 3) {
        f.timer = f.timer + 1;
        x.callLocal(fn::FadeStep, 0);
        if (f.timer < kFadeFrames) return;
        if (f.plain == 0) x.callLocal(fn::FadeFinished);
        f.state = 0;
        f.active = 0;
    } else if (f.state == 2) {
        return;
    } else if (f.state == 1) {
        f.timer = f.timer + 1;
        x.callLocal(fn::FadeStep, 1);
        if (f.timer < kFadeFrames) return;
        f.timer = 0;
        f.active = 0;
        f.state = 2;
    }
}
SwapReg reg_UpdateScreenFade({"libfield.UpdateScreenFade", MODULE, offsetOf(0x091EB67C), swap_UpdateScreenFade});

void swap_DrawScreenFade(SwapCtx &x) {
    ScreenFade &f = fade(x);
    if (f.state == 0) return;
    uint32_t list = gu::BeginList(x, 3, 0);
    if (f.plain == 0) {
        list = gu::SetColor(x, list, f.colour);
        list = gu::BindTexture(x, list, f.texture);
        list = gu::SetSpriteSize(x, list, 64, 256, 64, 256);
        for (int32_t i = 0; i < 7; i++) list = gu::DrawSprite(x, list, i * 64, 0, 0, i * 64, 0, 0, 0);
        list = gu::SetSpriteSize(x, list, 32, 256, 32, 256);
        list = gu::DrawSprite(x, list, 448, 0, 0, 448, 0, 0, 0);
        list = gu::SetSpriteSize(x, list, 16, 256, 256, 16);
        list = gu::DrawSprite(x, list, 0, 256, 0, 480, 0, 0, 3);
        list = gu::SetSpriteSize(x, list, 16, 256, 256, 16);
        list = gu::DrawSprite(x, list, 256, 256, 0, 496, 0, 0, 3);
        list = gu::DisableState(x, list, gu::kScissorTest);
    } else {
        list = x.callLocal(fn::DrawFadeQuad, list, 0, f.colour & 0xFF000000u);
    }
    gu::EndList(x, list);
}
SwapReg reg_DrawScreenFade({"libfield.DrawScreenFade", MODULE, offsetOf(0x091EB748), swap_DrawScreenFade});

/* ---- FUN_091ec204: on map 29 a sound plays louder the closer the player is to its source (tile 15.75, 9.75; reach 18 units) ---- */
constexpr uint32_t kAmbientMap = 29, kAmbientSound = 0x6253;
constexpr float kAmbientX = 15.75f, kAmbientZ = 9.75f, kAmbientReach = 18.0f, kAmbientVolumePerUnit = 7.0555553f;
void swap_PlayAmbient(SwapCtx &x) {
    GuestStack frame(x, 32);
    const uint32_t position = frame.addr() + 20;
    memset(x.mem(position), 0, 12);
    x.callLocal(fn::PlayerPosition, position);
    if (mapScene(x).mapId != (int32_t)kAmbientMap) return;
    const float *p = (const float *)x.mem(position);
    const float dx = p[0] - kAmbientX, dz = p[2] - kAmbientZ;
    x.c->f[12].f = dx * dx + dz * dz;
    x.callNid(engine::nid::Sqrt);
    const float distance = engine::resultFloat(x);
    int32_t volume = truncate(kAmbientVolumePerUnit * (kAmbientReach - distance));
    if (volume < 0) volume = 0;
    if (volume > 127) volume = 127;
    x.callNid(engine::nid::PlayAmbientSound, kAmbientSound, (uint32_t)volume, 0);
}
SwapReg reg_PlayAmbient({"libfield.PlayAmbient", MODULE, offsetOf(0x091EC204), swap_PlayAmbient});

}  // namespace
