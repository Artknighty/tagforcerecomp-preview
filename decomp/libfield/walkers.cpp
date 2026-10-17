/* libfield: the walkers: people who wander on the map around their home cell, one step at a time, and the fixed characters (spots). */
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

constexpr float kStepSpeed = 0.1f;                       /* part of a step walked per frame */
constexpr uint32_t kWaitMin = 0x5A, kWaitRange = 0x78;   /* frames a walker stands between steps: 90 .. 209 */
constexpr int32_t kTries = 10;                           /* random directions tried before a walker gives up for now */
constexpr int32_t kSpecialKind = 0x3A;
enum CellUse : uint32_t { kCellFree = 0, kCellWalker = 3, kCellBlockedByLarge = 5 };
enum Direction : int32_t { kFaceDown = 1, kFaceLeft = 3, kFaceUp = 5, kFaceRight = 7 };

void markCell(SwapCtx &x, int32_t cellX, int32_t cellZ, uint32_t use) { x.callLocal(fn::MarkCell, (uint32_t)cellX, (uint32_t)cellZ, use); }
bool isLiveObject(const SceneObject &object) { return object.present != 0 && object.ready != 0; }
bool hasResource(SwapCtx &x, const SceneObject &object) {
    return object.resource != 0 && *view<uint32_t>(x, object.resource) != 0;
}

/* ---- FUN_091ca240: clears the walkers and spots ---- */
void swap_ResetWalkers(SwapCtx &x) { memset(x.mem(guest(x, kWalkers) - 0x10), 0, 0x5020); }
SwapReg reg_ResetWalkers({"libfield.ResetWalkers", MODULE, offsetOf(0x091CA240), swap_ResetWalkers});

/* Picks a neighbour cell at random (and turns the walker towards it) until one is close enough to home, free and walkable; gives up after ten rolls.
 * Returns the number of rolls made (10 or more = no step found). */
int32_t chooseStep(SwapCtx &x, Walker &walker) {
    int32_t rolls = 0;
    for (;;) {
        for (;;) {
            walker.targetX = walker.cellX;
            walker.targetZ = walker.cellZ;
            switch (x.callNid(ehsys::nid::NextRandom) & 3) {
                case 3: walker.targetX = walker.cellX + 1; walker.object.direction = kFaceRight; break;
                case 2: walker.targetZ = walker.cellZ - 1; walker.object.direction = kFaceUp; break;
                case 1: walker.targetX = walker.cellX - 1; walker.object.direction = kFaceLeft; break;
                case 0: walker.targetZ = walker.cellZ + 1; walker.object.direction = kFaceDown; break;
            }
            if (rolls > 9) return rolls;
            rolls++;
            x.c->f[12].f = (float)(walker.homeX - walker.targetX);
            x.callNid(engine::nid::Abs);
            if (engine::resultFloat(x) <= (float)walker.radius) break;
        }
        x.c->f[12].f = (float)(walker.homeZ - walker.targetZ);
        x.callNid(engine::nid::Abs);
        const bool tooFar = (float)walker.radius < engine::resultFloat(x);
        if (tooFar || x.callLocal(fn::CellIsFree, (uint32_t)walker.targetX, (uint32_t)walker.targetZ) == 0 ||
            x.callLocal(fn::CellIsWalkable, (uint32_t)walker.targetX, (uint32_t)walker.targetZ, 0) == 0 ||
            x.callLocal(fn::CellIsReserved, (uint32_t)walker.targetX, (uint32_t)walker.targetZ) != 0) continue;
        return rolls;
    }
}

/* ---- FUN_091ca320: one frame of every walker and spot ---- */
void swap_UpdateWalkers(SwapCtx &x) {
    const FieldState &state = fieldState(x);
    for (int32_t i = 0; i < kWalkerCount; i++) {
        Walker &walker = walkers(x)[i];
        if (!isLiveObject(walker.object)) continue;
        const uint32_t walkerAddress = guest(x, kWalkers) + i * sizeof(Walker);
        int32_t cellX = walker.cellX;
        if (walker.radius >= 1) {
            if (walker.object.mode == 1) {
                if (walker.paused == 0) {
                    GuestStack scratch(x, 32);
                    const uint32_t from = scratch.addr(), to = scratch.addr() + 16;
                    x.callLocal(fn::CellToWorld, from, (uint32_t)walker.cellX, (uint32_t)walker.cellZ);
                    x.callLocal(fn::CellToWorld, to, (uint32_t)walker.targetX, (uint32_t)walker.targetZ);
                    x.c->f[12].f = walker.progress;
                    x.callNid(engine::nid::VectorLerp, walkerAddress + offsetof(Walker, object) + offsetof(SceneObject, position), from, to);
                    if (walker.progress < 1.0f) {
                        const float next = walker.progress + kStepSpeed;
                        walker.progress = next;
                        if (1.0f < next) walker.progress = 1.0f;
                        markCell(x, walker.targetX, walker.targetZ, kCellWalker);
                    } else {
                        walker.object.mode = 0;
                        walker.wait = (int32_t)((x.callNid(ehsys::nid::NextRandom) & 0xFFFF) % kWaitRange + kWaitMin);
                        markCell(x, walker.cellX, walker.cellZ, kCellFree);
                        x.c->f[12].f = walker.object.position.x;
                        x.c->f[13].f = walker.object.position.z;
                        x.callLocal(fn::WorldToCell, walkerAddress + offsetof(Walker, cellX), walkerAddress + offsetof(Walker, cellZ));
                    }
                }
            } else if (walker.object.mode == 0 && state.mapIdle != 0) {
                if (walker.wait < 1) {
                    const int32_t tries = chooseStep(x, walker);
                    if (tries < kTries) {
                        walker.object.mode = 1;
                        walker.progress = 0;
                    } else {
                        walker.object.mode = 0;
                        walker.wait = (int32_t)((x.callNid(ehsys::nid::NextRandom) & 0xFFFF) % kWaitRange + kWaitMin);
                    }
                } else {
                    walker.wait--;
                }
            }
            cellX = walker.cellX;
        }
        markCell(x, cellX, walker.cellZ, kCellWalker);
        if (walker.object.kind == kSpecialKind) {
            for (int32_t a = -3; a < 2; a++)
                for (int32_t b = -4; b < 3; b++) markCell(x, walker.cellX + a, walker.cellZ + b, kCellBlockedByLarge);
        }
        x.callLocal(fn::AnimateObject, walkerAddress);
    }
    for (int32_t i = 0; i < kSpotCount; i++) {
        Spot &spot = spots(x)[i];
        if (isLiveObject(spot.object)) x.callLocal(fn::AnimateObject, guest(x, kSpots) + i * sizeof(Spot));
    }
}
SwapReg reg_UpdateWalkers({"libfield.UpdateWalkers", MODULE, offsetOf(0x091CA320), swap_UpdateWalkers});

/* ---- FUN_091ca704: draws the walkers and spots whose resource is loaded ---- */
void swap_DrawWalkers(SwapCtx &x) {
    for (int32_t i = 0; i < kWalkerCount; i++) {
        const SceneObject &object = walkers(x)[i].object;
        if (isLiveObject(object) && hasResource(x, object)) x.callLocal(fn::DrawObject, guest(x, kWalkers) + i * sizeof(Walker));
    }
    for (int32_t i = 0; i < kSpotCount; i++) {
        const SceneObject &object = spots(x)[i].object;
        if (isLiveObject(object) && hasResource(x, object)) x.callLocal(fn::DrawObject, guest(x, kSpots) + i * sizeof(Spot));
    }
}
SwapReg reg_DrawWalkers({"libfield.DrawWalkers", MODULE, offsetOf(0x091CA704), swap_DrawWalkers});

/* ---- FUN_091cacec(point): the id of the walker whose cells hold the point (-1 when none); the last one found wins ---- */
void swap_WalkerAt(SwapCtx &x) {
    const uint32_t point = x.arg(0);
    uint32_t found = 0xFFFFFFFFu;
    if (point == 0) { x.ret(found); return; }
    GuestStack scratch(x, 32);
    const uint32_t unused = scratch.addr(), cell = scratch.addr() + 16;
    *(uint32_t *)x.mem(unused) = 0;
    for (int32_t i = 0; i < kWalkerCount; i++) {
        Walker &walker = walkers(x)[i];
        const uint32_t walkerAddress = guest(x, kWalkers) + i * sizeof(Walker);
        if (!isLiveObject(walker.object)) continue;
        if (x.callLocal(fn::PointHitsCell, unused, walkerAddress + offsetof(SceneObject, position), point, walkerAddress) != 0) found = *view<uint32_t>(x, walkerAddress);
        if (walker.path != 0) {
            for (int32_t k = 0; k < walker.pathCount; k++) {
                const uint32_t entry = walker.path + k * 0x18;
                x.callLocal(fn::CellToWorld, cell, view<uint32_t>(x, entry)[0], view<uint32_t>(x, entry)[1]);
                if (x.callLocal(fn::PointHitsCell, unused, cell, point, walkerAddress) != 0) found = *view<uint32_t>(x, walkerAddress);
            }
        }
    }
    x.ret(found);
}
SwapReg reg_WalkerAt({"libfield.WalkerAt", MODULE, offsetOf(0x091CACEC), swap_WalkerAt});

}  // namespace
