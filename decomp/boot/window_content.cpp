/* ehsys window content (BOOT): what is drawn inside a window set each frame: the frames of its windows, the grid of cells (backgrounds, selection
 * highlight, per-cell callbacks), the scroll arrows and the dialog buttons. */
#include "boot.h"
#include <string.h>
#include "window.h"

#define MODULE "BOOT"

namespace {
using namespace boot;

constexpr uint32_t kWhite = 0xFFFFFFFFu;
constexpr uint32_t kColourCommand = 0x58;   /* the display list command that sets the vertex colour: its alpha is what fades a window */

/* ---- small helpers ---- */

/* FUN_08843984(block, style): the slot of the window item whose style is `style`, or -1 */
int32_t findItem(SwapCtx &x, const WindowBlock &block, uint32_t style) {
    for (int32_t i = 0; i < 16; i++) {
        const uint32_t item = block.item[i];
        if (item != 0 && x.at<WindowItem>(item).style == style) return i;
    }
    return -1;
}

/* FUN_0884a1fc(block): how far the open / close animation of the block's own window is (0 when it has no item) */
uint32_t animationStep(SwapCtx &x, uint32_t blockAddress) {
    const WindowBlock &block = x.at<WindowBlock>(blockAddress);
    for (int32_t i = 0; i < 16; i++) {
        const uint32_t item = block.item[i];
        if (item != 0 && x.at<WindowItem>(item).style == blockAddress + offsetof(WindowBlock, style)) return x.at<WindowItem>(item).animationStep;
    }
    return 0;
}

/* 8AC7E9E7(block, style): the y the cells start at: just below the style's body text (or, with no text, below its top margin). */
int32_t textBottom(SwapCtx &x, uint32_t blockAddress, uint32_t styleAddress) {
    const WindowBlock &block = x.at<WindowBlock>(blockAddress);
    const int32_t index = findItem(x, block, styleAddress);
    if (index < 0) return -1;
    const WindowStyle &style = x.at<WindowStyle>(x.at<WindowItem>(block.item[index]).style);
    const int16_t top = x.at<int16_t>(kInsetTop), left = x.at<int16_t>(kInsetLeft), right = x.at<int16_t>(kInsetRight), bottom = x.at<int16_t>(kInsetBottom);
    if (style.text == 0) return style.textTop + style.y + top + 1;
    ehsys::SetFontSize(x, style.fontSize, style.fontSize);
    const uint32_t savedUiFlag = ehsys::GetUiFlag4(x), savedWide = ehsys::GetWideScreenFlag(x);
    ehsys::SetUiFlag4(x, style.uiFlag4);
    ehsys::SetWideScreenFlag(x, style.wideScreenFlag);
    const int32_t boxW = ((-2 - right) - left) - style.textRight - style.textLeft + style.width;
    const int32_t boxH = ((-2 - bottom) - top) - style.textBottom - style.textTop + style.height;
    const uint32_t size = ehsys::MeasureTextBox(x, boxW * 64, boxH * 64, style.text);
    ehsys::SetUiFlag4(x, savedUiFlag);
    ehsys::SetWideScreenFlag(x, savedWide);
    const float textHeight = (float)(size & 0xFFFF) / 64.0f;
    return (int32_t)(textHeight + (float)(style.y + (top + 1) + style.textTop));
}

/* ---- the small pieces drawn with the block's sheets ---- */

/* FUN_08849fb8(cursor, x, y, width, height): a box of the frame sheet from nine 4x4 pieces (corners, edges stretched, middle stretched) at v 0x70.. */
uint32_t drawBoxSprites(SwapCtx &x, uint32_t cur, int32_t px, int32_t py, int32_t width, int32_t height) {
    constexpr int32_t kPiece = 4, kRowTop = 0x70, kRowMiddle = 0x74, kRowBottom = 0x78;
    cur = ehsys::SetTextureFilter(x, cur, 0, 0);
    cur = ehsys::SetSpriteSize(x, cur, kPiece, kPiece, kPiece, kPiece);
    const int32_t right = px + width - kPiece, bottom = py + height - kPiece;
    cur = ehsys::DrawSprite(x, cur, px, py, 0, 0, kRowTop, 0, 0);
    cur = ehsys::DrawSprite(x, cur, right, py, 0, 8, kRowTop, 0, 0);
    cur = ehsys::DrawSprite(x, cur, px, bottom, 0, 0, kRowBottom, 0, 0);
    cur = ehsys::DrawSprite(x, cur, right, bottom, 0, 8, kRowBottom, 0, 0);
    if (width > 8) {
        cur = ehsys::SetSpriteSize(x, cur, kPiece, kPiece, width - 8, kPiece);
        cur = ehsys::DrawSprite(x, cur, px + kPiece, py, 0, 4, kRowTop, 0, 0);
        cur = ehsys::DrawSprite(x, cur, px + kPiece, bottom, 0, 4, kRowBottom, 0, 0);
    }
    if (height > 8) {
        cur = ehsys::SetSpriteSize(x, cur, kPiece, kPiece, kPiece, height - 8);
        cur = ehsys::DrawSprite(x, cur, px, py + kPiece, 0, 0, kRowMiddle, 0, 0);
        cur = ehsys::DrawSprite(x, cur, right, py + kPiece, 0, 8, kRowMiddle, 0, 0);
    }
    if (width > 8 && height > 8) {
        cur = ehsys::SetSpriteSize(x, cur, kPiece, kPiece, width - 8, height - 8);
        cur = ehsys::DrawSprite(x, cur, px + kPiece, py + kPiece, 0, 4, kRowMiddle, 0, 0);
    }
    return cur;
}

/* 646197AD(cursor, block, x, y, width, height): the default background of a cell: the box in a fixed translucent blue */
uint32_t drawCellBackground(SwapCtx &x, uint32_t cur, const WindowBlock &block, int32_t px, int32_t py, int32_t width, int32_t height) {
    constexpr uint32_t kCellBlue = 0x803A768D;
    cur = ehsys::BindTexture(x, cur, block.sheet);
    cur = ehsys::SetColor(x, cur, kCellBlue);
    return drawBoxSprites(x, cur, px, py, width, height);
}

/* 40D6B29D(cursor, block, x, y, width, height): the default highlight of the selected cell: the box in orange that pulses (the frame counter over 33
 * frames sets its alpha). A closing window draws it, unpulsed, only while `counter` bit 1 is set. */
uint32_t drawSelectedCell(SwapCtx &x, uint32_t cur, const WindowBlock &block, int32_t px, int32_t py, int32_t width, int32_t height) {
    uint32_t colour = 0xA00080FFu;
    if (block.state == 4) {
        if (!(block.counter & 2)) return cur;
    } else {
        constexpr uint32_t kPulseFrames = 33;
        const uint32_t step = x.at<uint32_t>(ehsys::FrameCounter(x)) % kPulseFrames;
        const uint32_t ramp = (step < 17 ? step : kPulseFrames - step) << 7;
        colour = (((ramp >> 4) + 127) & 0xFF) << 24 | 0x80FF;
    }
    cur = ehsys::BindTexture(x, cur, block.sheet);
    cur = ehsys::SetColor(x, cur, colour);
    return drawBoxSprites(x, cur, px, py, width, height);
}

/* Which side a scroll arrow points to. */
enum Direction : int32_t { kDown = 0, kUp = 1, kLeft = 2, kRight = 3 };

/* 91E5BB91(cursor, x, y, direction): a 12x12 scroll arrow of the bound sheet, blinking (shown for three quarters of every 32 frames); (x, y) is
 * the middle of the edge it sits on. */
uint32_t drawScrollArrow(SwapCtx &x, uint32_t cur, int32_t px, int32_t py, int32_t direction) {
    constexpr int32_t kSize = 12, kRow = 0x48;
    if (((x.at<uint32_t>(ehsys::FrameCounter(x)) & 0x1F) >> 3) == 0) return cur;
    switch (direction) {
    case kDown: px -= 6; break;
    case kUp: px -= 6; py -= kSize; break;
    case kRight: py -= 6; break;
    case kLeft: px -= kSize; py -= 6; break;
    }
    cur = ehsys::SetColor(x, cur, kWhite);
    cur = ehsys::SetTextureFilter(x, cur, 0, 0);
    cur = ehsys::SetSpriteSize(x, cur, kSize, kSize, kSize, kSize);
    return ehsys::DrawSprite(x, cur, px, py, 0, direction * kSize, kRow, 0, 0);
}

/* B7DFD65E(cursor, block, x, y, button, selected): one dialog button (60 x 24): the button sprite (a lit one when selected, with a pulsing glow while
 * the window is open) and its label, centred, from the table of labels (one per button and language). */
uint32_t drawWindowButton(SwapCtx &x, uint32_t cur, const WindowBlock &block, int32_t px, int32_t py, int32_t button, int32_t selected) {
    constexpr int32_t kWidth = 0x3C, kHeight = 0x18, kLitRow = 0x30;
    int32_t lit = selected;
    if (block.state == 4 && !(block.counter & 2) && selected == 1) lit = 0;   /* a closing window shows the button unlit */
    cur = ehsys::BindTexture(x, cur, block.buttonSheet);
    cur = ehsys::SetColor(x, cur, kWhite);
    cur = ehsys::SetTextureFilter(x, cur, 0, 0);
    cur = ehsys::SetSpriteSize(x, cur, kWidth, kHeight, kWidth, kHeight);
    if (lit == 0) {
        cur = ehsys::DrawSprite(x, cur, px, py, 0, 0, 0, 0, 0);
    } else {
        int32_t u, v;   /* the lit sprite of the button's colour: buttons 0, 1 and 2, then the others */
        if (button < 3) { u = kWidth; v = button == 0 ? kHeight : 0; }
        else { u = 0; v = kHeight; }
        cur = ehsys::DrawSprite(x, cur, px, py, 0, u, v, 0, 0);
        cur = ehsys::DrawSprite(x, cur, px, py, 0, 0, kLitRow, 0, 0);
    }
    if (selected != 0 && block.state != 4) {   /* the glow: the same sprite added over, its brightness following the frame counter */
        constexpr uint32_t kGlowFrames = 33;
        const uint32_t step = x.at<uint32_t>(ehsys::FrameCounter(x)) % kGlowFrames;
        uint32_t brightness = step < 17 ? step << 7 : (kGlowFrames - step) * 0x80;
        brightness = (brightness & 0xFFF) >> 4;
        cur = ehsys::SetColor(x, cur, brightness | brightness << 8 | brightness << 24 | brightness << 16);
        cur = ehsys::SetBlend(x, cur, 0, 2, 10, 0xFFFFFFFFu, 0xFFFFFFFFu);
        cur = ehsys::SetSpriteSize(x, cur, kWidth, kHeight, kWidth, kHeight);
        cur = ehsys::DrawSprite(x, cur, px, py, 0, kWidth, kLitRow, 0, 0);
        cur = ehsys::SetBlend(x, cur, 0, 2, 3, 0, 0);
    }
    const uint32_t language = ehsys::GetLanguage(x);
    const uint32_t label = x.at<uint32_t>(kButtonLabels + button * 4 + language * 0x14);
    if (label != 0) {
        ehsys::SetFontSize(x, 10, 12);
        ehsys::SetTextColor(x, 0xFF000000u);
        const uint32_t size = ehsys::FitFontSize(x, 0xF00, label);
        ehsys::SetFontSize(x, size & 0xFF, 12);
        const int32_t labelWidth = ehsys::TextWidth(x, label);
        ehsys::BeginText(x, cur);
        const int32_t lineHeight = ehsys::GetLineHeight(x);
        int32_t centredY = -(lineHeight >> 6) + kHeight;
        if (centredY < 0) centredY = -(lineHeight >> 6) + kHeight + 1;
        int32_t centredX = -(labelWidth >> 6) + kWidth;
        if (centredX < 0) centredX = -(labelWidth >> 6) + kWidth + 1;
        const int32_t textX = px + (centredX >> 1);
        ehsys::DrawText(x, textX * 64, (py + (centredY >> 1) + 1) * 64, (480 - textX) * 64, label);
        cur = ehsys::EndText(x);
    }
    return cur;
}

/* ---- the whole set ---- */

/* 088418b8(list, block): lay out and draw the frame of every window item of the block, lowest priority first. Items are put in order by inserting
 * each into a list; the original compares the new item with the block's slot at the list position (not with the list's item there): kept as is. */
void drawWindows(SwapCtx &x, uint32_t listAddress, uint32_t blockAddress) {
    const WindowBlock &block = x.at<WindowBlock>(blockAddress);
    int32_t order[16];
    int32_t count = 0;
    auto priority = [&](uint32_t item) { return x.at<int16_t>(x.at<WindowItem>(item).style + offsetof(WindowStyle, priority)); };
    for (int32_t slot = 0; slot < 16; slot++) {
        if (block.item[slot] == 0) continue;
        int32_t at = 0;
        while (at < count && !(priority(block.item[slot]) < priority(block.item[at]))) at++;
        for (int32_t k = count - 1; k >= at; k--) order[k + 1] = order[k];
        order[at] = slot;
        count++;
    }
    for (int32_t k = 0; k < count; k++) {
        const uint32_t itemAddress = block.item[order[k]];
        layoutWindowItem(x, listAddress, itemAddress, blockAddress);
        WindowItem &item = x.at<WindowItem>(itemAddress);
        if (x.at<WindowStyle>(item.style).visible != 0) drawWindowItem(x, listAddress, itemAddress, blockAddress);
        memcpy(item.styleCopy, x.mem(item.style), sizeof(WindowStyle));   /* keep this frame's style for the next layout */
    }
}

/* Which dialog buttons a window shows (the low nibble of `buttonFlags`) and where they go, relative to the window's middle. */
enum ButtonLayout : uint8_t { kNoButtons = 0, kOneButton = 1, kTwoButtons = 2, kTwoButtonsLast = 3, kThreeButtons = 4 };

/* the half of a (possibly negative) number, rounded towards zero */
int32_t halfOf(int32_t value) { return (value < 0 ? value + 1 : value) >> 1; }

/* the dialog buttons under the cells (`rowY` is where the cells ended) */
void drawButtons(SwapCtx &x, uint32_t listAddress, const WindowBlock &block, int32_t rowY) {
    uint32_t &list = x.at<uint32_t>(listAddress);
    const int32_t middle = block.style.x + halfOf(block.style.width), y = rowY + 10;
    const int32_t selected = block.selectedButton & 0xF;
    auto button = [&](int32_t px, int32_t index) { list = drawWindowButton(x, list, block, px, y, index, selected == index); };
    const int32_t centredOne = block.style.x + (((block.style.width - 0x3C) < 0 ? block.style.width - 0x3B : block.style.width - 0x3C) >> 1);
    switch (block.buttonFlags & 0xF) {
    case kThreeButtons: button(middle - 0x60, 2); button(centredOne, 3); button(middle + 0x24, 4); break;
    case kTwoButtonsLast: button(middle - 0x46, 2); button(middle + 10, 3); break;
    case kTwoButtons: button(middle - 0x46, 1); button(middle + 10, 4); break;
    case kOneButton: button(centredOne, 1); break;
    }
}

/* The default content: the cells in their grid. Returns where the cells ended (the position the next one would take). */
void drawCells(SwapCtx &x, uint32_t listAddress, uint32_t blockAddress, int32_t left, int32_t top, int32_t width, int32_t height, int32_t &endX, int32_t &endY) {
    const WindowBlock &block = x.at<WindowBlock>(blockAddress);
    uint32_t &list = x.at<uint32_t>(listAddress);
    uint32_t shown = block.cellCount;
    if (block.visibleCells != 0 && block.visibleCells < shown) shown = block.visibleCells;
    endX = left;
    endY = top;
    if (((block.layoutFlags & 0x7F) >> 3) == 0) return;
    const bool sideways = (block.layoutFlags & 0x08) != 0, cursorByCallback = (block.flags & 0x02) != 0;
    auto cellBox = [&](uint32_t callback, int32_t cx, int32_t cy) {
        const int32_t bx = cx + block.cellOffsetX, by = cy + block.cellOffsetY;
        if (callback != 0) list = x.callAddr(callback, list, blockAddress, bx, by, block.cellWidth, block.cellHeight);
        else list = drawCellBackground(x, list, block, bx, by, block.cellWidth, block.cellHeight);
    };
    auto cellBoxDefaultSelected = [&](uint32_t callback, int32_t cx, int32_t cy) {
        const int32_t bx = cx + block.cellOffsetX, by = cy + block.cellOffsetY;
        if (callback != 0) list = x.callAddr(callback, list, blockAddress, bx, by, block.cellWidth, block.cellHeight);
        else list = drawSelectedCell(x, list, block, bx, by, block.cellWidth, block.cellHeight);
    };
    auto isSelected = [&](uint32_t i) { return (block.selection & 0x1F) == i; };   /* the callbacks may change the block: read it afresh each time */
    for (uint32_t i = 0; i < shown; i++) {
        const uint32_t cell = i + block.firstCell;
        const uint32_t bit = 1u << (cell & 31);
        if (block.cellMask & bit) cellBox(block.backgroundCallback, endX, endY);
        if (isSelected(i)) {
            if (cursorByCallback) cellBoxDefaultSelected(block.cursorCallback, endX, endY);
            else cellBox(block.backgroundCallback, endX, endY);
        }
        if (block.cellCallback != 0) list = x.callAddr(block.cellCallback, list, cell & 0xFFFF, endX, endY);
        if ((block.cellMask & bit) && block.overlayCallback != 0) list = x.callAddr(block.overlayCallback, list, blockAddress, endX + block.cellOffsetX, endY + block.cellOffsetY, block.cellWidth, block.cellHeight);
        if (isSelected(i)) {
            if (cursorByCallback) {
                if (block.cursorOverlayCallback != 0) list = x.callAddr(block.cursorOverlayCallback, list, blockAddress, endX + block.cellOffsetX, endY + block.cellOffsetY, block.cellWidth, block.cellHeight);
            } else if (block.overlayCallback != 0) {
                list = x.callAddr(block.overlayCallback, list, blockAddress, endX + block.cellOffsetX, endY + block.cellOffsetY, block.cellWidth, block.cellHeight);
            }
        }
        if (sideways) endX += block.cellStrideX;
        else endY += block.cellStrideY;
    }
    (void)width; (void)height;
}

/* FUN_08848a14 (EF9B5D06)(list, block): draw a window set: the frames of its windows, then its content. While the set opens or closes the whole
 * display list it adds is faded by rewriting the alpha of its colour commands. */
void drawWindowContent(SwapCtx &x, uint32_t listAddress, uint32_t blockAddress) {
    WindowBlock &block = x.at<WindowBlock>(blockAddress);
    uint32_t &list = x.at<uint32_t>(listAddress);
    const uint32_t listBeforeFrames = list;
    drawWindows(x, listAddress, blockAddress);
    if (block.layoutFlags & 0x04) list = listBeforeFrames;   /* the frames are not wanted: only their layout */

    uint32_t alpha = 0xFF;
    switch (block.state) {
    case 4: case 3: break;
    case 5: alpha = animationStep(x, blockAddress) < 2 ? 0x80 : 0; break;
    case 2: { const uint32_t step = animationStep(x, blockAddress); alpha = step < 3 ? 0 : step == 3 ? 0x80 : 0xFF; break; }
    default: return;
    }
    if (alpha == 0 || block.style.visible == 0) return;

    int32_t left = block.style.x + (int16_t)block.style.textLeft + 4;
    int32_t top = textBottom(x, blockAddress, blockAddress + offsetof(WindowBlock, style)) + 1;
    if (block.layoutFlags & 0x02) {   /* centre the cells in the room the window leaves */
        const uint32_t mode = (block.layoutFlags & 0x7F) >> 3;
        if (mode != 0) {
            uint32_t shown = block.cellCount;
            if (block.visibleCells != 0) shown = block.visibleCells <= block.cellCount ? block.visibleCells : block.cellCount;
            if ((mode & 1) == 0) {
                const int32_t used = block.cellStrideY * shown;
                int32_t room = ((block.style.y + block.style.height) - block.style.textBottom - 4) - top;
                if (used < room) { room -= used; if (room < 0) room++; top += room >> 1; }
            } else {
                const int32_t used = block.cellStrideX * shown;
                int32_t room = ((block.style.x + block.style.width) - block.style.textRight - 4) - left;
                if (used < room) { room -= used; if (room < 0) room++; left += room >> 1; }
            }
        }
    }
    const int32_t width = ((block.style.x + block.style.width) - left - block.style.textRight) - 5;
    const int32_t height = ((block.style.y + block.style.height) - top - block.style.textBottom) - 5;
    const uint32_t contentStart = list;

    if (block.contentCallback != 0) {
        list = x.callAddr(block.contentCallback, contentStart, left, top, width, height);
    } else {
        if (block.beforeCallback != 0) list = x.callAddr(block.beforeCallback, contentStart, left, top, width, height);
        int32_t endX, endY;
        drawCells(x, listAddress, blockAddress, left, top, width, height, endX, endY);
        {   /* the scroll arrows, when cells are hidden before or after the visible ones */
            const bool sideways = (block.layoutFlags & 0x08) != 0;
            if (block.firstCell != 0) {
                list = ehsys::BindTexture(x, list, block.buttonSheet);
                if (sideways) list = drawScrollArrow(x, list, left - block.arrowDistance, top + (block.cellStrideY >> 1), kLeft);
                else list = drawScrollArrow(x, list, left + (block.cellStrideX >> 1), top - block.arrowDistance, kUp);
            }
            if (block.visibleCells != 0 && block.firstCell + block.visibleCells < block.cellCount) {
                list = ehsys::BindTexture(x, list, block.buttonSheet);
                if (sideways) list = drawScrollArrow(x, list, endX + block.arrowDistance, top + (block.cellStrideY >> 1), kRight);
                else list = drawScrollArrow(x, list, left + (block.cellStrideX >> 1), endY + block.arrowDistance, kDown);
            }
        }
        if ((block.buttonFlags & 0xF) != kNoButtons) drawButtons(x, listAddress, block, ((block.layoutFlags & 0x7F) >> 3) != 0 ? endY : top);
        if (block.afterCallback != 0) list = x.callAddr(block.afterCallback, list, left, top, width, height);
    }
    if (alpha < 0xFF) {
        for (uint32_t at = contentStart; at < list; at += 4) {
            uint32_t &command = x.at<uint32_t>(at);
            if (command >> 24 == kColourCommand) command = (((command & 0xFF) * alpha) / 0xFF & 0xFF) | (kColourCommand << 24);
        }
    }
}

/* ---- glue ---- */

void swap_DrawWindows(SwapCtx &x) { const uint32_t list = x.arg(0); drawWindows(x, list, x.arg(1)); x.ret(x.at<uint32_t>(list)); }
SwapReg reg_DrawWindows({"BOOT.DrawWindows", MODULE, fn::DrawWindows, swap_DrawWindows});

void swap_WindowTextBottom(SwapCtx &x) { x.ret((uint32_t)textBottom(x, x.arg(0), x.arg(0) + offsetof(WindowBlock, style))); }
SwapReg reg_WindowTextBottom({"BOOT.WindowTextBottom", MODULE, fn::WindowTextBottom, swap_WindowTextBottom});

void swap_FindWindowItem(SwapCtx &x) { x.ret((uint32_t)findItem(x, x.at<WindowBlock>(x.arg(0)), x.arg(1))); }
SwapReg reg_FindWindowItem({"BOOT.FindWindowItem", MODULE, fn::FindWindowItem, swap_FindWindowItem, true});

void swap_WindowAnimationStep(SwapCtx &x) { x.ret(animationStep(x, x.arg(0))); }
SwapReg reg_WindowAnimationStep({"BOOT.WindowAnimationStep", MODULE, fn::WindowAnimationStep, swap_WindowAnimationStep, true});

void swap_DrawWindowContent(SwapCtx &x) { const uint32_t list = x.arg(0); drawWindowContent(x, list, x.arg(1)); x.ret(x.at<uint32_t>(list)); }
SwapReg reg_DrawWindowContent({"BOOT.DrawWindowContent", MODULE, fn::DrawWindowContent, swap_DrawWindowContent});

void swap_DrawCellBackground(SwapCtx &x) {
    x.ret(drawCellBackground(x, x.arg(0), x.at<WindowBlock>(x.arg(1)), (int32_t)x.arg(2), (int32_t)x.arg(3), (int32_t)x.arg(4), (int32_t)x.arg(5)));
}
SwapReg reg_DrawCellBackground({"BOOT.DrawCellBackground", MODULE, fn::DrawCellBackground, swap_DrawCellBackground});

void swap_DrawSelectedCell(SwapCtx &x) {
    x.ret(drawSelectedCell(x, x.arg(0), x.at<WindowBlock>(x.arg(1)), (int32_t)x.arg(2), (int32_t)x.arg(3), (int32_t)x.arg(4), (int32_t)x.arg(5)));
}
SwapReg reg_DrawSelectedCell({"BOOT.DrawSelectedCell", MODULE, fn::DrawSelectedCell, swap_DrawSelectedCell});

void swap_DrawBoxSprites(SwapCtx &x) { x.ret(drawBoxSprites(x, x.arg(0), (int32_t)x.arg(1), (int32_t)x.arg(2), (int32_t)x.arg(3), (int32_t)x.arg(4))); }
SwapReg reg_DrawBoxSprites({"BOOT.DrawBoxSprites", MODULE, fn::DrawBoxSprites, swap_DrawBoxSprites});

void swap_DrawScrollArrow(SwapCtx &x) { x.ret(drawScrollArrow(x, x.arg(0), (int32_t)x.arg(1), (int32_t)x.arg(2), (int32_t)x.arg(3))); }
SwapReg reg_DrawScrollArrow({"BOOT.DrawScrollArrow", MODULE, fn::DrawScrollArrow, swap_DrawScrollArrow});

void swap_DrawWindowButton(SwapCtx &x) {
    x.ret(drawWindowButton(x, x.arg(0), x.at<WindowBlock>(x.arg(1)), (int32_t)x.arg(2), (int32_t)x.arg(3), (int32_t)x.arg(4), (int32_t)x.arg(5)));
}
SwapReg reg_DrawWindowButton({"BOOT.DrawWindowButton", MODULE, fn::DrawWindowButton, swap_DrawWindowButton});

}  // namespace
