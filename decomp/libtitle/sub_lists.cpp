/* libtitle swaps: Sub list rows, the duel settings step, the card list entries and the small steps of screen 9. */
#include "internal.h"

/* ---- sub list rows, the options screen step, the duel settings step and the card list entries ---- */

/* FUN_08efdbf8 / 08efdffc(list, row, x, y, width): draw one row of sub list 5 / 6: its text (messages 45+ / 47+) in the list's colour for that row.
 * Rows of a list that is still opening are only drawn once their colour is opaque. The original also clears a 512-byte scratch buffer. */
static uint32_t drawSubListRow(SwapCtx &x, uint32_t list, uint32_t row, int32_t px, int32_t py, int32_t width, uint32_t firstMessage) {
    GuestStack scratch(x, 544);
    memset(x.mem(scratch.addr() + 32), 0, 512);
    const uint32_t p = kTitleList2;
    const bool opaque = (x.at<uint32_t>(p + 20 + 4 * (row & 0xFFFF)) & 0xFF000000u) == 0xFF000000u;
    if (x.at<uint8_t>(p) < 2 && !opaque) return list;
    const uint32_t colour = x.callLocal(fn::ItemColor, p, row);
    ehsys::SetFontSize(x, 15, 15);
    ehsys::SetTextColor(x, colour);
    ehsys::SetUiFlag4(x, true);
    return x.callLocal(fn::DrawMessageText, list, (uint32_t)(px + 8), (uint32_t)(py + 4), (uint32_t)(width - 16), 0, (row & 0xFFFF) + firstMessage, 3);
}
static void swap_DrawSubList5Row(SwapCtx &x) { x.ret(drawSubListRow(x, x.arg(0), x.arg(1), (int32_t)x.arg(2), (int32_t)x.arg(3), (int32_t)x.arg(4), 0x2D)); }
static SwapReg reg_DrawSubList5Row({"libtitle.DrawSubList5Row", MODULE, off(0x08efdbf8), swap_DrawSubList5Row});
static void swap_DrawSubList6Row(SwapCtx &x) { x.ret(drawSubListRow(x, x.arg(0), x.arg(1), (int32_t)x.arg(2), (int32_t)x.arg(3), (int32_t)x.arg(4), 0x2F)); }
static SwapReg reg_DrawSubList6Row({"libtitle.DrawSubList6Row", MODULE, off(0x08efdffc), swap_DrawSubList6Row});

/* FUN_08f180c0(kind): set up the options work block for the kind of options screen (0..10): clear its 68-byte record, store kind and the
 * language, and place the slide-in boxes (kinds 8 and 9 start two or three of them at x 0/100 with width 29/129). */
static void swap_InitOptionsBlock(SwapCtx &x) {
    const uint32_t block = x.at<uint32_t>(kOptionsBlockPtr), record = block + offsetof(OptionsWork, networkScene);
    OptionsWork &work = x.at<OptionsWork>(block);
    const uint32_t kind = x.arg(0);
    uint32_t rightX = 0, rightW = 0, leftX = 0, leftW = 0x1D;   /* r20, r19, r18, r21 of the original */
    memset(x.mem(record), 0, 0x44);
    work.kind = (uint8_t)kind;
    work.networkScene = (uint8_t)x.callNid(nid::GetNetworkScene);
    switch (work.kind) {
    case 0: case 1: case 5: case 8: rightW = 0x1D; break;
    case 2: case 3: case 4: case 6: case 7: case 9: rightX = 100; rightW = 0x81; break;
    }
    const bool wide = x.at<DuelSetup>(x.callNid(nid::GetSettingsBlock)).ruleA == 2;
    if (wide) { leftX = 100; leftW = 0x81; }
    switch (work.kind) {
    case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7:
        work.mode = 4;
        break;
    case 8: case 9:
        x.callLocal(fn::SetupPartnerColumns);
        work.mode = 2;
        if (x.at<DuelSetup>(x.callNid(nid::GetSettingsBlock)).ruleA == 2) x.callLocal(fn::LoadColumnImage, 3, 2, 100, 0x81);
        x.callLocal(fn::LoadColumnImage, 1, 2, rightX, rightW);
        x.callLocal(fn::LoadColumnImage, 2, 2, leftX, leftW);
        x.callLocal(fn::LoadColumnImage, 0, 2, 0, 0);
        x.callLocal(fn::SetColumnTexture, 1, 10);
        break;
    case 10:
        x.callLocal(fn::SetupPartnerColumns);
        break;
    }
}
static SwapReg reg_InitOptionsBlock({"libtitle.InitOptionsBlock", MODULE, off(0x08f180c0), swap_InitOptionsBlock});

/* FUN_08f1ad4c(screen, arg0): enter the card list / options screen (screen 9 family): reset the flow, lay out the panel set (a box 305x42 at
 * (4, 52) with five cells and its window), allocate the options block and clear the card tables, then load the slot-10 image. */
static void swap_EnterCardListScreen(SwapCtx &x) {
    State &s = state(x);
    s.screen = (int16_t)x.arg(0);
    s.arg0 = (int16_t)x.arg(1);
    s.arg1 = 0;
    state(x).arg2 = 0;
    s.transition = 0;
    state(x).panelSet.unknown364 = 0;
    state(x).panelSet.unknown365 = 2;
    state(x).menuColour[0] = 0xFFFFFFFFu;
    state(x).menuColour[1] = 0xFFFFFFFFu;
    PanelSet &set = state(x).panelSet;
    Window &win = set.window;
    x.callLocal(fn::InitPanelSet, kPanelBlock, state(x).heap);
    set.flags &= 0xFC;
    set.tabCount = 1;
    Panel &box = set.infoBox;
    box.x = 4;
    win.x = 4;
    box.y = 52;
    win.y = 52;
    box.h = 42;
    win.height = 42;
    win.textY = 43;
    win.colour = 0x00FFFFFFu;
    win.unknown204 = 20;
    win.unknown206 = 5;
    win.drawCallback = x.addr(fn::DrawInfoWindowCallback);   /* draw callback */
    win.textWidth = 305;
    win.unknown246 = 0;
    box.unknown25 = (uint8_t)win.unknown204;
    box.w = 305;
    win.width = 305;
    win.textRight = win.textWidth;
    box.cells = win.unknown206;
    win.unknown250 = (uint16_t)(44 - 2 * win.unknown246);
    for (uint16_t &margin : win.margin) margin = 0;
    win.unknown272 = 0;
    win.unknown244 = 0;
    box.label = 0;
    box.drawCell = 0;
    const uint32_t archive = x.callLocal(fn::CurrentLogoResource);
    x.callLocal(fn::NewOptionsBlock, state(x).heap, archive, 0);
    memset(x.mem(0x09B2B014), 0, 0x4C);   /* a 76-byte block in front of the card list tables (not understood yet) */
    memset(x.mem(kCardListFilters), 0, 0x1510);
    x.callLocal(fn::SlotLoad, 10, 2, 0, 479);
    x.callLocal(fn::ClearCardFlags);
}
static SwapReg reg_EnterCardListScreen({"libtitle.EnterCardListScreen", MODULE, off(0x08f1ad4c), swap_EnterCardListScreen});

/* FUN_08f1afc0: the options / card list screen's step (screen 9 family), by arg1:
 *  0 enter the kind (arg0)   1-4 the kind's own animation   5 start fading in   9 the kind's input step (kind 2 = duel settings, 3.. others)
 *  10 fade in   12-13 leave: fade out, free the options block and go back to the main menu   14 show the closing message
 *  16 come back to the first step (with the slot-12 and slot-15 images placed by kind). */
static void swap_OptionsScreenStep(SwapCtx &x) {
    State &s = state(x);
    x.callLocal(fn::OptionsKindStep, (uint32_t)(int32_t)s.arg0);
    switch ((uint16_t)s.arg1) {
    case 0:
        if (s.arg0 > 1) x.callLocal(fn::SetFadeMode, 0);
        x.callLocal(fn::EnterOptionsKind, (uint32_t)(int32_t)s.arg0);
        break;
    case 1: case 2: case 3: case 4:
        x.callLocal(fn::LoadCardListData, (uint32_t)(int32_t)s.arg0);
        break;
    case 5:
        x.callLocal(fn::FadeToBlack16, 0);
        s.pendingArg1 = 7;
        s.arg1 = 6;
        break;
    case 9:
        switch ((uint16_t)s.arg0) {
        case 2: x.callLocal(fn::DuelSettingsStep); break;
        case 3: x.callLocal(fn::CardListChoiceStep); break;
        case 4: x.callLocal(fn::CardListKind4Step); break;
        case 5: x.callLocal(fn::StepNothing); break;
        case 6: x.callLocal(fn::CardListWorkStep); break;
        case 7: x.callLocal(fn::CardListPickStep); break;
        }
        break;
    case 10:
        x.callLocal(fn::FadeToBlack16, 1);
        s.arg1++;
        break;
    case 12:
        x.at<Window>(kWindowA).drawCallback = x.addr(fn::DrawWindowCallback);
        x.callLocal(fn::RequestScreen16, 1, 0xFFF, 0xFFF);
        s.pendingArg1 = 0xFFF;
        s.arg1 = 13;
        break;
    case 13:
        if (x.callLocal(fn::GetPhase) == 0) {
            x.callNid(nid::ReleaseScreenResources);
            x.callLocal(fn::UnloadHelperModule);
            x.callLocal(fn::FreeCardListBuffers, 0);
            x.callLocal(fn::FreeOptionsBlock);
            x.callLocal(fn::SwitchScreen, 9, 4);
        }
        break;
    case 14:
        x.callLocal(fn::FreeCardListBuffers, 0);
        x.callLocal(fn::RequestScreen16, 1, 0x22, 0);
        s.pendingArg1 = 0;
        s.arg1 = 15;
        break;
    case 16:
        x.callLocal(fn::SetFadeMode, 0);
        x.callLocal(fn::EnterOptionsKind, (uint32_t)(int32_t)s.arg0);
        x.callLocal(fn::FadeToBlack16, 0);
        s.pendingArg1 = 9;
        s.arg1 = 6;
        x.callLocal(fn::SlotSetRange, 15, 2, 18, 53);
        if (s.arg0 == 3) x.callLocal(fn::SlotSetRange, 12, 2, 30, 59);
        else x.callLocal(fn::SlotSetRange, 12, 2, 130, 159);
        break;
    }
    state(x).publishedArg0 = (uint16_t)s.arg0;
}
static SwapReg reg_OptionsScreenStep({"libtitle.OptionsScreenStep", MODULE, off(0x08f1afc0), swap_OptionsScreenStep});

/* FUN_08f1b858: the duel settings questions (options kind 2, step 9). The answers live in two bytes at 0x09B2AFC4: bit 5 of the first is the
 * first answer, bit 6 the second, bit 7 the third (0 skips the last two), the second byte holds two 4-bit answers. arg2 is the question
 * (0..4) and 5 means "Next". The list window answers a row number, -1 nothing yet, -2 cancelled (back one question) and -3 not ready. */
static void swap_DuelSettingsStep(SwapCtx &x) {
    State &s = state(x);
    if (s.arg1 != 9) return;
    const uint32_t w = kWindowA;
    Window &window = x.at<Window>(w);
    FreeDuelSettings &settings = x.at<FreeDuelSettings>(kFreeDuelSettings);
    uint8_t &first = settings.answers, &second = settings.picks;
    window.drawCallback = 0;
    const uint32_t answer = x.callNid(nid::UpdateWindow, w);
    auto clearWindowState = [&] {
        window.stateFlags &= 0xFC;
        window.phase &= 0xE0;
        window.animationFrame = 0;
    };
    if (answer == 0xFFFFFFFDu) {
        /* not ready */
    } else if (answer == 0xFFFFFFFEu) {
        if (s.arg2 == 0) {
            s.arg1 = 12;
        } else {
            if (s.arg2 == 5 && (first & 0x80) == 0) s.arg2 = 2;
            else s.arg2 = (int16_t)(s.arg2 - 1);
            window.animationFrame = 0;
            clearWindowState();
            uint8_t &shown = window.phase;
            switch ((uint16_t)s.arg2) {
            case 0: shown |= (first >> 5) & 1; break;
            case 1: shown |= (first >> 6) & 1; break;
            case 2: shown |= (first >> 7) & 1; break;
            case 3: shown |= second & 0xF; break;
            case 4: shown |= (second >> 4) & 0xF; break;
            }
        }
    } else if (answer != 0xFFFFFFFFu) {
        switch ((uint16_t)s.arg2) {
        case 0: s.arg2++; first = (uint8_t)((first & 0xDF) | ((answer & 1) << 5)); break;
        case 1: s.arg2++; first = (uint8_t)((first & 0xBF) | ((answer & 1) << 6)); break;
        case 2:
            first = (uint8_t)((first & 0x7F) | ((answer & 1) << 7));
            if (answer == 1) s.arg2++;
            else if (answer == 0) { second = 2; s.arg2 = 5; }
            break;
        case 3: s.arg2++; second = (uint8_t)((second & 0xF0) | (answer & 0xF)); break;
        case 4: second = (uint8_t)((second & 0x0F) | ((answer & 0xF) << 4)); s.arg2 = 5; break;
        case 5:
            s.arg1 = 10;
            x.callLocal(fn::SetFadeMode, 1);
            s.pendingArg0 = (first & 0x20) ? 4 : 3;
            x.at<CardListFilters>(kCardListFilters).second = 0;
            s.pendingArg1 = 0;
            s.pendingArg2 = 0;
            break;
        }
        window.animationFrame = 0;
        clearWindowState();
    }
    switch ((uint16_t)s.arg2) {
    case 0: case 1: case 2: window.unknown204 = 2; break;
    case 3: case 4: window.unknown204 = 5; break;
    case 5: window.unknown204 = 0; break;
    }
}
static SwapReg reg_DuelSettingsStep({"libtitle.DuelSettingsStep", MODULE, off(0x08f1b858), swap_DuelSettingsStep});

/* FUN_08f1e244(card): the 2-bit class of a card record (ehsys DAEC7B62 on a copy of its 264 bytes) */
static void swap_CardClass(SwapCtx &x) {
    const uint32_t card = x.arg(0);
    GuestStack frame(x, 288);
    const uint32_t copy = frame.addr() + 24;
    memcpy(x.mem(copy), x.mem(card), 264);
    x.ret(x.callNid(nid::CheckDeckRules, copy, 0));
}
static SwapReg reg_CardClass({"libtitle.CardClass", MODULE, off(0x08f1e244), swap_CardClass});

/* FUN_08f1df4c(kind): build the tables of the card list screen at 0x09B2B29C. Entries are 6 bytes: +0 bits 0-1 the card's class and bits 2-11
 * its position, +2 the card (or save slot) number, +4 how many copies are owned. Kinds 1 and 41 list every owned card (ids 1..350, minus the
 * ids that do not exist), then the 51 deck records and the extra records of the second table; any other kind lists the (up to six) cards of
 * the list ehsys gives. The count ends in 0x09B29FA4 and the last byte of its header (+2169). */
static void swap_BuildCardListEntries(SwapCtx &x) {
    const uint32_t kind = x.arg(0);
    CardListTables &tables = x.at<CardListTables>(kCardListTables);
    auto setClass = [&](CardEntry &entry, uint32_t cls) { entry.classPosition = (uint16_t)((entry.classPosition & 0xFFFC) | (cls & 3)); };
    auto setPosition = [&](CardEntry &entry, uint32_t position) {
        entry.classPosition = (uint16_t)((entry.classPosition & 0xF003) | ((position & 0x3FF) << 2));
    };
    tables.kind = kind;
    if (kind == 1 || kind == 41) {
        GuestStack scratch(x, 3 * 272);
        const uint32_t buf1 = scratch.addr(), buf2 = buf1 + 272, buf3 = buf2 + 272;
        int32_t missing = 0;
        uint32_t found = 0;
        for (int32_t id = 1; id < 351; id++) {
            const uint32_t record = x.callNid(nid::GetProgressSlot, (uint32_t)id);
            if (x.callNid(nid::GetCardIndex, (uint32_t)id) == 0) { missing++; continue; }
            if ((x.at<uint8_t>(record) & 1) == 0) continue;
            x.callNid(nid::ReadDeckRecord, (uint32_t)id, buf1);
            CardEntry &entry = tables.owned[found];
            setClass(entry, x.callLocal(fn::CardClass, buf1));
            found++;
            setPosition(entry, (uint32_t)(id - missing));
            entry.number = (uint16_t)id;
            entry.copies = (uint8_t)((x.at<uint16_t>(record) & 0x1FF) >> 1);
            tables.count = (uint16_t)found;
        }
        for (int32_t slot = 0; slot < 51; slot++) {
            CardEntry &entry = tables.deckSlot[slot];
            if (slot == 0) {
                x.callNid(nid::CopyDeckBuffer, buf2);
                setClass(entry, x.callLocal(fn::CardClass, buf2));
                entry.number = 0;
            } else {
                const uint32_t save = x.callNid(nid::GetSaveRecordOffset, (uint32_t)(slot - 1));
                if (x.at<uint32_t>(save) != 0) {
                    setClass(entry, x.callLocal(fn::CardClass, save + 84));
                    entry.number = (uint16_t)slot;
                }
            }
        }
        for (int32_t i = 0; i < (int32_t)tables.mainRecords; i++) {
            memcpy(x.mem(buf3), x.mem(tables.mainBuffer + 0x208 * (uint32_t)i + 256), 264);
            tables.second[i].number = (uint16_t)i;
            setClass(tables.second[i], x.callLocal(fn::CardClass, buf3));
        }
        state(x).subListCount = 51;
        state(x).subListFound = 6;
    } else {
        const ProgressRecord &list = x.at<ProgressRecord>(x.callNid(nid::GetProgressRecord, kind));   /* o original passa o personagem (kind) em r4: sem isso o Free Duel lia a lista vazia e o oponente ficava com o deck 1 (Jaden) */
        uint32_t count = list.count;
        if ((int32_t)count > 6) count = 6;
        uint32_t found = 0;
        for (uint32_t i = 0; i < count; i++) {
            const uint16_t card = list.card[i];
            if (x.callNid(nid::GetCardIndex, card) == 0) continue;
            CardEntry &entry = tables.owned[found];
            entry.number = card;
            entry.classPosition = (uint16_t)((entry.classPosition & 0xFFFC) | 1);
            found++;
            setPosition(entry, i + 1);
            tables.count = (uint16_t)found;
        }
        state(x).subListCount = tables.count;
        state(x).subListFound = (uint8_t)found;
    }
}
static SwapReg reg_BuildCardListEntries({"libtitle.BuildCardListEntries", MODULE, off(0x08f1df4c), swap_BuildCardListEntries});

/* ---- sub list 3, the small screen steps (screen 9 family), dialog windows ---- */

/* ehsys calls the window blocks (kWindowA...) are driven with */
constexpr uint8_t kWindowClosed = 6;   /* Window::state once the closing animation has finished */

/* FUN_08efd644: build sub list 3 (four rows). Row 0 is greyed out when the first progress bit is clear, rows 2 and 3 depend on the story progress. */
static void swap_BuildSubList3(SwapCtx &x) {
    buildSubList(x, {3, 4, 81, 70, fn::DrawSubList3Row, false, false});
    GuestStack frame(x, 32);
    const uint32_t flags = frame.addr() + 24;
    x.callNid(nid::GetProgressFlags, flags);
    const uint32_t p = kTitleList2;
    if ((x.at<uint16_t>(flags) & 1) == 0) menu(x, p).itemState[0] = kItemDisabled;
    const int32_t progress = (int32_t)x.callNid(nid::GetSaveProgress);
    if (progress != 2) {
        if (progress != 1 && progress != 0) return;
        menu(x, p).itemState[2] = kItemDisabled;
    }
    menu(x, p).itemState[3] = kItemDisabled;
}
static SwapReg reg_BuildSubList3({"libtitle.BuildSubList3", MODULE, off(0x08efd644), swap_BuildSubList3});

/* FUN_08efd794: input of sub list 3: rows 0..3 fade out to screens 27..30 */
static void swap_SubList3Input(SwapCtx &x) {
    State &s = state(x);
    x.callLocal(fn::ListUpdate, kTitleList, 2);
    if (s.arg1 != 0) return;
    const int32_t answer = (int32_t)x.callLocal(fn::ListUpdate, kTitleList2, 1);
    if (answer >= 0 && answer <= 3) {
        x.callNid(nid::SetAttractTimer, 600);
        s.arg1 = (int16_t)(s.arg1 + 1);
        s.transition = 0;
        x.callLocal(fn::RequestScreen16, 1, (uint32_t)(int32_t)(int16_t)(answer + 0x1B), 0);
    } else if (answer == -2) {
        leaveSubList(x, s);
    }
}
static SwapReg reg_SubList3Input({"libtitle.SubList3Input", MODULE, off(0x08efd794), swap_SubList3Input});

/* FUN_08efd8a8: one row of sub list 3 */
static void swap_DrawSubList3Row(SwapCtx &x) { x.ret(drawSubListRow(x, x.arg(0), x.arg(1), (int32_t)x.arg(2), (int32_t)x.arg(3), (int32_t)x.arg(4), 0x1D)); }
static SwapReg reg_DrawSubList3Row({"libtitle.DrawSubList3Row", MODULE, off(0x08efd8a8), swap_DrawSubList3Row});

/* FUN_08efecbc: a screen of the options family (arg1 0..4): fade out and start the language load (6FFA2A28 / 778817A2), then go back to
 * the screen remembered at +0x18 (or to 9 / step 9 when the flag at +0x1A is clear) */
static void swap_LanguageReturnStep(SwapCtx &x) {
    State &s = state(x);
    const uint32_t heap = state(x).heap;
    auto mirror = [&] { state(x).publishedArg0 = (uint16_t)s.arg0; };
    if (s.arg1 == 4) {
        uint8_t &returnFlag = state(x).returnFlags;
        if ((returnFlag & 1) == 0) {
            x.callLocal(fn::SwitchScreen, 9, 9);
        } else {
            returnFlag &= 0xFE;
            x.callLocal(fn::SwitchScreen, 9, (uint32_t)(int32_t)state(x).returnArg);
        }
    } else {
        if (s.arg1 != 3) {
            if (s.arg1 != 2) {
                if (s.arg1 == 1) { mirror(); return; }
                if (s.arg1 != 0) { mirror(); return; }
                x.callNid(nid::SetAttractTimer, 600);
                x.callLocal(fn::RequestScreen16, 0, 0xFFF, 0xFFF);
                s.arg1++;
                s.pendingArg1 = 2;
                mirror();
                return;
            }
            if (x.callNid(nid::StartDataLoad, 5, heap) != 0) s.arg1++;
        }
        if (x.callNid(nid::PollDataLoad) != 1) s.arg1 = 4;
    }
    mirror();
}
static SwapReg reg_LanguageReturnStep({"libtitle.LanguageReturnStep", MODULE, off(0x08efecbc), swap_LanguageReturnStep});

/* FUN_08efedd8: the "save data" dialog flow (arg1 0..4): 0 open the dialog (message 75), 1 wait for the answer (2 = cancel: close, go to step 3 and
 * fade out; 3 or closed: close and go to step 2), 2 once closed go back to the remembered list screen */
static void swap_SaveDialogStep(SwapCtx &x) {
    State &s = state(x);
    const uint32_t w = kWindowA;
    Window &window = x.at<Window>(w);
    if (s.arg1 == 4 || s.arg1 == 3) return;
    if (s.arg1 == 2) {
        x.callNid(nid::UpdateWindow, w);
        if (window.state == kWindowClosed) {
            state(x).returnFlags &= 0xFE;
            x.callLocal(fn::EnterListScreen, 9, (uint32_t)(int32_t)state(x).returnArg, 1);
        }
    } else if (s.arg1 == 1) {
        if (window.state < 3) {
            x.callNid(nid::UpdateWindow, w);
        } else {
            const int32_t answer = (int32_t)x.callNid(nid::UpdateWindow, w);
            if (answer == 2) {
                x.callNid(nid::ResetWindow, w);
                s.arg1 = 3;
                x.callLocal(fn::RequestScreen16, 1, 0xFFF, 9);
            } else if (answer == 3 || answer == -2) {
                x.callNid(nid::ResetWindow, w);
                s.arg1 = 2;
            }
        }
    } else if (s.arg1 == 0) {
        x.callLocal(fn::OpenDialog, 0x4B, 1, 1);
        s.arg1 = 1;
        x.callNid(nid::PlaySound, kSoundDialogOpen);
    }
}
static SwapReg reg_SaveDialogStep({"libtitle.SaveDialogStep", MODULE, off(0x08efedd8), swap_SaveDialogStep});

/* FUN_08efef40(list): draw the dialog window while it is open (steps 1 and 2) */
static void swap_DrawSaveDialog(SwapCtx &x) {
    uint32_t list = x.arg(0);
    const State &s = state(x);
    if (s.arg1 != 4 && (s.arg1 == 2 || s.arg1 == 1)) list = x.callLocal(fn::CentreAndRun, list, kWindowA, 1);
    x.ret(list);
}
static SwapReg reg_DrawSaveDialog({"libtitle.DrawSaveDialog", MODULE, off(0x08efef40), swap_DrawSaveDialog});

/* FUN_08eff0b8(next, pending): start screen 2 (the logo sequence) with these as the next screen and its pending argument */
static void swap_StartLogoWith(SwapCtx &x) {
    const uint32_t next = x.arg(0), pending = x.arg(1);
    x.callLocal(fn::StartScreen, 2, 0);
    state(x).nextScreen = (int16_t)next;
    state(x).pendingArg0 = (uint16_t)pending;
}
static SwapReg reg_StartLogoWith({"libtitle.StartLogoWith", MODULE, off(0x08eff0b8), swap_StartLogoWith});

/* FUN_08eff45c: the "really quit / continue" dialog flow (arg1 0..4). The pad's button 8 skips it. The dialog (message 73) answers 1 = yes
 * (step 2), 4 or closed = no (step 3). Once the window has closed: step 2 -> arg0 5, step 3 -> back to screen 9 step 6, step 4 -> arg0 4. */
static void swap_QuitDialogStep(SwapCtx &x) {
    State &s = state(x);
    const uint32_t w = kWindowA;
    Window &window = x.at<Window>(w);
    const uint32_t pad = x.callNid(nid::GetInput);
    const uint32_t buttons = x.at<ehsys::PadState>(pad).pressed;
    if (s.arg1 == 4 || s.arg1 == 3 || s.arg1 == 2) {
        x.callNid(nid::UpdateWindow, w);
        if (window.state != kWindowClosed) return;
        if (s.arg1 == 2) { s.arg1 = 0; s.arg0 = 5; }
        else if (s.arg1 == 3) { state(x).savedSelection = 5; x.callLocal(fn::SwitchScreen, 9, 6); }
        else { s.arg1 = 0; s.arg0 = 4; }
        return;
    }
    if (s.arg1 != 1) {
        if (s.arg1 != 0) return;
        x.callNid(nid::PlaySound, kSoundDialogOpen);
        x.callLocal(fn::OpenDialog, 0x49, 2, 0);
        s.arg1++;
    }
    if ((buttons & 8) == 0) {
        const int32_t answer = (int32_t)x.callNid(nid::UpdateWindow, w);
        if (answer == 1) s.arg1 = 2;
        else if (answer == 4 || answer == -2) s.arg1 = 3;
    } else {
        s.arg1 = 4;
        x.callNid(nid::ResetWindow, w);
    }
}
static SwapReg reg_QuitDialogStep({"libtitle.QuitDialogStep", MODULE, off(0x08eff45c), swap_QuitDialogStep});

/* FUN_08f02884(a, b): push scene 13 onto the scene stack (ehsys 551A8EF2) after resetting the overlay; returns a & 0xFFFF */
static void swap_PushScene13(SwapCtx &x) {
    const uint32_t a = x.arg(0), b = x.arg(1);
    x.callNid(nid::SetSceneCallbacks, 0, 0, 0);
    ehsys::SetScreenOverlay(x, true);
    x.callNid(nid::WaitFrame);
    x.callNid(nid::PushScene, 13, a, b);
    x.ret(a & 0xFFFF);
}
static SwapReg reg_PushScene13({"libtitle.PushScene13", MODULE, off(0x08f02884), swap_PushScene13});

/* FUN_08f039cc(chapter, card): store the story progress. chapter 0 reads it from the progress word (bits 1-3 of the low byte, only if its bit 0
 * is set); card 0 takes bits 4-15. Sets the card's state, remembers the progress and pushes scene 13 with (2, 0). */
static void swap_StoreProgress(SwapCtx &x) {
    const uint32_t chapterIn = x.arg(0);
    uint32_t card = x.arg(1), chapter = chapterIn;
    GuestStack frame(x, 32);
    const uint32_t word = frame.addr() + 24;
    x.callNid(nid::GetProgressFlags, word);
    const uint16_t progress = x.at<uint16_t>(word);
    if (chapterIn == 0 && (progress & 1) != 0) chapter = ((progress & 0xFF) & 0xF) >> 1;
    if (card == 0) card = (progress & 0xFFF) >> 4;
    const uint32_t state = x.callNid(nid::CardState, card);
    x.callNid(nid::SetCardState, card, state);
    if (chapterIn != 0) x.callNid(nid::RecordChapterCard, chapter, card);
    x.callLocal(fn::PushScene13, 2, 0);
}
static SwapReg reg_StoreProgress({"libtitle.StoreProgress", MODULE, off(0x08f039cc), swap_StoreProgress});

/* FUN_08f03624(x, width, archiveKey, textIndex, outText, outX, outWidth): look a text up and say where to draw it to centre it in `width`:
 * *outText = the text, *outX = x + (width - textWidth) / 2, *outWidth = textWidth */
static void swap_CentredTextPlacement(SwapCtx &x) {
    const int32_t px = (int32_t)x.arg(0);
    int32_t width = (int32_t)x.arg(1);
    const uint32_t key = x.arg(2), index = x.arg(3), outText = x.arg(4), outX = x.arg(5), outWidth = x.arg(6);
    const uint32_t archive = x.callLocal(fn::CurrentLogoResource);
    const uint32_t file = x.callLocal(fn::MessageRecord, key);
    const uint32_t text = ehsys::GetText(x, ehsys::FindTexture(x, archive, file, 0), index);
    x.at<uint32_t>(outText) = text;
    const int32_t textWidth = ehsys::TextWidth(x, text);
    width -= textWidth >> 6;
    if (width < 0) width += 1;
    x.at<int32_t>(outX) = px + (width >> 1);
    x.at<int32_t>(outWidth) = textWidth >> 6;
}
static SwapReg reg_CentredTextPlacement({"libtitle.CentredTextPlacement", MODULE, off(0x08f03624), swap_CentredTextPlacement});

/* FUN_08f04e5c: clear the title text box (384 bytes) and free its window set */
static void swap_ClearMenuInput(SwapCtx &x) {
    x.at<TitleTextBox>(kTextBox).unknown15C = 0;
    x.callNid(nid::FreeWindowSet, kTextBoxWindow);
    memset(x.mem(kTextBox), 0, sizeof(TitleTextBox));
    x.ret(kTextBox);
}
static SwapReg reg_ClearMenuInput({"libtitle.ClearMenuInput", MODULE, off(0x08f04e5c), swap_ClearMenuInput});

/* FUN_08f08a34(message, kind, answer): set up the dialog window (block kWindowA): 360x104 at (60, 100) with the text of `message` (formatted with
 * two numbers). kind 1 = one button (OK), 2 = two buttons (yes / no), 3 = no button; `answer` picks the button that starts selected. */
static void swap_OpenDialog(SwapCtx &x) {
    const uint32_t message = x.arg(0), kind = x.arg(1), answer = x.arg(2);
    constexpr uint32_t kFormatBuffer = 0x09B2C7F0;
    const uint32_t w = kWindowA;
    Window &window = x.at<Window>(w);
    x.callLocal(fn::InitWindow, w);
    window.frameKind = 2;
    window.animationA = 2;
    window.animationB = 2;
    if (message == 0x49) { window.frameKind = 0; window.textColour = 0xFF000000u; }
    window.unknown96 = 1;
    window.margin[0] = 10;
    window.margin[1] = 10;
    window.margin[2] = 8;
    window.textSize = 12;
    window.x = 60;
    window.y = 100;
    window.width = 360;
    window.height = 104;
    window.colour = 0xFFFFFFFFu;
    window.margin[3] = 0;
    window.unknown160 = 0;
    const uint32_t text = x.callLocal(fn::FirstMessageText, message);
    x.callNid(nid::Format, kFormatBuffer, text, 0x2122, 0x2122);   /* sprintf with the number twice */
    window.text = kFormatBuffer;
    uint8_t &bits = window.flags;
    bits = (uint8_t)((bits & 0xC0) | 0x20);
    auto low = [&](uint32_t offset, uint32_t v) { x.at<uint8_t>(w + offset) = (uint8_t)((x.at<uint8_t>(w + offset) & 0xF0) | v); };
    if (kind == 1) {
        window.flags2 &= 0x87;
        window.unknown204 = 0;
        low(202, 3);
        low(211, answer == 0 ? 3 : 2);
        window.unknown212 = 3;
        window.unknown224 = 0x100;
    } else if (kind == 2) {
        window.flags2 &= 0x87;
        window.unknown204 = 0;
        low(202, 2);
        low(211, answer == 0 ? 1 : 4);
        window.unknown212 = 4;
        window.unknown224 = 0x100;
    } else if (kind == 3) {
        window.unknown212 = 1;
        low(211, 1);
        window.flags2 &= 0x87;
        window.unknown204 = 0;
        low(202, 1);
        window.unknown224 = 0x100;
        bits = (uint8_t)((window.flags & 0xC0) | 0x24);
    }
    x.callLocal(fn::RegisterWindow, w);
}
static SwapReg reg_OpenDialog({"libtitle.OpenDialog", MODULE, off(0x08f08a34), swap_OpenDialog});

/* FUN_08f0b954(window, screen, message, archiveKey): the message bar window (the same box DrawMessageBar draws as a panel): y 231 on screen 14, else 187 */
static void swap_InitMessageWindow(SwapCtx &x) {
    const uint32_t w = x.arg(0), screen = x.arg(1), message = x.arg(2), key = x.arg(3);
    Window &window = x.at<Window>(w);
    x.callLocal(fn::InitWindow, w);
    window.frameKind = 0;
    window.animationA = 0;
    window.animationB = 0;
    window.x = 4;
    window.y = screen == 14 ? 0xE7 : 0xBB;
    window.width = 0x140;
    window.height = 0x22;
    window.colour = 0xFFFFFFFFu;
    window.unknown160 = 0;
    window.text = x.callLocal(fn::MessageText, message, key);
    window.unknown136 = 1;
    window.margin[0] = 1;
    window.margin[1] = 0;
    window.margin[2] = 1;
    window.margin[3] = 0;
    window.textSize = 0x0B;
    window.textColour = 0xFF004040u;
    window.flags &= 0xC0;
    window.unknown204 = 0;
    window.unknown206 = 0;
    x.callLocal(fn::RegisterWindow, w);
}
static SwapReg reg_InitMessageWindow({"libtitle.InitMessageWindow", MODULE, off(0x08f0b954), swap_InitMessageWindow});

/* FUN_08f0f298: enter the deck editor's first screen: clear the 16 duel setting bytes, reset the flow, build the panel set and the first window */
static void swap_EnterDeckScreen(SwapCtx &x) {
    State &s = state(x);
    memset(x.mem(kFreeDuelSettings), 0, sizeof(FreeDuelSettings));
    s.arg1 = 0;
    state(x).arg2 = 0;
    s.transition = 0;
    state(x).panelSet.unknown364 = 0;
    state(x).panelSet.unknown365 = 2;
    state(x).menuColour[0] = 0xFFFFFFFFu;
    state(x).menuColour[1] = 0xFFFFFFFFu;
    x.callLocal(fn::InitDeckPanel, kPanelBlock);
    Window &infoWindow = state(x).panelSet.window;
    infoWindow.flags = (uint8_t)((infoWindow.flags & 0xBB) | 0x44);
    infoWindow.drawCallback = x.addr(fn::DrawWindowCallback);
    x.callLocal(fn::InitHelpWindow, kWindowB, 0x73);
    x.callLocal(fn::SlotLoad, 7, 2, 0, 480);
    x.callLocal(fn::SlotLoad, 14, 2, 0, 17);
}
static SwapReg reg_EnterDeckScreen({"libtitle.EnterDeckScreen", MODULE, off(0x08f0f298), swap_EnterDeckScreen});

