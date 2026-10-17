/* libfield: the player's character on the map: moving, the cells it occupies, footsteps. */
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

constexpr uint32_t kFootstepSoundA = 0x6259, kFootstepSoundB = 0x625A;   /* played on the walk frames 1 and 4 */
constexpr float kCellSize = 0.5f;
constexpr int32_t kPlainMap = 0x25, kMainMap = 0xC;

uint32_t objectAddress(SwapCtx &x) { return guest(x, kPlayer) + offsetof(Player, object); }

/* ---- FUN_091cb274: clears the player ---- */
void swap_ResetPlayer(SwapCtx &x) { memset(&player(x), 0, sizeof(Player)); }
SwapReg reg_ResetPlayer({"libfield.ResetPlayer", MODULE, offsetOf(0x091CB274), swap_ResetPlayer});

/* ---- FUN_091cb610: draws the player once both its resources are loaded ---- */
void swap_DrawPlayer(SwapCtx &x) {
    const Player &p = player(x);
    if (p.resourceA != 0 && *view<uint32_t>(x, p.resourceA) != 0 && p.resourceB != 0 && *view<uint32_t>(x, p.resourceB) != 0) x.callLocal(fn::DrawObject, objectAddress(x));
}
SwapReg reg_DrawPlayer({"libfield.DrawPlayer", MODULE, offsetOf(0x091CB610), swap_DrawPlayer});

/* ---- FUN_091cb73c: 1 once both numbers of a move under way are set ---- */
void swap_PlayerArrived(SwapCtx &x) {
    const SceneObject &object = player(x).object;
    x.ret(object.unknown08[0] != 0 && object.unknown08[1] != 0 ? 1 : 0);
}
SwapReg reg_PlayerArrived({"libfield.PlayerArrived", MODULE, offsetOf(0x091CB73C), swap_PlayerArrived});

/* ---- FUN_091cb770: the player's object, 0 while the player does not exist ---- */
void swap_PlayerObject(SwapCtx &x) { x.ret(playerObject(x)); }
SwapReg reg_PlayerObject({"libfield.PlayerObject", MODULE, offsetOf(0x091CB770), swap_PlayerObject});

/* ---- FUN_091cb798(out): copies the player's position ---- */
void swap_PlayerPosition(SwapCtx &x) {
    if (player(x).object.ready != 0) x.callLocal(fn::ObjectPosition, x.arg(0), objectAddress(x));
}
SwapReg reg_PlayerPosition({"libfield.PlayerPosition", MODULE, offsetOf(0x091CB798), swap_PlayerPosition});

/* ---- FUN_091cb81c(value): lets the player move (nonzero) or not ---- */
void swap_SetPlayerControl(SwapCtx &x) { player(x).object.control = x.arg(0); }
SwapReg reg_SetPlayerControl({"libfield.SetPlayerControl", MODULE, offsetOf(0x091CB81C), swap_SetPlayerControl});

/* ---- FUN_091cb83c(use): marks the four map cells under the player's corners ---- */
void swap_MarkPlayerCells(SwapCtx &x) {
    if (mapScene(x).layout == 0) return;
    const uint32_t use = x.arg(0);
    GuestStack scratch(x, 64);
    const uint32_t corners = scratch.addr(), cell = scratch.addr() + 48;
    x.c->f[12].f = 0.1f;
    x.callLocal(fn::PlayerCorners, guest(x, kPlayer) + offsetof(Player, object) + offsetof(SceneObject, position), corners, corners + 12, corners + 24, corners + 36);
    for (int32_t i = 0; i < 4; i++) {
        const Vec3 *corner = view<Vec3>(x, corners + i * 12);
        x.c->f[12].f = corner->x;
        x.c->f[13].f = corner->z;
        x.callLocal(fn::WorldToCell, cell, cell + 4);
        const uint32_t cellX = *(uint32_t *)x.mem(cell), cellZ = *(uint32_t *)x.mem(cell + 4);
        if (x.callLocal(fn::CellUse, cellX, cellZ) != 3) x.callLocal(fn::MarkCell, cellX, cellZ, use);
    }
}
SwapReg reg_MarkPlayerCells({"libfield.MarkPlayerCells", MODULE, offsetOf(0x091CB83C), swap_MarkPlayerCells});

/* ---- FUN_091cb294: one frame of the player: reads the move, keeps the player off the blocked spot of the main map, steps, picks the surface sound ---- */
bool atBlockedSpot(SwapCtx &x, const SceneObject &object) {
    const int32_t mapId = mapScene(x).mapId;
    if (mapId != kPlainMap && mapId != kMainMap) return false;
    return truncate(object.position.x / kCellSize) == 0xE && truncate(object.position.z / kCellSize) > 10 && truncate(object.position.z / kCellSize) < 0x1A &&
           7.25f <= object.position.x && 0.0f < object.velocity.x;
}
void step(SwapCtx &x, SceneObject &object) {
    x.callNid(engine::nid::VectorAdd, guest(x, kPlayer) + offsetof(Player, object) + offsetof(SceneObject, position), guest(x, kPlayer) + offsetof(Player, object) + offsetof(SceneObject, position),
              guest(x, kPlayer) + offsetof(Player, object) + offsetof(SceneObject, velocity));
}
void swap_UpdatePlayer(SwapCtx &x) {
    SceneObject &object = player(x).object;
    if (object.control == 0) {
        object.mode = 0;
        memset(&object.velocity, 0, sizeof(Vec3));
    } else {
        x.callLocal(fn::PlayerReadInput);
        x.callLocal(fn::PlayerCollide);
        if (atBlockedSpot(x, object)) object.velocity.x = 0.0f;
        if (object.slow != 0) {
            x.callLocal(fn::MarkPlayerCells, 0);
            step(x, object);
            x.callLocal(fn::MarkPlayerCells, 1);
            x.callLocal(fn::PlayerReadInput);
            x.callLocal(fn::PlayerCollide);
            if (atBlockedSpot(x, object)) object.velocity.x = 0.0f;
        }
    }
    if (object.resource != 0) x.callLocal(fn::PlayerEffects);
    x.callLocal(fn::MarkPlayerCells, 0);
    step(x, object);
    x.callLocal(fn::MarkPlayerCells, 1);
    x.callLocal(fn::AnimateObject, objectAddress(x));
    Player &p = player(x);
    const uint32_t surface = x.callLocal(fn::SurfaceAt, (uint32_t)mapScene(x).mapId, (uint32_t)truncate(object.position.x / kCellSize), (uint32_t)truncate(object.position.z / kCellSize));
    if (surface == 0) {
        if (p.onGround != 0) x.callLocal(fn::SetPlayerShadow, 0, 0);
        p.onGround = 0;
    } else {
        const uint32_t shadow = surface == 4 ? 0xE : surface == 3 ? 0xD : surface == 2 ? 0xC : 0xB;
        if (p.onGround == 0 || shadow != x.callLocal(fn::ShadowKind, object.shadow)) x.callLocal(fn::SetPlayerShadow, shadow, 0);
        p.onGround = 1;
    }
    if (object.mode == 1) {
        if (p.lastStepFrame != object.frame || p.lastStepFrame == -1) {
            if (object.frame == 4) x.callLocal(fn::PlayEffect, kFootstepSoundB, 0);
            else if (object.frame == 1) x.callLocal(fn::PlayEffect, kFootstepSoundA, 0);
            p.lastStepFrame = object.frame;
        }
    } else if (object.mode == 0) {
        p.lastStepFrame = -1;
    }
}
SwapReg reg_UpdatePlayer({"libfield.UpdatePlayer", MODULE, offsetOf(0x091CB294), swap_UpdatePlayer});

}  // namespace
