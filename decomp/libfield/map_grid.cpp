/* libfield: coordinate conversion, map-grid lookups, reserved place cells and pathfinding exceptions. */
#include "../swap.h"
#include "field_data.h"
#include "field_functions.h"

#define MODULE "libfield"

namespace {
using namespace field;

constexpr float kCellSize = 0.5f;
constexpr float kCellCentre = 0.25f;
constexpr int32_t kPlainMap = 0x25, kMainMap = 0x0C, kShopMap = 2, kHarbourMap = 3;

/* ---- FUN_091c8524(x, z, cellX, cellZ): converts world coordinates to grid coordinates ---- */
void swap_WorldToCell(SwapCtx &x) {
    const int32_t cellX = truncate(x.c->f[12].f / kCellSize);
    const int32_t cellZ = truncate(x.c->f[13].f / kCellSize);
    *view<int32_t>(x, x.arg(0)) = cellX;
    *view<int32_t>(x, x.arg(1)) = cellZ;
}
SwapReg reg_WorldToCell({"libfield.WorldToCell", MODULE, offsetOf(0x091C8524), swap_WorldToCell});

/* ---- FUN_091c84e0(out, cellX, cellZ): converts a cell to its centre on the ground ---- */
void swap_CellToWorld(SwapCtx &x) {
    Vec3 &position = *view<Vec3>(x, x.arg(0));
    position.x = (float)(int32_t)x.arg(1) * kCellSize + kCellCentre;
    position.z = (float)(int32_t)x.arg(2) * kCellSize + kCellCentre;
    x.c->f[12].f = position.x;
    x.c->f[13].f = position.z;
    x.callLocal(fn::GroundHeight, x.arg(0));
}
SwapReg reg_CellToWorld({"libfield.CellToWorld", MODULE, offsetOf(0x091C84E0), swap_CellToWorld});

/* ---- FUN_091c7dec(): ground-vertex array; the layout stores its offset from the header ---- */
void swap_GroundVertices(SwapCtx &x) {
    const uint32_t layoutAddress = mapScene(x).layout;
    if (layoutAddress == 0) { x.ret(0); return; }
    const MapLayout &layout = *view<MapLayout>(x, layoutAddress);
    x.ret(layoutAddress + layout.groundVerticesOffset);
}
SwapReg reg_GroundVertices({"libfield.GroundVertices", MODULE, offsetOf(0x091C7DEC), swap_GroundVertices});

/* ---- FUN_091c7e18(): byte-per-cell walkability array, also relative to the map-layout header ---- */
void swap_WalkabilityGrid(SwapCtx &x) {
    const uint32_t layoutAddress = mapScene(x).layout;
    if (layoutAddress == 0) { x.ret(0); return; }
    const MapLayout &layout = *view<MapLayout>(x, layoutAddress);
    x.ret(layoutAddress + layout.walkabilityOffset);
}
SwapReg reg_WalkabilityGrid({"libfield.WalkabilityGrid", MODULE, offsetOf(0x091C7E18), swap_WalkabilityGrid});

/* ---- FUN_091c7e44(x, z, pathMode): tests static walkability plus the map's doors and time-of-day rules ---- */
/* The main island and the shop block a few cells in front of the doors. Walking (pathMode 0) blocks eight of them; the night rule used for paths
 * (pathMode nonzero, from hour 22 on) has only seven: the cell (13, 12) is not in that list. */
bool isBlockedByMainMapRule(int32_t mapId, int32_t x, int32_t z, bool nightList) {
    if (mapId == kPlainMap || mapId == kMainMap) {
        constexpr int32_t blockedX[] = {15, 15, 15, 15, 13, 13, 13, 13};
        constexpr int32_t blockedZ[] = {24, 20, 16, 12, 24, 20, 16, 12};
        const unsigned count = nightList ? 7 : 8;
        for (unsigned i = 0; i < count; i++)
            if (x == blockedX[i] && z == blockedZ[i]) return true;
    }
    return false;
}

void swap_CellIsWalkable(SwapCtx &x) {
    const int32_t cellX = (int32_t)x.arg(0), cellZ = (int32_t)x.arg(1);
    const uint32_t pathMode = x.arg(2);
    const int32_t mapId = mapScene(x).mapId;

    if (pathMode == 0) {
        if (isBlockedByMainMapRule(mapId, cellX, cellZ, false)) { x.ret(0); return; }
    } else if ((mapId == kPlainMap || mapId == kMainMap) && *view<int32_t>(x, fieldArguments(x).hour) > 0x15) {
        if (isBlockedByMainMapRule(mapId, cellX, cellZ, true)) { x.ret(0); return; }
    }
    constexpr int32_t shopBlockedX[] = {1, 1, 10, 11, 21, 22};
    constexpr int32_t shopBlockedZ[] = {3, 4, 1, 1, 1, 1};
    if (mapId == kShopMap && fieldState(x).flag14 != 0) {
        for (unsigned i = 0; i < sizeof(shopBlockedX) / sizeof(shopBlockedX[0]); i++) {
            if (cellX == shopBlockedX[i] && cellZ == shopBlockedZ[i]) { x.ret(0); return; }
        }
    }

    const uint32_t layoutAddress = mapScene(x).layout;
    if (layoutAddress == 0) { x.ret(0); return; }
    const MapLayout &layout = *view<MapLayout>(x, layoutAddress);
    if (cellX < 0 || cellX >= layout.width || cellZ < 0 || cellZ >= layout.height) { x.ret(0); return; }

    const uint32_t walkability = x.callLocal(fn::WalkabilityGrid);
    const uint32_t indexBits = (uint32_t)cellX + (uint32_t)cellZ * layout.width;
    const int32_t index = (int32_t)indexBits;
    if (index < 0 || index >= layout.cellCount) { x.ret(0); return; }
    x.ret(view<uint8_t>(x, walkability)[index]);   /* the byte itself, like the original (callers only test it against 0) */
}
SwapReg reg_CellIsWalkable({"libfield.CellIsWalkable", MODULE, offsetOf(0x091C7E44), swap_CellIsWalkable});

/* ---- FUN_091c8550(x, z): true when a story-place marker reserves the cell ---- */
void swap_CellIsReserved(SwapCtx &x) {
    const int32_t cellX = (int32_t)x.arg(0), cellZ = (int32_t)x.arg(1);
    const MapScene &scene = mapScene(x);
    for (int32_t i = 0; i < scene.cellCount; i++) {
        const int16_t *cell = view<int16_t>(x, scene.cells + (uint32_t)i * 4);
        if (cell[0] == cellX && cell[1] == cellZ) { x.ret(1); return; }
    }
    x.ret(0);
}
SwapReg reg_CellIsReserved({"libfield.CellIsReserved", MODULE, offsetOf(0x091C8550), swap_CellIsReserved});

/* ---- FUN_091c8e04(x, z): the area of a cell: the harbour has two (0 from column 15 on, 1 before it); the other maps have none (-1) ---- */
void swap_AreaOfCell(SwapCtx &x) {
    const int32_t cellX = (int32_t)x.arg(0);
    if (mapScene(x).mapId != kHarbourMap) { x.ret(0xFFFFFFFFu); return; }
    x.ret(cellX < 15 ? 1 : 0);
}
SwapReg reg_AreaOfCell({"libfield.AreaOfCell", MODULE, offsetOf(0x091C8E04), swap_AreaOfCell});

}  // namespace
