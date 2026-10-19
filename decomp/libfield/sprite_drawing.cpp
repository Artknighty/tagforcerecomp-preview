/* libfield: the field module's two small textured-quad helpers. */
#include "../common/swap.h"
#include "../common/ehsys.h"
#include "field_data.h"
#include "field_functions.h"

#define MODULE "libfield"

namespace {
using namespace field;

/* ---- FUN_091cd838(list, x, y, u, v, sourceWidth, sourceHeight, width, height) ---- */
void swap_DrawSpriteQuad(SwapCtx &x) {
    const uint32_t list = x.arg(0);
    const int32_t screenX = (int32_t)x.arg(1), screenY = (int32_t)x.arg(2);
    const int32_t sourceX = (int32_t)x.arg(3), sourceY = (int32_t)x.arg(4);
    const int32_t sourceWidth = (int32_t)x.arg(5), sourceHeight = (int32_t)x.arg(6);
    const int32_t width = (int32_t)x.arg(7);
    const int32_t height = (int32_t)*view<uint32_t>(x, x.c->r[29]);   /* ninth argument is on the caller's stack */

    const uint32_t sized = ehsys::SetSpriteSize(x, list, sourceWidth, sourceHeight, width, height);
    x.ret(ehsys::DrawSprite(x, sized, screenX, screenY, 0, sourceX, sourceY, 0, 0));
}
SwapReg reg_DrawSpriteQuad({"libfield.DrawSpriteQuad", MODULE, offsetOf(0x091CD838), swap_DrawSpriteQuad});

/* ---- FUN_091cd818(list, x, y, u, v, width, height): draws at the source's native size ---- */
void swap_DrawSpriteUnscaled(SwapCtx &x) {
    const uint32_t list = x.arg(0);
    const uint32_t screenX = x.arg(1), screenY = x.arg(2);
    const uint32_t sourceX = x.arg(3), sourceY = x.arg(4);
    const uint32_t width = x.arg(5), height = x.arg(6);

    /* The original wrapper duplicates width/height into the destination-size fields. */
    GuestStack stackArgument(x, 16);
    *(uint32_t *)x.mem(stackArgument.addr()) = height;
    x.ret(x.callLocal(fn::DrawSpriteQuad, list, screenX, screenY, sourceX, sourceY, width, height, width));
}
SwapReg reg_DrawSpriteUnscaled({"libfield.DrawSpriteUnscaled", MODULE, offsetOf(0x091CD818), swap_DrawSpriteUnscaled});

}  // namespace
