/* libtitle swaps: Loading the language-dependent title menu archives, flow initialisation, the per-screen step functions, fades and the title menu text layout. */
#include "internal.h"

/* ---- loading the language-dependent title menu archives, a fade table, the menu setup ---- */

/* FUN_08f009c0(i): load title_menu/title_menu_?.ehp number i (names at 0x08f246f8, 0x40 apart) into a block of the title heap,
 * asynchronously; the completion callback is RegisterArchive (FUN_08f00a60) */
static void swap_LoadMenuArchive(SwapCtx &x) {
    const uint32_t i = x.arg(0);
    GuestStack frame(x, 272);
    const uint32_t path = frame.addr() + 16;
    x.callImport("sprintf", path, x.addr(off(0x08f246f8)) + (i << 6));
    const int32_t size = (int32_t)ehsys::FileSize(x, 3, path);
    if (size <= 0) { x.ret((uint32_t)size); return; }
    const uint32_t block = ehsys::Alloc(x, state(x).heap, (uint32_t)size);
    state(x).menuBuffer[i] = block;
    ehsys::ReadFileAsync(x, 3, path, block, (uint32_t)size, x.addr(fn::RegisterArchive), block);
}
static SwapReg reg_LoadMenuArchive({"libtitle.LoadMenuArchive", MODULE, off(0x08f009c0), swap_LoadMenuArchive});

/* FUN_08f008f0(mode): remember the system language, then load the menu archive for it (six archives, 3..8, one per language) */
static void swap_LoadMenuArchives(SwapCtx &x) {
    const uint32_t mode = x.arg(0);
    if (mode == 1) {
        x.callLocal(fn::FreeSlotBlock, x.at<uint32_t>(kLogoIndexAddr) + 3);
        x.callNid(nid::SetTutorialAndMessageSpeed, state(x).previousLanguage);
    }
    const uint32_t lang = x.callNidKeepRegs(nid::GetLanguage);
    x.at<uint32_t>(kLogoIndexAddr) = lang;     /* the "logo index" is the language */
    if (lang >= 6) return;
    x.callLocal(fn::LoadMenuArchive, 3 + lang);   /* one archive per language, numbered from 3 */
}
static SwapReg reg_LoadMenuArchives({"libtitle.LoadMenuArchives", MODULE, off(0x08f008f0), swap_LoadMenuArchives});

/* FUN_08f04c5c(heap, x, width, fontSize, d, e): clear the 384-byte title text box and store its six parameters, then BuildMenuInput */
static void swap_SetupMenuInput(SwapCtx &x) {
    memset(x.mem(kTextBox), 0, sizeof(TitleTextBox));
    TitleTextBox &box = x.at<TitleTextBox>(kTextBox);
    box.heap = x.arg(0);
    box.paramX = x.arg(1);
    box.paramWidth = x.arg(2);
    box.paramFontSize = x.arg(3);
    box.paramD = x.arg(4);
    box.paramE = x.arg(5);
    x.callLocal(fn::BuildMenuInput);
}
static SwapReg reg_SetupMenuInput({"libtitle.SetupMenuInput", MODULE, off(0x08f04c5c), swap_SetupMenuInput});

/* FUN_08f01ff8(list, colour): advance the fade-in of a list by one frame: item i gets alpha = clamp(frame - i*delay, 0, length) * 255 / length in the
 * top byte and the colour (low 24 bits) of `colour`. Returns 1 when the fade is over (and rewinds it), else 0. */
static void swap_StepFadeTable(SwapCtx &x) {
    MenuList &m = x.at<MenuList>(x.arg(0));
    const uint32_t colour = x.arg(1) & 0xFFFFFF;
    int16_t &frame = (int16_t &)m.fadeFrame;
    const int32_t step = (int16_t)m.fadeDelay, limit = (int16_t)m.fadeLength;
    if (!(frame < (int16_t)m.fadeEnd)) { frame = 0; m.fadeEnd = 0; x.ret(1); return; }
    const int32_t count = m.itemCount;
    for (int32_t i = 0, offset = 0; i < count; i++, offset += step) {
        int32_t v = frame - offset;
        if (v >= limit) v = limit; else if (v <= 0) v = 0;
        const int32_t alpha = limit ? (v * 255) / limit : (v * 255 < 0 ? 1 : -1);
        m.itemColour[i] = colour | ((uint32_t)alpha << 24);
    }
    frame++;
    x.ret(0);
}
static void regions_StepFadeTable(SwapCtx &x, Regions &r) { touch(r, x.arg(0) + offsetof(MenuList, fadeFrame), 4); touch(r, x.arg(0) + offsetof(MenuList, itemColour), 4 * 16); }
static SwapReg reg_StepFadeTable({"libtitle.StepFadeTable", MODULE, off(0x08f01ff8), swap_StepFadeTable, false, regions_StepFadeTable});

/* FUN_08f052d4(): prepare the title text box's drawing (font size, colours from its style) and return ehsys call GetFontFlags's low half */
static void swap_PrepareMenuDraw(SwapCtx &x) {
    const Window &style = x.at<Window>(kTextBoxWindow);
    const uint32_t keep = x.callNidKeepRegs(nid::GetFontFlags) & 0xFFFF;
    const uint32_t mode = x.callNidKeepRegs(nid::GetUiMode);
    const uint32_t shift = (mode == 1 || mode == 0) ? 1 : 3;
    if (x.callNidKeepRegs(nid::GetUiMode) != 0) {
        x.callNid(nid::SetUiFlag8, 1);
        x.callNid(nid::SetWideScreenFlag, 0);
    } else {
        x.callNid(nid::SetUiFlag1, 1);
        x.callNid(nid::SetWideScreenFlag, x.at<TitleTextBox>(kTextBox).wideFlag);
    }
    x.callNid(nid::SetUiFlag2, 1);
    const uint32_t size = style.textSize;
    x.callNid(nid::SetFontSize, size, size);
    x.callNid(nid::SetUiFlag4, style.unknown136);
    x.callNid(nid::SetTextColor, style.textColour);
    x.callNid(nid::SetTextFlags, shift << 6);
    x.ret(keep);
}
static SwapReg reg_PrepareMenuDraw({"libtitle.PrepareMenuDraw", MODULE, off(0x08f052d4), swap_PrepareMenuDraw});

/* ---- flow initialisation and the per-screen step functions ---- */
namespace {
constexpr uint32_t kHeapMemory = 0x08F29730;                                              /* 12 MB at the end of libtitle's image */

}  // namespace

/* FUN_08efcc30: set up the whole title flow: thread attributes, zero the state, make the 12 MB heap, build the windows and lists */
static void swap_InitFlow(SwapCtx &x) {
    x.callNidKeepRegs(nid::SeedRandom);
    x.callNid(nid::SetNetworkScene, 0);
    x.callImport("sceKernelChangeCurrentThreadAttr", 0, 16384);
    memset(x.mem(kStateAddr), 0, 13512);
    const uint32_t heap = ehsys::InitHeap(x, kHeapMemory, 0xC00000);
    state(x).heap = heap;
    x.callNid(nid::SetScreenOverlay, 0);
    x.callLocal(fn::InitList, kTitleList);
    x.callNid(nid::SetUiFlag2, 1);
    x.callLocal(fn::SetupMenuInput, state(x).heap, 39, 320, 14, 0, 0);
    x.callLocal(fn::InitWindow, kWindowA);
    x.callLocal(fn::InitWindow, kWindowB);
    x.callLocal(fn::InitWindow, kWindowC);
    x.callLocal(fn::InitList, kTitleList);
    x.callLocal(fn::InitList, kTitleList2);
    x.callLocal(fn::InitPanelSet, kPanelBlock, state(x).heap);
}
static SwapReg reg_InitFlow({"libtitle.InitFlow", MODULE, off(0x08efcc30), swap_InitFlow});

/* FUN_08efcfc0: step of the opening sequence. arg0 (0..15) picks, through a jump table in the module data, which of seven steps runs;
 * step 0 builds a window and advances, the others are the sub-steps FUN_08efd334, 08efd794, 08efda88, 08efddec, 08efecbc, 08efedd8. */
static void swap_OpeningStep(SwapCtx &x) {
    State &s = state(x);
    const uint32_t i = (uint16_t)s.arg0;
    if (i < 16) {
        switch (i) {
        case 1: x.callLocal(fn::InitWindow, kWindowC); s.arg0++; break;
        case 2: x.callLocal(fn::OpeningMenuInput, kWindowC); break;
        case 3: x.callLocal(fn::SubList3Input, kWindowC); break;
        case 5: x.callLocal(fn::SubList5Input, kWindowC); break;
        case 6: x.callLocal(fn::SubList6Input, kWindowC); break;
        case 9: x.callLocal(fn::LanguageReturnStep, kWindowC); break;
        case 14: x.callLocal(fn::SaveDialogStep, kWindowC); break;
        }
    }
    setStepResult(x);
}
static SwapReg reg_OpeningStep({"libtitle.OpeningStep", MODULE, off(0x08efcfc0), swap_OpeningStep});

/* FUN_08effb44: step of the loading sequence by arg0: 0 -> wait for loads and advance; 1 -> advance when 6FFA2A28 says so;
 * 2 -> advance when 778817A2 is not 1 (after 5836AFF1); 3 -> when 4E3BEA66 is true, go to screen 2 */
static void swap_LoadingStep(SwapCtx &x) {
    State &s = state(x);
    switch (s.arg0) {
    case 0:
        x.callNid(nid::SetScreenOverlay, 1);
        x.callLocal(fn::WaitLoadsAndPlay, 1);
        s.arg0++;
        x.callNid(nid::SetScreenOverlay, 0);
        break;
    case 1:
        if (x.callNid(nid::StartDataLoad, 1, state(x).heap) != 0) s.arg0++;
        break;
    case 2:
        if (x.callNidKeepRegs(nid::PollDataLoad) == 1) break;
        x.callNidKeepRegs(nid::StartLanguageDataLoad);
        s.arg0++;
        break;
    case 3:
        if (x.callNidKeepRegs(nid::IsLoadDone) != 0) x.callLocal(fn::SwitchScreen, 2, 0);
        break;
    }
    setStepResult(x);
}
static SwapReg reg_LoadingStep({"libtitle.LoadingStep", MODULE, off(0x08effb44), swap_LoadingStep});

/* FUN_08f03de0(list): draw the background slots for the title screens (not for steps 5, 6, 9) and run the step's extra drawing */
static void swap_DrawBackground(SwapCtx &x) {
    State &s = state(x);
    uint32_t list = x.arg(0);
    const uint16_t step = (uint16_t)s.arg0;
    if (step == 9 || step == 5 || step == 6) { x.ret(list); return; }
    x.callNidKeepRegs(nid::GetLanguage);
    list = x.callLocal(fn::SlotDraw, list, 0);
    const uint16_t v = (uint16_t)s.arg0;
    if (v < 13) {
        switch (v) {
        case 2: case 3: case 11: list = x.callLocal(fn::SlotDraw, list, 1); break;
        case 4: case 12: list = x.callLocal(fn::DrawList, list, kTitleList); break;
        case 7: if (s.arg1 >= 3) list = x.callLocal(fn::DrawWindowBlock, list, kWindowA); break;
        case 0: case 1: case 5: case 6: case 8: case 9: case 10: break;
        }
    }
    s.transition++;
    x.ret(list);
}
static SwapReg reg_DrawBackground({"libtitle.DrawBackground", MODULE, off(0x08f03de0), swap_DrawBackground});

/* FUN_08efd538(list, item, rowX, rowY, rowWidth): the label of one main menu row (the list's draw callback): the item's text, centred in the
 * row less 8 pixels on each side, 4 pixels down. While the list opens, a row whose fade has not reached full opacity has no label yet.
 * The texts are messages 20.. of the menu's message record in item order; message 24 is not one of them (it is skipped). */
static void swap_DrawListLabel(SwapCtx &x) {
    const uint32_t list = x.arg(0), item = x.arg(1) & 0xFFFF, rowX = x.arg(2), rowY = x.arg(3), rowWidth = x.arg(4);
    GuestStack frame(x, 544);
    memset(x.mem(frame.addr() + 32), 0, 512);   /* the original's text buffer (unused here, kept for identical memory) */
    const MenuList &m = menu(x, kTitleList);
    if (m.state < kListSkipDisabled && (m.itemColour[item] & 0xFF000000u) != 0xFF000000u) { x.ret(list); return; }
    constexpr uint32_t kFirstItemMessage = 20, kSkippedMessage = 24;
    uint32_t message = kFirstItemMessage + item;
    if (message >= kSkippedMessage) message++;
    x.callNid(nid::SetFontSize, 15, 15);
    x.callNid(nid::SetTextColor, itemColor(x, kTitleList, item));
    x.callNid(nid::SetUiFlag4, 1);
    if (item == kGraphics) { x.ret(drawAscii(x, list, (int32_t)rowX + 8, (int32_t)rowY + 4, (int32_t)rowWidth - 16, "Graphics", true)); return; }
    x.ret(x.callLocal(fn::DrawMessageText, list, rowX + 8, rowY + 4, rowWidth - 16, 0, message, 3));
}
static SwapReg reg_DrawListLabel({"libtitle.DrawListLabel", MODULE, off(0x08efd538), swap_DrawListLabel});

/* ---- more per-screen step functions ---- */

/* FUN_08f03a7c(screen, arg): enter the title menu screen. arg 12 (and 0) also set up the list; then load backgrounds 0 and 1 */
static void swap_EnterTitleMenu(SwapCtx &x) {
    const uint32_t screen = x.arg(0), arg = x.arg(1);
    State &s = state(x);
    x.callNidKeepRegs(nid::GetLanguage);
    const int16_t a = (int16_t)arg;
    if (a == 12 || a == 0) {
        const bool listed = a == 12;
        state(x).menuColour[0] = listed ? (0xFF400000u | 0x4040) : 0xFFFFFFFFu;
        state(x).menuColour[1] = 0xFFFFFFFFu;
        if (listed) {
            x.callLocal(fn::RequestScreen16, 0, 5, 4);
            x.callLocal(fn::InitTitleList);
            x.callLocal(fn::TitleMenuInput);
        } else {
            x.callLocal(fn::RequestScreen, 0, 30, 4095, 4094);
        }
        s.screen = (int16_t)screen;
        s.arg0 = a;
        s.arg1 = 0;
        s.transition = 0;
    }
    x.callLocal(fn::SlotLoad, 0, 2, 0, 89);
    x.callLocal(fn::SlotLoad, 1, 2, 0, 119);
    if (a != 9) x.callNid(nid::StopSoundChannel, 4);
}
static SwapReg reg_EnterTitleMenu({"libtitle.EnterTitleMenu", MODULE, off(0x08f03a7c), swap_EnterTitleMenu});

/* FUN_08f04f44: fill in the text box's 120-byte style record from the parameters SetupMenuInput stored, then StartMenuInput */
static void swap_BuildMenuInput(SwapCtx &x) {
    TitleTextBox &box = x.at<TitleTextBox>(kTextBox);
    Window &style = x.at<Window>(kTextBoxWindow);
    x.callNid(nid::InitWindowSet, kTextBoxWindow, box.heap);
    memset(&style.styleKind, 0, 120);   /* the style record: Window +0x4C .. +0xC4 */
    style.styleKind = 1;
    style.unknown124 = 1;
    style.unknown136 = 1;
    style.unknown85 = 2;
    style.margin[2] = 2;
    style.frameKind = (uint8_t)box.paramE;
    style.unknown120 = 0;
    style.y = 210;
    style.x = (uint16_t)box.paramX;
    style.text = 0;
    style.unknown160 = 0;
    style.height = 60;
    style.colour = 0xFFFFFFFFu;
    style.highlightColour = 0xFFFFFFFFu;
    style.width = (uint16_t)box.paramWidth;
    style.textColour = 0xFF000000u;
    style.margin[0] = 4;
    const uint8_t size = (uint8_t)box.paramFontSize;
    style.textSize = size;
    style.highlightTextSize = size;
    style.margin[1] = 20;
    x.callNid(nid::AddWindow, kTextBoxWindow, kTextBoxWindow + offsetof(Window, styleKind));
    x.callLocal(fn::StartMenuInput, box.paramX, box.paramWidth, box.paramFontSize, box.paramD, box.paramE);
}
static SwapReg reg_BuildMenuInput({"libtitle.BuildMenuInput", MODULE, off(0x08f04f44), swap_BuildMenuInput});

/* FUN_08efd090(list): the step of screen code 9 (and others that share it): draw the slots, then the extra drawing for arg0 */
static void swap_MenuScreenStep(SwapCtx &x) {
    State &s = state(x);
    uint32_t list = x.arg(0);
    if (s.arg0 == 9) { x.ret(list); return; }
    list = x.callLocal(fn::SlotDraw, list, 6);
    list = x.callLocal(fn::SlotDraw, list, 5);
    const int16_t v = s.arg0;
    if (!(v < 2) && v != 14 && v != 13 && v != 15) {
        list = x.callLocal(fn::SlotDraw, list, 13);
        list = x.callLocal(fn::DrawMenuArrow, list);
    }
    if ((uint16_t)s.arg0 < 16) {
        switch ((uint16_t)s.arg0) {
        case 0: case 1: case 2: list = x.callLocal(fn::DrawList, list, kTitleList); break;
        case 3: case 5: case 6: case 10: case 15:
            list = x.callLocal(fn::DrawList, list, kTitleList);
            list = x.callLocal(fn::DrawList, list, kTitleList2);
            break;
        case 14: list = x.callLocal(fn::DrawSaveDialog, list); break;
        case 4: case 7: case 8: case 9: case 11: case 12: case 13: break;
        }
    }
    s.transition++;
    x.ret(list);
}
static SwapReg reg_MenuScreenStep({"libtitle.MenuScreenStep", MODULE, off(0x08efd090), swap_MenuScreenStep});

/* FUN_08f00e88(list): draw the two sprites of the logo screen (kLogoScreenSprites), both from the first one's image */
static void swap_DrawLogos(SwapCtx &x) {
    uint32_t list = x.arg(0);
    const uint32_t *index = (const uint32_t *)x.mem(x.addr(off(kLogoScreenSprites)));
    list = x.callNid(nid::BindTexture, list, x.callNid(nid::FindTexture, currentArchive(x), logoSpriteName(x, index[0]), 0));
    for (int i = 0; i < 2; i++) {
        const LogoSprite &s = logoSprite(x, index[i]);
        list = ehsys::SetSpriteSize(x, list, s.sourceWidth, s.sourceHeight, s.width, s.height);
        list = ehsys::DrawSprite(x, list, s.x, s.y, 0, s.u, s.v, s.mirrorU, s.mirrorV);
    }
    x.ret(list);
}
static SwapReg reg_DrawLogos({"libtitle.DrawLogos", MODULE, off(0x08f00e88), swap_DrawLogos});

/* ---- fades and the title menu's text layout ---- */
#include <math.h>

/* FUN_08f013ac(type, mode, colour, frames): start a screen fade unless one is already running. Types 0-3 use black/white and mode 1
 * for the standard 16 frames; 4-5 take the colour; the two ehsys calls differ in direction (in / out). Then phase = 1, next screen = none. */
static void swap_StartFade(SwapCtx &x) {
    State &s = state(x);
    if (s.phase != 0) return;
    const uint32_t type = x.arg(0), mode = x.arg(1), colour = x.arg(2), frames = x.arg(3);
    const uint32_t standardMode = (frames & 0xFFFF) == 16 ? 1 : mode;
    if (type < 6) {   /* the jump table at 0x08f23c40 maps the type to one of six arms */
        switch (type) {
        case 0: x.callNid(nid::FadeFrom, standardMode, 0xFF000000u, frames); break;
        case 1: x.callNid(nid::FadeTo, standardMode, 0xFF000000u, frames); break;
        case 2: x.callNid(nid::FadeFrom, standardMode, 0xFFFFFFFFu, frames); break;
        case 3: x.callNid(nid::FadeTo, standardMode, 0xFFFFFFFFu, frames); break;
        case 4: x.callNid(nid::FadeFrom, mode, colour, frames); break;
        case 5: x.callNid(nid::FadeTo, mode, colour, frames); break;
        }
    }
    s.phase = 1;
    s.nextScreen = (int16_t)kNone;
}
static SwapReg reg_StartFade({"libtitle.StartFade", MODULE, off(0x08f013ac), swap_StartFade, false, regions_StateHead});


/* FUN_08f05058: lay out the text box's rows from its style record: each row gets x, y (rows step by the line height, in 1/64 units), height */
static void swap_LayoutRows(SwapCtx &x) {
    GuestStack frame(x, 64);
    const uint32_t out = frame.addr();
    const uint32_t outX = out + 60, outY = out + 56, outW = out + 52, outH = out + 48;   /* the content rectangle the engine reports */
    TitleTextBox &box = x.at<TitleTextBox>(kTextBox);
    const uint32_t keep = x.callLocal(fn::PrepareMenuDraw) & 0xFFFF;
    const uint32_t mode = x.callNidKeepRegs(nid::GetUiMode);
    const int32_t dy = (mode == 1 || mode == 0) ? 1 : 3;
    box.unknown13C = 0;
    box.firstChar = 0;
    x.callNid(nid::GetWindowContentRect, kTextBoxWindow, kTextBoxWindow + offsetof(Window, styleKind), outX, outY, outW, outH);
    box.rowWidth = x.at<uint32_t>(outW);
    const int32_t line = (int32_t)x.callNidKeepRegs(nid::GetLineHeight) >> 6;
    const int32_t count = (int32_t)box.rowCount;
    int32_t y = 0, extra = 0;
    for (int32_t i = 0; i < count; i++, y += line, extra += dy) {
        TextRow &row = box.row[i];
        row.x = x.at<uint32_t>(outX);
        row.y = x.at<uint32_t>(outY) + y;
        row.unknown8 = 0;
        row.height = line;
        row.textStart = 0;
        if (x.callNidKeepRegs(nid::GetUiMode) != 0) row.y += extra;
    }
    x.callLocal(fn::SetTextBoxFont, keep);
}
static SwapReg reg_LayoutRows({"libtitle.LayoutRows", MODULE, off(0x08f05058), swap_LayoutRows});

/* FUN_08f05190: same layout, with the position of the box taken from two float parameters (converted to integers into the style record) */
static void swap_LayoutRowsFloat(SwapCtx &x) {
    GuestStack frame(x, 64);
    const uint32_t out = frame.addr();
    const uint32_t outX = out + 60, outY = out + 56, outW = out + 52, outH = out + 48;   /* the content rectangle the engine reports */
    TitleTextBox &box = x.at<TitleTextBox>(kTextBox);
    Window &style = x.at<Window>(kTextBoxWindow);
    const uint32_t keep = x.callLocal(fn::PrepareMenuDraw) & 0xFFFF;
    const uint32_t mode = x.callNidKeepRegs(nid::GetUiMode);
    const int32_t dy = (mode == 1 || mode == 0) ? 1 : 3;
    auto toInt = [&](float v) -> int32_t { return v != v ? 0x7FFFFFFF : v >= 2147483648.0f ? 0x7FFFFFFF : v <= -2147483648.0f ? (int32_t)0x80000000 : (int32_t)truncf(v); };
    style.x = (uint16_t)toInt(box.x1);
    style.y = (uint16_t)toInt(box.y1);
    x.callNid(nid::GetWindowContentRect, kTextBoxWindow, kTextBoxWindow + offsetof(Window, styleKind), outX, outY, outW, outH);
    const int32_t step = ((int32_t)x.callNidKeepRegs(nid::GetLineHeight) >> 6) + 1;
    const int32_t count = (int32_t)box.rowCount;
    int32_t y = 0, extra = 0;
    for (int32_t i = 0; i < count; i++, y += step, extra += dy) {
        TextRow &row = box.row[i];
        row.x = x.at<uint32_t>(outX);
        row.y = x.at<uint32_t>(outY) + y;
        if (x.callNidKeepRegs(nid::GetUiMode) != 0) row.y += extra;
    }
    x.callLocal(fn::SetTextBoxFont, keep);
}
static SwapReg reg_LayoutRowsFloat({"libtitle.LayoutRowsFloat", MODULE, off(0x08f05190), swap_LayoutRowsFloat});

/* FUN_08f053d4: draw the text box's rows (if there is a text): each row's text starts where the previous one ended */
static void swap_DrawMenuRows(SwapCtx &x) {
    TitleTextBox &box = x.at<TitleTextBox>(kTextBox);
    if (box.text == 0) return;
    const uint32_t keep = x.callLocal(fn::PrepareMenuDraw) & 0xFFFF;
    x.callNid(nid::SetTextColor, 0);
    const int32_t count = (int32_t)box.rowCount;
    uint32_t start = box.firstChar;
    for (int32_t i = 0; i < count; i++) {
        TextRow &row = box.row[i];
        const uint32_t text = box.text;
        row.textStart = start;
        const uint32_t len = x.callNid(nid::DrawTextBox, row.x << 6, row.y << 6, box.rowWidth << 6, (row.height + 2) << 6, text + 2 * start, 0xFFFFFFFFu);
        row.textLength = len;
        const uint32_t total = x.callNid(nid::StringLength, box.text);
        const uint32_t end = start + total;
        if (end < total && x.at<uint16_t>(box.text + (end << 1)) == 32) row.textLength++;
        start += row.textLength;
    }
    x.callLocal(fn::SetTextBoxFont, keep);
}
static SwapReg reg_DrawMenuRows({"libtitle.DrawMenuRows", MODULE, off(0x08f053d4), swap_DrawMenuRows});

/* FUN_08f09574(box): reset the 44-byte info box record (position 50,50, size 380x170, shadow offsets, flags) */
static void swap_ResetInfoBox(SwapCtx &x) {
    Panel &box = x.at<Panel>(x.arg(0));
    memset(&box, 0, sizeof(Panel));
    box.x = 50;
    box.y = 50;
    box.w = 380;
    box.h = 170;
    box.colour = 0xFFFFFFFFu;
    box.colour2 = 0xFFFFFFFFu;
    box.frameVariant = 1;
    box.unknown25 = 8;
    box.cells = 8;
    box.columns = 1;
    box.gridX = 55;
    box.gridY = 55;
    box.cellW = 391;
    box.cellH = 34;
    x.ret(x.arg(0));   /* memset's result, left in r2 */
}
static void regions_ResetInfoBox(SwapCtx &x, Regions &r) { touch(r, x.arg(0), sizeof(Panel)); }
static SwapReg reg_ResetInfoBox({"libtitle.ResetInfoBox", MODULE, off(0x08f09574), swap_ResetInfoBox, false, regions_ResetInfoBox});

/* FUN_08f04d00(x, _, fontSize, d, e): start the title text box: slide-in positions as floats, row count from the ehsys UI mode, then reset it */
static void swap_StartMenuInput(SwapCtx &x) {
    const uint32_t xpos = x.arg(0), fontSize = x.arg(2), d = x.arg(3), e = x.arg(4);
    TitleTextBox &box = x.at<TitleTextBox>(kTextBox);
    box.rowCount = 0;
    box.text = 0;
    box.messageSpeed = x.callNidKeepRegs(nid::GetMessageSpeed);
    box.paramD = d;
    box.paramE = e;
    memset(box.row, 0, sizeof(box.row));
    box.unknown13C = 0; box.firstChar = 0;
    box.unknown148 = 0; box.unknown14C = 0; box.unknown150 = 0; box.unknown154 = 0;
    box.wideFlag = 0;
    const float fx = (float)(int32_t)xpos;
    box.x2 = fx;
    box.x3 = fx;
    box.x1 = fx;
    box.y2 = 300.0f;
    box.y1 = 300.0f;
    box.y3 = 210.0f;
    x.at<Window>(kTextBoxWindow).x = (uint16_t)xpos;
    if (x.callNidKeepRegs(nid::GetUiMode) != 0) { box.wideFlag = 0; box.rowCount = 3; }
    else { box.rowCount = 2; box.wideFlag = 1; }
    x.callLocal(fn::SetTextBoxFontSize, fontSize);
    x.callLocal(fn::ResetMenuInput, 0, 0);
    x.callLocal(fn::LayoutRowsFloat);
    box.changeRequested = 0;
}
static SwapReg reg_StartMenuInput({"libtitle.StartMenuInput", MODULE, off(0x08f04d00), swap_StartMenuInput});

