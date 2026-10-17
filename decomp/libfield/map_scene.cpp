/* libfield: the map scene: the terrain with its models, the people standing on it, the player's area and the height of the ground. */
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
namespace gu = ehsys;

constexpr int32_t kWalkCycleFrames = 6, kSpecialWalkerKind = 0x3A;
constexpr float kShadowScale = 0.2f;
constexpr float kCellSize = 0.5f;
constexpr int32_t kPlainMap = 0x25, kMainMap = 0xC, kShopMap = 2, kHarbourMap = 3;   /* map ids the code treats apart */
constexpr float kNearDistance = 0.75f;
constexpr int32_t kNightHour = 0x16;   /* from this hour on some people are gone */

/* ---- FUN_091c59ec(object): advances the walk cycle and puts the object's matrix and shadow offset in place ---- */
void swap_AnimateObject(SwapCtx &x) {
    const uint32_t address = x.arg(0);
    SceneObject &object = *view<SceneObject>(x, address);
    const uint32_t period = object.slow == 0 ? 3 : 2;
    if (object.mode == 1) {
        if (fieldState(x).frame % period == 0) {
            object.frame = object.frame + 1;
            object.frame = object.frame % kWalkCycleFrames;
        }
    } else if (object.mode == 0) {
        if (object.kind == kSpecialWalkerKind) {
            if (fieldState(x).frame % 5 == 0) object.frame = object.frame + 1;
        } else {
            object.frame = 0;
        }
    }
    x.callLocal(fn::UpdateObjectMatrix, address + offsetof(SceneObject, position), address + offsetof(SceneObject, matrix));
    GuestStack scratch(x, 16);
    x.c->f[12].f = kShadowScale;
    x.callNid(engine::nid::VectorScale, scratch.addr(), kCameraVector);
    const Vec3 shadow = *(Vec3 *)x.mem(scratch.addr());
    datum<float>(x, kShadowOffset + 12) = 0;
    datum<Vec3>(x, kShadowOffset) = shadow;
    if (object.shadow != 0) x.callLocal(fn::DrawShadow);
}
SwapReg reg_AnimateObject({"libfield.AnimateObject", MODULE, offsetOf(0x091C59EC), swap_AnimateObject});

/* ---- FUN_091c75c4: clears the map scene ---- */
void swap_ResetMapScene(SwapCtx &x) {
    memset(&mapScene(x), 0, sizeof(MapScene));
    x.ret(1);
}
SwapReg reg_ResetMapScene({"libfield.ResetMapScene", MODULE, offsetOf(0x091C75C4), swap_ResetMapScene});

/* ---- FUN_091c8e74: decides which people on the map are shown: one that is near the player and allowed appears (with a sound), one that is far leaves ---- */
void swap_UpdateMapNpcs(SwapCtx &x) {
    MapScene &scene = mapScene(x);
    const uint32_t player = playerObject(x);
    const FieldState &state = fieldState(x);
    if (player == 0 || state.flag10 == 0 || scene.npcCount <= 0) return;
    GuestStack scratch(x, 32);
    const uint32_t offsetVector = scratch.addr(), relative = scratch.addr() + 16;
    for (int32_t i = 0; i < scene.npcCount; i++) {
        NpcSlot &slot = scene.npcs[i];
        MapObject &object = *view<MapObject>(x, slot.object);
        Vec3 *local = (Vec3 *)x.mem(offsetVector);
        local->x = object.position.x + scene.origin.x * -1.0f;
        local->y = object.position.y + scene.origin.y * -1.0f;
        local->z = object.position.z + scene.origin.z * -1.0f;
        bool allowed = true;
        x.callNid(engine::nid::VectorSub, relative, player + offsetof(SceneObject, position), offsetVector);
        x.callLocal(fn::VectorLength, relative);
        const bool far = kNearDistance < engine::resultFloat(x);
        const int32_t mapId = datum<int32_t>(x, kMapScene + offsetof(MapScene, mapId));
        if (mapId == kPlainMap || mapId == kMainMap) {
            const int32_t hour = *view<int32_t>(x, fieldArguments(x).hour);
            if (hour >= kNightHour) {
                const int32_t type = object.type;
                if (type == 0x26) allowed = false;
                else if (type >= 0xD && type <= 0x13) allowed = false;
            }
        } else if (mapId == kShopMap) {
            if (state.flag14 != 0) allowed = false;
        }
        if (slot.state == kNpcLeaving || slot.state == kNpcShown) {
            if (!far && allowed) { slot.state = kNpcAppearing; x.callLocal(fn::PlayNpcSound, 0, object.loaded); }
        } else if (slot.state == kNpcAppearing) {
            if (far) { slot.state = kNpcLeaving; if (object.loaded == 0) x.callLocal(fn::PlayNpcSound, 1, 0); }
        } else if (far) {
            slot.state = kNpcLeaving;
            if (object.loaded == 0) x.callLocal(fn::PlayNpcSound, 1, 0);
        }
        MapObject &after = *view<MapObject>(x, slot.object);
        if (after.loaded == 1) x.callLocal(fn::DrawNpcLoaded, guest(x, kMapScene) + offsetof(MapScene, npcs) + i * sizeof(NpcSlot));
        else if (after.loaded == 0) x.callLocal(fn::DrawNpcPlain, guest(x, kMapScene) + offsetof(MapScene, npcs) + i * sizeof(NpcSlot));
    }
}
SwapReg reg_UpdateMapNpcs({"libfield.UpdateMapNpcs", MODULE, offsetOf(0x091C8E74), swap_UpdateMapNpcs});


/* ---- FUN_091c7de0 / FUN_091c8808: the ground's layout and the id of the map that is shown ---- */
void swap_MapLayout(SwapCtx &x) { x.ret(mapScene(x).layout); }
SwapReg reg_MapLayout({"libfield.MapLayout", MODULE, offsetOf(0x091C7DE0), swap_MapLayout});
void swap_MapId(SwapCtx &x) { x.ret((uint32_t)mapScene(x).mapId); }
SwapReg reg_MapId({"libfield.MapId", MODULE, offsetOf(0x091C8808), swap_MapId});
void swap_Nothing(SwapCtx &) {}
SwapReg reg_Nothing({"libfield.NothingA", MODULE, offsetOf(0x091C9FF8), swap_Nothing});

/* ---- FUN_091c76ec: one frame of the map scene: the models, the area the player stands in (a change starts a 30 frame transition), the people ---- */
constexpr int32_t kTransitionFrames = 0x1E;
void swap_UpdateMapScene(SwapCtx &x) {
    MapScene &scene = mapScene(x);
    for (int32_t i = 0; i < scene.modelCount; i++) {
        if (scene.models[i] != 0) x.callNid(engine::nid::UpdateModel, scene.models[i], 0);
    }
    const uint32_t player = playerObject(x);
    if (player != 0) {
        GuestStack cell(x, 8);
        const uint32_t cellX = cell.addr(), cellZ = cell.addr() + 4;
        x.c->f[12].f = view<SceneObject>(x, player)->position.x;
        x.c->f[13].f = view<SceneObject>(x, player)->position.z;
        x.callLocal(fn::WorldToCell, cellX, cellZ);
        const int32_t area = (int32_t)x.callLocal(fn::AreaOfCell, *(uint32_t *)x.mem(cellX), *(uint32_t *)x.mem(cellZ));
        if (scene.area != area) {
            scene.previousArea = scene.area;
            scene.transitionFrame = 0;
            scene.transitioning = 1;
            scene.area = area;
        }
    }
    x.callLocal(fn::UpdateMapNpcs);
    if (scene.transitioning == 1) {
        if (scene.transitionFrame < kTransitionFrames) scene.transitionFrame++;
        else scene.transitioning = 0;
    }
}
SwapReg reg_UpdateMapScene({"libfield.UpdateMapScene", MODULE, offsetOf(0x091C76EC), swap_UpdateMapScene});

/* ---- FUN_091c81dc(x, z in f12/f13, vector): sets the vector's height (y) so that it lies on the ground triangle under (x, z) ---- */
void swap_GroundHeight(SwapCtx &x) {
    const float px = x.c->f[12].f, pz = x.c->f[13].f;
    Vec3 *point = view<Vec3>(x, x.arg(0));
    MapScene &scene = mapScene(x);
    if (scene.layout == 0) return;
    const uint16_t *layout = view<uint16_t>(x, scene.layout);
    const uint32_t cellsAddress = x.callLocal(fn::GroundCells);
    int32_t column = truncate(px / kCellSize), row = truncate(pz / kCellSize);
    const int32_t mapId = scene.mapId;
    if (mapId == kPlainMap || mapId == kMainMap) {
        if (column == 0xF && row == 0x18) column = 0x10;
        else if ((column == 0xF && row == 0x14) || (column == 0xF && row == 0x10) || (column == 0xF && row == 0xC)) column = column + 1;
    }
    const uint32_t columns = layout[1];
    const int32_t index = column + row * (int32_t)columns;
    if (index < 0 || index >= (int32_t)layout[0]) return;
    auto cell = [&](int32_t c, int32_t r) { return view<Vec3>(x, cellsAddress + (uint32_t)(c + r * (int32_t)columns) * 12); };
    const Vec3 *base = view<Vec3>(x, cellsAddress + (uint32_t)index * 12);
    Vec3 a = *base, b, c;
    if (point->x - base->x < point->z - base->z) { b = *cell(column, row + 1); c = *cell(column + 1, row + 1); }
    else { b = *cell(column + 1, row + 1); c = *cell(column + 1, row); }
    GuestStack scratch(x, 96);
    const uint32_t vectorA = scratch.addr(), vectorB = vectorA + 16, edge1 = vectorB + 16, edge2 = edge1 + 16, normal = edge2 + 16;
    *(Vec3 *)x.mem(vectorA) = a; *(Vec3 *)x.mem(vectorB) = b;
    Vec3 *third = (Vec3 *)x.mem(normal + 16); *third = c;
    x.callNid(engine::nid::VectorSub, edge1, vectorB, vectorA);
    x.callNid(engine::nid::VectorSub, edge2, normal + 16, vectorA);
    x.callNid(engine::nid::VectorCross, normal, edge1, edge2);
    x.callNid(engine::nid::VectorDot, normal, vectorA);
    const float plane = engine::resultFloat(x);
    const Vec3 &n = *(Vec3 *)x.mem(normal);
    point->y = ((plane - point->x * n.x) - point->z * n.z) / n.y;
}
SwapReg reg_GroundHeight({"libfield.GroundHeight", MODULE, offsetOf(0x091C81DC), swap_GroundHeight});

/* ---- FUN_091c77e8: draws the map scene: sets the drawing state, puts the world in the map's coordinates, draws the terrain of the area (and, while the area
 * changes, the one that is being left), then the markers of the places ---- */
constexpr uint32_t kHarbourMarker = 0x091FBBE0, kHarbourMarkerW = 0x091FBBFC, kCellMarkerW = 0x091FBC0C;   /* a position (x, y, z) and the fourth component of the markers */
void drawTerrainState(SwapCtx &x) {
    uint32_t list = gu::BeginList(x, 1, 0);
    list = gu::SetClearDepth(x, list, 0);
    list = gu::Clear(x, list, 2);
    list = gu::EnableState(x, list, gu::kStencilTest);
    list = gu::EnableState(x, list, gu::kDepthTest);
    list = gu::SetBlend(x, list, 0, 2, 3, 0, 0);
    list = gu::EnableState(x, list, gu::kBlend);
    list = gu::AlphaFunc(x, list, 6, 0, 0xFF);
    list = gu::EnableState(x, list, gu::kAlphaTest);
    list = gu::SetTextureFilter(x, list, 1, 1);
    list = x.callNid(engine::nid::StencilFunc, list, 1, 2, 0xFF);
    list = x.callNid(engine::nid::StencilOp, list, 0, 0, 2);
    gu::EndList(x, list);
    list = gu::BeginList(x, 2, 0);
    list = gu::EnableState(x, list, gu::kDepthTest);
    list = gu::SetBlend(x, list, 0, 2, 3, 0, 0);
    list = gu::EnableState(x, list, gu::kBlend);
    list = gu::AlphaFunc(x, list, 6, 0, 0xFF);
    list = gu::EnableState(x, list, gu::kAlphaTest);
    list = x.callNid(engine::nid::StencilFunc, list, 1, 0, 0xFF);
    gu::EndList(x, list);
}
void swap_DrawMapScene(SwapCtx &x) {
    MapScene &scene = mapScene(x);
    if (scene.terrain == 0 || *view<uint32_t>(x, scene.terrain) == 0) return;
    drawTerrainState(x);
    GuestStack frame(x, 0x100);
    uint32_t *block = (uint32_t *)x.mem(frame.addr());               /* 0x34 bytes handed to the model animation call */
    memset(block, 0, 0x34);
    block[0] = 1;
    block[1] = 2;
    const uint32_t origin = frame.addr() + 0x40;                     /* the origin, then its opposite */
    *(Vec3 *)x.mem(origin) = scene.origin;
    x.callNid(engine::nid::MatrixMode, 2);
    x.callNid(engine::nid::PushMatrix);
    x.callNid(engine::nid::LoadIdentity);
    x.c->f[12].f = -1.0f;
    x.callNid(engine::nid::VectorScale, origin, origin);
    x.callNid(engine::nid::Translate, origin);
    if (scene.transitioning == 1 || scene.transitioning == 0) {
        x.callLocal(fn::TerrainStepA);
        x.callLocal(fn::TerrainStepB);
        x.callLocal(fn::TerrainStepC);
        uint32_t list = gu::BeginList(x, 1, 0);
        list = x.callLocal(fn::TerrainState, list);
        gu::EndList(x, list);
        if (scene.transitioning == 1) {
            x.callLocal(fn::TerrainStepD);
            block[0] = 1;
            block[1] = 1;
            const uint32_t model = scene.models[scene.previousArea];
            const uint32_t previous = x.callNid(engine::nid::SetModelCallback, model, 1, guest(x, 0x091C9C30));
            x.callNid(engine::nid::SetModelAnimation, model, frame.addr());
            x.callNid(engine::nid::SetModelCallback, model, 1, previous);
            x.callLocal(fn::TerrainStepE);
        }
    }
    x.callNid(engine::nid::MatrixMode, 2);
    x.callNid(engine::nid::PopMatrix);
    uint32_t list = gu::BeginList(x, 1, 0);
    list = x.callLocal(fn::CameraStateList, list);
    list = x.callLocal(fn::MarkerState, list);
    const int32_t mapId = scene.mapId;
    const uint32_t marker = frame.addr() + 0x20, markerCell = frame.addr() + 0x30;
    if (mapId == kHarbourMap) {
        const uint32_t point = frame.addr() + 0x50;
        *(Vec3 *)x.mem(point) = {asFloat(datum<uint32_t>(x, kHarbourMarker)), asFloat(datum<uint32_t>(x, kHarbourMarker + 4)), asFloat(datum<uint32_t>(x, kHarbourMarker + 8))};
        x.c->f[12].f = asFloat(datum<uint32_t>(x, kHarbourMarker));
        x.c->f[13].f = asFloat(datum<uint32_t>(x, kHarbourMarker + 8));
        x.callLocal(fn::GroundHeight, point);
        uint32_t *vector = (uint32_t *)x.mem(marker);
        const uint32_t *source = (const uint32_t *)x.mem(point);
        vector[0] = source[0]; vector[1] = source[1]; vector[2] = source[2];
        vector[3] = datum<uint32_t>(x, kHarbourMarkerW);
        list = x.callLocal(fn::DrawMarker, list, marker);
    } else {
        for (int32_t i = 0; i < scene.cellCount; i++) {
            const int16_t *cell = view<int16_t>(x, scene.cells + i * 4);
            x.callLocal(fn::CellToWorld, markerCell, (uint32_t)(int32_t)cell[0], (uint32_t)(int32_t)cell[1]);
            uint32_t *vector = (uint32_t *)x.mem(marker);
            const uint32_t *source = (const uint32_t *)x.mem(markerCell);
            vector[0] = source[0]; vector[1] = source[1]; vector[2] = source[2];
            vector[3] = datum<uint32_t>(x, kCellMarkerW);
            list = x.callLocal(fn::DrawMarker, list, marker);
        }
    }
    list = x.callLocal(fn::MarkerEnd, list);
    gu::EndList(x, list);
    x.callLocal(fn::MapSceneFinish, (uint32_t)scene.mapId);
}
SwapReg reg_DrawMapScene({"libfield.DrawMapScene", MODULE, offsetOf(0x091C77E8), swap_DrawMapScene});

}  // namespace
