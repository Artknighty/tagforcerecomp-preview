/* Shared by the decompiled parts of BOOT (the engine library): where its functions and data are, and the numbers its drawing code uses. */
#pragma once
#include "../swap.h"
#include "../ehsys.h"

namespace boot {

constexpr uint32_t kBootBase = 0x08804000;
constexpr uint32_t off(uint32_t address) { return address - kBootBase; }

/* The BOOT functions the swaps replace or call (addresses relative to the module). */
namespace fn {
constexpr uint32_t DrawWindowItem = off(0x08842808);
constexpr uint32_t PlaceRect = off(0x0884368C);
constexpr uint32_t DrawWindows = off(0x088418B8);
constexpr uint32_t LayoutWindowItem = off(0x08841E9C);
constexpr uint32_t WindowTextBottom = off(0x08841B8C);
constexpr uint32_t FindWindowItem = off(0x08843984);
constexpr uint32_t WindowAnimationStep = off(0x0884A1FC);
constexpr uint32_t DrawWindowContent = off(0x08848A14);
constexpr uint32_t DrawCellBackground = off(0x08849F44);
constexpr uint32_t DrawSelectedCell = off(0x08849E40);
constexpr uint32_t DrawBoxSprites = off(0x08849FB8);
constexpr uint32_t DrawScrollArrow = off(0x0884A4A0);
constexpr uint32_t DrawWindowButton = off(0x08849B0C);
constexpr uint32_t BuildFrameMesh = off(0x08842C68);
constexpr uint32_t FrameSpriteCount = off(0x088435B4);
constexpr uint32_t PlaceSprite = off(0x08843808);
constexpr uint32_t StyleDiffers = off(0x088439CC);
constexpr uint32_t FadeColour = off(0x08843AA0);
constexpr uint32_t ConfirmSetting = off(0x08840384);
constexpr uint32_t UpdateWindow = off(0x088493CC);
constexpr uint32_t BeginClose = off(0x08849A94);
constexpr uint32_t ItemSettled = off(0x08841B20);
constexpr uint32_t StartItemClosing = off(0x08841AD4);
constexpr uint32_t ConfirmCancelMask = off(0x0884038C);
constexpr uint32_t SelectPreviousCell = off(0x0884A2C8);
constexpr uint32_t SelectNextCell = off(0x0884A3D0);
constexpr uint32_t MoveSelectionBack = off(0x0884A23C);
constexpr uint32_t MoveSelectionOn = off(0x0884A344);
}  // namespace fn

/* The frame's thickness, in the data of BOOT: how far the text sits from each edge of a window, and the same for the title strip. */
constexpr uint32_t kInsetTop = 0x08873276, kInsetLeft = 0x0887327C, kInsetRight = 0x08873284, kInsetBottom = 0x0887328E;
constexpr uint32_t kTitleInsetTop = 0x088732A6, kTitleInsetLeft = 0x088732AC, kTitleInsetRight = 0x088732B4;

/* the dialog buttons' labels: five strings per language, from this table (indexed by button, then by language) */
constexpr uint32_t kButtonLabels = 0x088ED39C;

}  // namespace boot
