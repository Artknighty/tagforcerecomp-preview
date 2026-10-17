/* libtitle swaps: Small helpers of the menu screens: module start, main menu parts, styled text, the title sub lists and the limit list. */
#include "internal.h"

/* ---- small helpers of the menu screens ---- */

/* FUN_08f0cc84(p): clear a 4592-byte work block */
static void swap_ClearWorkBlock(SwapCtx &x) { memset(x.mem(x.arg(0)), 0, 4592); x.ret(x.arg(0)); }
static SwapReg reg_ClearWorkBlock({"libtitle.ClearWorkBlock", MODULE, off(0x08f0cc84), swap_ClearWorkBlock});

/* FUN_08f180b0: give the options work block back to the heap stored in its first word */
static void swap_FreeOptionsBlock(SwapCtx &x) {
    const uint32_t block = x.at<uint32_t>(kOptionsBlockPtr);
    x.callNid(nid::Free, x.at<OptionsWork>(block).heap, block);
}
static SwapReg reg_FreeOptionsBlock({"libtitle.FreeOptionsBlock", MODULE, off(0x08f180b0), swap_FreeOptionsBlock});

/* FUN_08f1854c(kind): which options screen the work block is for */
static void swap_SetOptionsByte(SwapCtx &x) { x.at<OptionsWork>(x.at<uint32_t>(kOptionsBlockPtr)).kind = (uint8_t)x.arg(0); }
static SwapReg reg_SetOptionsByte({"libtitle.SetOptionsByte", MODULE, off(0x08f1854c), swap_SetOptionsByte});

/* FUN_08f01f9c(list): a list that was in state 4 goes back to 3 */
static void swap_ListBackToThree(SwapCtx &x) { uint8_t &b = x.at<uint8_t>(x.arg(0)); if (b == 4) b = 3; }
static SwapReg reg_ListBackToThree({"libtitle.ListBackToThree", MODULE, off(0x08f01f9c), swap_ListBackToThree});

/* FUN_08f1b588: clear bits 1-4 of a flag byte */
static void swap_ClearCardFlags(SwapCtx &x) { x.at<CardListTables>(kCardListTables).bufferFlags &= 0xE1; }
static SwapReg reg_ClearCardFlags({"libtitle.ClearCardFlags", MODULE, off(0x08f1b588), swap_ClearCardFlags});

/* FUN_08efe914: if the limit list is up, give it its step (liblimitlist import) */
static void swap_LimitListStep(SwapCtx &x) { if (x.at<uint8_t>(kCharAlbumReadyAddr)) x.callNidKeepRegs(nid::LimitListStep); }
static SwapReg reg_LimitListStep({"libtitle.LimitListStep", MODULE, off(0x08efe914), swap_LimitListStep});

/* FUN_08f00bf0(list, small, index, x, y, uOffset, vOffset, srcW | stack srcH): DrawSprite with the entry's own size and no colour change */
static void swap_DrawSpriteAt(SwapCtx &x) {
    const uint32_t sp = x.c->r[29];
    x.ret(drawSprite(x, x.arg(0), x.arg(1), x.arg(2), x.arg(3), x.arg(4), -1, -1, (int32_t)x.arg(5), (int32_t)x.arg(6),
                     (int32_t)(int16_t)x.arg(7), (int32_t)x.at<int16_t>(sp), 0xFFFFFFFFu));
}
static SwapReg reg_DrawSpriteAt({"libtitle.DrawSpriteAt", MODULE, off(0x08f00bf0), swap_DrawSpriteAt});

/* FUN_08f00ba0(list, small, index, x, y): the same at the atlas origin */
static void swap_DrawSpritePlain(SwapCtx &x) {
    x.ret(drawSprite(x, x.arg(0), x.arg(1), x.arg(2), x.arg(3), x.arg(4), -1, -1, 0, 0, -1, -1, 0xFFFFFFFFu));
}
static SwapReg reg_DrawSpritePlain({"libtitle.DrawSpritePlain", MODULE, off(0x08f00ba0), swap_DrawSpritePlain});

/* FUN_08f0a71c: forwards its eight register arguments and one stack argument to FUN_08f0a928 */
static void swap_ForwardToCardList(SwapCtx &x) {
    const uint32_t extra = x.at<uint32_t>(x.c->r[29]);
    GuestStack frame(x, 32);
    x.at<uint32_t>(frame.addr()) = extra;
    x.ret(x.callLocal(fn::DrawFrame, x.arg(0), x.arg(1), x.arg(2), x.arg(3), x.arg(4), x.arg(5), x.arg(6), x.arg(7)));
}
static SwapReg reg_ForwardToCardList({"libtitle.ForwardToCardList", MODULE, off(0x08f0a71c), swap_ForwardToCardList});

/* FUN_08f007d8(index): a text of the first message record of the current logo */
static void swap_FirstMessageText(SwapCtx &x) {
    const uint32_t index = x.arg(0);   /* the calls below clobber the argument registers */
    const uint32_t archive = x.callLocal(fn::CurrentLogoResource);
    const uint32_t record = x.callLocal(fn::MessageRecord, 0);
    const uint32_t texture = x.callNid(nid::FindTexture, archive, record, 0);
    x.ret(x.callNid(nid::GetText, texture, index));
}
static SwapReg reg_FirstMessageText({"libtitle.FirstMessageText", MODULE, off(0x08f007d8), swap_FirstMessageText});

/* FUN_08f1bec4(list): draw the card list header; the sprite row depends on the step in arg2 (jump table at 0x08f245f8) */
static void swap_DrawCardHeader(SwapCtx &x) {
    State &s = state(x);
    uint32_t row = 0;
    if ((uint16_t)s.arg2 < 6 && x.at<uint32_t>(x.addr(off(0x08f245f8)) + 4 * (uint16_t)s.arg2) == 0x08f1befc) row = (uint32_t)(int32_t)s.arg2 + 77;
    x.ret(x.callLocal(fn::DrawTextPanel, x.arg(0), row, 9, 58, 309, 16));
}
static SwapReg reg_DrawCardHeader({"libtitle.DrawCardHeader", MODULE, off(0x08f1bec4), swap_DrawCardHeader});

/* FUN_08efe7f0, FUN_08efe29c, FUN_08f1da58 (arg 1 = module handle): the callbacks of the module loads below. Once the load is done
 * (FUN_08f03388), remember the handle and move the screen on. */
static void swap_LoadDoneA(SwapCtx &x) {
    State &s = state(x);
    const uint32_t handle = x.arg(1);
    if (x.callLocal(fn::StartLoadedModule, x.arg(0), handle) == 0) { x.ret(0); return; }
    const int16_t next = (int16_t)s.pendingArg0;
    state(x).moduleHandle = handle;
    s.arg0 = next;
    x.ret(1);
}
static SwapReg reg_LoadDoneA({"libtitle.LoadDoneA", MODULE, off(0x08efe7f0), swap_LoadDoneA});
static void swap_LoadDoneB(SwapCtx &x) {
    State &s = state(x);
    const uint32_t handle = x.arg(1);
    if (x.callLocal(fn::StartLoadedModule, x.arg(0), handle) == 0) { x.ret(0); return; }
    s.arg0 = 1;
    state(x).moduleHandle = handle;
    x.callNid(nid::SetScreenOverlay, 0);
    state(x).charaListLoaded = 1;
    x.ret(1);
}
static SwapReg reg_LoadDoneB({"libtitle.LoadDoneB", MODULE, off(0x08efe29c), swap_LoadDoneB});
static void swap_LoadDoneC(SwapCtx &x) {
    State &s = state(x);
    const uint32_t handle = x.arg(1);
    if (x.callLocal(fn::StartLoadedModule, x.arg(0), handle) == 0) { x.ret(0); return; }
    state(x).moduleHandle = handle;
    x.callNid(nid::SetScreenOverlay, 0);
    s.arg1 = 0;
    s.arg2 = 0;
    s.arg0 = 1;
    x.ret(1);
}
static SwapReg reg_LoadDoneC({"libtitle.LoadDoneC", MODULE, off(0x08f1da58), swap_LoadDoneC});

/* FUN_08efe248: start loading the module at 0x08f27380 with LoadDoneB as callback */
static void swap_StartLoadB(SwapCtx &x) {
    State &s = state(x);
    s.arg0 = 0; s.arg1 = 0; s.pendingArg0 = 1;
    x.callNid(nid::LoadModule, 2, x.addr(off(0x08f27380)), x.addr(fn::LoadDoneB), 0);
    x.callNid(nid::SetScreenOverlay, 1);
}
static SwapReg reg_StartLoadB({"libtitle.StartLoadB", MODULE, off(0x08efe248), swap_StartLoadB});

/* FUN_08efe794(screen): enter `screen` and start loading the module at 0x08f273bc with LoadDoneA as callback */
static void swap_StartLoadA(SwapCtx &x) {
    State &s = state(x);
    s.screen = (int16_t)x.arg(0);
    s.arg0 = 0; s.arg1 = 0; s.pendingArg0 = 1;
    x.callNid(nid::LoadModule, 2, x.addr(off(0x08f273bc)), x.addr(fn::LoadDoneA), 0);
    x.callNid(nid::SetScreenOverlay, 1);
}
static SwapReg reg_StartLoadA({"libtitle.StartLoadA", MODULE, off(0x08efe794), swap_StartLoadA});

/* FUN_08f1d9e8: step 0 of the card-list entry: start loading the module at 0x08f2923c with LoadDoneC as callback (arg2 0 -> 1) */
static void swap_StartLoadC(SwapCtx &x) {
    State &s = state(x);
    if (s.arg2 != 0) return;
    x.callNid(nid::LoadModule, 2, x.addr(off(0x08f2923c)), x.addr(fn::LoadDoneC), 0);
    x.callNid(nid::SetScreenOverlay, 1);
    s.arg2++;
}
static SwapReg reg_StartLoadC({"libtitle.StartLoadC", MODULE, off(0x08f1d9e8), swap_StartLoadC});

/* FUN_08f0877c: enter screen 8 (the main menu): reset the arguments, build its parts (FUN_08f0886c), arg0 = 1 */
static void swap_EnterMainMenu(SwapCtx &x) {
    State &s = state(x);
    s.screen = 8; s.arg0 = 0; s.arg1 = 0; s.arg2 = 0; s.transition = 0;
    x.callLocal(fn::BuildMainMenu);
    s.arg0 = 1;
}
static SwapReg reg_EnterMainMenu({"libtitle.EnterMainMenu", MODULE, off(0x08f0877c), swap_EnterMainMenu});

/* FUN_08f18650(i, ..., value at argument 5): FUN_08f1855c runs first with the same registers, then word +8 of the i-th 16-byte
 * entry (entries start at +20 of the options block) is set unless value is 0 */
static void swap_SetOptionsEntry(SwapCtx &x) {
    ColumnRecord &entry = x.at<OptionsWork>(x.at<uint32_t>(kOptionsBlockPtr)).column[(int16_t)x.arg(0)];
    const uint32_t value = x.arg(5);
    x.callLocal(fn::ConfigureColumn, x.arg(0), x.arg(1), x.arg(2), x.arg(3), x.arg(4), x.arg(5), x.arg(6), x.arg(7));
    if (value != 0) entry.value = value;
}
static SwapReg reg_SetOptionsEntry({"libtitle.SetOptionsEntry", MODULE, off(0x08f18650), swap_SetOptionsEntry});

/* FUN_08f017f4(list, block, centre): centre the block's window on the 480x272 screen if asked, then run ehsys EF9B5D06 on the list value */
static void swap_CentreAndRun(SwapCtx &x) {
    const uint32_t block = x.arg(1);
    GuestStack frame(x, 32);
    const uint32_t local = frame.addr() + 28;
    x.at<uint32_t>(local) = x.arg(0);
    if (x.arg(2) != 0) {
        Window &window = x.at<Window>(block);
        auto half = [](int32_t v) { return (uint16_t)(v >= 0 ? v >> 1 : (v + 1) >> 1); };
        window.x = half(480 - (int16_t)window.width);
        window.y = half(272 - (int16_t)window.height);
    }
    x.callNid(nid::DrawWindow, local, x.arg(1), x.arg(2), x.arg(3));   /* the callee also reads the block pointer in r5 */
    x.ret(x.at<uint32_t>(local));
}
static SwapReg reg_CentreAndRun({"libtitle.CentreAndRun", MODULE, off(0x08f017f4), swap_CentreAndRun});

/* FUN_08f03410: unload the helper module if one is loaded (flag bit 4) */
static void swap_UnloadHelperModule(SwapCtx &x) {
    if (!((x.at<uint8_t>(kFlagsAddr) >> 4) & 1)) return;
    uint8_t &f = x.at<uint8_t>(kFlagsAddr);
    f &= 0xEF;
    GuestStack frame(x, 32);
    const uint32_t handle = state(x).moduleHandle;
    x.callImport("sceKernelStopModule", handle, 0, 0, frame.addr() + 28, 0);
    x.callImport("sceKernelUnloadModule", state(x).moduleHandle);
}
static SwapReg reg_UnloadHelperModule({"libtitle.UnloadHelperModule", MODULE, off(0x08f03410), swap_UnloadHelperModule});

/* ---- module start, main menu parts, options block, a pad reader ---- */

/* FUN_08f03388(_, handle): start a module that was just loaded (a handle > 0): remember it, flag bit 4 = loaded, bit 5 = 0. Returns 1 if started. */
static void swap_StartLoadedModule(SwapCtx &x) {
    const uint32_t handle = x.arg(1);
    if ((int32_t)handle <= 0) { x.ret(0); return; }
    GuestStack frame(x, 32);
    const uint32_t local = frame.addr() + 28;
    x.at<uint32_t>(local) = 1;
    uint8_t &f = x.at<uint8_t>(kFlagsAddr);
    f = (uint8_t)((f & ~0x10) | 0x10);
    f = (uint8_t)(f & ~0x20);
    state(x).moduleHandle = handle;
    x.callImport("sceKernelStartModule", handle, 4, local, 0, 0);
    x.ret(1);
}
static SwapReg reg_StartLoadedModule({"libtitle.StartLoadedModule", MODULE, off(0x08f03388), swap_StartLoadedModule});

/* FUN_08f0886c: build the main menu's parts: its own list (FUN_08f088e0) and the background/arrow images in slots 5, 6, 13, 16 */
static void swap_BuildMainMenu(SwapCtx &x) {
    x.callLocal(fn::ResetMainMenuWindow);
    x.callLocal(fn::SlotLoad, 5, 2, 0, 480);
    x.callLocal(fn::SlotLoad, 6, 2, 0, 480);
    x.callLocal(fn::SlotLoad, 13, 2, 0, 16);
    x.callLocal(fn::SlotLoad, 16, 2, 0, 20);
    x.callNid(nid::StopSoundChannel, 5);
}
static SwapReg reg_BuildMainMenu({"libtitle.BuildMainMenu", MODULE, off(0x08f0886c), swap_BuildMainMenu});

/* FUN_08f1803c(heap, a, b): allocate and clear the 108-byte options work block, store heap and a in it, then FUN_08f180c0(b) */
static void swap_NewOptionsBlock(SwapCtx &x) {
    const uint32_t heap = x.arg(0), a = x.arg(1), b = x.arg(2);
    const uint32_t block = ehsys::Alloc(x, heap, 108);
    x.at<uint32_t>(kOptionsBlockPtr) = block;
    memset(x.mem(block), 0, 108);
    x.at<OptionsWork>(block).heap = heap;
    x.at<OptionsWork>(block).archive = a;
    x.ret(x.callLocal(fn::InitOptionsBlock, b));
}
static SwapReg reg_NewOptionsBlock({"libtitle.NewOptionsBlock", MODULE, off(0x08f1803c), swap_NewOptionsBlock});

/* FUN_08f1b65c(kind): the options step by kind (table at 0x08f24590): 0 -> FUN_08f1bca8, 1 -> FUN_08f1c4a8, 2 -> arg1 7 becomes 9 */
static void swap_OptionsKindStep(SwapCtx &x) {
    const int32_t kind = (int16_t)x.arg(0);
    if ((uint32_t)kind >= 8) return;
    switch ((uint32_t)kind) {
    case 2: x.callLocal(fn::OptionsImageStep); break;
    case 3: case 4: x.callLocal(fn::CardListSlideStep); break;
    case 6: case 7: if (state(x).arg1 == 7) state(x).arg1 = 9; break;
    }
}
static SwapReg reg_OptionsKindStep({"libtitle.OptionsKindStep", MODULE, off(0x08f1b65c), swap_OptionsKindStep});

/* FUN_08f1e28c(kind): the value of the last 6-byte card-list entry that has a type (bits 0-1 of byte +406) set, or 1; 0 for kinds 1 and 41.
 * For a duelist (kind = character id) the entries are his decks the save has unlocked and the value is a DECK id (duelists.h). The Free Duel uses
 * it as the opponent's deck, so a wrong value here gives the right opponent the wrong deck (2026-10-13: Zane with Jaden's deck, because the list
 * was built for the wrong kind). The check below reports that case. */
static void swap_LastTypedEntryValue(SwapCtx &x) {
    const uint32_t kind = x.arg(0);
    if (kind == 1 || kind == 41) { x.ret(0); return; }
    x.callLocal(fn::BuildCardListEntries, kind);   /* o original deixa o kind em r4 para BuildCardListEntries; sem ele o Free Duel lia a lista do personagem 0 (vazia) e o oponente ficava com o deck 1 */
    const CardListTables &tables = x.at<CardListTables>(kCardListTables);
    uint32_t value = 1;
    for (int32_t i = 0; i < (int32_t)tables.count; i++)
        if (tables.owned[i].classPosition & 3) value = tables.owned[i].number;
    if ((duelist::HasLadder(kind) || duelist::HasSingleDeck(kind)) && !duelist::IsDeckOf(kind, value))
        fprintf(stderr, "[decomp] AVISO LastTypedEntryValue: o deck %u nao e um deck do personagem %u (esperado %u..%u)\n", value, kind,
                duelist::HasLadder(kind) ? duelist::FirstDeck(kind) : duelist::SingleDeck(kind),
                duelist::HasLadder(kind) ? duelist::FirstDeck(kind) + duelist::kDecksPerLadder - 1 : duelist::SingleDeck(kind));
    x.ret(value);
}
static SwapReg reg_LastTypedEntryValue({"libtitle.LastTypedEntryValue", MODULE, off(0x08f1e28c), swap_LastTypedEntryValue});

/* FUN_08f090b0: read a dialog answer from the pad: 0 = start, 1 = confirm, 2 = cancel (with their sounds), -1 = nothing */
static void swap_DialogAnswer(SwapCtx &x) {
    const uint32_t pad = x.callNidKeepRegs(nid::GetInput);
    const uint32_t pressed = x.at<ehsys::PadState>(pad).pressed;
    constexpr uint32_t kStartButton = 8;
    if (pressed & kStartButton) { x.callNid(nid::PlaySound, kSoundConfirm); x.ret(0); return; }
    if (pressed & (x.callNid(nid::GetButtonMask, 0) & 0xFFFF)) { x.callNid(nid::PlaySound, kSoundConfirm); x.ret(1); return; }
    if (pressed & (x.callNid(nid::GetButtonMask, 1) & 0xFFFF)) { x.callNid(nid::PlaySound, kSoundCancel); x.ret(2); return; }
    x.ret(0xFFFFFFFFu);
}
static SwapReg reg_DialogAnswer({"libtitle.DialogAnswer", MODULE, off(0x08f090b0), swap_DialogAnswer});

/* FUN_08f1af30: set up the four card-list columns: column 0 from ehsys 1BFCC25C and FUN_08f1e28c(1), the others by FUN_08f1dba8 */
static void swap_BuildCardColumns(SwapCtx &x) {
    for (uint32_t i = 0; i < 4; i++) {
        if (i == 0) {
            const uint32_t value = x.callNidKeepRegs(nid::GetPlayerName);
            const uint32_t entry = x.callLocal(fn::LastTypedEntryValue, 1);
            x.callLocal(fn::SetOptionsEntry, 0, 1, 0, 0, entry, value);
        } else x.callLocal(fn::FillColumn, i);
    }
    x.callLocal(fn::SetOptionsByte, 4);
}
static SwapReg reg_BuildCardColumns({"libtitle.BuildCardColumns", MODULE, off(0x08f1af30), swap_BuildCardColumns});

/* ---- text with a style, small screen steps ---- */

/* FUN_08f036e8(list, a1..a6, fontW | stack: fontH, colour): set the font size and colour, then draw FUN_08f0378c(list, a1..a6) */
uint32_t drawStyledText(SwapCtx &x, uint32_t list, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6,
                               uint32_t fontW, uint32_t fontH, uint32_t colour) {
    x.callNid(nid::SetFontSize, fontW, fontH);
    x.callNid(nid::SetTextColor, colour);
    x.callNid(nid::SetUiFlag4, 1);
    return x.callLocal(fn::DrawMessageText, list, a1, a2, a3, a4, a5, a6);
}
static void swap_DrawStyledText(SwapCtx &x) {
    const uint32_t sp = x.c->r[29];
    x.ret(drawStyledText(x, x.arg(0), x.arg(1), x.arg(2), x.arg(3), x.arg(4), x.arg(5), x.arg(6), x.arg(7), x.at<uint32_t>(sp), x.at<uint32_t>(sp + 4)));
}
static SwapReg reg_DrawStyledText({"libtitle.DrawStyledText", MODULE, off(0x08f036e8), swap_DrawStyledText});

constexpr int32_t kModernBarLabelPad = 6;

/* The text messages (of the options archive) that name the answers of the Free Duel questions: the answer picks one of a run of messages. */
constexpr uint32_t kFirstQuestionAnswers = 0x58, kSecondQuestionAnswers = 0x5A, kFourthQuestionAnswers = 0x34, kFifthQuestionAnswers = 0x39;
constexpr uint32_t kCardCellMessageRecord = 12, kCardCellAlignment = 2, kCardCellFontSize = 13, kCardCellColour = 0xFF002020u;   /* ABGR: dark teal */

/* FUN_08f1bfbc(list, cell, x, y, w): draw callback of the Details list on the Free Duel settings page: cell `cell` shows the player's answer to one
 * question. The cells are the first, second, fourth and fifth questions (the third has none); a cell past those draws nothing. */
static void swap_DrawCardCell(SwapCtx &x) {
    const uint32_t list = x.arg(0), cell = x.arg(1) & 0xFFFF, px = x.arg(2), py = x.arg(3), width = x.arg(4);
    const FreeDuelSettings &settings = x.at<FreeDuelSettings>(kFreeDuelSettings);
    uint32_t message = 0;
    switch (cell) {
    case 0: message = kFirstQuestionAnswers + (settings.answers >> 5 & 1); break;
    case 1: message = kSecondQuestionAnswers + (settings.answers >> 6 & 1); break;
    case 2: message = kFourthQuestionAnswers + (settings.picks & 0xF); break;
    case 3: message = kFifthQuestionAnswers + (settings.picks >> 4); break;
    }
    if (message == 0) { x.ret(list); return; }
    x.ret(drawStyledText(x, list, px, py, width, kCardCellMessageRecord, message, kCardCellAlignment, kCardCellFontSize, kCardCellFontSize, kCardCellColour));
}
static SwapReg reg_DrawCardCell({"libtitle.DrawCardCell", MODULE, off(0x08f1bfbc), swap_DrawCardCell});

/* FUN_08f1dd24(list, _, x, y, w): the card-list label: which message (7, 8, 9 or 13) depends on arg0 (table at 0x08f246d8) */
static void swap_DrawCardLabel(SwapCtx &x) {
    State &s = state(x);
    uint32_t message = 0;
    if ((uint16_t)s.arg0 < 8) {
        switch ((uint16_t)s.arg0) {
        case 2: message = 7; break;
        case 3: message = 8; break;
        case 4: message = s.arg2 != 0 ? 9 : 13; break;
        }
    }
    const int32_t pad = gfx_modern_menus() ? kModernBarLabelPad : 0;   /* the PC style's slanted bar needs room at its left edge (o usuario, 2026-10-12) */
    x.ret(drawStyledText(x, x.arg(0), x.arg(2) + pad, x.arg(3), x.arg(4) - pad, 0, message, 0, 13, 13, 0xFFFFFFFFu));
}
static SwapReg reg_DrawCardLabel({"libtitle.DrawCardLabel", MODULE, off(0x08f1dd24), swap_DrawCardLabel});

/* FUN_08f1cc04(list, _, x, y, w): the options label: message 10 (grey while arg2 < 5) or 11, from arg0 (table at 0x08f24678) */
static void swap_DrawOptionsLabel(SwapCtx &x) {
    State &s = state(x);
    uint32_t message = 0, colour = 0xFFFFFFFFu;
    if ((uint16_t)s.arg0 < 8) {
        switch ((uint16_t)s.arg0) {
        case 2: message = 10; if (s.arg2 < 5) colour = 0xFF808080u; break;
        case 3: case 4: case 5: message = 11; break;
        }
    }
    if (message == 0) { x.ret(x.arg(0)); return; }
    x.ret(drawStyledText(x, x.arg(0), x.arg(2), x.arg(3), x.arg(4), 0, message, 3, 13, 13, colour));
}
static SwapReg reg_DrawOptionsLabel({"libtitle.DrawOptionsLabel", MODULE, off(0x08f1cc04), swap_DrawOptionsLabel});

/* FUN_08f1c1c4: the sprite row for the window block at 0x09B298D8 when arg1 is 9, by arg2 (table at 0x08f24648); 0 otherwise */
static void swap_OptionsSpriteRow(SwapCtx &x) {
    State &s = state(x);
    uint32_t row = 0;
    if (s.arg1 == 9 && (uint16_t)s.arg2 < 6) {
        const uint32_t phase = x.at<Window>(kWindowA).phase & 0x1F;
        switch ((uint16_t)s.arg2) {
        case 0: row = phase + 67; break;
        case 1: row = phase + 69; break;
        case 2: row = phase + 71; break;
        case 3: row = 73; break;
        case 4: row = 74; break;
        }
    }
    x.ret(row);
}
static SwapReg reg_OptionsSpriteRow({"libtitle.OptionsSpriteRow", MODULE, off(0x08f1c1c4), swap_OptionsSpriteRow});

/* FUN_08f087cc: the step of screen 8 (main menu) by arg0: 1 -> FUN_08f08d58, 2 -> FUN_08f092f4, 3 -> when FUN_08f093f4 says so, go to screen 9 */
static void swap_MainMenuStep(SwapCtx &x) {
    switch (state(x).arg0) {
    case 1: x.callLocal(fn::MainMenuOwnStep); break;
    case 2: x.callLocal(fn::MainMenuNameStep); break;
    case 3:
        if (x.callLocal(fn::MainMenuStartStep) != 0) { x.callNidKeepRegs(nid::UpdateSaveChecksum); x.callLocal(fn::SwitchScreen, 9, 0); }
        break;
    }
}
static SwapReg reg_MainMenuStep({"libtitle.MainMenuStep", MODULE, off(0x08f087cc), swap_MainMenuStep});

/* FUN_08efe2f4: the character list step (libcharalist): arg0 1 -> start it; 2 -> when it reports 1 or 2 (done), unload and go to screen 9 */
static void swap_CharListStep(SwapCtx &x) {
    State &s = state(x);
    if (s.arg0 == 2) {
        const uint32_t r = x.callNidKeepRegs(nid::CharaListStep);
        if (r == 2 || r == 1) {
            state(x).charaListLoaded = 0;
            x.callLocal(fn::UnloadHelperModule);
            x.callLocal(fn::SwitchScreen, 9, 6);
        }
    } else if (s.arg0 == 1) {
        x.callNid(nid::StartCharaList, 0, state(x).heap);
        x.callNidKeepRegs(nid::CharaListStep);
        s.arg0 = 2;
    }
}
static SwapReg reg_CharListStep({"libtitle.CharListStep", MODULE, off(0x08efe2f4), swap_CharListStep});

/* ---- the title sub lists (screens 5 and 6), the limit list, card-list setup and message text ---- */

void buildSubList(SwapCtx &x, const SubListShape &shape) {
    State &s = state(x);
    s.arg0 = (int16_t)shape.screen;
    s.arg1 = 0;
    s.transition = 0;
    MenuList &title = menu(x, kTitleList);
    title.openFadeColour = 0xFF808080u;
    title.normalColour = 0xFF808080u;
    title.selectedColour = 0xFF000000u;
    const uint32_t p = kTitleList2;
    x.callLocal(fn::InitList, p);
    MenuList &m = menu(x, p);
    m.itemCount = shape.itemCount;
    m.unknown7 = 0;
    x.callLocal(fn::ResetList, p, 4, 1);
    if (shape.tightRows) { m.itemState[5] = kItemAlternate; m.itemState[6] = kItemAlternate; }
    const bool firstLanguage = ehsys::GetLanguage(x) == 0;
    m.x = firstLanguage ? 180 : 160;
    m.y = firstLanguage ? shape.yFirstLanguage : shape.yOtherLanguages;
    m.width = firstLanguage ? 160 : 200;
    m.rowHeight = 26;
    m.spriteCell = 4;
    m.drawCallback = x.addr(shape.drawCallback);
    if (shape.highlightFirst) x.callLocal(fn::ListUpdate, p, 1);
}
static void regions_BuildSubList(SwapCtx &, Regions &r) { touch(r, kStateAddr, 0x24); touch(r, kTitleList + 92, 12); touch(r, kTitleList2, 0x7C); }

/* FUN_08efd9a0: build sub list 5 (two rows) */
static void swap_BuildSubList5(SwapCtx &x) { buildSubList(x, {5, 2, 133, 118, fn::DrawSubList5Row, false, false}); }
static SwapReg reg_BuildSubList5({"libtitle.BuildSubList5", MODULE, off(0x08efd9a0), swap_BuildSubList5, false, regions_BuildSubList});
/* FUN_08efdcf0: build sub list 6 (seven rows) */
static void swap_BuildSubList6(SwapCtx &x) { buildSubList(x, {6, 7, 81, 70, fn::DrawSubList6Row, true, true}); }
static SwapReg reg_BuildSubList6({"libtitle.BuildSubList6", MODULE, off(0x08efdcf0), swap_BuildSubList6, false, regions_BuildSubList});

/* Both sub-list input steps do the same: keep the first list closing, and while nothing was chosen yet read the second list. `answer`
 * is the row that was confirmed, or -2 when the list was cancelled (then the flow goes back to screen 2). */
static void subListInput(SwapCtx &x, void (*onAnswer)(SwapCtx &, State &, int32_t)) {
    State &s = state(x);
    const int32_t progress = (int32_t)x.callLocal(fn::Slot13Progress);
    x.callLocal(fn::ListUpdate, kTitleList, 2);
    if (progress >= 3 || s.arg1 != 0) return;
    const int32_t answer = (int32_t)x.callLocal(fn::ListUpdate, kTitleList2, progress != 0);
    onAnswer(x, s, answer);
}
void leaveSubList(SwapCtx &x, State &s) {
    menu(x, kTitleList).blink = 0;
    x.callLocal(fn::ListBackToThree, kTitleList);
    s.transition = 0;
    s.arg0 = 2;
}
/* a confirmed row: optionally play the sound (6367AE39), fade out to `screen`, and count the step */
static void chooseRow(SwapCtx &x, State &s, bool sound, uint32_t screen, uint32_t pending) {
    if (sound) x.callNid(nid::SetAttractTimer, 600);
    x.callLocal(fn::RequestScreen16, 1, screen, pending);
    s.arg1 = (int16_t)(s.arg1 + 1);
}
static void sub5Answer(SwapCtx &x, State &s, int32_t answer) {
    if (answer == 1) chooseRow(x, s, true, 0x15, 0);
    else if (answer == 0) chooseRow(x, s, true, 0x14, 0);
    else if (answer == -2) leaveSubList(x, s);
}
/* FUN_08efda88: input of sub list 5 */
static void swap_SubList5Input(SwapCtx &x) { subListInput(x, sub5Answer); }
static SwapReg reg_SubList5Input({"libtitle.SubList5Input", MODULE, off(0x08efda88), swap_SubList5Input});

static void sub6Answer(SwapCtx &x, State &s, int32_t answer) {
    switch (answer) {
    case 0: chooseRow(x, s, true, 0x16, 0); break;
    case 1: chooseRow(x, s, false, 0x17, 0); break;
    case 2: chooseRow(x, s, true, 0x18, 0xFFFFFFFFu); break;
    case 3: chooseRow(x, s, false, 0x19, 0); break;
    case 4: chooseRow(x, s, true, 0x1A, 0); break;
    case 5: chooseRow(x, s, true, 0x24, 0); break;
    case 6: chooseRow(x, s, false, 6, 0); break;
    case -2: leaveSubList(x, s); break;
    }
}
/* FUN_08efddec: input of sub list 6 */
static void swap_SubList6Input(SwapCtx &x) { subListInput(x, sub6Answer); }
static SwapReg reg_SubList6Input({"libtitle.SubList6Input", MODULE, off(0x08efddec), swap_SubList6Input});

/* FUN_08efe83c: the limit list screen (liblimitlist). arg0 1 -> start it, then every frame ask how it went: 0 still open, 1 closed
 * (back to the main list), 2 closed with a choice (go to screen 20). Returns the screen code, or 41 after a choice. */
static void swap_LimitListScreen(SwapCtx &x) {
    State &s = state(x);
    if (s.arg0 == 1) {
        x.callNid(nid::LimitListStart, 0, state(x).heap);
        s.arg0 = (int16_t)(s.arg0 + 1);
    } else if (s.arg0 == 0) {
        state(x).publishedArg0 = (uint16_t)s.arg0;
        x.ret((uint32_t)(int32_t)s.screen);
        return;
    }
    const uint32_t result = x.callNid(nid::LimitListPoll);
    bool returnsChoice = false;
    if (result == 2) {
        x.at<uint8_t>(kCharAlbumReadyAddr) = 0;
        x.callLocal(fn::UnloadHelperModule);
        x.callLocal(fn::SwitchScreen, 20, 2);
        returnsChoice = true;
    } else if (result == 1) {
        x.at<uint8_t>(kCharAlbumReadyAddr) = 0;
        x.callLocal(fn::UnloadHelperModule);
        state(x).savedSelection = 4;
        x.callLocal(fn::SwitchScreen, 9, 6);
    } else if (result == 0) {
        x.at<uint8_t>(kCharAlbumReadyAddr) = 1;
    }
    state(x).publishedArg0 = (uint16_t)s.arg0;
    x.ret(returnsChoice ? 41u : (uint32_t)(int32_t)s.screen);
}
static SwapReg reg_LimitListScreen({"libtitle.LimitListScreen", MODULE, off(0x08efe83c), swap_LimitListScreen});

/* FUN_08f0082c(index, archiveKey): the text `index` of the current logo's text archive. Texts 118 and 119 are templates that get the name
 * of the card being shown (0x09B2B290) filled in; the result is then in a shared buffer. */
static void swap_MessageText(SwapCtx &x) {
    constexpr uint32_t kFormatBuffer = 0x09B2C7F0, kShownCard = 0x09B2B290;
    const uint32_t index = x.arg(0), archiveKey = x.arg(1);
    const uint32_t archive = x.callLocal(fn::CurrentLogoResource);
    const uint32_t file = x.callLocal(fn::MessageRecord, archiveKey);
    const uint32_t texts = ehsys::FindTexture(x, archive, file, 0);
    uint32_t text = ehsys::GetText(x, texts, index);
    if (index == 0x77 || index == 0x76) {
        ehsys::Format(x, kFormatBuffer, text, ehsys::CardName(x, x.at<uint16_t>(kShownCard)));
        text = kFormatBuffer;
    }
    x.ret(text);
}
static SwapReg reg_MessageText({"libtitle.MessageText", MODULE, off(0x08f0082c), swap_MessageText});

