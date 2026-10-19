/* libfield: the calls into the engine (ehsys) that the field module makes besides the ones in ../common/ehsys.h. Names are by what the call does (from how it is
 * used); "(guess)" marks the ones that are inferred. Floats are passed in the PSP's float registers: the first in f12, the next in f13 ...; results come in f0. */
#pragma once
#include "../common/ehsys.h"
#include "field_data.h"

namespace field {
namespace engine {

namespace nid {
/* matrices and 3D state */
constexpr uint32_t MatrixMode = 0x4F22C9AA;   /* (guess) (0 projection, 1 view, 2 world) */
constexpr uint32_t PushMatrix = 0xF2BD0694;   /* (guess) */
constexpr uint32_t PopMatrix = 0xF8CDBCAB;    /* (guess) */
constexpr uint32_t LoadIdentity = 0x9EFDC9B9; /* (guess) */
constexpr uint32_t Ortho = 0x8EA5CCE1;        /* (guess) (left, right, bottom, top, near, far) in f12..f17 */
constexpr uint32_t Translate = 0x0089B865;    /* (guess) (vector) */
constexpr uint32_t TranslateB = 0x42C464D5;   /* (guess) (vector) */
constexpr uint32_t StencilFunc = 0x578BC0A3;  /* (guess) (list, function, reference, mask) */
constexpr uint32_t StencilOp = 0xF566A6A1;    /* (guess) (list, fail, depth fail, pass) */
/* vectors (pointers to three floats) */
constexpr uint32_t VectorScale = 0xBA5DC138;  /* (factor in f12, out, in) */
constexpr uint32_t VectorSub = 0xE71B4A22;    /* (out, a, b): out = a - b */
constexpr uint32_t VectorDot = 0x12DA0DE2;    /* (a, b): result in f0 */
constexpr uint32_t VectorCross = 0x44AF2C28;  /* (out, a, b) */
constexpr uint32_t VectorAdd = 0xD3890ECB;    /* (guess) (out, a, b): out = a + b */
constexpr uint32_t VectorLerp = 0x64CE5F24;   /* (guess) (t in f12, out, from, to) */
constexpr uint32_t Abs = 0x317716E5;          /* (guess) (value in f12): the absolute value in f0 */
constexpr uint32_t CreatePool = 0x291D6262;   /* (guess) (memory, bytes, 0): a pool of blocks over a memory area; returns its handle */
constexpr uint32_t ClearScreenList = 0x459B889D;   /* (guess) (list) returns the list */
constexpr uint32_t SetAllocator = 0xEBD1986B;      /* (guess) (alloc function, free function): the engine allocates through the module */
constexpr uint32_t SetEngineHeap = 0xB00C5619;     /* (guess) (heap) */
constexpr uint32_t CharacterStat = 0xF8E2482B;   /* (guess) (kind 0..2, character): a number of the character */
constexpr uint32_t Lerp2D = 0x006C8EE8;          /* (guess) (t in f12, out, from, to): two floats each */
constexpr uint32_t Vec2Add = 0xAA6E3AEA;         /* (guess) (out, a, b) two floats */
constexpr uint32_t MakeVector = 0x295BF133;      /* (guess) (x, y, z in f12..f14, out) */
constexpr uint32_t MatrixIdentity = 0x29D1B9D5;  /* (guess) (matrix) */
constexpr uint32_t MatrixRotate = 0x73918516;    /* (guess) (angle in f12, out, in) */
constexpr uint32_t TransformPoint = 0x89EE81BB;  /* (guess) (out, matrix, point) */
constexpr uint32_t DrawWindowSet = 0x14BFFB01;   /* (guess) (address of the cursor, window set) draws the set */
constexpr uint32_t PlayerDp = 0x02D2A1D5;      /* (index) the player's Duel Points */
constexpr uint32_t Power = 0x424598D9;         /* (guess) (base in f12, exponent in f13): the power in f0 */
constexpr uint32_t Round = 0x0A0CC3A4;         /* (guess) (value in f12): the value rounded to a whole number, in f0 */
constexpr uint32_t SetDrawOffset = 0xB56890DA;   /* (guess) (list, x, y) */
constexpr uint32_t SetViewport = 0x0939B6BE;     /* (guess) (list, centre x, centre y, width, height) */
constexpr uint32_t SetDepthRange = 0x758DE09D;   /* (guess) (list, near, far) */
constexpr uint32_t SetFrontFace = 0xA36635DA;    /* (guess) (list, mode) */
constexpr uint32_t SetShadeMode = 0x5C1AEE8E;    /* (guess) (list, mode) */
constexpr uint32_t SetFog = 0x917CC4D5;          /* (guess) (near in f12, far in f13, list, colour) */
constexpr uint32_t LookAt = 0xE2A5CE8D;          /* (guess) (eye, target, up) */
constexpr uint32_t UpdateViewMatrix = 0xD1C6F209;/* (guess) () */
constexpr uint32_t CurrentMatrix = 0x53636C1D;   /* () the address of the current matrix; its +0x30 is the translation row */
constexpr uint32_t VectorAddB = 0x15BE7861;      /* (guess) (out, a, b): four floats */
constexpr uint32_t SetLight = 0x1C2A8EDD;        /* (guess) (list, index, light block) */
constexpr uint32_t SetAmbientColour = 0xD6E0AA6A;/* (guess) (list, ABGR) */
constexpr uint32_t EnableLights = 0xD71FA940;    /* (guess) (list) */
constexpr uint32_t FinishLights = 0x94982653;    /* (guess) (list, 0) */
constexpr uint32_t Cosine = 0x7D880441;          /* (guess) (angle in f12): in f0 */
constexpr uint32_t Normalize2D = 0x803FC64B;     /* (guess) (out, in): two floats */
constexpr uint32_t Sub2D = 0x8B52B60D;           /* (guess) (out, a, b): two floats, a - b */
constexpr uint32_t Dot2D = 0xBB54BCFE;           /* (guess) (a, b): the dot product in f0 */
constexpr uint32_t Sqrt = 0x8D0CE141;            /* (guess) (value in f12): in f0 */
constexpr uint32_t RestartSprite = 0x42144DB8;   /* (guess) (sprite, frame): starts the sprite's animation again */
constexpr uint32_t AttachTextureFile = 0xFDAFCF3A;/* (guess) (sprite, texture file) */
constexpr uint32_t Normalize3 = 0x767DE7AB;      /* (out, in): a vector of three floats scaled to length 1 */
constexpr uint32_t VectorZero = 0xC0028BAA;      /* (guess) (out): the call takes the vector and has no effect we can see in the module; kept as the original makes it */
constexpr uint32_t MatrixRotateAxis = 0xD63C9E95;/* (guess) (angle in f12, out, in) */
constexpr uint32_t AlphaFunc = 0xC8DF61CD;       /* (guess) (list, function, reference, mask) */
constexpr uint32_t PixelMask = 0x942E589C;       /* (guess) (list, mask) */
constexpr uint32_t SetUvOffset = 0x7B1755CF;     /* (guess) (u in f12, v in f13, list) */
constexpr uint32_t SetUvScale = 0x3E24ACE1;      /* (guess) (u in f12, v in f13, list) */
constexpr uint32_t ApplyTextureState = 0x02C26DF1;/* (guess) (list) */
constexpr uint32_t UploadMatrix = 0x5B05F797;    /* (guess) (list) writes the matrix chosen with MatrixMode */
constexpr uint32_t Copy4 = 0x1FE543F1;           /* (out, in): four floats */
constexpr uint32_t Multiply4 = 0xF16047DB;       /* (guess) (out, a, b): four floats each (a rotation as a quaternion) */
constexpr uint32_t Matrix3Identity = 0x13A603EB; /* (guess) (matrix) */
constexpr uint32_t Matrix3RotateB = 0x8B0A8B96;  /* (guess) (angle in f12, out, in) */
constexpr uint32_t Matrix3Multiply = 0xD697947E; /* (guess) (out, a, b) */
constexpr uint32_t LoadPanelTexture = 0x3765ED01;/* (guess) (sprite, 0): the texture handle of a system sprite */
constexpr uint32_t RegisterPanel = 0x1CC5878C;   /* (guess) (sprite table) */
constexpr uint32_t PlayAmbientSound = 0xBF62545B;/* (guess) (sound, volume 0..127, 0) */
/* models and scenes */
constexpr uint32_t UpdateModel = 0x9F25AE2F;  /* (guess) (model, 0) */
constexpr uint32_t SetModelCallback = 0x9490E557;   /* (guess) (model, slot, function) returns the previous function */
constexpr uint32_t SetModelAnimation = 0xBB22B13B;  /* (guess) (model, block) */
}  // namespace nid

inline float resultFloat(SwapCtx &x) { return x.c->f[0].f; }

}  // namespace engine
}  // namespace field
