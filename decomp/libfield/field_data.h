/* libfield: the story's field module: the map the player walks on, its places, the clock / day / DP display, and the 14-place world map menu. The module
 * image loads at 0x091C4600 (one fixed base); its data (globals, tables, the .bss) has fixed addresses, listed here as absolute guest addresses. The
 * story (libstory) drives it by "handler" number (`Invoke`, `HandlerDone`) and calls `Start`, `FrameUpdate` and `FrameDraw` every frame. */
#pragma once
#include "../common/swap.h"
#include <stdint.h>

namespace field {

constexpr uint32_t kModuleBase = 0x091C4600;
constexpr uint32_t offsetOf(uint32_t guestAddress) { return guestAddress - kModuleBase; }
inline uint32_t guest(SwapCtx &x, uint32_t address) { return x.addr(offsetOf(address)); }                 /* the address of a module datum in this run */
template <class T> T &datum(SwapCtx &x, uint32_t address) { return x.at<T>(guest(x, address)); }
template <class T> T *view(SwapCtx &x, uint32_t guestAddress) { return guestAddress ? (T *)x.mem(guestAddress) : nullptr; }   /* a typed view of the block at a guest pointer */

/* The game's conversions: float -> int truncates (and saturates like the PSP's FPU). */
inline int32_t truncate(float v) { return f2i_(v, 1); }
inline float asFloat(uint32_t bits) { union { uint32_t u; float f; } c; c.u = bits; return c.f; }

struct Vec3 { float x, y, z; };
static_assert(sizeof(Vec3) == 12, "Vec3 layout");

/* ---- the three indicators (arrows / markers that blink next to the places; FUN_091c4b34) ----
 * A header of flags and three 0x100-byte objects. An indicator fades in, stays, fades out; a sprite blinks at random times while it is shown. */
constexpr uint32_t kIndicatorHeader = 0x09205E80, kIndicatorObjects = 0x09205EB0;
constexpr int32_t kIndicatorCount = 3;
struct IndicatorHeader {
    uint32_t unknown00[2];
    uint32_t anyFadeInRequest;      /* +0x08 cleared when no slot asks for a fade in */
    uint32_t unknown0C;
    uint32_t anyFadeOutRequest;     /* +0x10 cleared when no slot asks for a fade out */
    uint32_t fadeInRequest[3];      /* +0x14 per slot, cleared when its fade in is over */
    uint32_t fadeEnabled;           /* +0x20 the fades only run while this is set */
    uint32_t fadeOutRequest[3];     /* +0x24 per slot, cleared when its fade out is over */
};
static_assert(sizeof(IndicatorHeader) == 0x30, "IndicatorHeader layout");
enum IndicatorState : int32_t { kIndicatorIdle = 0, kIndicatorFadeIn = 1, kIndicatorShown = 2, kIndicatorFadeOut = 3 };
struct Indicator {
    uint32_t resource;              /* +0x00 pointer to the resource the indicator draws (its first word is nonzero while it is loaded) */
    uint32_t block;                 /* +0x04 the heap block that holds the resource; freed when the indicator is cleared */
    uint32_t sprite;                /* +0x08 sprite resource */
    uint32_t unknown0C;
    int32_t frame;                  /* +0x10 animation frame of the sprite */
    uint32_t unknown14[18];
    int32_t alpha;                  /* +0x5C 0..255 while fading */
    uint32_t unknown60[4];
    int32_t variant;                /* +0x70 which sprite record the indicator uses (with `kind`) */
    int32_t blinking;               /* +0x74 0 = waiting for the next blink, 1 = the blink animation runs */
    int32_t blinkWait;              /* +0x78 frames left until the next blink */
    uint32_t sprite2;               /* +0x7C second sprite, drawn behind the first */
    int32_t frame2;                 /* +0x80 its animation frame */
    uint32_t unknown84[18];
    int32_t alpha2;                 /* +0xCC follows `alpha` */
    uint32_t unknownD0[4];
    int32_t animate2;               /* +0xE0 the second sprite's frame counts up while this is set */
    int32_t active;                 /* +0xE4 */
    int32_t state;                  /* +0xE8 IndicatorState */
    int32_t kind;                   /* +0xEC */
    uint32_t unknownF0[4];
};
static_assert(sizeof(Indicator) == 0x100, "Indicator layout");
inline Indicator *indicators(SwapCtx &x) { return (Indicator *)x.mem(guest(x, kIndicatorObjects)); }
inline IndicatorHeader &indicatorHeader(SwapCtx &x) { return datum<IndicatorHeader>(x, kIndicatorHeader); }

/* ---- the field's state (FUN_091cd50c returns its address) ---- */
constexpr uint32_t kFieldState = 0x09992C68;
struct FieldState {
    uint32_t frame;          /* +0x00 frames since the field started (FrameUpdate counts it) */
    int32_t mapIdle;         /* +0x04 1 while the map waits for the player (handler state 1) */
    int32_t selected;        /* +0x08 the place the cursor is on; -1 when none */
    uint32_t flag0C;         /* +0x0C */
    uint32_t flag10;         /* +0x10 */
    uint32_t flag14;         /* +0x14 */
};
static_assert(sizeof(FieldState) == 0x18, "FieldState layout");
inline FieldState &fieldState(SwapCtx &x) { return datum<FieldState>(x, kFieldState); }

/* ---- the module's heap and the lazily loaded sprites used by the field UI ---- */
constexpr uint32_t kFieldHeapHandle = 0x09992900;
constexpr uint32_t kSystemSpriteArchive = 0x091FD60C;
constexpr uint32_t kSystemSpriteRecords = 0x091FDCA8;
struct SystemSpriteRecord {
    uint32_t name;       /* +0x00 resource name in the archive */
    uint32_t texture;    /* +0x04 cached texture handle */
    uint32_t unknown08;
};
static_assert(sizeof(SystemSpriteRecord) == 0x0C, "SystemSpriteRecord layout");

/* ---- the map scene (FUN_091c75c4 clears it): the terrain, the models of the areas, the people standing on the map ---- */
constexpr uint32_t kMapScene = 0x092061E0;
constexpr int32_t kMapModelSlots = 8, kMapNpcSlots = 10;
enum NpcState : int32_t { kNpcHidden = 0, kNpcAppearing = 1, kNpcShown = 2, kNpcLeaving = 3 };
/* a person on the map: the slot points to the object that describes them and keeps whether they are shown */
struct NpcSlot {
    uint32_t object;          /* +0x00 -> MapObject */
    uint32_t unknown04[19];
    int32_t state;            /* +0x50 NpcState */
    uint32_t unknown54[3];
};
static_assert(sizeof(NpcSlot) == 0x60, "NpcSlot layout");
struct MapObject {
    uint32_t unknown00;
    int32_t loaded;           /* +0x04 0 or 1: selects how the slot is drawn */
    uint32_t unknown08;
    Vec3 position;            /* +0x0C */
    uint32_t unknown18[4];
    int32_t type;             /* +0x28 */
};
struct MapScene {
    uint32_t unknown00;
    uint32_t unknown04;
    uint32_t terrain;         /* +0x08 -> the terrain resource (its first word is nonzero while it is loaded) */
    int32_t mapId;            /* +0x0C which map is shown (FUN_091c8808) */
    uint32_t models[kMapModelSlots];   /* +0x10 one model per area */
    NpcSlot npcs[kMapNpcSlots];        /* +0x30 */
    int32_t modelCount;       /* +0x3F0 */
    int32_t area;             /* +0x3F4 the area the player is in */
    int32_t previousArea;     /* +0x3F8 */
    uint32_t unknown3FC;
    int32_t npcCount;         /* +0x400 */
    Vec3 origin;              /* +0x404 the map's origin (the scene is drawn translated by its opposite) */
    int32_t transitioning;    /* +0x410 1 while the change of area fades */
    int32_t transitionFrame;  /* +0x414 */
    uint32_t layout;          /* +0x418 -> {u16 cellCount, u16 columns, ...} */
    uint32_t cells;           /* +0x41C -> array of {i16 x, i16 y} */
    int32_t cellCount;        /* +0x420 */
    uint32_t unknown424[3];
};
static_assert(sizeof(MapScene) == 0x430, "MapScene layout");
inline MapScene &mapScene(SwapCtx &x) { return datum<MapScene>(x, kMapScene); }

/* Header shared by the map's ground vertices and its byte-per-cell walkability grid. */
struct MapLayout {
    uint16_t cellCount, columns;
    uint16_t unknown04, unknown06;
    uint16_t width, height;
    uint32_t groundVerticesOffset;  /* +0x0C, relative to this header */
    uint32_t walkabilityOffset;     /* +0x10, relative to this header */
};
static_assert(sizeof(MapLayout) == 0x14, "MapLayout layout");

/* the player's object (FUN_091cb770 gives its address while the player exists) */
constexpr uint32_t kPlayerObject = 0x0920B640, kPlayerExists = 0x0920B6D0;
inline uint32_t playerObject(SwapCtx &x) { return datum<uint32_t>(x, kPlayerExists) == 0 ? 0 : guest(x, kPlayerObject); }

/* the part of a drawn character (the player, a walker, a spot on the map) that the field code uses */
struct SceneObject {
    int32_t id;               /* +0x00 */
    int32_t kind;             /* +0x04 which character (0x3A is the one that has its own walk cycle) */
    uint32_t unknown08[3];
    Vec3 position;            /* +0x14 */
    int32_t mode;             /* +0x20 0 = standing, 1 = walking */
    int32_t direction;        /* +0x24 the way it faces (1, 3, 5, 7) */
    int32_t frame;            /* +0x28 animation frame of the walk cycle */
    Vec3 velocity;            /* +0x2C how far the object moves this frame */
    uint32_t unknown38[2];
    float matrix[16];         /* +0x40 */
    int32_t present;          /* +0x80 the object exists */
    uint32_t shadow;           /* +0x84 handle of the shadow (0 = none) */
    int32_t slow;             /* +0x88 the walk cycle runs slower while this is 0 (every 3rd frame) than when set (every 2nd) */
    uint32_t unknown8C;
    int32_t ready;            /* +0x90 its resource is loaded */
    uint32_t control;           /* +0x94 (the player: nonzero when it may move) */
    uint32_t resource;        /* +0x98 -> resource (its first word is nonzero while it is loaded) */
};
static_assert(sizeof(SceneObject) == 0x9C, "SceneObject layout");
/* a person that wanders on the map inside a radius around their home cell (cells are 0.5 units wide) */
struct Walker {
    SceneObject object;
    int32_t homeX, homeZ;     /* +0x9C the cell they wander around */
    int32_t radius;           /* +0xA4 how far (in cells); 0 = they stay */
    int32_t wait;             /* +0xA8 frames to stand until the next step */
    int32_t cellX, cellZ;     /* +0xAC the cell they are in */
    int32_t targetX, targetZ; /* +0xB4 the cell they walk to */
    float progress;           /* +0xBC 0..1 of the step */
    uint32_t unknownC0;
    int32_t paused;           /* +0xC4 */
    uint32_t unknownC8[8];
    uint32_t path;            /* +0xE8 -> list of {i32 x, i32 z, ...24 bytes} cells the walker also occupies */
    int32_t pathCount;        /* +0xEC */
};
static_assert(sizeof(Walker) == 0xF0, "Walker layout");
struct Spot { SceneObject object; uint32_t unknown9C; };   /* a fixed character on the map */
static_assert(sizeof(Spot) == 0xA0, "Spot layout");
constexpr uint32_t kWalkers = 0x09206620, kSpots = 0x09208420;
constexpr int32_t kWalkerCount = 32, kSpotCount = 80;
inline Walker *walkers(SwapCtx &x) { return (Walker *)x.mem(guest(x, kWalkers)); }
inline Spot *spots(SwapCtx &x) { return (Spot *)x.mem(guest(x, kSpots)); }
constexpr uint32_t kCameraVector = 0x09993B70, kShadowOffset = 0x092061D0;   /* the camera's position / the offset the shadows are drawn at (Vec3, then a zero word) */

/* ---- the block the story hands the field (libstory's FieldSetup builds it; FUN_091d173c copies it here) ---- */
constexpr uint32_t kFieldArguments = 0x0999331C;
struct FieldArguments {
    uint32_t month, day, weekday, hour, minute, daysLeft;   /* +0x00 guest pointers to the clock's six numbers (libstory's Date) */
    uint32_t playerValue30, playerValue84, playerValue2C;   /* +0x18 pointers to three of the player's values */
    uint32_t flag;                                          /* +0x24 */
    uint32_t callbacks[6];                                  /* +0x28 functions of the story the field calls back */
};
static_assert(sizeof(FieldArguments) == 0x40, "FieldArguments layout");
inline FieldArguments &fieldArguments(SwapCtx &x) { return datum<FieldArguments>(x, kFieldArguments); }

/* ---- the player's character (FUN_091cb274 clears it): the object, the two resources it needs and the footstep state ---- */
constexpr uint32_t kPlayer = 0x0920B630;
struct Player {
    uint32_t resourceA, resourceB;   /* +0x00 the player is drawn only while both are loaded */
    uint32_t unknown08[2];
    SceneObject object;              /* +0x10 (its position is the player's position; unknown08[0..1] are set while a move is under way) */
    uint32_t unknownAC;
    uint32_t unknownB0;
    int32_t onGround;                /* +0xB4 1 while the player stands on a surface that has its own shadow / sound (FUN_091cd574 says which) */
    int32_t lastStepFrame;           /* +0xB8 the walk frame whose footstep sound played last; -1 when not walking */
    uint32_t unknownBC;
};
static_assert(sizeof(Player) == 0xC0, "Player layout");
inline Player &player(SwapCtx &x) { return datum<Player>(x, kPlayer); }

/* ---- the beacons: two rows of eight markers (the places a character may stand on); each is an object with a position and a matrix ---- */
constexpr uint32_t kBeacons = 0x09992C90, kBeaconsB = 0x09992F90;   /* the first and the second row */
constexpr int32_t kBeaconCount = 8;
struct Beacon {
    int32_t active;           /* +0x00 */
    uint32_t unknown04;
    Vec3 position;            /* +0x08 */
    uint32_t unknown14[3];
    float matrix[16];         /* +0x20 */
};
static_assert(sizeof(Beacon) == 0x60, "Beacon layout");
constexpr uint32_t kBeaconResource = 0x09992C88;   /* -> resource; the beacons are drawn while its first word is nonzero */


/* ---- what the story's handlers have asked the field for and that is not finished yet ---- */
constexpr uint32_t kFieldFlags = 0x099932A4;
struct FieldFlags {
    uint32_t unknown00;
    int32_t worldMapPending;   /* +0x04 the place menu was opened and is not answered (cleared when it is) */
    int32_t bannerPending;     /* +0x08 a title banner is showing */
    uint32_t unknown0C;
    int32_t fadePending;       /* +0x10 a fade was started */
    int32_t dpPending;         /* +0x14 the DP counter is animating */
    uint32_t unknown18;
    int32_t started;           /* +0x1C a handler has been invoked: the field runs its frames */
    int32_t drawing;           /* +0x20 the field draws */
    int32_t mapOnly;           /* +0x24 the map is shown without the town menu (0 = with it) */
    int32_t fadeKind;          /* +0x28 0 = the fade ends the drawing, else it only fades */
    uint32_t unknown2C;
};
static_assert(sizeof(FieldFlags) == 0x30, "FieldFlags layout");
inline FieldFlags &fieldFlags(SwapCtx &x) { return datum<FieldFlags>(x, kFieldFlags); }

/* the story calls the field through a table of handlers: {start function, "is it done" function, active} per handler number */
constexpr uint32_t kHandlerTable = 0x091FDEC0;
constexpr int32_t kHandlerCount = 33;
struct HandlerEntry { uint32_t start, isDone; int32_t active; };
static_assert(sizeof(HandlerEntry) == 12, "HandlerEntry layout");
inline HandlerEntry *handlers(SwapCtx &x) { return (HandlerEntry *)x.mem(guest(x, kHandlerTable)); }


/* ---- the flow of the map (FUN_091d0e14): what the player is doing and what the story has been told. FUN_091d0d5c clears it (0xB8 bytes) ---- */
constexpr uint32_t kFlow = 0x099932D4;
struct FieldFlow {
    int32_t argumentsPending;     /* +0x00 the story handed new arguments and the system sprites are not loaded yet */
    int32_t playerArrivedPending; /* +0x04 cleared when the player's move has ended */
    int32_t followerPending;      /* +0x08 cleared when the follower has reached the player */
    int32_t active;               /* +0x0C the flow is running (0 = waiting for the story) */
    int32_t panelPending;         /* +0x10 cleared when the panel (FUN_091d3368) says it is over */
    int32_t listPending;          /* +0x14 cleared when the list (FUN_091d3434) says it is over */
    int32_t requestPending;       /* +0x18 the story asked for something and has not got it */
    int32_t dialogPending;        /* +0x1C cleared when the dialog (FUN_091e7534) is over */
    int32_t answer;               /* +0x20 what the field tells the story when the flow ends (2..0x14) */
    int32_t lastCharacter;        /* +0x24 the character the player was next to when the flow ended */
    int32_t argFC;                /* +0x28 */
    int32_t pick1, pick2;         /* +0x2C the two numbers the panel returns */
    uint32_t unknown34;
    uint32_t argB, argC;          /* +0x38 */
    int32_t lastTarget;           /* +0x40 */
    uint32_t systemArchive;       /* +0x44 -> the system sprites archive (set when it is loaded) */
    FieldArguments arguments;     /* +0x48 */
    int32_t step;                 /* +0x88 FlowStep */
    int32_t character;            /* +0x8C the character next to the player; -1 when none */
    uint32_t callArgA, callArgB, callArgC, callArgD, callArgE, callArgF;   /* +0x90 */
    int32_t townMenuOpen;         /* +0xA8 */
    uint32_t unknownAC, unknownB0;
    int32_t scrollSpeed;          /* +0xB4 7, or 0xF while the third player value is 0 */
};
static_assert(sizeof(FieldFlow) == 0xB8, "FieldFlow layout");
inline FieldFlow &flow(SwapCtx &x) { return datum<FieldFlow>(x, kFlow); }
inline uint32_t flowAddress(SwapCtx &x, uint32_t offset) { return guest(x, kFlow) + offset; }

}  // namespace field
