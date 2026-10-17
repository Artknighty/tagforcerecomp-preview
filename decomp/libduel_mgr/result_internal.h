/* Shared by the .cpp files of the Duel Result screen. */
#pragma once
#include "result_screen.h"

namespace duel_mgr {

constexpr uint32_t kWhite = 0xffffffff;
constexpr int32_t kScreenWidth = 0x1e0;   /* the screen is 480 pixels wide; text boxes run to its right edge */

/* The common start of a draw from the table sheet: bind it, white, nearest filtering. */
inline uint32_t BindSheet(SwapCtx &x, uint32_t cur, uint32_t sheet) {
    uint32_t u = ehsys::BindTexture(x, cur, sheet);
    u = ehsys::SetColor(x, u, kWhite);
    return ehsys::SetTextureFilter(x, u, 0, 0);
}

}  // namespace duel_mgr
