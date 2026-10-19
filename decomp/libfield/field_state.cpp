/* libfield: the field's state record, the player's progress stage, the ground under the player and the beacons. */
#include "../common/swap.h"
#include "../common/ehsys.h"
#include "field_data.h"
#include "field_engine.h"
#include "field_functions.h"
#include <stddef.h>

#define MODULE "libfield"

namespace {
using namespace field;

constexpr float kCellSize = 0.5f;

/* ---- FUN_091cd50c: the address of the field's state record ---- */
void swap_FieldStateAddress(SwapCtx &x) { x.ret(guest(x, kFieldState)); }
SwapReg reg_FieldStateAddress({"libfield.FieldStateAddress", MODULE, offsetOf(0x091CD50C), swap_FieldStateAddress});

/* ---- FUN_091d1730: the address of the arguments the story gives the field (date, player values, callbacks) ---- */
void swap_FieldArgumentsAddress(SwapCtx &x) { x.ret(guest(x, kFieldArguments)); }
SwapReg reg_FieldArgumentsAddress({"libfield.FieldArgumentsAddress", MODULE, offsetOf(0x091D1730), swap_FieldArgumentsAddress});

/* ---- FUN_091cd518: how far the player is in the game (0..7), from the progress flags the save keeps ---- */
void swap_ProgressStage(SwapCtx &x) {
    GuestStack flags(x, 16);
    x.callNid(ehsys::nid::GetProgressFlags, flags.addr());
    x.ret((*x.mem(flags.addr()) & 0xF) >> 1);
}
SwapReg reg_ProgressStage({"libfield.ProgressStage", MODULE, offsetOf(0x091CD518), swap_ProgressStage});

/* ---- FUN_091cd548(level): 0 below level 16, 1 up to 18, 2 from 19 on ---- */
void swap_LevelClass(SwapCtx &x) {
    const int32_t level = (int32_t)x.arg(0);
    x.ret(level < 16 ? 0 : level < 19 ? 1 : 2);
}
SwapReg reg_LevelClass({"libfield.LevelClass", MODULE, offsetOf(0x091CD548), swap_LevelClass});

/* ---- FUN_091cd574(map, cellX, cellZ): the special ground at a cell: 1 and 2 are the stairs / doors of map 0x14, 3 and 4 the two kinds of step on map 0x20; 0 for none ---- */
void swap_SurfaceAt(SwapCtx &x) {
    const int32_t map = (int32_t)x.arg(0), cellX = (int32_t)x.arg(1), cellZ = (int32_t)x.arg(2);
    uint32_t surface = 0;
    if (map == 0x20) {
        if ((cellX == 0x20 && cellZ == 2) || (cellX == 0x21 && cellZ == 2)) surface = 3;
        if ((cellX == 0x1E && cellZ == 2) || (cellX == 0x1F && cellZ == 2)) surface = 4;
    } else if (map == 0x14) {
        if (cellZ == 4 && cellX >= 1 && cellX <= 6) {
            if ((int32_t)x.callLocal(fn::ProgressStage) < 2) surface = 1;
        } else if ((cellX == 3 || cellX == 4) && cellZ == 6) {
            surface = 2;
        }
    }
    x.ret(surface);
}
SwapReg reg_SurfaceAt({"libfield.SurfaceAt", MODULE, offsetOf(0x091CD574), swap_SurfaceAt});

/* ---- FUN_091cf918: one frame of the beacons: put their matrices in place and mark their cells ---- */
void updateBeaconRow(SwapCtx &x, uint32_t row) {
    for (int32_t i = 0; i < kBeaconCount; i++) {
        const uint32_t address = guest(x, row) + i * sizeof(Beacon);
        Beacon &beacon = *view<Beacon>(x, address);
        if (beacon.active == 0) continue;
        x.callLocal(fn::UpdateObjectMatrix, address + offsetof(Beacon, position), address + offsetof(Beacon, matrix));
        x.callLocal(fn::MarkCell, (uint32_t)truncate(beacon.position.x / kCellSize), (uint32_t)truncate(beacon.position.z / kCellSize), 4);
    }
}
void swap_UpdateBeacons(SwapCtx &x) {
    if (mapScene(x).layout == 0) return;
    updateBeaconRow(x, kBeacons);
    updateBeaconRow(x, kBeaconsB);
}
SwapReg reg_UpdateBeacons({"libfield.UpdateBeacons", MODULE, offsetOf(0x091CF918), swap_UpdateBeacons});

/* ---- FUN_091cfa1c: draws the beacons (their two rows have their own draw function) ---- */
void drawBeaconRow(SwapCtx &x, uint32_t row, uint32_t drawFunction) {
    for (int32_t i = 0; i < kBeaconCount; i++) {
        const uint32_t address = guest(x, row) + i * sizeof(Beacon);
        if (view<Beacon>(x, address)->active == 0) continue;
        x.c->f[12].f = 1.0f;
        x.callLocal(fn::DrawModelAt, address + offsetof(Beacon, matrix), 0, 1, guest(x, drawFunction));
    }
}
void swap_DrawBeacons(SwapCtx &x) {
    const uint32_t resource = datum<uint32_t>(x, kBeaconResource);
    if (resource == 0 || *view<uint32_t>(x, resource) == 0) return;
    drawBeaconRow(x, kBeacons, 0x091CFE6C);
    drawBeaconRow(x, kBeaconsB, 0x091CFEDC);
}
SwapReg reg_DrawBeacons({"libfield.DrawBeacons", MODULE, offsetOf(0x091CFA1C), swap_DrawBeacons});

}  // namespace
