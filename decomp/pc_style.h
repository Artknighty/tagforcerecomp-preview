/* The PC style's hooks into engine code that lives outside the Module that owns the art (docs/DECOMP.md, "PC style"). */
#pragma once
#include "swap.h"

/* libtitle/panels.cpp. Draws a window of ehsys as a glass panel instead of the game's frame, when the PC style is on and libtitle is loaded (its
 * button sheet holds the pieces). `listAddress` is the address of the display list cursor; the rectangle is in pixels; `alpha` fades the panel with
 * the window. `dialog` is a window with body text (a question with buttons): dark blue glass, so its white text reads; the others (the Free Duel
 * list) keep the light glass. Returns false, drawing nothing, when the game's own frame must be used. */
bool pcStyleWindow(SwapCtx &caller, uint32_t listAddress, float left, float top, float right, float bottom, uint32_t alpha, bool dialog = false);

/* The PC style's dark blue glass, for every box that carries white text (o usuario, 2026-10-12): fill and border as RGB, and the border's width in pixels
 * where a panel is drawn flat (BOOT windows on screens without libtitle's glass sheet). */
constexpr uint8_t kDarkFill[3] = {22, 38, 92}, kDarkBorder[3] = {112, 172, 245};
constexpr uint8_t kLightFill[3] = {234, 239, 245}, kLightBorder[3] = {96, 160, 235};   /* the Free Duel's light glass (the glass piece's light grey times the tint of panelStyle(0, 3)), for list windows */
constexpr int32_t kPanelBorderWidth = 2;
