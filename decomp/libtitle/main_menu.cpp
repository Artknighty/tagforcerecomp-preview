/* libtitle swaps: The card list / options screen setup (screen 9) and the main menu (screen 8). */
#include "internal.h"

/* ---- card list / options screen (screen 9) setup ---- */

/* The card list and options screen keeps an OptionsWork block (pointer at kOptionsBlockPtr) with four column records. */

/* FUN_08f1855c(column, kind, a, b, count): update the column's record (a negative argument keeps the old value) and point it at its data.
 * Kinds other than 1 and 41 take the data from ehsys E1677D35(count). For kinds 1 and 41 the source selector b decides: 0 = the save's
 * record count - 1 (B6E8E7AE) + 4 (nothing for count 0), 1 = E1677D35(count), 2 and 3 = no data. */
static void swap_ConfigureColumn(SwapCtx &x) {
    const int32_t column = (int16_t)x.arg(0), kind = (int32_t)x.arg(1), a = (int32_t)x.arg(2), b = (int32_t)x.arg(3), count = (int32_t)x.arg(4);
    ColumnRecord &rec = x.at<OptionsWork>(x.at<uint32_t>(kOptionsBlockPtr)).column[column];
    if (kind >= 0) rec.kind = (int16_t)kind;
    if (a >= 0) rec.sourceA = (int16_t)a;
    if (b >= 0) rec.sourceB = (int16_t)b;
    if (count >= 0) rec.count = (int16_t)count;
    if (rec.kind != 1 && rec.kind != 41) { rec.data = x.callNid(nid::GetCardRecordData, (uint32_t)count); return; }
    if (rec.sourceB == 3 || rec.sourceB == 2) return;
    if (rec.sourceB == 1) rec.data = x.callNid(nid::GetCardRecordData, (uint32_t)count);
    else if (rec.sourceB == 0) rec.data = count == 0 ? 0 : x.callNid(nid::GetSaveRecordOffset, (uint32_t)(count - 1)) + 4;
}
static SwapReg reg_ConfigureColumn({"libtitle.ConfigureColumn", MODULE, off(0x08f1855c), swap_ConfigureColumn});

/* FUN_08f1b5b0(whole): give back the card list buffers if any were taken (flag bits 1-4): the main one and clear its 0x600-byte table when
 * whole is 0, otherwise the second one. */
static void swap_FreeCardListBuffers(SwapCtx &x) {
    const uint32_t whole = x.arg(0);
    CardListTables &tables = x.at<CardListTables>(kCardListTables);
    if (((tables.bufferFlags >> 1) & 0xF) == 0) return;
    const uint32_t heap = state(x).heap;
    if (whole == 0) {
        ehsys::Free(x, heap, tables.mainBuffer);
        memset(tables.second, 0, 0x600);
    } else {
        ehsys::Free(x, heap, tables.secondBuffer);
    }
    tables.bufferFlags = (uint8_t)(tables.bufferFlags & 0xE0);
    tables.mainSize = 0;
    tables.mainRecords = 0;
}
static SwapReg reg_FreeCardListBuffers({"libtitle.FreeCardListBuffers", MODULE, off(0x08f1b5b0), swap_FreeCardListBuffers});

/* FUN_08f1dba8(column): fill column `column` of the card list from the character list (libcharalist) when a card is owned, otherwise with
 * the default kind 2. Kind 41 is the only one that has no value. For a duelist (kind = character id) the "extra" it stores in `count` is his
 * DECK id (LastTypedEntryValue), which the Free Duel start then hands to the duel (ColumnRecord, duelists.h). */
static void swap_FillColumn(SwapCtx &x) {
    const uint32_t column = (uint32_t)(int32_t)(int16_t)x.arg(0);
    uint32_t kind = 2, source = 0, hasValue = 1, deck;   /* kind = card list kind / character id; deck = ColumnRecord::count */
    if (x.callLocal(fn::AnyCardOwned) == 0) {
        deck = x.callLocal(fn::LastTypedEntryValue, kind);
    } else {
        x.callNid(nid::StartSaveReader, state(x).heap);
        kind = x.callNid(nid::GetSaveKind);
        source = x.callNid(nid::GetSaveSource);
        deck = x.callLocal(fn::LastTypedEntryValue, kind);
        if (kind == 41) hasValue = 0;
    }
    const uint32_t value = x.callNid(nid::GetLanguageText, 0, kind);
    x.callLocal(fn::SetOptionsEntry, column, kind, source, hasValue, deck, value);
    state(x).subListCount = 8;
}
static SwapReg reg_FillColumn({"libtitle.FillColumn", MODULE, off(0x08f1dba8), swap_FillColumn});

/* FUN_08f1bca8: step of the options window's slot-15 image (screen 9, kind 0): 7 -> load it, 8 -> when loaded play its second range and go
 * to 9, 11 -> when its second range is over, park it. */
static void swap_OptionsImageStep(SwapCtx &x) {
    State &s = state(x);
    if (s.arg1 == 11) {
        if (x.callLocal(fn::SlotQuery, 15) == 58) x.callLocal(fn::SlotSetRange, 15, 2, 0, 0);
    } else if (s.arg1 == 8) {
        if (x.callLocal(fn::SlotQuery, 15) == 17) {
            x.callLocal(fn::SlotSetRange, 15, 2, 18, 53);
            s.arg1 = 9;
        }
    } else if (s.arg1 == 7) {
        x.callLocal(fn::SlotLoad, 15, 2, 0, 17);
        s.arg1 = 8;
    }
}
static SwapReg reg_OptionsImageStep({"libtitle.OptionsImageStep", MODULE, off(0x08f1bca8), swap_OptionsImageStep});

/* FUN_08f1b6d8: set up the card list's window (block kWindowA): 320 pixels wide at (4, 53), draw callbacks, then load slot 15 */
static void swap_SetupCardListWindow(SwapCtx &x) {
    State &s = state(x);
    s.arg1 = 0;
    state(x).arg2 = 0;
    s.transition = 0;
    state(x).panelSet.unknown364 = 0;
    state(x).panelSet.unknown365 = 2;
    state(x).menuColour[0] = 0xFFFFFFFFu;
    state(x).menuColour[1] = 0xFFFFFFFFu;
    const uint32_t w = kWindowA;
    Window &window = x.at<Window>(w);
    x.callLocal(fn::InitWindow, w);
    window.x = 4;        /* x */
    window.y = 53;       /* y */
    window.height = 133;
    window.margin[0] = 1;
    window.textWidth = 320 - (1 + 2);
    window.width = 320;
    window.margin[2] = 18;
    window.margin[3] = 18;
    window.colour = 0xFFFFFFFFu;
    window.unknown206 = 10;
    window.margin[1] = 2;
    window.unknown204 = 2;
    uint8_t &bits = window.flags;
    bits = (uint8_t)((bits & 0xF4) | 0x0A);
    window.drawCallback = x.addr(fn::DrawWindowCallback);   /* draw callback */
    window.textY = 16;
    window.unknown250 = 15;
    window.textRight = (uint16_t)(window.textWidth - 8);
    window.drawCallbackB = x.addr(fn::DrawWindowCallbackB);   /* second draw callback */
    window.animationA = 0;
    window.animationB = 0;
    window.unknown272 = 0;
    window.unknown244 = 0;
    window.unknown246 = 0;
    x.callLocal(fn::RegisterWindow, w);   /* register the window with ehsys */
    x.callLocal(fn::ClearWorkBlock, kCardWork);
    x.callLocal(fn::SlotLoad, 15, 2, 0, 17);
}
static SwapReg reg_SetupCardListWindow({"libtitle.SetupCardListWindow", MODULE, off(0x08f1b6d8), swap_SetupCardListWindow});

/* FUN_08f1b24c(kind): enter the options/card-list screen's kind (0..7): each kind prepares its part and sets the step (arg1) to 5 */
static void swap_EnterOptionsKind(SwapCtx &x) {
    State &s = state(x);
    const uint32_t kind = (uint32_t)(int32_t)(int16_t)x.arg(0);
    x.at<uint8_t>(kCharAlbumReadyAddr) = 0;
    switch (kind) {
    case 0: x.callLocal(fn::StartLoadC); break;
    case 1:
        x.callLocal(fn::BuildCardColumns);
        s.arg0 = 2;
        x.callLocal(fn::SetupCardListWindow);
        s.arg1 = 5;
        break;
    case 2: x.callLocal(fn::SetupCardListWindow); s.arg1 = 5; break;
    case 3: x.callLocal(fn::EnterCardListKind3); s.arg1 = 5; break;
    case 4: x.callLocal(fn::EnterCardListKind4); s.arg1 = 5; break;
    case 5: s.arg1 = 5; break;
    case 6:
        x.callLocal(fn::SetCardListFilter, x.at<CardListFilters>(kCardListFilters).first, x.at<CardListFilters>(kCardListFilters).second);
        x.callNid(nid::StartCharaList, 2, state(x).heap);
        x.callNid(nid::CharaListStep);
        x.at<uint8_t>(kCharAlbumReadyAddr) = 1;
        s.arg1 = 5;
        break;
    case 7: s.arg1 = 1; break;
    }
}
static SwapReg reg_EnterOptionsKind({"libtitle.EnterOptionsKind", MODULE, off(0x08f1b24c), swap_EnterOptionsKind});

/* ---- the main menu (screen 8) ---- */

/* FUN_08f088e0: reset the main menu's window (block kWindowA): 401x46 at (40, 224), 12-pixel margins, first message text, no flags */
static void swap_ResetMainMenuWindow(SwapCtx &x) {
    const uint32_t w = kWindowA;
    Window &window = x.at<Window>(w);
    x.callLocal(fn::InitWindow, w);
    window.animationA = 1;
    window.animationB = 1;
    window.x = 40;
    window.y = 224;
    window.width = 401;
    window.height = 46;
    window.margin[0] = 6;
    window.margin[1] = 6;
    window.margin[2] = 5;
    window.margin[3] = 5;
    window.colour = 0xFFFFFFFFu;
    window.textColour = 0xFF000000u;
    window.frameKind = 0;
    window.unknown160 = 0;
    window.text = x.callLocal(fn::FirstMessageText, 0x45);
    window.flags2 &= 0x87;
    window.flags &= 0xC0;
    x.callLocal(fn::RegisterWindow, w);
}
static SwapReg reg_ResetMainMenuWindow({"libtitle.ResetMainMenuWindow", MODULE, off(0x08f088e0), swap_ResetMainMenuWindow});

/* FUN_08f08d58: the main menu's own step (screen 8, arg0 1), by arg1:
 *  0 build it and fade in      2, 3 start the two background animations     4 menu open: wait for an answer
 *  5 a choice was made: ask the save (08f094e4) and show a dialog           6 dialog open     7, 8 message shown, back to 4
 *  9 leaving: let the slot-16 animation run out.
 * The window (block kWindowA) answers 1 = confirm, 2 = cancel, 3 = confirm in the second dialog, -2 = closed. */
static void swap_MainMenuOwnStep(SwapCtx &x) {
    State &s = state(x);
    const uint32_t w = kWindowA;
    Window &window = x.at<Window>(w);
    auto windowState = [&]() -> uint8_t { return window.state; };
    auto leaveThroughSlot16 = [&] { s.arg1 = 9; x.callLocal(fn::SlotSetRange, 16, 2, 30, 35); };
    switch (s.arg1) {
    case 0:
        x.callLocal(fn::BuildMainMenu);
        x.callLocal(fn::RequestScreen16, 0, 0xFFF, 0xFFF);
        s.pendingArg1 = 2;
        s.arg1 = 1;
        break;
    case 2:
        if (x.callLocal(fn::SlotQuery, 16) != 20) return;
        x.callLocal(fn::SlotSetRange, 16, 2, 21, 29);
        s.arg1++;
        break;
    case 3:
        if (x.callLocal(fn::SlotQuery, 13) == 16) {
            x.callLocal(fn::SlotSetRange, 13, 2, 17, 96);
            s.arg1++;
        }
        break;
    case 4:
        x.callNid(nid::UpdateWindow, w);
        if (windowState() > 2) {
            const uint32_t answer = x.callLocal(fn::DialogAnswer);
            if (answer == 2) {
                x.callLocal(fn::RequestScreen16, 1, 5, 0);
                leaveThroughSlot16();
            } else if (answer == 1) {
                x.callLocal(fn::RequestScreen16, 1, 0xFFF, 2);
                s.arg1 = 9;
            } else if (answer == 0) {
                s.arg1 = 5;
                x.callNid(nid::ResetWindow, w);
                x.callNid(nid::UpdateWindow, w);
            }
        }
        break;
    case 5:
        x.callNid(nid::UpdateWindow, w);
        if (windowState() == 6) {
            if (x.callLocal(fn::PlayerNameIsSet) == 0) {
                x.callLocal(fn::OpenDialog, 15, 3, 0);
                s.arg1 = 7;
            } else {
                x.callLocal(fn::OpenDialog, x.callNid(nid::SaveHasErrors) == 0 ? 71 : 72, 1, 0);
                s.arg1 = 6;
            }
            x.callNid(nid::PlaySound, kSoundDialogOpen);
        }
        break;
    case 6: {
        const int32_t answer = (int32_t)x.callNid(nid::UpdateWindow, w);
        if (answer == 2) {
            x.callLocal(fn::RequestScreen16, 1, 0xFFF, 3);
            leaveThroughSlot16();
        } else if (answer == 3 || answer == -2) {
            x.callNidKeepRegs(nid::UpdateSaveChecksum);   /* update the save's checksum */
            x.callLocal(fn::RequestScreen16, 1, 9, 0);
            leaveThroughSlot16();
        }
        break;
    }
    case 7: {
        const int32_t answer = (int32_t)x.callNid(nid::UpdateWindow, w);
        if (answer == 1 || answer == -2) s.arg1 = 8;
        break;
    }
    case 8:
        x.callNid(nid::UpdateWindow, w);
        if (windowState() == 6) {
            x.callLocal(fn::ResetMainMenuWindow);
            s.arg1 = 4;
        }
        break;
    case 9:
        if (x.callLocal(fn::SlotQuery, 16) == 34) x.callLocal(fn::SlotSetRange, 16, 0, 35, 35);
        break;
    }
}
static SwapReg reg_MainMenuOwnStep({"libtitle.MainMenuOwnStep", MODULE, off(0x08f08d58), swap_MainMenuOwnStep});

/* FUN_08f1aaac(list, x, y, width, text, style): draw a text in a box `width` wide. Styles 0 and 1 use the plain text call, 2 and 3 the one
 * with the box; 1 centres the text (and shrinks the box to it), 3 centres it only when it is narrower than the box. Returns the new list. */
static void swap_DrawCentredText(SwapCtx &x) {
    uint32_t list = x.arg(0), px = x.arg(1);
    const uint32_t py = x.arg(2), width = x.arg(3), text = x.arg(4), style = x.arg(5);
    uint32_t drawWidth = width;
    auto centre = [&](int32_t textWidth) { const int32_t d = (int32_t)width - textWidth; px += (uint32_t)(d >= 0 ? d >> 1 : (d + 1) >> 1); };
    if (style == 1) { const int32_t tw = ehsys::TextWidth(x, text) >> 6; drawWidth = (uint32_t)tw; centre(tw); }
    else if (style == 3) { const int32_t tw = ehsys::TextWidth(x, text) >> 6; if (tw < (int32_t)width) centre(tw); }
    switch (style) {
    case 0: case 1: list = x.callLocal(fn::DrawTextLine, list, px, py, drawWidth, text); break;
    case 2: case 3: list = x.callLocal(fn::DrawBoxedText, list, px, py, drawWidth, text); break;
    }
    x.ret(list);
}
static SwapReg reg_DrawCentredText({"libtitle.DrawCentredText", MODULE, off(0x08f1aaac), swap_DrawCentredText});

