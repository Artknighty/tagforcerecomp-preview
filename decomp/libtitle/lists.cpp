/* libtitle swaps: The panel set, the list controller and list drawing (including the PC-style buttons). */
#include "internal.h"

/* ---- panel set (four tab records, an info box and a window), built once by InitFlow ---- */

/* FUN_08f09978(set, heap): the window part of the set, taken from the info box at set+144 */
static void swap_InitPanelWindow(SwapCtx &x) {
    const uint32_t address = x.arg(0), heap = x.arg(1);
    PanelSet &set = x.at<PanelSet>(address);
    const Panel &box = set.infoBox;
    Window &window = set.window;
    const uint32_t win = address + offsetof(PanelSet, window);
    x.callNid(nid::ClearWindow, win);
    memset(x.mem(win), 0, 288);
    window.styleKind = 1;
    window.frameKind = 0;
    window.unknown85 = 2;
    window.x = (uint16_t)box.x;
    window.y = (uint16_t)box.y;
    window.unknown96 = 13;
    window.width = (uint16_t)box.w;
    window.height = (uint16_t)box.h;
    window.colour = 0x00FFFFFFu;
    window.text = 0;
    window.unknown136 = 1;
    window.margin[0] = 5;
    window.margin[1] = 4;
    window.margin[2] = 6;
    window.margin[3] = 0;
    window.textSize = 0;
    window.textColour = 0xFFFFFFFFu;
    window.unknown160 = 0;
    window.highlightTextSize = 14;
    window.highlightColour = 0xFFFFFFFFu;
    window.animationA = 0;
    window.animationB = 0;
    window.unknown120 = 1;
    window.unknown124 = 1;
    window.heap = heap;
    window.flags = 0x0A;     /* bit fields: bits 1 and 3 set */
    window.flags2 = 0x10;
    window.unknown204 = 20;
    window.unknown206 = 8;
    window.unknown224 = 136;
    window.textWidth = (uint16_t)(box.cellW - ((int16_t)window.margin[0] + (int16_t)window.margin[1]));
    window.textY = (uint16_t)(box.cellH + 1);
    window.textRight = window.textWidth;
    window.unknown250 = 14;
    window.drawCallbackB = 0;
    window.drawCallback = x.addr(fn::DrawWindowCallback);
    x.callNid(nid::ClearWindow, win);          /* FUN_08f015fc(win) */
    x.callNid(nid::LayoutWindow, win);
}
static void regions_InitPanelWindow(SwapCtx &x, Regions &r) { touch(r, x.arg(0) + offsetof(PanelSet, window) + 0x4C, sizeof(Window) - 0x4C); }
static SwapReg reg_InitPanelWindow({"libtitle.InitPanelWindow", MODULE, off(0x08f09978), swap_InitPanelWindow, false, regions_InitPanelWindow});

/* FUN_08f0973c(set, heap): lay out the set: a strip of tab headers across the box (x, y, w, h) of the set, an info box under it and a window */
static void swap_InitPanelSet(SwapCtx &x) {
    const uint32_t address = x.arg(0), heap = x.arg(1);
    PanelSet &set = x.at<PanelSet>(address);
    set.flags = (uint8_t)((set.flags & ~7) | 5);
    set.x = 106;
    set.y = 39;
    set.w = 268;
    set.h = 218;
    set.selectedTab = 0;
    set.tabCount = 2;
    const int32_t count = set.tabCount ? set.tabCount : 1;
    for (int32_t i = 0; i < 4; i++) {
        TabRecord &tab = set.tab[i];
        x.callLocal(fn::ResetPanel, address + sizeof(TabRecord) * i);
        if (i == 0) tab.shape = (uint8_t)((tab.shape & ~0xF) | 4);                          /* first tab: left edge */
        else if (i < (int32_t)set.tabCount - 1) tab.shape = (uint8_t)((tab.shape & ~0xF) | 9);   /* middle tabs */
        else tab.shape = (uint8_t)((tab.shape & ~0xF) | 0xE);                               /* last tab: right edge */
        tab.w = (int16_t)(set.w / count);
        tab.h = 15;
        tab.x = (int16_t)(set.x + tab.w * i);
        tab.y = set.y;
    }
    x.callLocal(fn::ResetInfoBox, address + offsetof(PanelSet, infoBox));
    Panel &box = set.infoBox;
    box.x = set.x;
    box.y = (int16_t)(set.y + set.tab[0].h);
    box.w = set.w;
    box.h = (int16_t)(set.h - set.tab[0].h);
    box.gridX = (int16_t)(box.x + 5);
    box.gridY = (int16_t)(box.y + 6);
    box.cellW = 248;
    box.cellH = 23;
    if (box.h < 248) box.h = 0;
    x.callLocal(fn::InitPanelWindow, address, heap);
}
static SwapReg reg_InitPanelSet({"libtitle.InitPanelSet", MODULE, off(0x08f0973c), swap_InitPanelSet});

/* ---- the list controller ---- */

/* FUN_08f019ac(list, mode): one frame of a menu list. Block layout (see initList): +0 state, +1 state to go to after a cancel/confirm
 * animation, +2 what was chosen (0 stay on the list, 1/3 animate out), +3 state after a plain confirm, +4 open animation kind,
 * +5/+6 confirm/cancel behaviour, +8 item count, +60.. one byte per item (1 = disabled), +80 selected item, +82 flags (bit 0: wrap
 * around, bit 1: use the auto-repeat buttons), +92 colour of the open fade.
 * Result: the selected item when a choice was made, -1 nothing happened this frame, -2 closed, -3 not usable (wrong mode/state).
 * mode 1 = normal; mode 2 = force the closing states. States: see ListState. */
static int32_t listUpdate(SwapCtx &x, uint32_t p, uint32_t mode) {
    MenuList &ml = menu(x, p);
    auto sound = [&](uint32_t id) { x.callNid(nid::PlaySound, id); };
    if (mode == 2 && ml.state >= kListIdle) ml.state = kListSkipDisabled;
    const uint32_t state = ml.state;
    if (state >= 8) return -3;
    uint32_t last = 0;   /* index of the last item (r19) */
    uint32_t arm = state;
    for (;;) switch (arm) {
    case kListStart: ml.state = (uint8_t)(state + 1); arm = kListOpening; continue;
    case kListOpening: {                                   /* opening animation */
        const uint8_t kind = ml.openAnimation;
        if (kind == 3) return -1;
        if (kind == 2) { if (x.callLocal(fn::StepFadeTable, p, ml.openFadeColour) != 0) ml.state++; }
        else if (kind == 1 || kind == 0) ml.state++;         /* kind 1 waits on FUN_08f020b0, which always says yes */
        return -1;
    }
    case kListSkipDisabled: {                                   /* idle: skip disabled items under the cursor first */
        if (mode != 1) return -3;
        ml.state = (uint8_t)(state + 1);
        const uint32_t count = ml.itemCount;
        if (count) last = (count - 1) & 0xFF;
        int32_t skipped = 0, found = 0, i = ml.selected;
        if (!((int32_t)last < i)) {
            for (;;) {
                if (ml.itemState[i] == 1) { i++; if ((int32_t)last < i) break; skipped++; continue; }
                found = 1;
                break;
            }
        }
        if (found) ml.selected = (uint8_t)(ml.selected + (skipped & 0xFF));
        arm = kListIdle;
        continue;
    }
    case kListIdle: {                                   /* idle: buttons */
        const uint32_t pad = x.callNidKeepRegs(nid::GetInput);
        const uint32_t pressed = x.at<ehsys::PadState>(pad).pressed, repeat = x.at<ehsys::PadState>(pad).pressedOrRepeating;
        const uint32_t held = (ml.flags & 2) ? repeat : pressed;
        if (ml.itemCount) last = (ml.itemCount - 1) & 0xFF;
        if (pressed & (x.callNid(nid::GetButtonMask, 0) & 0xFFFF)) {                         /* confirm */
            if (ml.itemState[ml.selected] == 1) { sound(25109); return -1; }                      /* disabled item */
            sound(25107);
            ml.blink = (uint8_t)((ml.blink & ~0xC0) | 0x40);
            ml.chosen = ml.confirmBehaviour;
            switch (ml.chosen) {
            case 3: case 1: ml.state = kListAnimatingOut; ml.stateAfterAnimation = kListChosen; return -1;
            case 0: ml.state = ml.stateAfterConfirm; return ml.selected;
            }
            return -1;
        }
        if (pressed & (x.callNid(nid::GetButtonMask, 1) & 0xFFFF) && ml.cancelBehaviour != 4) {            /* cancel */
            ml.blink = (uint8_t)((ml.blink & ~0xC0) | 0x80);
            sound(25108);
            ml.state = kListAnimatingOut; ml.stateAfterAnimation = kListCancelled; ml.chosen = ml.cancelBehaviour;
            return -1;
        }
        const bool up = (pressed & 0x10) || (held & 0x10), down = (pressed & 0x40) || (held & 0x40);
        const int32_t sel = ml.selected;
        if (up) {
            if (sel <= 0) {
                if (!(ml.flags & 1)) { ml.selected = 0; sound(25106); return -1; }
                if (!(pressed & 0x10)) return -1;
                if (ml.itemState[last] == 1) sound(25109); else { ml.selected = (uint8_t)last; sound(25106); }
                return -1;
            }
            int32_t i = sel - 1, steps = 1, found = 0;
            while (i >= 0) {
                if (ml.itemState[i] == 1) { i--; steps++; continue; }
                found = 1;
                break;
            }
            if (!found) sound(25109); else { ml.selected = (uint8_t)(sel - (steps & 0xFF)); sound(25106); }
            return -1;
        }
        if (down) {
            if (sel < (int32_t)last) {
                int32_t i = sel + 1, steps = 1, found = 0;
                while (i <= (int32_t)last) {
                    if (ml.itemState[i] == 1) { i++; steps++; continue; }
                    found = 1;
                    break;
                }
                if (!found) sound(25109); else { ml.selected = (uint8_t)(sel + (steps & 0xFF)); sound(25106); }
                return -1;
            }
            if (!(ml.flags & 1)) { ml.selected = (uint8_t)last; sound(25106); return -1; }
            if (!(pressed & 0x40)) return -1;
            if (ml.itemState[0] == 1) sound(25109); else { ml.selected = 0; sound(25106); }
            return -1;
        }
        return -1;
    }
    case kListChosen:                                     /* closing after a choice */
        switch (ml.chosen) {
        case 3: ml.state = kListGone; return -1;
        case 1: ml.state = kListGone; return ml.selected;
        case 0: ml.state = kListGone; return ml.selected;
        }
        return -1;
    case kListAnimatingOut:                                     /* the blink after a confirm or cancel */
        switch (ml.chosen) {
        case 1: case 0: ml.state = ml.stateAfterAnimation; return -1;
        }
        return -1;
    case kListCancelled: ml.state = kListGone; return -2;
    case kListGone: ml.state = kListStart; return -3;
    default: return -3;
    }
}
static void swap_ListUpdate(SwapCtx &x) { x.ret((uint32_t)listUpdate(x, x.arg(0), x.arg(1))); }
static void regions_ListUpdate(SwapCtx &x, Regions &r) { touch(r, x.arg(0), 0x80); }
static SwapReg reg_ListUpdate({"libtitle.ListUpdate", MODULE, off(0x08f019ac), swap_ListUpdate, false, regions_ListUpdate});

/* ---- list drawing ---- */

struct SheetPos { int32_t u, v; };
static SheetPos cellOrigin(uint32_t cell) { return {(int32_t)(cell % kSheetColumns) * kCellWidth, (int32_t)(cell / kSheetColumns) * kCellHeight}; }

/* The highlight drawn over the cursor row: MenuList::blink holds a frame counter (bits 0-5, 0..59) and how to blink (bits 6-7) */
enum HighlightMode : uint32_t { kHighlightPulse = 0, kHighlightFlash = 1 };   /* any other mode hides the highlight */
static uint32_t highlightAlpha(uint8_t blink) {
    const uint32_t mode = blink >> 6, frame = blink & 0x3F;
    if (mode == kHighlightPulse) {   /* rises 207 -> 255 in 20 frames, holds 20, falls for the last 20 */
        if (frame < 20) return 207 + 48 * frame / 20;
        if (frame < 40) return 255;
        return 255 - 48 * (frame - 40) / 20;
    }
    if (mode == kHighlightFlash) return frame % 6 < 3 ? 255 : 64;   /* the quick blink after a confirm */
    return 0;
}

constexpr SheetPos kModernMainIcons[8] = {{80, 48}, {100, 48}, {40, 72}, {56, 72}, {72, 72}, {88, 72}, {104, 72}, {40, 96}};
constexpr SheetPos kModernTitleIcons[3] = {{56, 96}, {72, 96}, {88, 96}};   /* Continue, New Game, Exit to Desktop */

constexpr uint32_t kCursorCell = 0;   /* the orange cell: the cursor row of every list; gold in the PC style */


/* One neutral piece of the PC button across the row: the two caps and the stretched column, all from the half-height piece at line `v`.
 * All three are drawn scaled (caps 9 -> 10 px): the renderer samples a scaled sprite half a texel lower than an unscaled one, so mixing the
 * two showed as a step where the caps meet the middle. */
uint32_t drawModernPiece(SwapCtx &x, uint32_t list, int32_t rowX, int32_t rowY, int32_t width, int32_t v) {
    constexpr int32_t capWidth = kModernCap + 1;
    list = ehsys::SetSpriteSize(x, list, kModernCap, kModernPieceHeight, capWidth, kCellHeight);
    list = ehsys::DrawSprite(x, list, rowX, rowY, 0, kModernSheetU, v, 0, 0);
    list = ehsys::DrawSprite(x, list, rowX + width - capWidth, rowY, 0, kModernSheetU + kModernRightCap, v, 0, 0);
    list = ehsys::SetSpriteSize(x, list, 1, kModernPieceHeight, width - 2 * capWidth, kCellHeight);
    return ehsys::DrawSprite(x, list, rowX + capWidth, rowY, 0, kModernSheetU + kModernStretched, v, 0, 0);
}

/* One row's button in the PC style, in the colours of `cell`: the fill a quarter darker than the cell, the border and glow 45% towards white;
 * the cursor cell is gold. `colour` is the row's vertex colour (fade, disabled dimming, the highlight's pulse); it is set again at the end. */
static uint32_t drawModernButton(SwapCtx &x, uint32_t list, int32_t rowX, int32_t rowY, int32_t rowWidth, uint32_t cell, uint32_t colour) {
    const Rgb base = kSheetCellColours[cell % std::size(kSheetCellColours)];
    Rgb fill = {base.r * 3 / 4, base.g * 3 / 4, base.b * 3 / 4};
    Rgb border = {base.r + (255 - base.r) * 45 / 100, base.g + (255 - base.g) * 45 / 100, base.b + (255 - base.b) * 45 / 100};
    if (cell == kCursorCell) { fill = {255, 196, 72}; border = {255, 238, 160}; }
    const int32_t width = std::max(rowWidth, kCellWidth);
    list = ehsys::SetBlendColor(x, list, tinted(colour, fill));
    list = drawModernPiece(x, list, rowX, rowY, width, kModernFillV);
    list = ehsys::SetBlendColor(x, list, tinted(colour, border));
    list = drawModernPiece(x, list, rowX, rowY, width, kModernBorderV);
    return ehsys::SetBlendColor(x, list, colour);
}

/* One row's button as the game draws it: three LogoSprites (kListRowSprites: left cap, stretched middle, right cap) moved to the row's cell
 * of the sheet. The middle is stretched to the row's width less 9 pixels and the right cap sits 3 pixels before the row's end. */
static uint32_t drawClassicButton(SwapCtx &x, uint32_t list, int32_t rowX, int32_t rowY, int32_t rowWidth, SheetPos cell) {
    const uint32_t *pieces = (const uint32_t *)x.mem(x.addr(off(kListRowSprites)));
    const int32_t width = std::max(rowWidth, 9);
    for (int part = 0; part < 3; part++) {
        const LogoSprite &s = logoSprite(x, pieces[part]);
        int32_t offsetX = s.x, drawWidth = s.width;
        if (part == 1) drawWidth = width - 9;
        if (part == 2) offsetX = width - 3;
        list = ehsys::SetSpriteSize(x, list, s.sourceWidth, s.sourceHeight, drawWidth, s.height);
        list = ehsys::DrawSprite(x, list, rowX + offsetX, rowY + s.y, 0, s.u + cell.u, s.v + cell.v, s.mirrorU, s.mirrorV);
    }
    return list;
}

/* FUN_08f020b8(list, block): draw every item of a menu list (block layout: MenuList). Each row is a button whose cell of the sheet depends on
 * the row: the marked item (the language in use) and items in state kItemAlternate have their own cells; the cursor row gets a second pass
 * with the "pressed" cell as a blinking highlight. After each row the list's draw callback (if any) puts the label on it. In the PC style the
 * main menu and the title list also get an icon at the left of each row. */
constexpr int32_t kModernTitleRowTrim = 2;   /* the PC-style title list is 2 px tighter than the PSP's 24-pixel rows (o usuario, 2026-10-12) */
static uint32_t drawList(SwapCtx &x, uint32_t list, uint32_t p) {
    MenuList &ml = menu(x, p);
    const bool modern = gfx_modern_menus();
    const bool open = (int32_t)ml.state >= kListIdle;
    /* the highlight's frame counter runs 0..59 while the list is open, else it is held at 0 */
    if (!open) ml.blink &= 0xC0;
    else {
        uint8_t frame = (ml.blink + 1) & 0x3F;
        if (frame >= 60) frame = 0;
        ml.blink = (uint8_t)((ml.blink & 0xC0) | frame);
    }
    const uint32_t archive = currentArchive(x);
    const uint32_t buttonImage = logoSpriteName(x, *(const uint32_t *)x.mem(x.addr(off(kListRowSprites))));
    const bool mainMenu = ml.drawCallback == x.addr(fn::DrawListLabel), titleMenu = ml.drawCallback == x.addr(fn::DrawTitleItem);
    const int32_t rowX = (int16_t)ml.x, rowWidth = (int16_t)ml.width;
    const int32_t rowPitch = (int16_t)ml.rowHeight - (modern && titleMenu ? kModernTitleRowTrim : 0);
    for (int32_t i = 0; i < ml.itemCount; i++) {
        const int32_t rowY = (int16_t)((int16_t)ml.y + rowPitch * i);
        const bool disabled = ml.itemState[i] == kItemDisabled, onCursor = open && i == ml.selected;
        const uint32_t colour = ml.openAnimation == 2 && ml.state < kListSkipDisabled ? ml.itemColour[i] : disabled ? ml.disabledTextColour : ml.openFadeColour;
        list = x.callLocal(fn::SetupDrawState, list, 0);
        list = ehsys::SetBlendColor(x, list, colour);
        list = ehsys::BindTexture(x, list, ehsys::FindTexture(x, archive, buttonImage, 0));
        for (int pass = 0; pass <= (onCursor ? 1 : 0); pass++) {
            uint32_t cell = ml.spriteCell, passColour = colour;
            if (pass == 1) {
                const uint32_t alpha = highlightAlpha(ml.blink);
                passColour = alpha << 24 | alpha << 16 | alpha << 8 | alpha;
                list = ehsys::SetBlendColor(x, list, passColour);
                cell = ml.spriteCellPressed;
            } else if (ml.hasMarkedItem != 0 && i == ml.markedItem) cell = ml.spriteCellMarked;
            else if (ml.itemState[i] == kItemAlternate) cell = ml.spriteCellAlternate;
            list = modern ? drawModernButton(x, list, rowX, rowY, rowWidth, cell, passColour) : drawClassicButton(x, list, rowX, rowY, rowWidth, cellOrigin(cell));
        }
        const SheetPos *icon = !modern ? nullptr : mainMenu && i < (int32_t)std::size(kModernMainIcons) ? &kModernMainIcons[i]
                             : titleMenu && i < (int32_t)std::size(kModernTitleIcons) ? &kModernTitleIcons[i] : nullptr;
        if (icon) {   /* the row's icon, tinted: pink, dark on the cursor row, dim when disabled */
            const uint32_t rgb = disabled ? 0x00504060u : onCursor ? 0x00101A30u : 0x00D0A0FFu;
            list = ehsys::SetBlendColor(x, list, (colour & 0xFF000000u) | rgb);
            list = ehsys::SetSpriteSize(x, list, kModernIconSize, kModernIconSize, kModernIconSize, kModernIconSize);
            list = ehsys::DrawSprite(x, list, rowX + 16, rowY + 4, 0, icon->u, icon->v, 0, 0);
            list = ehsys::SetBlendColor(x, list, colour);
        }
        if (const uint32_t callback = ml.drawCallback)
            list = x.callAddr(callback, list, (uint32_t)i, (uint32_t)rowX, (uint32_t)rowY, (uint32_t)rowWidth, (uint32_t)(int16_t)ml.rowHeight);
    }
    return list;
}
static void swap_DrawList(SwapCtx &x) { x.ret(drawList(x, x.arg(0), x.arg(1))); }
static SwapReg reg_DrawList({"libtitle.DrawList", MODULE, off(0x08f020b8), swap_DrawList});

/* FUN_08f00c28(list, small, index, x, y, w, h, uOffset | stack: vOffset, mirrorU, mirrorV, colour): draw one sprite record at (x, y):
 * LogoSprite `index` (small = 0) or FramePart `index` (small != 0). A colour other than -1 is set first; w/h < 0 mean "the record's own
 * size"; mirrorU/mirrorV >= 0 replace the record's. uOffset/vOffset move the piece in its sheet (another colour set of the same piece). */
uint32_t drawSprite(SwapCtx &x, uint32_t list, uint32_t small, uint32_t index, uint32_t px, uint32_t py, int32_t w, int32_t h,
                           int32_t uOffset, int32_t vOffset, int32_t mirrorU, int32_t mirrorV, uint32_t colour) {
    list = x.callLocal(fn::SetupDrawState, list, 0);
    if (colour != 0xFFFFFFFFu) list = ehsys::SetBlendColor(x, list, colour);
    const uint32_t archive = x.callLocal(fn::CurrentLogoResource);
    struct { int32_t u, v, width, height, mirrorU, mirrorV; } piece;
    uint32_t image;
    if (small == 0) {
        const LogoSprite &s = logoSprite(x, index);
        piece = {s.u, s.v, s.sourceWidth, s.sourceHeight, s.mirrorU, s.mirrorV};
        image = x.callLocal(fn::ImageName, x.addr(off(kLogoSprites)), index);
    } else {
        const FramePart &f = ((const FramePart *)x.mem(x.addr(off(kFrameParts))))[index];
        piece = {f.u, f.v, f.width, f.height, f.mirrorU, f.mirrorV};
        image = x.callLocal(fn::ImageName, x.addr(off(kFrameParts)), index);
    }
    list = ehsys::BindTexture(x, list, ehsys::FindTexture(x, archive, image, 0));
    if (mirrorU >= 0) piece.mirrorU = mirrorU;
    if (mirrorV >= 0) piece.mirrorV = mirrorV;
    if (w < 0) w = piece.width;
    if (h < 0) h = piece.height;
    list = ehsys::SetSpriteSize(x, list, piece.width, piece.height, w, h);
    return ehsys::DrawSprite(x, list, (int32_t)px, (int32_t)py, 0, (int16_t)(piece.u + uOffset), (int16_t)(piece.v + vOffset),
                             (int16_t)piece.mirrorU, (int16_t)piece.mirrorV);
}
static void swap_DrawSprite(SwapCtx &x) {
    const uint32_t sp = x.c->r[29];   /* the four stack arguments are at the caller's sp+0..12 */
    x.ret(drawSprite(x, x.arg(0), x.arg(1), x.arg(2), x.arg(3), x.arg(4), (int32_t)x.arg(5), (int32_t)x.arg(6), (int32_t)x.arg(7),
                     (int32_t)x.at<uint32_t>(sp), (int32_t)x.at<int16_t>(sp + 4), (int32_t)x.at<int16_t>(sp + 8), x.at<uint32_t>(sp + 12)));
}
static SwapReg reg_DrawSprite({"libtitle.DrawSprite", MODULE, off(0x08f00c28), swap_DrawSprite});

/* FUN_08f030bc(list): the cursor/arrow decoration of the menu screens: which sprite and where depends on the screen (+8) and its step
 * (+0xC); draws nothing for screen 17 */
static void swap_DrawMenuArrow(SwapCtx &x) {
    State &s = state(x);
    uint32_t list = x.arg(0);
    int32_t type = 17, limit = 97, x2 = 112, y2 = 7, spriteIndex = 117, variant = 0;
    const int16_t screen = s.screen;
    if (screen == 40) { type = 13; }
    else if (screen == 12) { limit = 54; type = 15; spriteIndex = 118; }
    else if (screen == 14) { type = 14; limit = 54; spriteIndex = 118; }
    else if (screen == 19) { type = 13; }
    else if (screen == 17 || screen == 13 || screen == 6 || screen == 8 || screen == 9) { type = 13; }
    if (screen == 14) {
        x2 = 15; y2 = 4;
        const uint32_t k = (uint32_t)(int32_t)(s.arg0 - 4);
        if (k < 16) {
            switch (k) {
            case 0: variant = 0; break;   case 1: variant = 21; break;  case 2: case 3: variant = 42; break;
            case 4: variant = 189; break; case 5: variant = 210; break; case 10: variant = 84; break;
            case 12: variant = 126; break; case 13: variant = 105; break; case 14: variant = 147; break;
            case 15: variant = 126; break; }
        }
    } else if (screen == 12) {
        y2 = 4; x2 = 15;
        if (s.arg0 == 4) variant = 210; else if (s.arg0 == 3) variant = 189; else if (s.arg0 == 2) variant = 168;
    } else if (screen == 40 || screen == 19) variant = 105;
    else if (screen == 17) variant = 84;
    else if (screen == 13) variant = 63;
    else if (screen == 6) variant = 42;
    else if (screen == 8) variant = 21;
    else if (screen == 9) variant = 0;
    if (type == 17) { x.ret(list); return; }
    const int32_t slotState = (int32_t)slotQuery(x, (uint32_t)type);
    int32_t a6 = -1, active = 0;
    uint32_t colour = 0xFFFFFFFFu;
    if (slotState == 6 || slotState == limit) { colour = 0x80FFFFFFu; active = 1; a6 = 10; y2 += 5; }
    else if (slotState >= 7 && slotState < limit) active = 1;
    if (!active) { x.ret(list); return; }
    x.ret(drawSprite(x, list, 1, (uint32_t)spriteIndex, (uint32_t)x2, (uint32_t)y2, -1, a6, 0, variant, -1, -1, colour));
}
static SwapReg reg_DrawMenuArrow({"libtitle.DrawMenuArrow", MODULE, off(0x08f030bc), swap_DrawMenuArrow});

