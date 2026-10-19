/* libfield: the effects layer (the list of effects over the map, each drawn by its own callback) and the matrix an object is drawn with. */
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

constexpr uint32_t kEffects = 0x09993FE0, kEffectCount = 0x09996880, kEffectBytes = 0x28B0;
constexpr uint32_t kEffectStride = 0x50;
constexpr uint32_t kEffectFade = 0x09993FD0;
constexpr uint32_t kRotationA = 0x091FE230, kRotationB = 0x091FE240, kEffectCamera = 0x091FEC00;
constexpr uint32_t kEffectRotation = 0x09993F90, kEffectQuaternion = 0x09993FA0, kEffectCombined = 0x09993FB0, kEffectVector = 0x09993FC0;
constexpr uint32_t kCompareEffects = 0x091DDB90;
constexpr uint32_t kShadowTable = 0x09996890, kShadowWidth = 0x09996894, kShadowHeight = 0x09996898;
constexpr float kShadowScale = 0.3f, kEighthTurn = 0.7853982f;   /* 0x3E99999A, 0x3F490FDB */
constexpr uint32_t kUiShowTexture = 0x200;

/* ---- FUN_091dd094(vec4): the rotation the effects are drawn with, and the vectors derived from it ---- */
void swap_PrepareEffectCamera(SwapCtx &x) {
    const uint32_t param = x.arg(0);
    GuestStack frame(x, 80);
    const uint32_t base = frame.addr();
    const uint32_t constB = base + 16, product = base + 32, copy = base + 48, constA = base + 64;
    memcpy(x.mem(constA), x.mem(guest(x, kRotationA)), 16);
    x.callNid(engine::nid::Copy4, copy, param);
    x.callNid(engine::nid::Multiply4, product, constA, copy);
    x.callNid(engine::nid::Multiply4, constA, copy, product);
    x.callNid(engine::nid::Copy4, guest(x, kEffectRotation), constA);
    memcpy(x.mem(guest(x, kEffectQuaternion)), x.mem(copy), 16);
    x.callNid(engine::nid::Copy4, guest(x, kEffectCombined), product);
    memcpy(x.mem(constB), x.mem(guest(x, kRotationB)), 16);
    memcpy(x.mem(guest(x, kEffectVector)), x.mem(constB), 16);
}
SwapReg reg_PrepareEffectCamera({"libfield.PrepareEffectCamera", MODULE, offsetOf(0x091DD094), swap_PrepareEffectCamera});

/* ---- FUN_091dd1b4(value) ---- */
void swap_SetEffectFade(SwapCtx &x) { datum<uint32_t>(x, kEffectFade) = x.arg(0); }
SwapReg reg_SetEffectFade({"libfield.SetEffectFade", MODULE, offsetOf(0x091DD1B4), swap_SetEffectFade});

/* ---- FUN_091dd1c0(list): clears the depth and enables the stencil, so the effects mark where they draw ---- */
void swap_BeginEffectMask(SwapCtx &x) {
    uint32_t list = gu::SetClearDepth(x, x.arg(0), 0);
    list = gu::Clear(x, list, 2);
    list = gu::DisableState(x, list, gu::kTexture2D);
    list = gu::DepthMask(x, list, 1);
    list = gu::EnableState(x, list, gu::kStencilTest);
    list = x.callNid(engine::nid::StencilFunc, list, 1, 0, 0xFF);
    list = gu::EnableState(x, list, gu::kCullFace);
    x.ret(x.callNid(engine::nid::PixelMask, list, 0xFFFFFF));
}
SwapReg reg_BeginEffectMask({"libfield.BeginEffectMask", MODULE, offsetOf(0x091DD1C0), swap_BeginEffectMask});

/* ---- FUN_091dd478(list): a half-dark rectangle over the screen where the effects drew (the stencil marks it) ---- */
void swap_DrawEffectVeil(SwapCtx &x) {
    uint32_t list = gu::EnableState(x, x.arg(0), gu::kTexture2D);
    list = x.callNid(engine::nid::SetFrontFace, list, 1);
    list = gu::DepthMask(x, list, 0);
    list = gu::DisableState(x, list, gu::kStencilTest);
    list = x.callNid(engine::nid::StencilOp, list, 0, 0, 0);
    list = x.callNid(engine::nid::PixelMask, list, 0);
    list = gu::EnableState(x, list, gu::kStencilTest);
    list = gu::DisableState(x, list, gu::kTexture2D);
    list = gu::DisableState(x, list, gu::kDepthTest);
    list = x.callNid(engine::nid::StencilFunc, list, 4, 0, 0xFF);
    list = x.callNid(engine::nid::StencilOp, list, 0, 0, 0);
    list = gu::SetBlendColor(x, list, 0x80000000u);
    list = gu::SetBlend(x, list, 0, 2, 3, 0, 0);
    list = gu::EnableState(x, list, gu::kBlend);
    list = gu::SetSpriteSize(x, list, 1, 1, 480, 272);
    list = gu::DrawSprite(x, list, 0, 0, 0, 0, 0, 0, 0);
    list = gu::DisableState(x, list, gu::kStencilTest);
    list = gu::EnableState(x, list, gu::kTexture2D);
    list = gu::EnableState(x, list, gu::kDepthTest);
    x.ret(gu::SetBlendColor(x, list, 0xFFFFFFFFu));
}
SwapReg reg_DrawEffectVeil({"libfield.DrawEffectVeil", MODULE, offsetOf(0x091DD478), swap_DrawEffectVeil});

/* ---- FUN_091dd638(position, out matrix): the matrix of an object at the position: turned an eighth, set down on the ground ---- */
void swap_UpdateObjectMatrix(SwapCtx &x) {
    const uint32_t position = x.arg(0), out = x.arg(1);
    GuestStack frame(x, 176);
    const uint32_t base = frame.addr();
    const uint32_t turned = base + 32, plain = base + 96, at = base + 144, shifted = base + 164;
    const uint32_t layout = mapScene(x).layout;
    if (datum<uint32_t>(x, kShadowTable) == 0 && layout != 0) {
        const uint32_t bytes = (uint32_t)(*view<uint16_t>(x, layout + 6)) << 2;
        const uint32_t table = x.callLocal(fn::HeapAlloc, bytes);
        datum<uint32_t>(x, kShadowTable) = table;
        memset(x.mem(table), 0, bytes);
        datum<uint32_t>(x, kShadowWidth) = *view<uint16_t>(x, layout + 8);
        datum<uint32_t>(x, kShadowHeight) = *view<uint16_t>(x, layout + 10);
    }
    x.c->f[12].f = *(float *)x.mem(position);
    x.c->f[13].f = *(float *)x.mem(position + 8);
    x.callLocal(fn::GroundHeight, position);
    const uint32_t matrix = x.callNid(engine::nid::CurrentMatrix);
    x.c->f[12].f = kShadowScale;
    x.callNid(engine::nid::VectorScale, shifted, matrix + 0x30);
    x.callNid(engine::nid::Matrix3Identity, turned);
    x.c->f[12].f = kEighthTurn;
    x.callNid(engine::nid::Matrix3RotateB, turned, turned);
    x.callNid(engine::nid::Matrix3Identity, plain);
    memcpy(x.mem(at), x.mem(position), 12);
    x.callNid(engine::nid::Matrix3Multiply, out, plain, turned);
}
SwapReg reg_UpdateObjectMatrix({"libfield.UpdateObjectMatrix", MODULE, offsetOf(0x091DD638), swap_UpdateObjectMatrix});

/* ---- FUN_091dd758: draws the effects: the lit ones first (sorted), then the ones that go over the mask, then the screen state is put back ---- */
void swap_DrawEffects(SwapCtx &x) {
    uint32_t list = gu::BeginList(x, 1, 0);
    x.callNid(engine::nid::LoadIdentity);
    list = x.callLocal(fn::CameraStateList, list);
    list = gu::SetBlend(x, list, 0, 2, 3, 0, 0);
    list = gu::EnableState(x, list, gu::kBlend);
    list = x.callNid(engine::nid::AlphaFunc, list, 6, 0, 0xFF);
    list = gu::EnableState(x, list, gu::kAlphaTest);
    const uint32_t pad = gu::GetInput(x);
    if ((*view<uint32_t>(x, pad) & kUiShowTexture) == 0) list = gu::EnableState(x, list, gu::kTexture2D);
    else list = gu::DisableState(x, list, gu::kTexture2D);
    list = gu::SetTextureFunction(x, list, 0, 1);
    x.c->f[12].f = 0.0f; x.c->f[13].f = 0.0f;
    list = x.callNid(engine::nid::SetUvOffset, list);
    x.c->f[12].f = 1.0f; x.c->f[13].f = 1.0f;
    list = x.callNid(engine::nid::SetUvScale, list);
    list = gu::SetTextureMode(x, list, 3, 0, 0, 0);
    list = gu::SetTextureWrap(x, list, 0, 0);
    list = gu::SetTextureFilter(x, list, 0, 0);
    list = x.callNid(engine::nid::ApplyTextureState, list);
    GuestStack frame(x, 48);
    const uint32_t camera = frame.addr() + 32;
    memcpy(x.mem(camera), x.mem(guest(x, kEffectCamera)), 16);
    x.callLocal(fn::PrepareEffectCamera, camera);
    x.callLocal(fn::SetEffectFade, 100);
    list = x.callLocal(fn::BeginEffectMask, list);
    for (int32_t i = 0; i < s32(x, kEffectCount); i++) {
        const uint32_t effect = guest(x, kEffects) + i * kEffectStride;
        if (*view<uint32_t>(x, effect + 0x40) != 0) {
            x.c->f[12].f = *(float *)x.mem(effect + 0x44);
            list = x.callLocal(fn::DrawEffect, list, effect + 0x30);
        }
    }
    list = x.callLocal(fn::DrawEffectVeil, list);
    list = gu::DepthMask(x, list, 1);
    gu::Sort(x, guest(x, kEffects), (uint32_t)s32(x, kEffectCount), kEffectStride, guest(x, kCompareEffects));
    for (int32_t i = 0; i < s32(x, kEffectCount); i++) {
        const uint32_t effect = guest(x, kEffects) + i * kEffectStride;
        if (*view<uint32_t>(x, *view<uint32_t>(x, effect + 0x4C) + 0x8C) == 0) list = x.callAddr(*view<uint32_t>(x, effect + 0x48), list, *view<uint32_t>(x, effect + 0x4C));   /* the effect draws itself: (list, its object) */
    }
    list = gu::DepthMask(x, list, 0);
    list = x.callNid(engine::nid::PixelMask, list, 0xFFFFFFFFu);
    for (int32_t i = 0; i < s32(x, kEffectCount); i++) {
        const uint32_t effect = guest(x, kEffects) + i * kEffectStride;
        if (*view<uint32_t>(x, *view<uint32_t>(x, effect + 0x4C) + 0x8C) == 0) list = x.callAddr(*view<uint32_t>(x, effect + 0x48), list, *view<uint32_t>(x, effect + 0x4C));   /* the effect draws itself: (list, its object) */
    }
    list = x.callNid(engine::nid::PixelMask, list, 0);
    for (int32_t i = 0; i < s32(x, kEffectCount); i++) {
        const uint32_t effect = guest(x, kEffects) + i * kEffectStride;
        if (*view<uint32_t>(x, *view<uint32_t>(x, effect + 0x4C) + 0x8C) != 0) list = x.callAddr(*view<uint32_t>(x, effect + 0x48), list, *view<uint32_t>(x, effect + 0x4C));   /* the effect draws itself: (list, its object) */
    }
    memset(x.mem(guest(x, kEffects)), 0, kEffectBytes);
    for (uint32_t mode = 0; mode < 3; mode++) {
        x.callNid(engine::nid::MatrixMode, mode);
        list = x.callNid(engine::nid::UploadMatrix, list);
    }
    gu::EndList(x, list);
}
SwapReg reg_DrawEffects({"libfield.DrawEffects", MODULE, offsetOf(0x091DD758), swap_DrawEffects});

}  // namespace
