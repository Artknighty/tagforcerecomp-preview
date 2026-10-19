/* libfield: the camera of the map: the 3D drawing state, the view that follows the player (with a small random shake), the light, a full-screen quad. */
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

/* the shake of the camera (FUN_091d9578 clears it) */
struct CameraShake {
    int32_t enabled;         /* +0x00 0x09993B40: the offset below is added to the camera */
    uint32_t unknown04[3];
    int32_t level;           /* +0x14 0x09993B54: 0..3, how strong */
    int32_t frames;          /* +0x18 frames shaken so far */
    int32_t limit;           /* +0x1C */
    Vec3 offset;             /* +0x20 0x09993B60 */
};
constexpr uint32_t kShake = 0x09993B40, kShakeContinuous = 0x09993B50, kLightMode = 0x09993BBC, kLightBlock = 0x09993B80;
constexpr float kShakeAmplitude[4] = {0.005f, 0.01f, 0.02f, 0.04f};   /* by level */
constexpr uint32_t kUpVector = 0x091FE0F0, kEyeOffset = 0x091FA2A0, kProjectionTranslate = 0x091FE0DC;
constexpr uint32_t kCameraBlock = kCameraVector;                     /* 0x09993B70: the view matrix's translation row */

/* ---- FUN_091d9c80: new random offsets for the shake while it lasts ---- */
void swap_UpdateShakeValues(SwapCtx &x) {
    const int32_t continuous = datum<int32_t>(x, kShakeContinuous);
    int32_t &frames = datum<int32_t>(x, kShake + 0x18);
    const int32_t limit = datum<int32_t>(x, kShake + 0x1C);
    if (continuous == 0 && limit <= frames) { datum<int32_t>(x, kShake) = 0; return; }
    const int32_t level = datum<int32_t>(x, kShake + 0x14);
    float amplitude = x.c->f[20].f;   /* any other level leaves the caller's register in use, like the original */
    if (level >= 0 && level <= 3) amplitude = kShakeAmplitude[level];
    Vec3 &offset = datum<Vec3>(x, kShake + 0x20);
    offset.x = amplitude * (float)((x.callNid(ehsys::nid::NextRandom) & 0xFFFF) % 10);
    offset.y = amplitude * (float)((x.callNid(ehsys::nid::NextRandom) & 0xFFFF) % 10);
    const uint32_t random = x.callNid(ehsys::nid::NextRandom);
    frames++;
    offset.z = amplitude * (float)((random & 0xFFFF) % 10);
}
SwapReg reg_UpdateShakeValues({"libfield.UpdateShakeValues", MODULE, offsetOf(0x091D9C80), swap_UpdateShakeValues});

/* ---- FUN_091d959c: one frame of the shake (the pad is read and ignored) ---- */
void swap_UpdateShake(SwapCtx &x) {
    gu::GetInput(x);
    x.callLocal(fn::UpdateShakeValues);
}
SwapReg reg_UpdateShake({"libfield.UpdateShake", MODULE, offsetOf(0x091D959C), swap_UpdateShake});

/* ---- FUN_091d96cc: where the camera is (the translation of the view matrix, kept by LookAt) ---- */
void swap_CameraVector(SwapCtx &x) { x.ret(guest(x, kCameraBlock)); }
SwapReg reg_CameraVector({"libfield.CameraVector", MODULE, offsetOf(0x091D96CC), swap_CameraVector});

/* ---- FUN_091d9814(list): the drawing state of the 3D map and the 2D projection (480 x 272, depth +-10000) ---- */
void swap_SetupCamera3D(SwapCtx &x) {
    uint32_t list = x.arg(0);
    list = x.callNid(engine::nid::SetDrawOffset, list, 0x710, 0x778);
    list = x.callNid(engine::nid::SetViewport, list, 0x800, 0x800, 480, 272);
    list = x.callNid(engine::nid::SetDepthRange, list, 0xFFFF, 0);
    list = gu::SetBlend(x, list, 0, 2, 3, 0, 0);
    list = gu::DisableState(x, list, gu::kBlend);
    list = gu::SetScissor(x, list, 0, 0, 480, 272);
    list = gu::EnableState(x, list, gu::kScissorTest);
    list = gu::AlphaFunc(x, list, 6, 0, 0xFF);
    list = gu::DisableState(x, list, gu::kAlphaTest);
    list = gu::DepthFunc(x, list, 7);
    list = gu::EnableState(x, list, gu::kDepthTest);
    list = x.callNid(engine::nid::SetFrontFace, list, 1);
    list = gu::EnableState(x, list, gu::kCullFace);
    list = gu::DisableState(x, list, gu::kTexture2D);
    list = x.callNid(engine::nid::SetShadeMode, list, 1);
    list = gu::EnableState(x, list, gu::kClipPlanes);
    list = gu::SetBlendColor(x, list, 0xFF0000FF);
    x.c->f[12].f = 50.0f; x.c->f[13].f = 100.0f;
    list = x.callNid(engine::nid::SetFog, list, 0x404040);
    list = gu::DisableState(x, list, gu::kFog);
    x.callNid(engine::nid::MatrixMode, 0);
    x.callNid(engine::nid::LoadIdentity);
    x.c->f[12].f = -240.0f; x.c->f[13].f = 240.0f; x.c->f[14].f = -136.0f; x.c->f[15].f = 136.0f; x.c->f[16].f = -10000.0f; x.c->f[17].f = 10000.0f;
    x.callNid(engine::nid::Ortho);
    GuestStack translate(x, 16);
    memcpy(x.mem(translate.addr()), x.mem(guest(x, kProjectionTranslate)), 12);
    x.callNid(engine::nid::TranslateB, translate.addr());
    x.callNid(engine::nid::MatrixMode, 1);
    x.callNid(engine::nid::LoadIdentity);
    x.callNid(engine::nid::MatrixMode, 2);
    x.callNid(engine::nid::LoadIdentity);
    list = gu::SetClearColor(x, list, 0);
    list = gu::SetClearStencil(x, list, 0);
    list = gu::SetClearDepth(x, list, 0);
    x.ret(gu::SetTextureFilter(x, list, 1, 1));
}
SwapReg reg_SetupCamera3D({"libfield.SetupCamera3D", MODULE, offsetOf(0x091D9814), swap_SetupCamera3D});

/* ---- FUN_091d9a30(list): the view: looks at the player from a fixed offset (plus the shake) ---- */
void swap_SetupLookAt(SwapCtx &x) {
    const uint32_t list = x.arg(0);
    GuestStack frame(x, 96);
    const uint32_t up = frame.addr(), target = frame.addr() + 16, eye = frame.addr() + 32, position = frame.addr() + 48;
    memcpy(x.mem(up), x.mem(guest(x, kUpVector)), 16);
    x.callNid(engine::nid::MatrixMode, 1);
    x.callLocal(fn::PlayerPosition, position);
    const Vec3 player = *(Vec3 *)x.mem(position);
    float *t = (float *)x.mem(target), *e = (float *)x.mem(eye);
    t[0] = player.x; t[1] = player.y; t[2] = player.z; t[3] = 1.0f;
    e[0] = player.x + datum<float>(x, kEyeOffset);
    e[1] = player.y + datum<float>(x, kEyeOffset + 4);
    e[2] = player.z + datum<float>(x, kEyeOffset + 8);
    e[3] = 1.0f;
    if (datum<int32_t>(x, kShake) != 0) {
        x.callNid(engine::nid::VectorAddB, eye, eye, guest(x, kShake + 0x20));
        x.callNid(engine::nid::VectorAddB, target, target, guest(x, kShake + 0x20));
    }
    x.callNid(engine::nid::LookAt, eye, target, up);
    x.callNid(engine::nid::UpdateViewMatrix);
    const uint32_t matrix = x.callNid(engine::nid::CurrentMatrix);
    memcpy(x.mem(guest(x, kCameraBlock)), x.mem(matrix + 0x30), 16);
    x.callNid(engine::nid::MatrixMode, 2);
    x.callNid(engine::nid::LoadIdentity);
    x.ret(list);
}
SwapReg reg_SetupLookAt({"libfield.SetupLookAt", MODULE, offsetOf(0x091D9A30), swap_SetupLookAt});

/* ---- FUN_091d9b84(list): the light colour for the time of day (mode 0 white, 1 warm, 2 dusk) ---- */
void swap_SetupLight(SwapCtx &x) {
    const uint32_t list = x.arg(0);
    uint32_t red = x.c->r[18], green = x.c->r[17], blue = x.c->r[16];   /* other modes keep what the caller had in these registers */
    const int32_t mode = datum<int32_t>(x, kLightMode);
    if (mode == 2) { red = 0x78; green = 0x8C; blue = 0xFF; }
    else if (mode == 1) { red = 0xFF; green = 200; blue = 0x5A; }
    else if (mode == 0) { red = 0xFF; green = 0xFF; blue = 0xFF; }
    memset(x.mem(guest(x, kLightBlock)), 0, 0x3C);
    uint32_t drawn = x.callNid(engine::nid::SetLight, list, 0, guest(x, kLightBlock));
    drawn = x.callNid(engine::nid::SetAmbientColour, drawn, (blue & 0xFF) << 16 | (red & 0xFF) | (green & 0xFF) << 8 | 0xFF000000u);
    drawn = x.callNid(engine::nid::EnableLights, drawn);
    x.ret(x.callNid(engine::nid::FinishLights, drawn, 0));
}
SwapReg reg_SetupLight({"libfield.SetupLight", MODULE, offsetOf(0x091D9B84), swap_SetupLight});

/* ---- FUN_091d9610(list): the three steps above in order ---- */
void swap_SceneStateList(SwapCtx &x) {
    uint32_t list = x.callLocal(fn::SetupCamera3D, x.arg(0));
    list = x.callLocal(fn::SetupLookAt, list);
    x.ret(x.callLocal(fn::SetupLight, list));
}
SwapReg reg_SceneStateList({"libfield.SceneStateList", MODULE, offsetOf(0x091D9610), swap_SceneStateList});

/* ---- FUN_091d95c0: records the scene's state in list 0 and clears the model and world matrices ---- */
void swap_DrawShakeScene(SwapCtx &x) {
    uint32_t list = gu::BeginList(x, 0, 0);
    list = x.callLocal(fn::SetupCamera3D, list);
    list = x.callLocal(fn::SetupLookAt, list);
    list = x.callLocal(fn::SetupLight, list);
    gu::EndList(x, list);
    x.callNid(engine::nid::MatrixMode, 2);
    x.callNid(engine::nid::LoadIdentity);
}
SwapReg reg_DrawShakeScene({"libfield.DrawShakeScene", MODULE, offsetOf(0x091D95C0), swap_DrawShakeScene});

/* ---- FUN_091d9db0(list, vertices): a flat coloured quad over the screen, blended ---- */
void swap_DrawQuad(SwapCtx &x) {
    const uint32_t vertices = x.arg(1);
    uint32_t list = gu::SetBlend(x, x.arg(0), 0, 2, 3, 0, 0);
    list = gu::EnableState(x, list, gu::kBlend);
    list = gu::SetColor(x, list, 0xFFFFFFFF);
    list = gu::DisableState(x, list, gu::kTexture2D);
    list = gu::DisableState(x, list, gu::kDepthTest);
    list = gu::DrawArray(x, list, 6, 0x80011C, 2, 0, vertices);
    list = gu::EnableState(x, list, gu::kTexture2D);
    x.ret(gu::EnableState(x, list, gu::kDepthTest));
}
SwapReg reg_DrawQuad({"libfield.DrawQuad", MODULE, offsetOf(0x091D9DB0), swap_DrawQuad});

/* ---- FUN_091d963c(list, depth, colour): a full-screen rectangle (0,0)-(480,272) of one colour ---- */
void swap_DrawFadeQuad(SwapCtx &x) {
    const uint32_t depth = x.arg(1) & 0xFFFF, colour = x.arg(2);
    GuestStack cursor(x, 16);
    const uint32_t cursorAddress = cursor.addr();
    *(uint32_t *)x.mem(cursorAddress) = x.arg(0);
    const uint32_t verticesAddress = gu::ReserveListData(x, cursorAddress, 0x18);
    uint8_t *v = x.mem(verticesAddress);
    memset(v, 0, 0x18);
    *(uint32_t *)(v + 0x00) = colour;
    *(int16_t *)(v + 0x04) = 0; *(int16_t *)(v + 0x06) = 0; *(int16_t *)(v + 0x08) = (int16_t)depth;
    *(uint32_t *)(v + 0x0C) = colour;
    *(int16_t *)(v + 0x10) = 480; *(int16_t *)(v + 0x12) = 272; *(int16_t *)(v + 0x14) = (int16_t)depth;
    x.ret(x.callLocal(fn::DrawQuad, *(uint32_t *)x.mem(cursorAddress), verticesAddress));
}
SwapReg reg_DrawFadeQuad({"libfield.DrawFadeQuad", MODULE, offsetOf(0x091D963C), swap_DrawFadeQuad});

}  // namespace
