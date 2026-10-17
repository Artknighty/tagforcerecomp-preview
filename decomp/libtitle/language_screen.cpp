/* libtitle swaps: The card database language screen and the last functions the recorded sessions still ran as generated code. */
#include "internal.h"

/* ---- the card database language screen (five rows, one per language) ---- */

constexpr uint32_t kSelectedLanguage = 0x09B2C7A8;   /* byte: language chosen on this screen, 1..5 (0 = none yet) */
/* kLogoIndexAddr (0x09B2C7A4) holds the language the card database is loaded for */

/* FUN_08f0768c: enter the screen: build the five-row list (cursor on the language in use) and load the three background images */
static void swap_EnterLanguageSelect(SwapCtx &x) {
    constexpr uint32_t p = kTitleList;
    x.callLocal(fn::RequestScreen16, 0, 0xFFF, 0xFFE);
    x.callLocal(fn::InitList, p);
    MenuList &m = menu(x, p);
    m.stateAfterConfirm = kListIdle;
    m.chosen = 0;
    m.openAnimation = 0;
    m.confirmBehaviour = 0;
    m.cancelBehaviour = state(x).screen == 40 ? 4 : 0;
    m.itemCount = 5;
    m.unknown7 = 0;
    x.callLocal(fn::ResetList, p, 4, 1);
    m.x = 140; m.y = 128; m.width = 200; m.rowHeight = 26;
    m.spriteCell = 7;
    m.spriteCellMarked = 1;
    m.hasMarkedItem = 1;
    const uint32_t inUse = x.at<uint32_t>(kLogoIndexAddr);
    m.spriteCellPressed = 0;
    if (inUse == 0) {
        x.at<uint8_t>(kSelectedLanguage) = 0;
        m.markedItem = 0;
    } else {
        x.at<uint8_t>(kSelectedLanguage) = (uint8_t)inUse;
        m.markedItem = (uint8_t)((inUse & 0xFF) - 1);
        m.selected = m.markedItem;
    }
    m.unknown86 = 1;
    m.flags = 0xFF;
    m.drawCallback = x.addr(fn::DrawLanguageRow);   /* draws one row */
    x.callLocal(fn::SlotLoad, 5, 2, 0, 480);
    x.callLocal(fn::SlotLoad, 6, 2, 0, 480);
    x.callLocal(fn::SlotLoad, 13, 2, 0, 16);
}
static SwapReg reg_EnterLanguageSelect({"libtitle.EnterLanguageSelect", MODULE, off(0x08f0768c), swap_EnterLanguageSelect});

/* FUN_08f077e8: the language screen's step by arg0: 1 wait for the slot-13 image then (2) run the list; a confirmed row remembers the language and
 * waits (3) for the list's own closing animation; 4 fade out; 5 either load the card database for the new language (when it changed) or leave;
 * 6 wait for the load (engine 5836AFF1 started it); 7 when the database is loaded, leave. The list's byte +112 holds a blink state: bits 6-7 = 1
 * for the confirm blink (cleared by any key or after 31 frames). */
static void swap_LanguageSelectStep(SwapCtx &x) {
    State &s = state(x);
    constexpr uint32_t p = kTitleList;
    uint8_t &blink = menu(x, p).blink;
    uint32_t mode = 1;
    if ((blink >> 6) == 1) {
        if ((blink & 0x3F) >= 31) {
            blink = 0;
        } else if (s.arg0 == 2) {
            const uint32_t pad = x.callNid(nid::GetInput);
            const uint32_t pressed = x.at<ehsys::PadState>(pad).pressed;
            if ((pressed & 0x10) != 0 || (pressed & 0x40) != 0 || (pressed & (x.callNid(nid::GetButtonMask, 1) & 0xFFFF)) != 0) blink = 0;
        }
    }
    auto runList = [&] {
        const int32_t result = (int32_t)x.callLocal(fn::ListUpdate, p, mode);
        if (result >= 0 && result <= 4) {
            blink &= 0xC0;
            menu(x, p).markedItem = (uint8_t)result;
            x.at<uint8_t>(kSelectedLanguage) = (uint8_t)(result + 1);
            s.arg0 = 3;
        } else if (result == -2) {
            x.callLocal(fn::RequestScreen16, 1, 0xFFF, 5);
            s.arg0 = 4;
        }
    };
    switch ((uint16_t)s.arg0) {
    case 1: {
        const int32_t slot = (int32_t)x.callLocal(fn::SlotQuery, 13);
        if (slot == 16) {
            x.callLocal(fn::SlotSetRange, 13, 2, 17, 96);
            s.arg0++;
            runList();
        } else if (slot < 16) {
            mode = 0;
            runList();
        } else {
            runList();
        }
        break;
    }
    case 2: runList(); break;
    case 3:
        if ((blink >> 6) == 0) {
            x.callLocal(fn::RequestScreen16, 1, 0xFFF, 5);
            s.arg0 = 4;
        }
        break;
    case 5:
        if (s.screen == 0x28) {
            x.callLocal(fn::FreeSlotBlock, x.at<uint32_t>(kLogoIndexAddr) + 3);
            x.callNid(nid::SetTutorialAndMessageSpeed, x.at<uint8_t>(kSelectedLanguage));
            x.at<uint16_t>(kReadyAddr) = 0;
            s.screen = 0;
            s.arg0 = 0;
        } else if (x.at<uint32_t>(kLogoIndexAddr) == x.at<uint8_t>(kSelectedLanguage)) {
            x.callLocal(fn::SwitchScreen, 9, 10);
        } else {
            x.callLocal(fn::LoadMenuArchives, 1);
            x.callNid(nid::StartLanguageDataLoad);
            s.arg0 = 6;
            ehsys::SetScreenOverlay(x, true);
        }
        break;
    case 7:
        if (x.callNid(nid::IsLoadDone) != 0) {
            ehsys::SetScreenOverlay(x, false);
            x.callLocal(fn::SwitchScreen, 9, 10);
        }
        break;
    case 0: case 4: case 6: default: break;
    }
    state(x).publishedArg0 = (uint16_t)s.arg0;
}
static SwapReg reg_LanguageSelectStep({"libtitle.LanguageSelectStep", MODULE, off(0x08f077e8), swap_LanguageSelectStep});

/* FUN_08f07e60(list, row, x, y): the little flag of a row, a sprite of the screen's sheet (u shifted by the row; row 0 uses the first flag when the
 * engine says the second language set is not in use) */
static uint32_t drawLanguageFlag(SwapCtx &x, uint32_t list, int32_t row, int32_t px, int32_t py) {
    const int16_t *rect = (const int16_t *)x.mem(x.addr(off(0x08f2722c)));   /* u, v, w, h */
    int16_t u = rect[0];
    const int16_t v = rect[1], w = rect[2], h = rect[3];
    const uint32_t set = x.callNid(nid::GetUiMode);
    if (row == 4 || row == 3 || row == 2 || row == 1) u = (int16_t)(u + (int16_t)((w + 1) * (row + 1)));
    else if (row == 0 && set > 1) u = (int16_t)(u + (int16_t)((w + 1) * (row + 1)));
    list = x.callLocal(fn::SetupDrawState, list, 1);
    const uint32_t archive = x.callLocal(fn::CurrentLogoResource);
    const uint32_t sheet = x.callLocal(fn::LogoName, 0x24);
    list = ehsys::BindTexture(x, list, ehsys::FindTexture(x, archive, sheet, 0));
    list = ehsys::SetSpriteSize(x, list, w, h, w, h);
    return ehsys::DrawSprite(x, list, px, py, 0, u, v, 0, 0);
}
static void swap_DrawLanguageFlag(SwapCtx &x) { x.ret(drawLanguageFlag(x, x.arg(0), (int32_t)x.arg(1), (int32_t)x.arg(2), (int32_t)x.arg(3))); }
static SwapReg reg_DrawLanguageFlag({"libtitle.DrawLanguageFlag", MODULE, off(0x08f07e60), swap_DrawLanguageFlag});

/* FUN_08f07cac(list): the cursor sprite beside the list (at 198, 53), a sprite of the same sheet picked by the selected row (0x09B2A000) */
static void swap_DrawLanguageCursor(SwapCtx &x) {
    uint32_t list = x.arg(0);
    const int16_t *rect = (const int16_t *)x.mem(x.addr(off(0x08f27194)));   /* u, v, w, h */
    int16_t u = rect[0], v = rect[1];
    const int16_t w = rect[2], h = rect[3];
    const uint32_t set = x.callNid(nid::GetUiMode);
    const uint32_t row = menu(x, kTitleList).markedItem;
    if (row == 4 || row == 3 || row == 2) {
        v = (int16_t)(v + h);
        u = (int16_t)(u + (int16_t)(((int32_t)(row - 2) % 3) * w));
    } else if (row == 1 || (row == 0 && set >= 2)) {
        u = (int16_t)(u + (int16_t)(((row + 1) % 3) * w));
    } else if (row == 0) {
        u = (int16_t)(u + (int16_t)((row % 3) * w));
    }
    list = x.callLocal(fn::SetupDrawState, list, 1);
    const uint32_t archive = x.callLocal(fn::CurrentLogoResource);
    const uint32_t sheet = x.callLocal(fn::LogoName, 0x23);
    list = ehsys::BindTexture(x, list, ehsys::FindTexture(x, archive, sheet, 0));
    list = ehsys::SetSpriteSize(x, list, w, h, w, h);
    x.ret(ehsys::DrawSprite(x, list, 0xC6, 0x35, 0, u, v, 0, 0));
}
static SwapReg reg_DrawLanguageCursor({"libtitle.DrawLanguageCursor", MODULE, off(0x08f07cac), swap_DrawLanguageCursor});

/* FUN_08f07b64(list, row, x, y, width): one row of the language list: the language's name (messages 0x36..0x3A) centred in the row, then its flag */
static void swap_DrawLanguageRow(SwapCtx &x) {
    uint32_t list = x.arg(0);
    const uint32_t rowArg = x.arg(1);
    const int32_t px = (int32_t)x.arg(2), py = (int32_t)x.arg(3), width = (int32_t)x.arg(4);
    GuestStack scratch(x, 576);
    memset(x.mem(scratch.addr() + 64), 0, 512);
    const uint32_t row = rowArg & 0xFFFF;
    static const uint32_t kNameMessage[5] = {0x36, 0x38, 0x37, 0x39, 0x3A};
    const uint32_t message = row < 5 ? kNameMessage[row] : row + 0x36;
    const uint32_t colour = x.callLocal(fn::ItemColor, kTitleList, rowArg);
    ehsys::SetFontSize(x, 15, 15);
    ehsys::SetTextColor(x, colour);
    ehsys::SetUiFlag4(x, true);
    const uint32_t outText = scratch.addr() + 52, outX = scratch.addr() + 56, outWidth = scratch.addr() + 60;
    x.callLocal(fn::CentredTextPlacement, (uint32_t)px, (uint32_t)width, 0, message, outText, outX, outWidth);
    list = x.callLocal(fn::DrawTextLine, list, x.at<uint32_t>(outX), (uint32_t)(py + 4), x.at<uint32_t>(outWidth), x.at<uint32_t>(outText));
    x.ret(drawLanguageFlag(x, list, (int32_t)row, px + 16, py + 4));
}
static SwapReg reg_DrawLanguageRow({"libtitle.DrawLanguageRow", MODULE, off(0x08f07b64), swap_DrawLanguageRow});

/* FUN_08f07acc(list): draw the language screen: the two background images, the cursor arrow once the list is up, the cursor sprite and the list */
static void swap_DrawLanguageSelect(SwapCtx &x) {
    State &s = state(x);
    uint32_t list = x.arg(0);
    if (s.arg0 < 5) {
        list = x.callLocal(fn::SlotDraw, list, 6);
        list = x.callLocal(fn::SlotDraw, list, 5);
        if (s.arg0 > 0) {
            list = x.callLocal(fn::SlotDraw, list, 13);
            list = x.callLocal(fn::DrawMenuArrow, list);
        }
        list = x.callLocal(fn::DrawLanguageCursor, list, menu(x, kTitleList).selected);
        list = x.callLocal(fn::DrawList, list, kTitleList);
    }
    x.ret(list);
}
static SwapReg reg_DrawLanguageSelect({"libtitle.DrawLanguageSelect", MODULE, off(0x08f07acc), swap_DrawLanguageSelect});

/* ---- the last two functions that the recorded sessions still ran as generated code ---- */

/* FUN_08f04b90(list, message, x, y): draw callback of the tab header of the panel set: the header sprite and its text, whose message number
 * depends on the selected tab of the panel set (0, 1 or 2). Returns the new list. */
static void swap_DrawTabHeader(SwapCtx &x) {
    const uint32_t list = x.arg(0), px = x.arg(2), py = x.arg(3);
    uint32_t message = x.arg(1) & 0xFFFF;
    switch (state(x).panelSet.selectedTab) {
    case 0: message += 0x10; break;
    case 1: message += 0x1A; break;
    case 2: message += 0x21; break;
    }
    const uint32_t withSprite = x.callLocal(fn::DrawSpritePlain, list, 1, 0x3D, px + 1, py - 1);
    ehsys::SetFontSize(x, 13, 13);
    x.callNid(nid::SetTextColor, 0xFF000000u);
    x.callNid(nid::SetUiFlag4, 1);
    x.ret(x.callLocal(fn::DrawMessageText, withSprite, px + 21, py + 1, 218, 0, message, 2));
}
static SwapReg reg_DrawTabHeader({"libtitle.DrawTabHeader", MODULE, off(0x08f04b90), swap_DrawTabHeader});

/* FUN_08f028e4: start a random duel from the title: seed and coin toss from the engine's random numbers, the two players' characters and
 * decks picked by shuffling two seven-entry tables of the module, then ask ehsys to prepare the duel and push scene 6. */
static void swap_StartRandomDuel(SwapCtx &x) {
    const uint32_t address = x.callNid(nid::GetSettingsBlock);
    DuelSetup &setup = x.at<DuelSetup>(address);
    setup.kind = 5;
    setup.ruleA = 1;
    setup.ruleB = 1;
    setup.unknown11E = 0;
    setup.unknown11F = 0;
    setup.seed = x.callNid(nid::RandomNumber);
    setup.coinToss = x.callNid(nid::RandomNumber) & 1;
    uint32_t character[7], deck[7];
    for (uint32_t i = 0; i < 7; i++) {
        character[i] = x.at<uint32_t>(x.addr(off(0x08f27450)) + 4 * i);
        deck[i] = x.at<uint32_t>(x.addr(off(0x08f2746c)) + 4 * i);
    }
    setup.firstPlayer = (x.callNid(nid::RandomNumber) & 1) == 0 ? 3 : 1;
    for (int32_t i = 0; i < 7; i++) {      /* shuffle: swap entry i with a random other entry */
        int32_t other = (int32_t)(x.callNid(nid::RandomNumber) % 6);
        if (i <= other) other++;
        if (other >= 7) other = 6;
        std::swap(character[i], character[other]);
        std::swap(deck[i], deck[other]);
    }
    for (uint32_t player = 0; player < 2; player++) {
        DuelSlot &slot = setup.slot[player];
        slot.character = (uint16_t)character[player];
        slot.isSpecial = 0;
        if (slot.character == 6) slot.isSpecial = 1;
        slot.deck = (uint16_t)deck[player];
    }
    x.callNid(nid::PrepareDuel, address);
    x.callLocal(fn::PushScene13, 6, 0);
}
static SwapReg reg_StartRandomDuel({"libtitle.StartRandomDuel", MODULE, off(0x08f028e4), swap_StartRandomDuel});   /* not verifiable: it draws random numbers and pushes a scene */

/* FUN_08f092f4: the main menu's name-entry stage (screen 8, arg0 2): in its first step it opens the keyboard for the player's name. */
static void swap_MainMenuNameStep(SwapCtx &x) {
    if (state(x).arg1 != 0) return;
    text_input_use_game_ui(true);   /* the keyboard the game opens next is the in-game name box (DrawMainMenu draws it) */
    x.callLocal(fn::StartNameEntry);
    text_input_use_game_ui(false);
}
static SwapReg reg_MainMenuNameStep({"libtitle.MainMenuNameStep", MODULE, off(0x08f092f4), swap_MainMenuNameStep});

/* FUN_08f09384: while the name is being typed (screen 8, arg0 2, arg1 3 or more) ask the keyboard (NameEntryStep 4) whether it finished:
 * step 4 goes back to the menu (arg0 1), the others just restart the stage. */
static void swap_MainMenuNameFinished(SwapCtx &x) {
    State &s = state(x);
    if (s.arg0 != 2 || s.arg1 < 3) return;
    std::wstring typed, description; bool caretLit; int maxLength;
    if (text_input_live(&typed, &caretLit, &description, &maxLength)) stepNameBox(x, description);
    if (x.callLocal(fn::NameEntryStep, 4) == 0) return;
    closeNameBox(x);
    if (s.arg1 == 4) { s.arg1 = 0; s.arg0 = 1; }
    else s.arg1 = 0;
}
static SwapReg reg_MainMenuNameFinished({"libtitle.MainMenuNameFinished", MODULE, off(0x08f09384), swap_MainMenuNameFinished});
