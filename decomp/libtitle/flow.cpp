/* libtitle swaps: The title flow: small helpers, the per-frame screen dispatcher, widget initialisers, resource slots and the logo steps. */
#include "internal.h"

/* FUN_08f015f0: phase of the running screen */
static void swap_GetPhase(SwapCtx &x) { x.ret(state(x).phase); }
static SwapReg reg_GetPhase({"libtitle.GetPhase", MODULE, off(0x08f015f0), swap_GetPhase, true});

/* FUN_08f00ad4: resource of the logo being shown (table indexed by kLogoIndex) */
static void swap_CurrentLogoResource(SwapCtx &x) { x.ret(currentArchive(x)); }
static SwapReg reg_CurrentLogoResource({"libtitle.CurrentLogoResource", MODULE, off(0x08f00ad4), swap_CurrentLogoResource, true});

/* FUN_08f00af4(i): the image file name of the i-th LogoSprite */
static void swap_LogoName(SwapCtx &x) { x.ret(logoSpriteName(x, x.arg(0))); }
static SwapReg reg_LogoName({"libtitle.LogoName", MODULE, off(0x08f00af4), swap_LogoName, true});

/* FUN_08f02ff4(slot): size/handle query on the resource in a slot, -1 when empty */
uint32_t slotQuery(SwapCtx &x, uint32_t slot) {
    const uint32_t h = x.at<uint32_t>(kHandleTable + 4 * slot);
    return h ? x.callNid(nid::GetSpriteFrameCount, h) : 0xFFFFFFFFu;
}
static void swap_SlotQuery(SwapCtx &x) { x.ret(slotQuery(x, x.arg(0))); }
static SwapReg reg_SlotQuery({"libtitle.SlotQuery", MODULE, off(0x08f02ff4), swap_SlotQuery});

/* FUN_08f0a870(mode): fade mode in the low two bits of the flags */
static void setFadeMode(SwapCtx &x, uint32_t mode) {
    x.at<uint16_t>(kFlagsTouchedAddr) = 0;
    uint8_t &f = x.at<uint8_t>(kFlagsAddr);
    f = (uint8_t)((f & 0xFC) | (mode & 3));
}
static void swap_SetFadeMode(SwapCtx &x) { setFadeMode(x, x.arg(0)); }
static void regions_SetFadeMode(SwapCtx &, Regions &r) { touch(r, kFlagsTouchedAddr, 2); touch(r, kFlagsAddr, 1); }
static SwapReg reg_SetFadeMode({"libtitle.SetFadeMode", MODULE, off(0x08f0a870), swap_SetFadeMode, false, regions_SetFadeMode});

/* FUN_08f04f34: ask for the screen change */
static void swap_RequestChange(SwapCtx &x) { x.at<TitleTextBox>(kTextBox).changeRequested = 1; }
static void regions_RequestChange(SwapCtx &, Regions &r) { touch(r, kTextBox, 4); }
static SwapReg reg_RequestChange({"libtitle.RequestChange", MODULE, off(0x08f04f34), swap_RequestChange, false, regions_RequestChange});

/* FUN_08f04e98(size): set both font sizes (normal and highlighted) of the text box's style */
static void swap_SetTextBoxFontSize(SwapCtx &x) {
    Window &style = x.at<Window>(kTextBoxWindow);
    style.textSize = (uint8_t)x.arg(0);
    style.highlightTextSize = (uint8_t)x.arg(0);
}
static void regions_SetTextBoxFontSize(SwapCtx &, Regions &r) { touch(r, kTextBoxWindow + offsetof(Window, textSize), 1); touch(r, kTextBoxWindow + offsetof(Window, highlightTextSize), 1); }
static SwapReg reg_SetTextBoxFontSize({"libtitle.SetTextBoxFontSize", MODULE, off(0x08f04e98), swap_SetTextBoxFontSize, false, regions_SetTextBoxFontSize});

/* FUN_08effc3c(screen, arg): start a screen straight away (next screen 3 = the movie/logo step) */
static void swap_StartScreen(SwapCtx &x) {
    State &s = state(x);
    s.screen = (int16_t)x.arg(0);
    s.nextScreen = 3;
    s.arg0 = (int16_t)x.arg(1);
    s.pendingArg0 = 0;
    s.arg1 = 0;
    s.transition = 0;
}
static SwapReg reg_StartScreen({"libtitle.StartScreen", MODULE, off(0x08effc3c), swap_StartScreen, false, regions_StateHead});

static void swap_FadeToBlack(SwapCtx &x) { fadeToBlack(x, x.arg(0), x.arg(1)); }
static SwapReg reg_FadeToBlack({"libtitle.FadeToBlack", MODULE, off(0x08f0139c), swap_FadeToBlack});

/* FUN_08f015a0(a, b, next, pending): fade out, then ask for a screen; args 1 and 2 are cleared */
static void swap_RequestScreen(SwapCtx &x) {
    const uint32_t next = x.arg(2), pending = x.arg(3);   /* the call clobbers the argument registers */
    fadeToBlack(x, x.arg(0), x.arg(1));
    State &s = state(x);
    s.nextScreen = (int16_t)next;
    s.pendingArg0 = (uint16_t)pending;
    s.pendingArg1 = 0;
    s.pendingArg2 = 0;
}
static SwapReg reg_RequestScreen({"libtitle.RequestScreen", MODULE, off(0x08f015a0), swap_RequestScreen, false, regions_StateHead});

/* FUN_08f00a60(archive, count): register an archive in slot 0 and count it, 0 when there is nothing to register */
static void swap_RegisterArchive(SwapCtx &x) {
    if ((int32_t)x.arg(1) < 1 || x.arg(0) == 0) { x.ret(0); return; }
    ehsys::RegisterArchive(x, 0, x.arg(0));
    state(x).arg0++;
    x.ret(1);
}
static SwapReg reg_RegisterArchive({"libtitle.RegisterArchive", MODULE, off(0x08f00a60), swap_RegisterArchive, false, regions_StateHead});

/* FUN_08f01fc0(list, length, delay): reset the fade-in of a list: each item fades in over `length` frames, an item starting `delay` frames after the previous one */
static void swap_ResetList(SwapCtx &x) {
    MenuList &m = x.at<MenuList>(x.arg(0));
    m.fadeLength = (uint16_t)x.arg(1);
    m.fadeDelay = (uint16_t)x.arg(2);
    m.fadeFrame = 0;
    m.fadeEnd = (uint16_t)((int16_t)m.fadeLength + (int16_t)m.fadeDelay * m.itemCount);
    memset(m.itemColour, 0, sizeof(m.itemColour));
    x.ret(x.arg(0) + offsetof(MenuList, itemColour));   /* memset's result */
}
static void regions_ResetList(SwapCtx &x, Regions &r) { touch(r, x.arg(0) + offsetof(MenuList, fadeLength), 0x40 - offsetof(MenuList, fadeLength)); }
static SwapReg reg_ResetList({"libtitle.ResetList", MODULE, off(0x08f01fc0), swap_ResetList, false, regions_ResetList});

/* FUN_08f015fc(window): tell ehsys about a window block (two ehsys calls); FUN_08f053b0(font): select the font of the text boxes (two ehsys calls) */
static void swap_RegisterWindow(SwapCtx &x) { const uint32_t a = x.arg(0); x.callNid(nid::ClearWindow, a); x.callNid(nid::LayoutWindow, a); }
static SwapReg reg_RegisterWindow({"libtitle.RegisterWindow", MODULE, off(0x08f015fc), swap_RegisterWindow});
static void swap_SetTextBoxFont(SwapCtx &x) { x.callNid(nid::SetFontFlags, x.arg(0)); x.callNid(nid::SetTextFlags, 0); }
static SwapReg reg_SetTextBoxFont({"libtitle.SetTextBoxFont", MODULE, off(0x08f053b0), swap_SetTextBoxFont});

/* FUN_08efefa8: runs every frame. Once the running screen says it is done (phase 2), commit the pending arguments (replace, or +1
 * for kIncrement) and, if a next screen was requested, switch to it (FUN_08eff100, not swapped yet). Returns the current screen. */
static void commit(int16_t &arg, uint16_t &pending) {
    if (pending == kNone) return;
    if (pending == kIncrement) arg++; else arg = (int16_t)pending;
    pending = kNone;
}
static void swap_UpdateFlow(SwapCtx &x) {
    State &s = state(x);
    if ((x.at<uint8_t>(kFlagsAddr) & 4) || s.phase != 2) { x.ret((uint32_t)(int32_t)s.screen); return; }
    setFadeMode(x, 0);
    s.phase = 0;
    commit(s.arg0, s.pendingArg0);
    commit(s.arg1, s.pendingArg1);
    commit(s.arg2, s.pendingArg2);
    if (s.nextScreen == (int16_t)kNone) { x.ret((uint32_t)(int32_t)s.screen); return; }
    s.screen = s.nextScreen;
    x.ret(x.callLocal(fn::SwitchScreen, (uint32_t)(int32_t)s.nextScreen, (uint32_t)(int32_t)s.arg0));
}
static SwapReg reg_UpdateFlow({"libtitle.UpdateFlow", MODULE, off(0x08efefa8), swap_UpdateFlow});   /* not verifiable: it starts screens (allocations), and verify runs a function more than once */

/* ---- the per-frame screen dispatcher: FUN_08f00238 ----
 * Opens a command list (ehsys threads one value through its drawing calls: each takes it and returns the next), runs the code of the
 * running screen, advances the phase when the screen reports it finished loading, closes the list, then lets some screens poke the
 * next step of the flow. The screen functions are not swapped yet and are called as they are. */
namespace {


/* the screen functions that take and return the command list value, by screen code; 0 = none (4 is special: takes nothing) */
uint32_t screenFunction(int16_t code) {
    switch (code) {
    case 40: return fn::TitleDraw; case 36: return fn::Original_08eff870; case kScreenLanguage: return fn::DrawLanguageSelect; case kScreenOptions: return fn::DrawOptions;
    case kScreenNetwork: return fn::Original_08f16c18; case 13: return fn::Original_08f048bc; case kScreenCardList: return fn::DrawOptionsScreen; case kScreenLists: return fn::MenuScreenStep;
    case kScreenMainMenu: return fn::DrawMainMenu;  case 6: return fn::Original_08f0c2fc;  case kScreenTitle: return fn::DrawBackground;  case 3: return fn::LogoStep;
    }
    return 0;
}
}  // namespace

static void swap_RunScreen(SwapCtx &x) {
    State &s = state(x);
    GuestStack frame(x, 32);
    const uint32_t slot = frame.addr() + 28;   /* the command list value lives here, its address is passed to PollLoad */
    uint32_t list = x.callNid(nid::BeginList, 0, 0);
    list = x.callNid(nid::Clear, list, 23);
    list = x.callLocal(fn::SetupDrawState, list, 0);
    list = x.callNid(nid::SetBlendColor, list, 0xFFFFFFFFu);
    x.callNid(nid::SetUiFlag1, 1);
    x.callNid(nid::SetUiFlag4, 1);

    const int16_t code = state(x).publishedScreen;
    if (code == 4) x.callLocal(fn::LeaveLogoScreen);
    else if (const uint32_t function = screenFunction(code)) list = x.callLocal(function, list);
    x.at<uint32_t>(slot) = list;

    if (x.callNid(nid::DrawFade, slot) == 1 && s.phase == 1) s.phase = 2;
    x.callNid(nid::EndList, x.at<uint32_t>(slot));

    /* some screens tell the flow to move on once their step is reached */
    auto arg0 = [&] { return state(x).arg0; };   /* read when tested: advancing can change them */
    auto arg1 = [&] { return state(x).arg1; };
    auto advance = [&] { x.callNidKeepRegs(nid::AdvanceFlow); };
    switch (state(x).publishedScreen) {
    case 40: if (x.at<int16_t>(kReadyAddr) == 1 && arg0() < 3) advance(); break;
    case 39: x.callLocal(fn::DeckTutorialFrame); break;
    case 24: x.callLocal(fn::TutorialListFrame); break;
    case kScreenNetwork: x.callLocal(fn::Screen14Step); break;
    case 26: x.callLocal(fn::LimitListStep); break;
    case 25: x.callLocal(fn::DuelRecordFrame); break;
    case 23: x.callLocal(fn::CardAlbumFrame); break;
    case kScreenCardList: case 22: case 30: case 29: if (x.at<uint8_t>(kCharAlbumReadyAddr)) x.callNidKeepRegs(nid::CharAlbumStep); break;
    case 21: if (x.at<uint8_t>(kCharAlbumReadyAddr)) x.callNidKeepRegs(nid::RecipeViewerStep); break;
    case kScreenMainMenu:
        if (arg0() == 3) { if (arg1() == 3) advance(); }
        else if (arg0() == 2) x.callLocal(fn::MainMenuNameFinished);
        break;
    case kScreenLists: if (arg0() == 9 && arg1() == 3) advance(); break;
    case 36: if (arg0() == 1 || arg0() == 4) advance();   /* then the case 5 test below */
        if (arg0() == 5 && arg1() == 3) advance();
        break;
    case kScreenTitle: if (arg0() == 5 && arg1() == 3) advance(); break;
    case 0: if (arg0() < 3) advance(); break;
    }
}
static SwapReg reg_RunScreen({"libtitle.RunScreen", MODULE, off(0x08f00238), swap_RunScreen});

/* ---- small helpers ---- */

/* FUN_08f01394(a) = FadeToBlack(a, 16) */
static void swap_FadeToBlack16(SwapCtx &x) { fadeToBlack(x, x.arg(0), 16); }
static SwapReg reg_FadeToBlack16({"libtitle.FadeToBlack16", MODULE, off(0x08f01394), swap_FadeToBlack16});

/* FUN_08f01550(a, next, pending): like RequestScreen with a 16-frame fade */
static void swap_RequestScreen16(SwapCtx &x) {
    const uint32_t next = x.arg(1), pending = x.arg(2);
    fadeToBlack(x, x.arg(0), 16);
    State &s = state(x);
    s.nextScreen = (int16_t)next;
    s.pendingArg0 = (uint16_t)pending;
    s.pendingArg1 = 0;
    s.pendingArg2 = 0;
}
static SwapReg reg_RequestScreen16({"libtitle.RequestScreen16", MODULE, off(0x08f01550), swap_RequestScreen16, false, regions_StateHead});

/* FUN_08f01208(fade): start the colour fade (its `active` flag = 1) */
static void swap_ActivateColourFade(SwapCtx &x) { x.at<ColourFade>(x.arg(0)).active = 1; }
static void regions_ActivateColourFade(SwapCtx &x, Regions &r) { touch(r, x.arg(0) + offsetof(ColourFade, active), 2); }
static SwapReg reg_ActivateColourFade({"libtitle.ActivateColourFade", MODULE, off(0x08f01208), swap_ActivateColourFade, false, regions_ActivateColourFade});

/* FUN_08f011e8(fade, colour, to, frames): prepare a fade of the ARGB colour at guest address `colour` (its current value is the start) to `to` */
static void swap_StartColourFade(SwapCtx &x) {
    ColourFade &fade = x.at<ColourFade>(x.arg(0));
    const uint32_t colour = x.arg(1);
    fade.target = colour;
    fade.from = x.at<uint32_t>(colour);
    fade.to = x.arg(2);
    fade.frames = (uint16_t)x.arg(3);
    fade.step = 0;
    fade.active = 0;
}
static void regions_StartColourFade(SwapCtx &x, Regions &r) { touch(r, x.arg(0), 18); }
static SwapReg reg_StartColourFade({"libtitle.StartColourFade", MODULE, off(0x08f011e8), swap_StartColourFade, false, regions_StartColourFade});

/* FUN_08f03564: save flag 8 set? */
static void swap_SaveFlag8(SwapCtx &x) { x.ret(ehsys::SaveFlag(x, 8) ? 1 : 0); }
static SwapReg reg_SaveFlag8({"libtitle.SaveFlag8", MODULE, off(0x08f03564), swap_SaveFlag8});

/* FUN_08f00b74(i): i-th message record (0x84 bytes) of title_msg, counted from the current logo index */
static void swap_MessageRecord(SwapCtx &x) {
    const uint32_t i = x.at<uint32_t>(kLogoIndexAddr) + x.arg(0);
    x.ret(x.addr(off(0x08f252fc)) + i * 0x84);
}
static SwapReg reg_MessageRecord({"libtitle.MessageRecord", MODULE, off(0x08f00b74), swap_MessageRecord, true});

/* FUN_08f00b1c(table, row): name of the image a table entry (0x16-byte rows, image index at +1) refers to; images are 0x40 bytes apart */
static void swap_ImageName(SwapCtx &x) {
    const uint8_t index = x.at<uint8_t>(x.arg(0) + x.arg(1) * 0x16 + 1);
    x.ret(x.addr(off(0x08f24d78)) + index * 0x40u);
}
static SwapReg reg_ImageName({"libtitle.ImageName", MODULE, off(0x08f00b1c), swap_ImageName, true});


/* FUN_08f03518: does any of the card-collection records 2..151 have a non-zero count? */
static void swap_AnyCardOwned(SwapCtx &x) {
    for (uint32_t i = 2; i < 0x98; i++) {
        if (x.at<ProgressRecord>(x.callNid(nid::GetProgressRecord, i)).count != 0) { x.ret(1); return; }
    }
    x.ret(0);
}
static SwapReg reg_AnyCardOwned({"libtitle.AnyCardOwned", MODULE, off(0x08f03518), swap_AnyCardOwned});

/* FUN_08f046b4: copy settings 2, 0, 1 through a get/set pair of ehsys calls */
static void swap_RewriteSettings(SwapCtx &x) {
    for (uint32_t i : {2u, 0u, 1u}) { const uint32_t v = x.callNid(nid::GetVolume, i); x.callNid(nid::SetVolume, i, v); }
}
static SwapReg reg_RewriteSettings({"libtitle.RewriteSettings", MODULE, off(0x08f046b4), swap_RewriteSettings});

/* FUN_08efd1a8: progress of the resource in slot 13: 0 not started (<16), 1 at 16 (restarts it: FUN_08f02f68), 2 loading (<96), 3 done */
static void swap_Slot13Progress(SwapCtx &x) {
    const int32_t v = (int32_t)slotQuery(x, 13);
    if (v < 16) { x.ret(0); return; }
    if (v == 16) { x.callLocal(fn::SlotSetRange, 13, 2, 17, 96); x.ret(1); return; }
    x.ret(v < 96 ? 2 : 3);
}
static SwapReg reg_Slot13Progress({"libtitle.Slot13Progress", MODULE, off(0x08efd1a8), swap_Slot13Progress});



GraphicsScreen graphics;

/* Draw an ASCII text (the engine wants UTF-16) at (px, py) in a field `width` pixels wide; with `centre`, a narrower text is centred in it.
 * Returns the new list. For the texts this screen adds to the game: the game's own texts come from its message files. */
uint32_t drawAscii(SwapCtx &x, uint32_t list, int32_t px, int32_t py, int32_t width, const char *text, bool centre) {
    GuestStack frame(x, 128);
    uint16_t *chars = (uint16_t *)x.mem(frame.addr());
    int32_t length = 0;
    for (; text[length] != 0 && length < 60; length++) chars[length] = (uint8_t)text[length];
    chars[length] = 0;
    if (centre) {
        const int32_t textWidth = ehsys::TextWidth(x, frame.addr()) >> 6;
        if (textWidth < width) { px += (width - textWidth) / 2; width = textWidth; }
    }
    ehsys::BeginText(x, list);
    ehsys::DrawTextAligned(x, px << 6, py << 6, (width + 2) * 0x40, frame.addr());
    return ehsys::EndText(x);
}


/* FUN_08f0270c(p, i): colour of item i of a list: selected item's, else the list's normal/disabled colours */
uint32_t itemColor(SwapCtx &x, uint32_t p, uint32_t item) {
    const uint32_t i = item & 0xFFFF;
    const MenuList &m = menu(x, p);
    if (m.itemState[i] == kItemDisabled) return m.disabledColour;
    if (m.state < kListIdle) return m.normalColour;
    if (i == m.selected && (m.blink >> 6) < 2) return m.selectedColour;
    return m.normalColour;
}
static void swap_ItemColor(SwapCtx &x) { x.ret(itemColor(x, x.arg(0), x.arg(1))); }
static SwapReg reg_ItemColor({"libtitle.ItemColor", MODULE, off(0x08f0270c), swap_ItemColor, true});

/* FUN_08f1abfc / ace4 / ac64: draw a text line (08f1ace4: DrawText), a text in a box (08f1abfc: DrawTextAligned) or a filled rectangle (ac64) at a pixel
 * position, converted to 1/64 units (width + 2). All bracket the call with BeginText / EndText; the begin call also gets the list. */
static void swap_DrawBoxedText(SwapCtx &x) {
    const uint32_t list = x.arg(0), px = x.arg(1), py = x.arg(2), width = x.arg(3), text = x.arg(4);
    ehsys::BeginText(x, list);
    ehsys::DrawTextAligned(x, (int32_t)(px << 6), (int32_t)(py << 6), (int32_t)((width + 2) << 6), text);
    x.callNidKeepRegs(nid::EndText);
}
static SwapReg reg_DrawBoxedText({"libtitle.DrawBoxedText", MODULE, off(0x08f1abfc), swap_DrawBoxedText});
static void swap_DrawTextLine(SwapCtx &x) {
    const uint32_t list = x.arg(0), px = x.arg(1), py = x.arg(2), width = x.arg(3), text = x.arg(4);
    ehsys::BeginText(x, list);
    ehsys::DrawText(x, (int32_t)(px << 6), (int32_t)(py << 6), (int32_t)((width + 2) << 6), text);
    x.callNidKeepRegs(nid::EndText);
}
static SwapReg reg_DrawTextLine({"libtitle.DrawTextLine", MODULE, off(0x08f1ace4), swap_DrawTextLine});
static void swap_DrawRect(SwapCtx &x) {
    const uint32_t a0 = x.arg(0), px = x.arg(1), py = x.arg(2), w = x.arg(3), h = x.arg(4), col = x.arg(5);
    x.callNid(nid::BeginText, a0);
    x.callNid(nid::DrawTextBox, px << 6, py << 6, (w + 2) << 6, (h + 2) << 6, col, 0xFFFFFFFFu);
    x.callNidKeepRegs(nid::EndText);
}
static SwapReg reg_DrawRect({"libtitle.DrawRect", MODULE, off(0x08f1ac64), swap_DrawRect});

/* ---- widget initialisers ----
 * The title menus are built from fixed-size control blocks in guest memory (a list of items with a cursor and colours, and a bigger
 * window block). Only the offsets used are named here. */
/* the shared body of FUN_08f01860 (the callers 08f03ed8 and others reuse it) */
void initList(SwapCtx &x, uint32_t p) {
    MenuList &m = menu(x, p);
    m.state = kListStart;
    m.unknown7 = 0;
    m.itemCount = 8;
    m.stateAfterConfirm = kListChosen;
    m.confirmBehaviour = 0;
    m.cancelBehaviour = 0;
    m.chosen = 0;
    m.openAnimation = 2;           /* the original writes 0, 1, 2 in turn */
    x.callLocal(fn::ResetList, p, 4, 1);
    for (uint8_t &item : m.itemState) item = kItemEnabled;
    m.x = 128; m.y = 40; m.width = 240; m.rowHeight = 32;
    m.spriteCell = 1;
    m.spriteCellAlternate = 5;
    m.selected = 0;
    m.spriteCellPressed = 0;
    m.flags = 255;
    m.hasMarkedItem = 0;
    m.markedItem = 0;
    m.spriteCellMarked = 4;
    m.unknown86 = 1;
    m.openFadeColour = 0xFFFFFFFFu;
    m.normalColour = 0xFFFFFFFFu;
    m.selectedColour = 0xFF000000u;
    m.disabledTextColour = 0xFF808080u;
    m.disabledColour = 0xFF808080u;
    m.unknown88 = 255;
    m.blink = 0;
    m.drawCallback = 0;
}
static void swap_InitList(SwapCtx &x) { initList(x, x.arg(0)); }
static void regions_InitList(SwapCtx &x, Regions &r) { touch(r, x.arg(0), 0x7C); }
static SwapReg reg_InitList({"libtitle.InitList", MODULE, off(0x08f01860), swap_InitList, false, regions_InitList});



/* FUN_08f03ed8: the main title list at 0x09B29FAC */
static void swap_InitTitleList(SwapCtx &x) {
    const uint32_t p = kTitleList;
    initList(x, p);
    MenuList &m = menu(x, p);
    m.unknown7 = 0;
    /* the game starts on item 1 (Continue) when SaveHasErrors says so, else on item 0 (New Game): same rule, with the new order */
    m.selected = x.callNidKeepRegs(nid::SaveHasErrors) != 0 ? kContinue : kNewGame;
    m.itemCount = kTitleItemCount;
    m.x = 140; m.y = 176; m.width = 200; m.rowHeight = 24;   /* three rows, spaced like the original two: the last one ends just above the copyright line */
    x.callLocal(fn::ResetList, p, 4, 1);
    m.drawCallback = x.addr(fn::DrawTitleItem);   /* the list's draw callback */
}
static void regions_InitTitleList(SwapCtx &, Regions &r) { touch(r, kTitleList, sizeof(MenuList)); }
static SwapReg reg_InitTitleList({"libtitle.InitTitleList", MODULE, off(0x08f03ed8), swap_InitTitleList, false, regions_InitTitleList});

/* FUN_08f01628(p): initialise a 0x120-byte window block (the end calls FUN_08f015fc) */
static void swap_InitWindow(SwapCtx &x) {
    const uint32_t p = x.arg(0);
    Window &window = x.at<Window>(p);
    x.callNid(nid::ClearWindow, p);
    memset(x.mem(p), 0, 288);
    window.styleKind = 1;
    window.frameKind = 0;
    window.unknown85 = 2;
    window.x = 39;
    window.y = 224;
    window.unknown96 = 0;
    window.width = 401;
    window.height = 46;
    window.colour = 0xFFFFFFFFu;
    window.text = 0;
    window.unknown136 = 1;
    window.margin[0] = 12;
    window.margin[1] = 12;
    window.margin[2] = 6;
    window.margin[3] = 6;
    window.textSize = 13;
    window.textColour = 0xFFFFFFFFu;
    window.unknown160 = 0;
    window.highlightTextSize = 13;
    window.highlightColour = 0xFFFFFFFFu;
    window.animationA = 2;
    window.animationB = 2;
    window.unknown120 = 0;
    window.unknown124 = 1;
    window.heap = state(x).heap;
    window.flags = 0x01;   /* bit fields: bit 0 set, bits 1-5 clear */
    window.flags2 = 0x10;
    window.unknown204 = 2;
    window.unknown206 = 2;
    window.textWidth = 377;
    window.textY = 13;
    x.callNid(nid::ClearWindow, p);        /* FUN_08f015fc(p) */
    x.callNid(nid::LayoutWindow, p);
}
/* the first 0x4c bytes hold ids taken from a global pool (they differ between two runs from the same state), so they are not compared */
static void regions_InitWindow(SwapCtx &x, Regions &r) { touch(r, x.arg(0) + 0x4c, 288 - 0x4c); }
static SwapReg reg_InitWindow({"libtitle.InitWindow", MODULE, off(0x08f01628), swap_InitWindow, false, regions_InitWindow});

/* ---- resource slots ----
 * The flow keeps up to 0x30 loaded images/movies in kHandleTable. A slot's handle comes from FindTexture in the archive of the current
 * logo; a "mode" then configures how it plays (a pair of numbers passed on to ehsys). */
static void slotSetRange(SwapCtx &x, uint32_t slot, uint32_t mode, uint32_t a, uint32_t b) {
    const uint32_t handle = x.at<uint32_t>(kHandleTable + 4 * slot);
    if (!handle) return;   /* the original leaves the result register alone */
    if (mode == 0) x.ret(x.callNid(nid::SetSpriteFrame, handle, a, a + 1));
    else if (mode == 1 || mode == 2) x.ret(x.callNid(nid::SetSpriteFrame, handle, a, b + 1));
}

/* FUN_08f02f68(slot, mode, a, b) */
static void swap_SlotSetRange(SwapCtx &x) { slotSetRange(x, x.arg(0), x.arg(1), x.arg(2), x.arg(3)); }
static SwapReg reg_SlotSetRange({"libtitle.SlotSetRange", MODULE, off(0x08f02f68), swap_SlotSetRange});

/* FUN_08f02edc(slot, mode, a, b): load the slot's image (name table at 0x08f24938, 0x40 bytes each) from the current logo's archive, then SlotSetRange */
static void swap_SlotLoad(SwapCtx &x) {
    const uint32_t slot = x.arg(0), mode = x.arg(1), a = x.arg(2), b = x.arg(3);
    const uint32_t archive = currentArchive(x);
    x.at<uint32_t>(kHandleTable + 4 * slot) = x.callNid(nid::FindTexture, archive, x.addr(off(0x08f24938)) + (slot << 6), 0);
    slotSetRange(x, slot, mode, a, b);
}
static SwapReg reg_SlotLoad({"libtitle.SlotLoad", MODULE, off(0x08f02edc), swap_SlotLoad});

/* FUN_08f0302c(list, slot): draw the slot's image into the command list (returns the new list value; unchanged when the slot is empty) */
static void swap_SlotDraw(SwapCtx &x) {
    const uint32_t list = x.arg(0), slot = x.arg(1);
    const uint32_t handle = x.at<uint32_t>(kHandleTable + 4 * slot);
    if (!handle) { x.ret(list); return; }
    GuestStack frame(x, 16 + 96);
    uint8_t *params = x.mem(frame.addr() + 16);
    memset(params, 0, 96);
    const uint32_t src = x.callNidKeepRegs(nid::FrameCounter);
    x.at<uint32_t>(frame.addr() + 16) = x.at<uint32_t>(src);
    const uint32_t l = x.callNid(nid::BeginSpriteDraw, list);
    const uint32_t r = x.callNid(nid::DrawSpriteResource, l, handle, frame.addr() + 16);
    x.ret(x.callNid(nid::EndSpriteDraw, r));
}
static SwapReg reg_SlotDraw({"libtitle.SlotDraw", MODULE, off(0x08f0302c), swap_SlotDraw});

/* FUN_08f0378c(a,b,c,d,msg,text,g): look up a text of the current logo's message record, then FUN_08f03840(a,b,c,d,0,text,g) */
static uint32_t drawMessageText(SwapCtx &x, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t msg, uint32_t textIndex, uint32_t g) {
    const uint32_t archive = currentArchive(x);
    const uint32_t record = x.addr(off(0x08f252fc)) + (x.at<uint32_t>(kLogoIndexAddr) + msg) * 0x84;
    const uint32_t table = x.callNid(nid::FindTexture, archive, record, 0);
    const uint32_t text = x.callNid(nid::GetText, table, textIndex);
    return x.callLocal(fn::DrawAlignedText, a, b, c, d, 0, text, g);
}
static void swap_DrawMessageText(SwapCtx &x) { x.ret(drawMessageText(x, x.arg(0), x.arg(1), x.arg(2), x.arg(3), x.arg(4), x.arg(5), x.arg(6))); }
static SwapReg reg_DrawMessageText({"libtitle.DrawMessageText", MODULE, off(0x08f0378c), swap_DrawMessageText});

/* FUN_08f02770(list, mode): set up the 2D drawing state; mode 2 uses the "overlay" blend values, otherwise the defaults */
static void swap_SetupDrawState(SwapCtx &x) {
    const uint32_t mode = x.arg(1);
    uint32_t v = x.callNid(nid::SetClearColor, x.arg(0), 0);
    v = x.callNid(nid::SetClearStencil, v, 0);
    v = x.callNid(nid::SetClearDepth, v, 0);
    if (mode == 2) {
        v = x.callNid(nid::DisableState, v, 9);
        v = x.callNid(nid::DisableState, v, 1);
        v = x.callNid(nid::SetTextureFunction, v, 4, 0);
        v = x.callNid(nid::SetColor, v, 0x60000000);
    } else {
        v = x.callNid(nid::EnableState, v, 9);
        v = x.callNid(nid::EnableState, v, 1);
        v = x.callNid(nid::SetTextureFunction, v, 0, 1);
        v = x.callNid(nid::SetColor, v, 0xFFFFFFFFu);
    }
    v = x.callNid(nid::SetTextureFilter, v, 0, 0);
    v = x.callNid(nid::DisableState, v, 1);
    v = x.callNid(nid::DisableState, v, 10);
    v = x.callNid(nid::EnableState, v, 4);
    v = x.callNid(nid::SetBlend, v, 0, 2, 3, 0, 0);
    x.ret(x.callNid(nid::SetBlendColor, v, 0xFFFFFFFFu));
}
static SwapReg reg_SetupDrawState({"libtitle.SetupDrawState", MODULE, off(0x08f02770), swap_SetupDrawState});

/* ---- screen "3" (logo/message step) and small screen helpers ---- */

/* FUN_08f00744(list): the step of screen code 3. arg0 (0..8) picks one of three drawing routines through a jump table in the module data. */
static void swap_LogoStep(SwapCtx &x) {
    const uint32_t list = x.arg(0);
    x.callNid(nid::SetFontSize, 32, 32);     /* font size */
    x.callNid(nid::SetTextColor, 0xFFFFFFFFu);  /* text colour */
    const uint32_t i = (uint16_t)state(x).arg0;
    if (i >= 9) { x.ret(list); return; }
    uint32_t function = 0;
    switch (i / 3) {   /* three steps for each of the three drawing routines */
    case 0: function = fn::DrawLogos; break;
    case 1: function = fn::Original_08f00fa4; break;
    case 2: function = fn::Original_08f010c0; break;
    }
    x.ret(function ? x.callLocal(function, list) : list);
}
static SwapReg reg_LogoStep({"libtitle.LogoStep", MODULE, off(0x08f00744), swap_LogoStep});

/* FUN_08f00160: leave the logo/message screen: build a filename from the system language, clear the whole edram, free the screen's
 * block and mark the flow as ready for the next screen */
static void swap_LeaveLogoScreen(SwapCtx &x) {
    GuestStack frame(x, 272);
    const uint32_t buffer = frame.addr() + 16;
    const uint32_t format = x.addr(off(x.callNidKeepRegs(nid::GetLanguage) == 3 ? 0x08f27410 : 0x08f273fc));
    x.callNid(nid::BuildSavePath, buffer, 3, format);
    x.callNid(nid::PrepareSaveDialog, buffer, 3, 64);
    const uint32_t edram = x.callImport("sceGeEdramGetAddr");
    x.callNid(nid::MemSet, edram, 0, 0x110000);   /* ehsys memset: goes through the guest stores like the original */
    x.callNid(nid::SetScratchBuffer, 0, 0);
    ehsys::Free(x, state(x).heap, x.at<uint32_t>(kLogoBlockAddr));
    x.callNidKeepRegs(nid::UnloadSoundBanks);
    state(x).arg0 = 1;
    uint8_t &f = x.at<uint8_t>(kFlagsAddr);
    f = (f & 0xF7) | 8;
}
static SwapReg reg_LeaveLogoScreen({"libtitle.LeaveLogoScreen", MODULE, off(0x08f00160), swap_LeaveLogoScreen});   /* not verifiable: it frees a heap block, and verify runs a function more than once */

/* FUN_08f000ac(screen): start a screen: reset the state, take a 5 MB block from the title heap, and wait until ehsys says it is ready */
static void swap_EnterLogoScreen(SwapCtx &x) {
    State &s = state(x);
    s.screen = (int16_t)x.arg(0);
    s.arg0 = 0;
    s.pendingArg0 = 0;
    s.arg1 = 0;
    s.transition = 0;
    x.at<uint32_t>(kLogoBlockAddr) = ehsys::Alloc(x, state(x).heap, 0x500000);
    x.callNid(nid::LoadSoundBanks, 0, 1);
    while (x.callNidKeepRegs(nid::IsSoundLoadIdle) == 0) x.callNidKeepRegs(nid::WaitFrame);
    x.callNid(nid::SetScratchBuffer, x.at<uint32_t>(kLogoBlockAddr), 0x500000);
}
static SwapReg reg_EnterLogoScreen({"libtitle.EnterLogoScreen", MODULE, off(0x08f000ac), swap_EnterLogoScreen});

/* FUN_08f040e0(list, item, a, b, c): draw callback of the title list: one item's label, in the colour for its state */
static void swap_DrawTitleItem(SwapCtx &x) {
    const uint32_t list = x.arg(0), item = x.arg(1) & 0xFFFF, a = x.arg(2), b = x.arg(3), c = x.arg(4);
    const uint32_t p = kTitleList;
    if (x.at<uint8_t>(p) < 2 && (x.at<uint32_t>(p + item * 4 + 20) & 0xFF000000u) != 0xFF000000u) { x.ret(list); return; }
    const uint32_t color = itemColor(x, p, item);
    x.callNid(nid::SetFontSize, 15, 15);
    x.callNid(nid::SetTextColor, color);
    x.callNid(nid::SetUiFlag4, 1);
    switch (item) {
    case kContinue: x.ret(drawMessageText(x, list, a, b + 4, c, 0, kContinueText, 1)); break;
    case kNewGame: x.ret(drawMessageText(x, list, a, b + 4, c, 0, kNewGameText, 1)); break;
    case kExitToDesktop: x.ret(drawAscii(x, list, (int32_t)a, (int32_t)b + 4, (int32_t)c, "Exit to Desktop", true)); break;
    default: x.ret(list); break;
    }
}
static SwapReg reg_DrawTitleItem({"libtitle.DrawTitleItem", MODULE, off(0x08f040e0), swap_DrawTitleItem});

/* ---- small screen helpers ---- */
namespace {
constexpr uint32_t kReadyHalf = 0x09B2CBF4;   /* same address as kReadyAddr */
}

/* FUN_08f08084(list): the title screen's drawing step; only step 3 draws (FUN_08f07acc) */
static void swap_TitleDraw(SwapCtx &x) {
    uint32_t list = x.arg(0);
    if (x.at<int16_t>(kReadyAddr) == 3) list = x.callLocal(fn::DrawLanguageSelect, list);
    x.ret(list);
}
static SwapReg reg_TitleDraw({"libtitle.TitleDraw", MODULE, off(0x08f08084), swap_TitleDraw});

/* FUN_08f096c8(tab): reset a tab header record (0x24 bytes) */
static void swap_ResetPanel(SwapCtx &x) {
    TabRecord &tab = x.at<TabRecord>(x.arg(0));
    tab.unknown0 = 0;
    tab.x = 0;
    tab.y = 0;
    tab.w = 64;
    tab.h = 15;
    tab.shape = (uint8_t)((tab.shape & ~0xF) | 4);
    tab.colour[0] = 0xFFFFFFFFu;
    tab.colour[2] = 0xFFFFFFFFu;
    tab.colour[1] = 0xFF808080u;
    tab.colour[3] = 0xFF808080u;
    tab.unknown28 = 0;
    tab.unknown32 = 0;
}
static void regions_ResetPanel(SwapCtx &x, Regions &r) { touch(r, x.arg(0), sizeof(TabRecord)); }
static SwapReg reg_ResetPanel({"libtitle.ResetPanel", MODULE, off(0x08f096c8), swap_ResetPanel, false, regions_ResetPanel});

/* FUN_08f04eac(text, show): reset the title text box's state for a new text */
static void swap_ResetMenuInput(SwapCtx &x) {
    const uint32_t text = x.arg(0), show = x.arg(1);
    TitleTextBox &box = x.at<TitleTextBox>(kTextBox);
    Window &style = x.at<Window>(kTextBoxWindow);
    style.unknown120 = show != 0;
    box.unknown148 = 0; box.unknown14C = 0; box.unknown150 = 0; box.unknown154 = 0; box.unknown144 = 0;
    style.unknown160 = show;
    box.text = text;
    x.callLocal(fn::LayoutRows);
    x.callLocal(fn::DrawMenuRows);
    if (box.changeRequested == 0) box.changeRequested = 1;   /* RequestChange */
    x.ret(0);   /* void; the original leaves the result of its last ehsys call, which is 0 */
}
static void regions_ResetMenuInput(SwapCtx &, Regions &r) {
    touch(r, kTextBoxWindow + offsetof(Window, unknown120), 4);
    touch(r, kTextBoxWindow + offsetof(Window, unknown160), 4);
    touch(r, kTextBox + offsetof(TitleTextBox, unknown144), 4);
    touch(r, kTextBox + offsetof(TitleTextBox, unknown148), 16);
    touch(r, kTextBox + offsetof(TitleTextBox, text), 4);
    touch(r, kTextBox, 4);
}
static SwapReg reg_ResetMenuInput({"libtitle.ResetMenuInput", MODULE, off(0x08f04eac), swap_ResetMenuInput, false, regions_ResetMenuInput});

/* FUN_08effd08(screen, arg): start a screen after a 30-frame fade; arg0's pending value says "add one" */
static void swap_StartScreenFaded(SwapCtx &x) {
    const uint32_t screen = x.arg(0), arg = x.arg(1);
    fadeToBlack(x, 4, 30);
    State &s = state(x);
    s.screen = (int16_t)screen;
    s.arg0 = (int16_t)arg;
    s.pendingArg0 = kIncrement;
    s.arg1 = 0;
    s.transition = 0;
    uint8_t &f = x.at<uint8_t>(kFlagsAddr);
    f = (f & 0xF7) | 8;
}
static void regions_StartScreenFaded(SwapCtx &x, Regions &r) { regions_StateHead(x, r); touch(r, kFlagsAddr, 1); }
static SwapReg reg_StartScreenFaded({"libtitle.StartScreenFaded", MODULE, off(0x08effd08), swap_StartScreenFaded, false, regions_StartScreenFaded});

/* FUN_08f07ff4: the title step, by the "ready" value: 0 -> mark ready and restart the arguments; 1..3 -> the step functions; 4 -> nothing */
static void swap_TitleStep(SwapCtx &x) {
    switch (x.at<int16_t>(kReadyAddr)) {
    case 0: x.at<int16_t>(kReadyAddr) = 1; state(x).arg0 = 0; break;
    case 1: x.callLocal(fn::LanguageStep); break;
    case 2: x.callLocal(fn::LanguageChangedStep); break;
    case 3: x.callLocal(fn::LanguageSelectStep); break;
    }
}
static SwapReg reg_TitleStep({"libtitle.TitleStep", MODULE, off(0x08f07ff4), swap_TitleStep});

/* FUN_08f03484(keepSounds): wait until ehsys has nothing left to load, then play the standard sounds (330, and 327 + 337 unless asked not to) */
static void swap_WaitLoadsAndPlay(SwapCtx &x) {
    const uint32_t keep = x.arg(0);
    x.callNidKeepRegs(nid::BeginLoadWait);
    for (;;) {
        const uint32_t a = x.callNidKeepRegs(nid::PendingLoadsA);
        const uint32_t b = x.callNidKeepRegs(nid::PendingLoadsB);
        if (a + b == 0) break;
        x.callNidKeepRegs(nid::WaitFrame);
    }
    x.callNidKeepRegs(nid::EndLoadWait);
    x.callNid(nid::StartSoundScene, 330);
    if (keep) return;
    x.callNid(nid::StartSoundScene, 327);
    x.callNid(nid::StartSoundScene, 337);
}
static SwapReg reg_WaitLoadsAndPlay({"libtitle.WaitLoadsAndPlay", MODULE, off(0x08f03484), swap_WaitLoadsAndPlay});

/* FUN_08effc74: the intro step by arg0: 0 -> wait for loads, advance; 2 -> switch to the requested screen; 1 -> nothing. Then copies arg0 to +0x1E. */
static void swap_IntroStep(SwapCtx &x) {
    State &s = state(x);
    const int16_t step = s.arg0;
    if (step == 0) {
        x.callNid(nid::SetScreenOverlay, 1);
        x.callLocal(fn::WaitLoadsAndPlay, 0);
        x.callLocal(fn::LoadMenuArchives, 0);
        s.arg0++;
    } else if (step == 2) {
        x.callNid(nid::SetScreenOverlay, 0);
        x.callLocal(fn::SwitchScreen, (uint32_t)(int32_t)s.nextScreen, (uint32_t)(int32_t)(int16_t)s.pendingArg0);
    }
    state(x).publishedArg0 = s.arg0;
}
static SwapReg reg_IntroStep({"libtitle.IntroStep", MODULE, off(0x08effc74), swap_IntroStep});

