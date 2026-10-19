/* libfield: the follower (a character that walks behind the player, steering around the people on the map), and the character matrix / effects drawing. */
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

constexpr uint32_t kFollowerResources = 0x09993EE0;     /* two resources the follower needs */
constexpr uint32_t kFollowerActive = 0x09993EE8, kFollowerObject = 0x09993EF0, kFollowerTarget = 0x09993F80, kFollowerSlow = 0x09993F78;
constexpr uint32_t kBasisTable = 0x091FE218;            /* two vectors: (0,1,0) and (0,-1,0): the sides to steer to */
constexpr float kWalkSpeed = 0.1f, kRunFactor = 2.0f, kArriveDistance = 0.5f, kSteerDistance = 1.0f, kCellSize = 0.5f, kObstacleRadius = 0.5f;
constexpr float kQuarterTurn = 1.5707964f;              /* 0x3FC90FDB */
constexpr uint32_t kCellFollower = 2, kCellFree = 0;

/* ---- FUN_091dccfc: draws the follower once both resources are loaded ---- */
void swap_DrawFollower(SwapCtx &x) {
    const uint32_t a = datum<uint32_t>(x, kFollowerResources), b = datum<uint32_t>(x, kFollowerResources + 4);
    if (a != 0 && *view<uint32_t>(x, a) != 0 && b != 0 && *view<uint32_t>(x, b) != 0) x.callLocal(fn::DrawObject, guest(x, kFollowerObject));
}
SwapReg reg_DrawFollower({"libfield.DrawFollower", MODULE, offsetOf(0x091DCCFC), swap_DrawFollower});

/* ---- FUN_091dcde8: 1 once both numbers of the follower's move are set ---- */
void swap_FollowerArrived(SwapCtx &x) {
    const SceneObject &object = datum<SceneObject>(x, kFollowerObject);
    x.ret(object.unknown08[0] != 0 && object.unknown08[1] != 0 ? 1 : 0);
}
SwapReg reg_FollowerArrived({"libfield.FollowerArrived", MODULE, offsetOf(0x091DCDE8), swap_FollowerArrived});

/* ---- FUN_091dc904: one frame of the follower: walks towards where the player was, going round the people on the way ---- */
void swap_UpdateFollower(SwapCtx &x) {
    const uint32_t player = playerObject(x);
    if (datum<int32_t>(x, kFollowerActive) == 0) return;
    float speed = kWalkSpeed;
    const uint32_t followerAddress = guest(x, kFollowerObject);
    SceneObject &follower = datum<SceneObject>(x, kFollowerObject);
    const int32_t slow = view<SceneObject>(x, player)->slow;
    follower.slow = slow;
    if (slow != 0) speed = speed * kRunFactor;
    GuestStack frame(x, 0x150);
    const uint32_t base = frame.addr();
    /* same places as the original's frame: the vectors are three floats each */
    const uint32_t side = base + 88, steerBack = base + 160, steerFront = base + 172, list = base + 184, count = base + 84, matrixB = base + 124;
    const uint32_t rel = base + 100, turned = base + 112, diff = base + 312, copy = base + 324;
    const uint32_t position = followerAddress + offsetof(SceneObject, position), velocity = followerAddress + offsetof(SceneObject, velocity);
    float *d = (float *)x.mem(diff), *c = (float *)x.mem(copy);
    x.callNid(engine::nid::VectorSub, diff, guest(x, kFollowerTarget), position);
    c[0] = d[0]; c[1] = d[1]; c[2] = d[2];
    d[1] = 0.0f;
    x.callLocal(fn::VectorLength, copy);
    if (engine::resultFloat(x) < kSteerDistance) {
        x.callNid(engine::nid::Normalize3, diff, diff);
        x.c->f[12].f = speed;
        x.callNid(engine::nid::VectorScale, diff, diff);
        x.c->f[12].f = speed;
        x.callLocal(fn::MoveOnGround, diff, diff, position, 0);
        x.callNid(engine::nid::Normalize3, diff, diff);
        x.c->f[12].f = speed;
        x.callNid(engine::nid::VectorScale, diff, diff);
        memcpy(x.mem(steerFront), x.mem(guest(x, kBasisTable)), 12);
        memcpy(x.mem(steerBack), x.mem(guest(x, kBasisTable + 12)), 12);
        x.c->f[12].f = kObstacleRadius;
        x.callLocal(fn::FindNearObjects, list, count, followerAddress);
        for (int32_t i = 0; i < *(int32_t *)x.mem(count); i++) {
            const uint32_t obstacle = *(uint32_t *)x.mem(list + i * 4);
            x.callNid(engine::nid::VectorSub, rel, obstacle + 20, position);
            x.callNid(engine::nid::VectorDot, rel, diff);
            if (engine::resultFloat(x) < 0.0f) continue;
            x.callNid(engine::nid::MatrixIdentity, matrixB);
            x.c->f[12].f = kQuarterTurn;
            x.callNid(engine::nid::MatrixRotateAxis, matrixB, matrixB);
            x.callNid(engine::nid::TransformPoint, turned, matrixB, diff);
            x.callNid(engine::nid::VectorDot, rel, turned);
            const float towards = engine::resultFloat(x);
            if (towards > 0.0f) x.callNid(engine::nid::VectorCross, side, steerBack, rel);
            else if (towards < 0.0f) x.callNid(engine::nid::VectorCross, side, steerFront, rel);
            else x.callNid(engine::nid::VectorZero, side);
            x.callNid(engine::nid::VectorDot, side, diff);
            x.c->f[12].f = speed;
            x.callNid(engine::nid::VectorScale, side, side);
            memcpy(d, x.mem(side), 12);
        }
    }
    x.callLocal(fn::VectorLength, copy);
    if (engine::resultFloat(x) < kArriveDistance) {
        follower.mode = 0;
    } else {
        x.callNid(engine::nid::Normalize3, diff, diff);
        x.c->f[12].f = speed;
        x.callNid(engine::nid::VectorScale, diff, diff);
        follower.velocity = Vec3{d[0], d[1], d[2]};
        x.callLocal(fn::MarkCell, (uint32_t)truncate(follower.position.x / kCellSize), (uint32_t)truncate(follower.position.z / kCellSize), kCellFree);
        x.callNid(engine::nid::VectorAdd, position, position, velocity);
        x.callLocal(fn::MarkCell, (uint32_t)truncate(follower.position.x / kCellSize), (uint32_t)truncate(follower.position.z / kCellSize), kCellFollower);
        x.callLocal(fn::VectorLength, diff);
        follower.mode = engine::resultFloat(x) <= 0.0f ? 0 : 1;
        follower.direction = (int32_t)x.callLocal(fn::NearestDirection, velocity);
    }
    datum<Vec3>(x, kFollowerTarget) = view<SceneObject>(x, player)->position;
    x.callLocal(fn::FollowerAim);
    x.callLocal(fn::AnimateObject, followerAddress);
}
SwapReg reg_UpdateFollower({"libfield.UpdateFollower", MODULE, offsetOf(0x091DC904), swap_UpdateFollower});

}  // namespace
