/* libtitle swaps: Window frames and panels, and the PC style that draws glass panels in place of them. */
#include "internal.h"

/* ---- window frames and panels (the card list / options screens' drawing) ---- */

constexpr uint32_t kPanelFlagsPlain = 0x3E;   /* the setup functions below all use bits 1-5 set */

/* the CPU's divide: the quotient and remainder, and what it leaves for a zero divisor */
static void divide(int32_t a, int32_t b, int32_t &quotient, int32_t &remainder) {
    if (b == 0) { quotient = a < 0 ? 1 : -1; remainder = a; }
    else if (a == INT32_MIN && b == -1) { quotient = INT32_MIN; remainder = 0; }
    else { quotient = a / b; remainder = a % b; }
}

/* ---- the PC style's panels: glass boxes in place of the game's frames (the same menu sheet as the buttons) ---- */

/* Two neutral 12x12 pieces in the free strip of the menu sheet (tools/menu_estilo_pc.py): the fill and the border, each cut in nine (corners 5x5,
 * stretched 2x2 middle). As with the buttons, all nine slices are drawn scaled (corners 5 -> 6 px) so the renderer samples them alike. */
constexpr int32_t kPanelFillU = 0, kPanelBorderU = 12, kPanelV = 116, kPanelCorner = 5, kPanelMiddle = 2, kPanelCornerDrawn = 6;

static uint32_t drawPanelPieces(SwapCtx &x, uint32_t list, int32_t px, int32_t py, int32_t w, int32_t h, int32_t u) {
    const int32_t c = kPanelCorner, m = kPanelMiddle, d = kPanelCornerDrawn;
    const int32_t innerW = std::max(w - 2 * d, 0), innerH = std::max(h - 2 * d, 0);
    const int32_t srcX[3] = {u, u + c, u + c + m}, srcW[3] = {c, m, c};
    const int32_t srcY[3] = {kPanelV, kPanelV + c, kPanelV + c + m}, srcH[3] = {c, m, c};
    const int32_t dstX[3] = {px, px + d, px + d + innerW}, dstW[3] = {d, innerW, d};
    const int32_t dstY[3] = {py, py + d, py + d + innerH}, dstH[3] = {d, innerH, d};
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            if (dstW[col] == 0 || dstH[row] == 0) continue;
            list = ehsys::SetSpriteSize(x, list, srcW[col], srcH[row], dstW[col], dstH[row]);
            list = ehsys::DrawSprite(x, list, dstX[col], dstY[row], 0, srcX[col], srcY[row], 0, 0);
        }
    }
    return list;
}

static bool panelStyle(int32_t kind, int32_t variant, PanelStyle &style) {
    if (kind == 0 && variant == 3) { style = {false, {244, 249, 255}, {96, 160, 235}}; return true; }
    if (kind == 93 && variant == 14) { style = {false, {255, 236, 178}, {235, 165, 40}}; return true; }
    if (kind == 84 && variant == 10) { style = {true, {34, 54, 104}, {130, 190, 255}}; return true; }
    return false;
}

/* The glass pieces of `style` over the rectangle, in the sheet that is bound now. `colour` is the vertex colour (its alpha fades the panel). */
uint32_t drawGlassPieces(SwapCtx &x, uint32_t list, uint32_t colour, int32_t px, int32_t py, int32_t width, int32_t height, const PanelStyle &style) {
    list = ehsys::SetBlendColor(x, list, tinted(colour, style.fill));
    list = style.slanted ? drawModernPiece(x, list, px, py, std::max(width, kCellWidth), kModernFillV) : drawPanelPieces(x, list, px, py, width, height, kPanelFillU);
    list = ehsys::SetBlendColor(x, list, tinted(colour, style.border));
    list = style.slanted ? drawModernPiece(x, list, px, py, std::max(width, kCellWidth), kModernBorderV) : drawPanelPieces(x, list, px, py, width, height, kPanelBorderU);
    return ehsys::SetBlendColor(x, list, colour);
}

static uint32_t drawModernFrame(SwapCtx &x, uint32_t list, uint32_t colour, int32_t px, int32_t py, int32_t width, int32_t height, const PanelStyle &style) {
    list = x.callLocal(fn::SetupDrawState, list, 0);
    const uint32_t archive = currentArchive(x);
    const uint32_t buttonImage = logoSpriteName(x, *(const uint32_t *)x.mem(x.addr(off(kListRowSprites))));
    list = ehsys::BindTexture(x, list, ehsys::FindTexture(x, archive, buttonImage, 0));
    return drawGlassPieces(x, list, colour, px, py, width, height, style);
}

/* The window frames of ehsys (BOOT) in the PC style: the body glass, over the frame's whole rectangle. */
bool pcStyleWindow(SwapCtx &caller, uint32_t listAddress, float left, float top, float right, float bottom, uint32_t alpha, bool dialog) {
    SwapCtx x{};
    if (!gfx_modern_menus() || !swap_module_ctx(MODULE, caller.c, x)) return false;
    const int16_t screen = state(x).screen;   /* the glass pieces are in the sheet of the screens that draw their panels this way */
    if (screen != kScreenLists && screen != kScreenCardList) return false;
    PanelStyle style;
    if (!panelStyle(0, 3, style)) return false;
    if (dialog) style = kDarkPanelStyle;
    uint32_t &list = x.at<uint32_t>(listAddress);
    const int32_t px = (int32_t)lroundf(left), py = (int32_t)lroundf(top);
    const int32_t width = (int32_t)lroundf(right) - px, height = (int32_t)lroundf(bottom) - py;
    list = drawModernFrame(x, list, alpha << 24 | 0x00FFFFFFu, px, py, width, height, style);
    return true;
}

/* FUN_08f0a928 / 08f0a71c(list, colour, x, y, w, h, kind, variant, big): draw a frame of `kind` (pieces from the sheet above) around the box x, y, w, h
 * and return the new list. The middle is stretched; `variant` picks the colour set (a row of the sheet) and `big` (kind 1 only) the larger corners. */
static uint32_t drawFrame(SwapCtx &x, uint32_t list, uint32_t colour, int32_t px, int32_t py, int32_t width, int32_t height, int32_t kind,
                          int32_t variant, int32_t big) {
    PanelStyle style;
    if (gfx_modern_menus() && panelStyle(kind, variant, style)) return drawModernFrame(x, list, colour, px, py, width, height, style);
    const FramePart *part = (const FramePart *)x.mem(x.addr(off(kFrameParts)));
    int32_t repeat = 1, partCount = 9, rightEdge = 2;
    int16_t uShift = 0, vShift = 0;
    if (kind == 0x46) { uShift = (int16_t)((int16_t)variant * 14); big = 0; partCount = 3; }
    else if (kind == 0x49) { partCount = 1; big = 0; }
    else if (kind == 0x66) { big = 0; partCount = 3; repeat = 2; }
    else if (kind == 0x5d) {
        if (variant < 13 || variant > 16) variant = 13;
        vShift = (int16_t)((variant - 13) * 16);
        big = 0;
    } else if (kind == 0x54 || kind == 0x4b) {
        if (variant < 4 || variant > 12) variant = 4;
        variant -= 4;
        uShift = (int16_t)((variant / 3) * 13);
        big = 0;
        vShift = (int16_t)((variant % 3) * 13);
    } else vShift = (int16_t)(variant << 5);
    if (big == 1) { rightEdge = 9; partCount = 12; }

    const int32_t leftW = part[kind].width, topH = part[kind].height;
    const int16_t left = (int16_t)(px + leftW);
    int32_t innerW = (int16_t)(width - (leftW + part[kind + rightEdge].width));
    if (innerW < 0) innerW = 0;
    const int16_t right = (int16_t)(left + innerW);
    const int16_t top = (int16_t)(py + topH);
    int32_t innerH = (int16_t)(height - (topH + part[kind + 6].height));
    if (innerH < 0) innerH = 0;
    const int16_t bottom = (int16_t)(top + innerH);

    uint32_t cur = x.callLocal(fn::SetupDrawState, list, 0);
    cur = ehsys::SetBlendColor(x, cur, colour);
    const uint32_t archive = x.callLocal(fn::CurrentLogoResource);
    const uint32_t sheet = x.callLocal(fn::ImageName, x.addr(off(kFrameParts)), (uint32_t)kind);
    cur = ehsys::BindTexture(x, cur, ehsys::FindTexture(x, archive, sheet, 0));
    int32_t rowOffset = 0;
    for (int32_t pass = 0; pass < repeat; pass++) {
        if (pass != 0 && kind == 0x66) vShift = (int16_t)rowOffset;
        for (int32_t i = kind; i < kind + partCount; i++) {
            const FramePart piece = part[i];
            int16_t sx = (int16_t)px, sy = (int16_t)py, dw = piece.width, dh = piece.height;
            switch (i - kind) {   /* where this piece goes: 0 top left, 1 top edge, 2 top right, 3 left edge, 4 middle, 5 right edge, 6.. bottom */
            case 1: dw = (int16_t)innerW; sx = left; break;
            case 2: sx = right; break;
            case 3: dh = (int16_t)innerH; sy = top; break;
            case 4: dw = (int16_t)innerW; dh = (int16_t)innerH; sx = left; sy = top; break;
            case 5: dh = (int16_t)innerH; sx = right; sy = top; break;
            case 6: sy = bottom; break;
            case 7: dw = (int16_t)innerW; sx = left; sy = bottom; break;
            case 8: sx = right; sy = bottom; break;
            case 9: sx = right; break;
            case 10: dh = (int16_t)innerH; sx = right; sy = top; break;
            case 11: sx = right; sy = bottom; break;
            }
            cur = ehsys::SetSpriteSize(x, cur, piece.width, piece.height, dw, dh);
            cur = ehsys::DrawSprite(x, cur, sx, sy, 0, (int16_t)(piece.u + uShift), (int16_t)(piece.v + vShift), piece.mirrorU, piece.mirrorV);
        }
        rowOffset = (int16_t)(rowOffset + 0x20);
    }
    return cur;
}
static void swap_DrawFrame(SwapCtx &x) {
    const uint32_t sp = x.c->r[29];
    x.ret(drawFrame(x, x.arg(0), x.arg(1), (int32_t)x.arg(2), (int32_t)x.arg(3), (int32_t)x.arg(4), (int32_t)x.arg(5), (int32_t)x.arg(6),
                    (int32_t)x.arg(7), (int32_t)x.at<uint32_t>(sp)));
}
static SwapReg reg_DrawFrame({"libtitle.DrawFrame", MODULE, off(0x08f0a928), swap_DrawFrame});

/* FUN_08f0a73c(list, colour, x, y, w, h): the cursor box around a cell: a frame (kind 40) one or two pixels larger than the cell on each side, pulsing
 * between the two every 8 frames. While a fade mode is on (the flags byte, see SetFadeMode) it blinks instead: shown on the steps 2 and 3 of every 4
 * of the blink counter; in fade mode 2 the blink ends after 15 steps (the fade mode is cleared) with the box shown. */
constexpr int32_t kCursorBoxFrameKind = 40, kCursorBoxPulsePeriod = 16, kBlinkPeriod = 4, kBlinkEndStep = 15, kBlinkCounterWrap = 4096;
constexpr uint8_t kFadeModeMask = 3, kFadeModeBlinkThenStop = 2;
static uint32_t drawCursorBox(SwapCtx &x, uint32_t list, uint32_t colour, int32_t px, int32_t py, int32_t width, int32_t height) {
    const int32_t grow = (int32_t)state(x).frame % kCursorBoxPulsePeriod / (kCursorBoxPulsePeriod / 2) + 1;   /* 1 or 2 pixels */
    px -= grow;
    py -= grow;
    width += 2 * grow;
    height += 2 * grow;
    const uint8_t fadeMode = x.at<uint8_t>(kFlagsAddr) & kFadeModeMask;
    if (fadeMode == 0) return drawFrame(x, list, colour, px, py, width, height, kCursorBoxFrameKind, 0, 0);
    int16_t &blink = x.at<int16_t>(kFlagsTouchedAddr);
    if (blink % kBlinkPeriod >= kBlinkPeriod / 2) list = drawFrame(x, list, colour, px, py, width, height, kCursorBoxFrameKind, 0, 0);
    if (++blink >= kBlinkCounterWrap) blink = 0;
    if ((x.at<uint8_t>(kFlagsAddr) & kFadeModeMask) != kFadeModeBlinkThenStop || blink < kBlinkEndStep) return list;
    x.callLocal(fn::SetFadeMode, 0);
    return drawFrame(x, list, colour, px, py, width, height, kCursorBoxFrameKind, 0, 0);
}
static void swap_DrawFramedBox(SwapCtx &x) {
    x.ret(drawCursorBox(x, x.arg(0), x.arg(1), (int32_t)x.arg(2), (int32_t)x.arg(3), (int32_t)x.arg(4), (int32_t)x.arg(5)));
}
static SwapReg reg_DrawFramedBox({"libtitle.DrawFramedBox", MODULE, off(0x08f0a73c), swap_DrawFramedBox});   /* not verifiable: it counts a blink step per call */

/* FUN_08f09b78(list, panel): draw a panel: its frame, one frame per cell, the label, the scroll bar, each cell through the panel's callback, and
 * the cursor on the selected cell. Cells are laid out column by column: cell i is at column i / rows, row i % rows, where rows = cells / columns. */
static uint32_t drawPanel(SwapCtx &x, uint32_t list, uint32_t address) {
    Panel &p = x.at<Panel>(address);
    int32_t cursor = p.cursor;
    int32_t columns = p.columns == 0 ? 1 : p.columns;
    int32_t rows, ignored;
    divide(p.cells, columns, rows, ignored);
    rows = (int16_t)rows;
    if (!(cursor < (int32_t)p.cells)) cursor = (int32_t)p.cells - 1;
    const bool bigCorners = !(p.flags & 0x20);
    auto cellX = [&](int32_t i) { int32_t q, r; divide(i, rows, q, r); return (int32_t)(int16_t)(p.gridX + (p.cellW + 3) * q); };
    auto cellY = [&](int32_t i) { int32_t q, r; divide(i, rows, q, r); return (int32_t)(int16_t)(p.gridY + (p.cellH + 1) * r); };

    if (!(p.flags & 0x01)) list = drawFrame(x, list, p.colour, p.x, p.y, p.w, p.h, p.frameKind, p.frameVariant, bigCorners);
    if (!(p.flags & 0x08)) {
        for (int32_t i = 0; i < (int32_t)p.cells; i++) list = drawFrame(x, list, p.colour, cellX(i), cellY(i), p.cellW, p.cellH, 0, 3, 0);
    }
    if (p.label != 0) list = x.callLocal(fn::DrawAlignedText, list, (uint32_t)(int32_t)p.gridX, (uint32_t)(int32_t)p.gridY, (uint32_t)(int32_t)p.cellW,
                                         (uint32_t)(int32_t)p.cellH, p.label, 4);
    if (!(p.flags & 0x20)) {
        const int32_t barX = (int16_t)(p.x + p.w - 11), barY = (int16_t)(p.y + 12), barH = (int16_t)(p.h - 24);
        GuestStack scratch(x, 16);
        const uint32_t outA = scratch.addr() + 14, outB = scratch.addr() + 12;
        x.callLocal(fn::ScrollThumbPosition, outA, outB, address, (uint32_t)barX, (uint32_t)barY, (uint32_t)barH);
        list = x.callLocal(fn::DrawScrollBar, list, 0xFFFFFFFFu, (uint32_t)barX, (uint32_t)barY, (uint32_t)barH,
                           (uint32_t)(int32_t)x.at<int16_t>(outA), (uint32_t)(int32_t)x.at<int16_t>(outB));
    }
    if (p.drawCell != 0) {
        for (int32_t i = 0; i < (int32_t)p.cells; i++) {
            list = x.callAddr(p.drawCell, list, (uint32_t)i & 0xFFFF, (uint32_t)cellX(i), (uint32_t)cellY(i), (uint32_t)(int32_t)p.cellW,
                              (uint32_t)(int32_t)p.cellH);
        }
    }
    if (!(p.flags & 0x04)) {
        list = x.callLocal(fn::DrawFramedBox, list, p.colour, (uint32_t)cellX(cursor), (uint32_t)cellY(cursor), (uint32_t)(int32_t)p.cellW,
                           (uint32_t)(int32_t)p.cellH);
    }
    return list;
}
static void swap_DrawPanel(SwapCtx &x) { x.ret(drawPanel(x, x.arg(0), x.arg(1))); }
static SwapReg reg_DrawPanel({"libtitle.DrawPanel", MODULE, off(0x08f09b78), swap_DrawPanel});

/* FUN_08f0b510(list, text, x, y, w, h): a small panel (frame kind 93, variant 14) at the fixed record 0x09B29F44, with `text` over it if given */
static uint32_t drawTextPanel(SwapCtx &x, uint32_t list, uint32_t text, int16_t px, int16_t py, int16_t w, int16_t h) {
    constexpr uint32_t kRecord = kCardBox;
    Panel &p = x.at<Panel>(kRecord);
    p.gridX = (int16_t)(px + 3);
    p.gridY = (int16_t)(py + 2);
    p.cellW = (int16_t)(w - 6);
    p.cellH = (int16_t)(h - 4);
    p.colour = 0xFFFFFFFFu;
    p.colour2 = 0xFFFFFFFFu;
    p.frameKind = 0x5d;
    p.frameVariant = 14;
    p.cells = 1;
    p.unknown25 = 1;
    p.flags = (uint8_t)((p.flags & 0xC0) | kPanelFlagsPlain);
    p.unknown0 = 0;
    p.unknown23 = 0;
    p.cursor = 0;
    p.label = 0;
    p.drawCell = 0;
    p.x = px;
    p.y = py;
    p.w = w;
    p.h = h;
    list = drawPanel(x, list, kRecord);
    if (text != 0) {
        list = drawStyledText(x, list, (uint32_t)(int32_t)p.gridX, (uint32_t)(int32_t)p.gridY, (uint32_t)(int32_t)p.cellW, 12, text, 2, 13, 13, 0xFF002020u);
    }
    return list;
}
static void swap_DrawTextPanel(SwapCtx &x) {
    x.ret(drawTextPanel(x, x.arg(0), x.arg(1), (int16_t)x.arg(2), (int16_t)x.arg(3), (int16_t)x.arg(4), (int16_t)x.arg(5)));
}
static SwapReg reg_DrawTextPanel({"libtitle.DrawTextPanel", MODULE, off(0x08f0b510), swap_DrawTextPanel});

/* FUN_08f0b690(list, drawCell): the button panel under the card list (327, 242, 151x23), one cell drawn by the callback */
static void swap_DrawOptionsPanel(SwapCtx &x) {
    constexpr uint32_t kRecord = kInfoBox;
    const uint32_t list = x.arg(0), drawCell = x.arg(1);
    Panel &p = x.at<Panel>(kRecord);
    p.x = 327;
    p.gridX = 332;
    p.y = 242;
    p.gridY = 247;
    p.w = 151;
    p.cellW = 140;
    p.h = 23;
    p.cellH = 12;
    p.colour = p.colour2 = 0xFFFFFFFFu;
    p.frameKind = 84;
    p.frameVariant = 10;
    p.drawCell = drawCell;
    p.flags = (uint8_t)((p.flags & 0xC0) | kPanelFlagsPlain);
    p.unknown25 = 1;
    p.cells = 1;
    p.unknown0 = 0;
    p.unknown23 = 0;
    p.cursor = 0;
    p.label = 0;
    x.ret(drawPanel(x, list, kRecord));
}
static SwapReg reg_DrawOptionsPanel({"libtitle.DrawOptionsPanel", MODULE, off(0x08f0b690), swap_DrawOptionsPanel});

/* FUN_08f0b7ac(list, drawCell): the wide panel across the card list's top, with a frame that pulses (the counter at 0x09B2B28C, steps of 5
 * frames over 0, 1, 2, 1) by growing one pixel on each side */
static void swap_DrawPulsingPanel(SwapCtx &x) {
    constexpr uint32_t kRecord = kInfoBox, kPulseCounter = 0x09B2B28C;
    const uint32_t list = x.arg(0), drawCell = x.arg(1);
    Panel &p = x.at<Panel>(kRecord);
    p.x = 4;
    p.gridX = 9;
    p.y = 28;
    p.gridY = 33;
    p.w = 473;
    p.cellW = 462;
    p.h = 23;
    p.cellH = 12;
    p.colour = p.colour2 = 0xFFFFFFFFu;
    p.frameKind = 84;
    p.frameVariant = 10;
    p.flags = (uint8_t)((p.flags & 0xC0) | kPanelFlagsPlain);
    p.cells = 1;
    p.unknown25 = 1;
    p.unknown0 = 0;
    p.unknown23 = 0;
    p.cursor = 0;
    const uint16_t counter = x.at<uint16_t>(kPulseCounter);
    if (counter != 0) {
        int32_t grow = (counter / 5) & 3;
        if (grow >= 3) grow = 1;
        p.x = (int16_t)(p.x - grow);
        p.y = (int16_t)(p.y - grow);
        p.w = (int16_t)(p.w + 2 * grow);
        p.h = (int16_t)(p.h + 2 * grow);
    }
    p.drawCell = drawCell;
    p.label = 0;
    x.ret(drawPanel(x, list, kRecord));
}
static SwapReg reg_DrawPulsingPanel({"libtitle.DrawPulsingPanel", MODULE, off(0x08f0b7ac), swap_DrawPulsingPanel});

/* FUN_08f0bae8(list, textIndex, archiveKey): the message bar at the bottom (y 231 on screen 14, else 187): a panel with the text on it */
static void swap_DrawMessageBar(SwapCtx &x) {
    constexpr uint32_t kRecord = kInfoBox;
    const uint32_t list = x.arg(0), textIndex = x.arg(1), archiveKey = x.arg(2);
    Panel &p = x.at<Panel>(kRecord);
    p.unknown0 = 0;
    p.y = state(x).screen == 14 ? 231 : 187;
    p.h = 34;
    p.w = 320;
    p.x = 4;
    p.colour = p.colour2 = 0xFFFFFFFFu;
    p.frameVariant = 3;
    p.unknown25 = 1;
    p.cells = 1;
    p.flags = (uint8_t)((p.flags & 0xC0) | kPanelFlagsPlain);
    p.cellW = (int16_t)(p.w - 11);
    p.gridX = (int16_t)(p.x + 5);
    p.gridY = (int16_t)(p.y + 5);
    p.cellH = (int16_t)(p.h - 11);
    p.frameKind = 0;
    p.unknown23 = 0;
    p.cursor = 0;
    p.label = x.callLocal(fn::MessageText, textIndex, archiveKey);
    p.drawCell = 0;
    ehsys::SetFontSize(x, 12, 12);
    ehsys::SetTextColor(x, 0xFF004040u);
    ehsys::SetUiFlag4(x, true);
    x.ret(drawPanel(x, list, kRecord));
}
static SwapReg reg_DrawMessageBar({"libtitle.DrawMessageBar", MODULE, off(0x08f0bae8), swap_DrawMessageBar});

/* FUN_08f0bc9c(list, text, cells, drawCell): the list panel on the right (327, 53, 151x185) with `cells` cells drawn by the callback, then a
 * title strip with `text` on its top */
static void swap_DrawListPanel(SwapCtx &x) {
    constexpr uint32_t kRecord = kCardBox;
    const uint32_t list = x.arg(0), text = x.arg(1), cells = x.arg(2), drawCell = x.arg(3);
    Panel &p = x.at<Panel>(kRecord);
    p.x = 327;
    p.gridX = 333;
    p.y = 53;
    p.gridY = 78;
    p.w = 151;
    p.h = 185;
    p.colour = p.colour2 = 0xFFFFFFFFu;
    p.frameVariant = 3;
    p.unknown25 = 1;
    p.flags = (uint8_t)((p.flags & 0xC0) | kPanelFlagsPlain);
    p.cellW = 138;
    p.cellH = 15;
    p.cells = (uint8_t)cells;
    p.drawCell = drawCell;
    p.unknown0 = 0;
    p.frameKind = 0;
    p.unknown23 = 0;
    p.cursor = 0;
    p.label = 0;
    uint32_t cur = drawPanel(x, list, kRecord);
    x.ret(drawTextPanel(x, cur, text, (int16_t)(p.x + 5), (int16_t)(p.y + 5), 140, 16));
}
static SwapReg reg_DrawListPanel({"libtitle.DrawListPanel", MODULE, off(0x08f0bc9c), swap_DrawListPanel});

/* FUN_08f1bd74(list): draw the card list page (options screen kind 2): the pulsing top panel and its two labels, the message bar, the window,
 * the header, the right-hand list panel (its cell index follows the step in arg2) and, on the last step, the cursor box */
static void swap_DrawCardListPage(SwapCtx &x) {
    State &s = state(x);
    uint32_t list = x.arg(0);
    if (s.arg1 > 7) {
        list = x.callLocal(fn::SlotDraw, list, 15);
        list = x.callLocal(fn::DrawMenuArrow, list);
    }
    list = x.callLocal(fn::DrawPulsingPanel, list, x.addr(fn::DrawCardLabel));
    list = x.callLocal(fn::DrawOptionsPanel, list, x.addr(fn::DrawOptionsLabel));
    const uint32_t spriteRow = x.callLocal(fn::OptionsSpriteRow);
    list = x.callLocal(fn::DrawMessageBar, list, spriteRow, 12);
    GuestStack frame(x, 32);
    const uint32_t local = frame.addr() + 28;
    x.at<uint32_t>(local) = list;
    x.callNid(nid::DrawWindow, local, kWindowA);
    list = x.at<uint32_t>(local);
    if (s.arg2 != 5) list = x.callLocal(fn::DrawCardHeader, list);
    int32_t cell = 0;
    if (s.arg2 != 0) { cell = s.arg2; if (!(cell < 3)) cell -= 1; }
    list = x.callLocal(fn::DrawListPanel, list, 20, (uint32_t)cell, x.addr(fn::DrawCardCell));
    if (s.arg2 == 5) list = x.callLocal(fn::DrawFramedBox, list, 0xFFFFFFFFu, 327, 242, 151, 23);
    x.ret(list);
}
static SwapReg reg_DrawCardListPage({"libtitle.DrawCardListPage", MODULE, off(0x08f1bd74), swap_DrawCardListPage});

/* FUN_08f1c0a0(list): draw the options / card list screen (screen 9) every frame: the slot-10 image except on the first steps, then the page
 * of the current kind (arg0), then count the frame */
static void swap_DrawOptionsScreen(SwapCtx &x) {
    State &s = state(x);
    uint32_t list = x.arg(0);
    if (s.arg1 != 5 && s.arg1 != 4 && s.arg1 != 3 && s.arg1 != 2 && s.arg1 != 1) list = x.callLocal(fn::SlotDraw, list, 10);
    switch ((uint16_t)s.arg0) {
    case 2: list = x.callLocal(fn::DrawCardListPage, list); break;
    case 3: list = x.callLocal(fn::DrawCardListKind3, list); break;
    case 4: list = x.callLocal(fn::DrawCardListKind4, list); break;
    case 5: list = x.callLocal(fn::DrawNothing, list); break;
    case 7:
        if (s.arg1 < 1 || s.arg1 > 4) list = x.callLocal(fn::DrawCardListPage2, list);
        else list = x.callNid(nid::FilterInput, list);
        break;
    }
    s.transition++;
    x.ret(list);
}
static SwapReg reg_DrawOptionsScreen({"libtitle.DrawOptionsScreen", MODULE, off(0x08f1c0a0), swap_DrawOptionsScreen});

