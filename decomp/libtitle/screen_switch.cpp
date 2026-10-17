/* libtitle swaps: The main title list, text alignment, the language-change steps, the screen switch and the boot entry. */
#include "internal.h"

/* ---- colour fade of a list entry, the main title list setup and its input ---- */

/* FUN_08f01214(fade): advance a colour fade by one step. Each channel is mixed as (from*(frames-step))/frames + (to*step)/frames.
 * Returns 1 while running, 0 when done (the colour is written every step). */
static void swap_StepColourFade(SwapCtx &x) {
    ColourFade &fade = x.at<ColourFade>(x.arg(0));
    if (fade.active != 1) { x.ret(fade.active); return; }
    const uint32_t frames = fade.frames;
    fade.step = (uint16_t)(fade.step + 1);
    if (frames == 0) {
        x.at<uint32_t>(fade.target) = fade.to;
        fade.active = 0;
        x.ret(0);
        return;
    }
    const int32_t current = fade.step, rest = (int32_t)((frames - fade.step) & 0xFFFF);
    uint32_t result = 0;
    for (int shift : {24, 16, 8, 0}) {
        const int32_t a = (fade.from >> shift) & 0xFF, b = (fade.to >> shift) & 0xFF;
        result |= (uint32_t)(((a * rest) / (int32_t)frames + (b * current) / (int32_t)frames) & 0xFF) << shift;
    }
    x.at<uint32_t>(fade.target) = result;
    if (!((int32_t)fade.step < (int32_t)frames)) { fade.frames = 0; fade.step = 0; fade.active = 0; }
    x.ret(fade.active);
}
static void regions_StepColourFade(SwapCtx &x, Regions &r) {
    touch(r, x.arg(0) + offsetof(ColourFade, frames), 6);
    const uint32_t target = x.at<ColourFade>(x.arg(0)).target;
    if (target >= 0x08800000u && target < 0x0A000000u) touch(r, target, 4);   /* the pointer is garbage until a fade is set up */
}
static SwapReg reg_StepColourFade({"libtitle.StepColourFade", MODULE, off(0x08f01214), swap_StepColourFade, false, regions_StepColourFade});

/* FUN_08efd20c(selected): build the main menu list (items: see MainMenuItem): item states depend on the collection and save flag 8, geometry on the language */
static void swap_BuildTitleList(SwapCtx &x) {
    const uint32_t selected = x.arg(0), p = kTitleList;
    state(x).arg1 = 0;
    state(x).transition = 0;
    initList(x, p);
    MenuList &m = menu(x, p);
    m.spriteCell = 1;
    m.itemCount = 9;
    m.unknown7 = 0;
    x.callLocal(fn::AnyCardOwned);
    if (x.c->r[2] == 0) m.itemState[kFreeDuel] = kItemDisabled;       /* no card owned yet */
    x.callLocal(fn::SaveFlag8);
    if (x.c->r[2] == 0) m.itemState[8] = kItemDisabled;               /* save flag 8 not set: a ninth item, past the rows the menu shows */
    if (!getenv("TF_TEST_NETWORK_ENABLED")) m.itemState[kNetworkMode] = kItemDisabled;                         /* not supported on PC yet: shown dark, like the locked parts */
    if (x.callNidKeepRegs(nid::GetLanguage) != 0) {
        m.x = 140; m.y = 50; m.width = 200; m.rowHeight = 26;
    } else {
        m.itemCount = 8;
        m.x = 160; m.width = 160; m.y = 61; m.rowHeight = 26;
    }
    if (gfx_modern_menus()) { m.x -= 14; m.width += 28; }   /* PC style: room for the icon at the left of each row */
    m.itemCount--;
    x.callLocal(fn::ResetList, p, 4, 1);
    m.drawCallback = x.addr(fn::DrawListLabel);   /* draw callback: DrawListLabel */
    m.selected = (uint8_t)selected;
    x.callLocal(fn::ListUpdate, p, 1);
    x.callLocal(fn::SlotLoad, 13, 2, 0, 16);
}
static SwapReg reg_BuildTitleList({"libtitle.BuildTitleList", MODULE, off(0x08efd20c), swap_BuildTitleList});

/* FUN_08f03f60: the title menu's input step. Runs the list controller (FUN_08f019ac) and acts on what it reports (-3..3):
 * confirm -> fade out to a screen; the table at 0x08f23d20 maps the result to one of six reactions */
static void swap_TitleMenuInput(SwapCtx &x) {
    State &s = state(x);
    x.callNidKeepRegs(nid::GetLanguage);
    const int16_t step = s.arg1;
    if (step == 3) return;
    if (step == 2) {
        const uint32_t v = state(x).soundHandle;
        if (v) { x.callNid(nid::SetLayerFade, v, 1000); state(x).soundHandle = 0; }
        return;
    }
    if (step != 0 && step != 1) return;
    int32_t result = (int32_t)x.callLocal(fn::ListUpdate, kTitleList, 1);
    /* translate the chosen item to the number the game's own reactions below expect (0 = New Game, 1 = Continue) */
    if (result == kContinue) result = kOriginalContinue;
    else if (result == kNewGame) result = kOriginalNewGame;
    else if (result == kExitToDesktop) {
        if (display_confirm(L"Are you sure you want to quit the game?")) display_quit();
        /* answered No: the list had already closed after the confirm; open it again on the same item, as if nothing was pressed */
        MenuList &m = menu(x, kTitleList);
        m.state = kListIdle;
        m.chosen = 0;
        m.blink &= 0x3F;
        return;
    }
    if ((uint32_t)(result + 3) >= 7) return;
    switch ((result + 3)) {
    case 1:
        x.callLocal(fn::StartColourFade, kColourFade, kMenuColours, 0xFFFFFFFFu, 15);
        x.callLocal(fn::ActivateColourFade, kColourFade);
        s.arg0 = 2;
        s.transition = 0;
        x.callLocal(fn::SlotSetRange, 1, 2, 0, 119);
        break;
    case 3: x.callLocal(fn::RequestScreen16, 1, 8, 0); s.arg1 = 2; x.callNidKeepRegs(nid::ClearSoundScene); break;
    case 4: x.callLocal(fn::RequestScreen16, 1, 4095, 5); s.arg1 = 2; break;
    case 5: x.callLocal(fn::RequestScreen16, 1, 4095, 9); s.arg1++; break;
    case 6: x.callLocal(fn::RequestScreen16, 1, 6, 0); s.arg1++; break;
    }
}
static SwapReg reg_TitleMenuInput({"libtitle.TitleMenuInput", MODULE, off(0x08f03f60), swap_TitleMenuInput});

/* ---- text alignment and the language-change steps ---- */

/* FUN_08f03840(list, x, y, w, e, text, style): draw a text. style 0/1: shape B; 2/3: shape A then the rectangle; 4: the rectangle only.
 * 1 and 3 centre the text in a width of w (1 always, 3 only if the text is narrower). Returns the new list value. */
static void swap_DrawAlignedText(SwapCtx &x) {
    uint32_t list = x.arg(0), px = x.arg(1);
    const uint32_t py = x.arg(2), w = x.arg(3), e = x.arg(4), text = x.arg(5), style = x.arg(6);
    uint32_t drawW = w;
    auto centre = [&](int32_t textW) { const int32_t d = (int32_t)w - textW; px += (uint32_t)(d >= 0 ? d >> 1 : (d + 1) >> 1); };
    if (style == 1) { const int32_t tw = (int32_t)x.callNid(nid::MeasureTextWidth, text) >> 6; drawW = (uint32_t)tw; centre(tw); }
    else if (style == 3) { const int32_t tw = (int32_t)x.callNid(nid::MeasureTextWidth, text) >> 6; if (tw < (int32_t)w) { drawW = (uint32_t)tw; centre(tw); } }
    switch (style) {
    case 0: case 1: list = x.callLocal(fn::DrawTextLine, list, px, py, drawW, text); break;
    case 2: case 3:
        list = x.callLocal(fn::DrawBoxedText, list, px, py, drawW, text);
        list = x.callLocal(fn::DrawRect, list, px, py, drawW, e, text);
        break;
    case 4: list = x.callLocal(fn::DrawRect, list, px, py, drawW, e, text); break;
    }
    x.ret(list);
}
static SwapReg reg_DrawAlignedText({"libtitle.DrawAlignedText", MODULE, off(0x08f03840), swap_DrawAlignedText});

/* FUN_08f080f0: the language-change step of the title (arg0): 0 wait for loads; 1 wait for 6FFA2A28; 2 act on 778817A2 (3 -> back to
 * the menu, 2 -> remember the language and continue); 3 when 4E3BEA66 is true, leave to screen 0 */
static void swap_LanguageStep(SwapCtx &x) {
    State &s = state(x);
    switch (s.arg0) {
    case 0:
        x.callNid(nid::SetScreenOverlay, 1);
        x.callLocal(fn::WaitLoadsAndPlay, 1);
        s.arg0++;
        x.callNid(nid::SetScreenOverlay, 0);
        break;
    case 1:
        if (x.callNid(nid::StartDataLoad, 3, state(x).heap) != 0) s.arg0++;
        break;
    case 2: {
        const uint32_t r = x.callNidKeepRegs(nid::PollDataLoad);
        if (r == 3) { s.arg0 = 0; x.at<int16_t>(kReadyAddr) = 2; }
        else if (r == 2) {
            const uint32_t lang = x.callNidKeepRegs(nid::GetLanguage);
            x.at<uint32_t>(kLogoIndexAddr) = lang;
            x.callNid(nid::SetTutorialAndMessageSpeed, lang);
            x.callNidKeepRegs(nid::StartLanguageDataLoad);
            s.arg0++;
        }
        break;
    }
    case 3:
        if (x.callNidKeepRegs(nid::IsLoadDone) != 0) { x.at<int16_t>(kReadyAddr) = 0; s.screen = 0; s.arg0 = 0; }
        break;
    }
}
static SwapReg reg_LanguageStep({"libtitle.LanguageStep", MODULE, off(0x08f080f0), swap_LanguageStep});

/* FUN_08f04528: the title's language sub-flow, by arg1 (0..8, jump table at 0x08f23d54) */
static void swap_LanguageFlow(SwapCtx &x) {
    State &s = state(x);
    const uint32_t i = (uint16_t)s.arg1;
    if (i < 9) {
        switch (i) {
        case 0: case 1: case 2:
            x.at<uint32_t>(kLogoIndexAddr) = x.callNidKeepRegs(nid::GetLanguage);
            if (x.callNid(nid::StartDataLoad, 4, state(x).heap) != 0) s.arg1 = 3;
            break;
        case 3: {
            const uint32_t r = x.callNidKeepRegs(nid::PollDataLoad);
            if (r == 3) s.arg1 = 7;
            else if (r == 2) { x.callLocal(fn::RewriteSettings); s.arg1 = 4; }
            break;
        }
        case 4: {
            state(x).previousLanguage = (uint8_t)x.callNidKeepRegs(nid::GetLanguage);
            if (x.at<uint32_t>(kLogoIndexAddr) == state(x).previousLanguage) {
                x.callNidKeepRegs(nid::StartLanguageDataLoad);
                x.callNid(nid::SetScreenOverlay, 1);
                s.arg1 = 6;
            } else {
                x.callLocal(fn::LoadMenuArchives, 1);
                x.callNid(nid::SetScreenOverlay, 1);
                s.arg1 = 5;
            }
            break;
        }
        case 5:
            if (s.arg0 == 6) { x.callNidKeepRegs(nid::StartLanguageDataLoad); s.arg1 = 6; }
            break;
        case 6:
            if (x.callNidKeepRegs(nid::IsLoadDone) != 0) { x.callNid(nid::SetScreenOverlay, 0); x.callLocal(fn::SwitchScreen, 9, 0); }
            break;
        case 7: x.callLocal(fn::SwitchScreen, 5, 0); break;
        }
    }
    setStepResult(x);
}
static SwapReg reg_LanguageFlow({"libtitle.LanguageFlow", MODULE, off(0x08f04528), swap_LanguageFlow});

/* ---- the screen switch, the boot entry and the confirm/back step ---- */

/* FUN_08eff100(code, arg): switch to screen `code` (0..40): the table at 0x08f23a80 gives the routine that starts it. Returns the code, or 41
 * for the screens that handle themselves (it tells the caller the flow is over). Other codes are returned unchanged. */
static void swap_SwitchScreen(SwapCtx &x) {
    int16_t code = (int16_t)x.arg(0);
    /* Network Mode (screen 14) is not supported on PC yet: its first step calls sceNetInit, fails and leaves a black screen. Choosing it
     * plays the cancel sound and returns to the main menu list (screen 9, arg 9, as the options screens do). */
    if (code == kScreenNetwork) {
        x.callNid(nid::PlaySound, 25108);
        x.ret(x.callLocal(fn::SwitchScreen, kScreenLists, 9));
        return;
    }
    const uint32_t a0 = x.arg(0), a1 = x.arg(1), a2 = x.arg(2), a3 = x.arg(3);
    if ((uint32_t)(int32_t)code >= kScreenEnd) { x.ret((uint32_t)(int32_t)code); return; }
    auto call = [&](uint32_t function) { x.callLocal(function, a0, a1, a2, a3); };
    bool returnsCode = true;
    switch (code) {
    case 2: x.callLocal(fn::StartScreen, a0, 0); break;
    case 3: call(fn::StartScreenFaded); break;
    case 4: x.callLocal(fn::EnterLogoScreen, a0, 0); break;
    case kScreenTitle: call(fn::EnterTitleMenu); break;
    case 6: call(fn::Original_08f0bf24); break;
    case kScreenMainMenu: call(fn::EnterMainMenu); break;
    case kScreenLists: x.callLocal(fn::EnterListScreen, a0, a1, 0); break;
    case kScreenCardList: call(fn::EnterCardListScreen); break;
    case 13: call(fn::Original_08f04704); break;
    case kScreenNetwork: call(fn::Original_08f0cc90); break;
    case kScreenOptions: call(fn::EnterOptionsScreen); break;
    case kScreenLanguage: call(fn::EnterLanguageSelect); break;
    case 20: x.callLocal(fn::PushScene13, 4, a1 & 0xFFFF); returnsCode = false; break;
    case 21: call(fn::Original_08efe0f4); break;
    case 22: call(fn::StartLoadB); break;
    case 23: call(fn::Original_08efe4c8); break;
    case 24: call(fn::Original_08efe940); break;
    case 25: call(fn::Original_08efe630); break;
    case 26: call(fn::StartLoadA); break;
    case 27: case 28: x.callLocal(fn::StoreProgress, (uint32_t)((int32_t)code - 27), 0); returnsCode = false; break;
    case 29: case 30: call(fn::StartLoadB); break;
    case 31: x.callNidKeepRegs(nid::ClearSoundScene); x.callLocal(fn::PushScene13, 2, 0); returnsCode = false; break;
    case 32: call(fn::StartRandomDuel); returnsCode = false; break;
    case 33: call(fn::Original_08f02b34); returnsCode = false; break;
    case 34: x.callNidKeepRegs(nid::ReleaseScreenResources); x.callLocal(fn::UnloadHelperModule); x.callLocal(fn::StartFreeDuel); returnsCode = false; break;
    case 35: call(fn::Original_08f0e9c4); returnsCode = false; break;
    case 36: call(fn::Original_08eff364); break;
    case 37: x.callNid(nid::SetNetworkScene, 1); x.callLocal(fn::PushScene13, 14, 0); returnsCode = false; break;
    case 38: call(fn::Original_08f02e50); returnsCode = false; break;
    case 39: call(fn::Original_08efeb48); break;
    case 40: call(fn::Original_08f07fe8); break;
    case 0: case 1: case 7: case 10: case 11: case 15: case 16: case 18: break;
    }
    x.ret(returnsCode ? (uint32_t)(int32_t)code : 41);
}
static SwapReg reg_SwitchScreen({"libtitle.SwitchScreen", MODULE, off(0x08eff100), swap_SwitchScreen});

/* FUN_08efca34: the module's main loop. Initialise the flow, read how the game was started (ED2013FB), pick the first screen from that,
 * register two callbacks (FUN_08efcd18 / 08efcd20) and run FUN_08eff908 every frame until it returns 41. */
static void swap_Main(SwapCtx &x) {
    State &s = state(x);
    x.callLocal(fn::InitFlow);
    x.callNid(nid::GetClock, kLaunchInfo);
    const LaunchInfo &launch = x.at<LaunchInfo>(kLaunchInfo);
    const uint16_t mode = launch.mode, sub = launch.sub, param = launch.param;
    auto first = [&](uint32_t screen, uint32_t arg) { x.callLocal(fn::StartLogoWith, screen, arg); };
    if (mode == 3) {
        if (sub == 6 && param != 0) first(5, 0);
    } else if (mode == 2) {
        if (sub >= 15) first(5, 0);
        else switch (sub) {
        case 2: if (param != 0) first(3, 0); else first(5, 0); break;
        case 4: if (param == 2) first(26, 0); else first(9, 5); break;
        case 6: {
            const DuelSetup &setup = x.at<DuelSetup>(x.callNid(nid::GetSettingsBlock));
            if (setup.kind == 5) first(5, 0);
            else if (setup.kind == 2) first(24, (uint32_t)(int32_t)(int16_t)(setup.stage + 7));
            else first(9, 0);
            break;
        }
        case 14: state(x).savedSelection = 5; first(9, 6); break;
        case 0: case 1: case 3: case 5: case 7: case 8: case 9: case 10: case 11: case 12: case 13: first(5, 0); break;
        }
    } else if (mode == 1 || mode == 0) {
        s.screen = 40;
    }
    x.callNid(nid::SetSceneCallbacks, 13, x.addr(fn::SceneEnterCallback), x.addr(fn::SceneLeaveCallback));
    while ((uint32_t)x.callLocal(fn::Tick) != 41) x.callNidKeepRegs(nid::WaitFrame);
}
static SwapReg reg_Main({"libtitle.Main", MODULE, off(0x08efca34), swap_Main});

/* FUN_08efcd58(screen, sel, quiet): enter one of the title list screens (sel 14 = main list, 5/6 = the two sub lists, otherwise a plain
 * fade). `quiet` skips the fades and the background loads. */
static void swap_EnterListScreen(SwapCtx &x) {
    const uint32_t screen = x.arg(0), quiet = x.arg(2);
    uint32_t sel = x.arg(1);
    State &s = state(x);
    if (quiet == 0 && x.callNidKeepRegs(nid::UpdateSaveChecksum) != 0) {
        state(x).returnArg = (uint16_t)sel;   /* remember where to come back to */
        sel = 14;
        s.pendingArg0 = 14;
    }
    const int32_t v = (int16_t)sel;
    auto resetCommon = [&] {
        s.arg1 = 0;
        s.arg2 = 0;
        s.transition = 0;
        state(x).menuColour[0] = 0xFFFFFFFFu;
        state(x).menuColour[1] = 0xFFFFFFFFu;
    };
    /* the two sub lists differ only in which builder runs and which list gets the saved selection */
    auto subList = [&](uint32_t builder) {
        x.callLocal(fn::BuildTitleList, (uint32_t)(v - 3));
        x.callLocal(builder);
        menu(x, kTitleList2).selected = (uint8_t)state(x).savedSelection;
        state(x).savedSelection = 0;
        if (quiet) { s.screen = (int16_t)screen; s.arg0 = (int16_t)sel; }
        else { x.callLocal(fn::FadeToBlack16, 0); s.screen = (int16_t)screen; s.pendingArg0 = (uint16_t)sel; s.arg0 = 15; }
        resetCommon();
    };
    if (v == 6) subList(fn::BuildSubList6);
    else if (v == 5) subList(fn::BuildSubList5);
    else if (v == 14) {
        x.callLocal(fn::FadeToBlack16, 0);
        x.callLocal(fn::BuildTitleList, (uint32_t)(int32_t)state(x).returnArg - 3);
        uint8_t &b = state(x).returnFlags;
        b = (b & 0xFE) | 1;
        s.arg0 = 13;
        s.screen = (int16_t)screen;
        s.pendingArg0 = (uint16_t)sel;
        resetCommon();
    } else {
        if (quiet == 0) { x.callLocal(fn::FadeToBlack16, 0); s.screen = (int16_t)screen; s.arg0 = 0; s.pendingArg0 = kIncrement; }
        else { s.arg0 = 1; s.pendingArg0 = kNone; }
        resetCommon();
        x.callLocal(fn::BuildTitleList, sel != 0 ? (uint32_t)(v - 3) : 0);
    }
    if (quiet == 0) {
        x.callLocal(fn::SlotLoad, 5, 2, 0, 480);
        x.callLocal(fn::SlotLoad, 6, 2, 0, 480);
        x.callNid(nid::StopSoundChannel, 5);
    }
}
static SwapReg reg_EnterListScreen({"libtitle.EnterListScreen", MODULE, off(0x08efcd58), swap_EnterListScreen});

/* FUN_08eff908: the per-frame tick of the flow. Reads the pad, publishes the screen code (+0x1C) and arg0 (+0x1E) for the draw
 * dispatcher, runs UpdateFlow (which may switch screens) and then the logic step of the running screen (jump table at 0x08f23b3c).
 * Counts frames in +0 (wrapping at 0x10000). Returns the value UpdateFlow gave, or a step's result for screens that report one. */
static void swap_Tick(SwapCtx &x) {
    State &s = state(x);
    state(x).padState = x.callNidKeepRegs(nid::GetInput);
    state(x).publishedScreen = s.screen;
    state(x).publishedArg0 = s.arg0;
    uint32_t result = x.callLocal(fn::UpdateFlow);
    const uint32_t screen = (uint16_t)s.screen;
    if (screen < 41) {
        auto step = [&](uint32_t function) { x.callLocal(function); };
        auto stepResult = [&](uint32_t function, uint32_t a0) { result = x.callLocal(function, a0); };
        switch (screen) {
        case 0: step(fn::LoadingStep); break;
        case 2: step(fn::IntroStep); break;
        case 3: step(fn::LogoSequence); break;
        case 4: if (s.arg0 != 0) x.callLocal(fn::SwitchScreen, 5, 0); break;
        case kScreenTitle: state(x).skipRequested = 0; step(fn::TitleScreen); break;
        case 6: step(fn::Original_08f0bfd0); break;
        case kScreenMainMenu: step(fn::MainMenuStep); break;
        case kScreenLists: step(fn::OpeningStep); break;
        case kScreenCardList: step(fn::OptionsScreenStep); break;
        case 13: step(fn::Original_08f0478c); break;
        case kScreenNetwork: step(fn::NetworkSceneStep); break;
        case kScreenOptions: step(fn::OptionsStep); break;
        case kScreenLanguage: step(fn::LanguageSelectStep); break;
        case 21: step(fn::Original_08efe190); break;
        case 22: step(fn::CharListStep); break;
        case 23: step(fn::Original_08efe568); break;
        case 24: stepResult(fn::Original_08efea04, (uint32_t)(int32_t)s.screen); break;
        case 25: step(fn::Original_08efe6d0); break;
        case 26: result = x.callLocal(fn::LimitListScreen); break;
        case 29: case 30: stepResult(fn::Original_08efe3a0, (uint32_t)(int32_t)s.screen); break;
        case 39: step(fn::Original_08efebf0); break;
        case 36: result = x.callLocal(fn::StartupSequenceStep); break;
        case 40: step(fn::TitleStep); break;
        }
    }
    state(x).publishedScreen = s.screen;
    const int32_t frames = (int32_t)x.at<uint32_t>(kStateAddr) + 1;
    x.at<uint32_t>(kStateAddr) = frames < 0x10000 ? (uint32_t)frames : 0;
    x.ret(result);
}
static SwapReg reg_Tick({"libtitle.Tick", MODULE, off(0x08eff908), swap_Tick});

/* FUN_08effd94: the logo sequence (screen code 2). Any of the "skip" buttons sets the skip flag (+0x3C); arg0 (0..8) is the step:
 * wait / fade out / fade in with a frame timer in +4. The table at 0x08f23be0 gives each step's behaviour. */
static void swap_LogoSequence(SwapCtx &x) {
    State &s = state(x);
    const uint32_t pad = x.callNidKeepRegs(nid::GetInput);
    const uint32_t pressed = x.at<ehsys::PadState>(pad).pressed;
    auto skip = [&]() -> uint32_t & { return state(x).skipRequested; };
    auto timer = [&]() -> uint32_t & { return state(x).transition; };
    auto checkSkip = [&] {
        if (pressed & 8) { skip() = 1; return; }
        const uint32_t mask = x.callNid(nid::GetButtonMask, 0) & 0xFFFF;
        if (pressed & mask) skip() = 1;
    };
    if (skip() == 0 && s.arg0 >= 3) checkSkip();
    if (state(x).skipLocked != 0) checkSkip();
    auto request = [&](uint32_t a, uint32_t b, uint32_t next, uint32_t pending) { x.callLocal(fn::RequestScreen, a, b, next, pending); };
    /* skipped while showing: stop the music, fade out quickly and go to `target` */
    auto skipShow = [&](int16_t target) {
        x.callNidKeepRegs(nid::ResetCursorLimit);
        s.phase = 0;
        timer() = 0;
        request(1, 7, 5, 0);
        timer() = 0;
        s.arg0 = target;
    };
    const uint32_t step = (uint16_t)s.arg0;
    if (step < 9) {
        switch (step) {
        case 0:                                   /* wait for a button */
            timer() = 0;
            if (skip() != 0) s.arg0 = (int16_t)(step + 1);
            break;
        case 1:                                   /* show for 61 frames, or until skipped */
            if (skip() != 0) { skipShow(2); break; }
            if ((int32_t)timer() < 61) timer() = timer() + 1;
            else { s.arg0 = (int16_t)(step + 1); timer() = 0; }
            break;
        case 2:                                   /* fade out, by language */
            if (s.phase != 0) break;
            if (skip() != 0) { request(1, 30, 5, 0); timer() = 0; break; }
            if (x.callNidKeepRegs(nid::GetLanguage) != 0) request(1, 30, 4, 0);
            else request(3, 30, 4095, 4094);
            timer() = 0;
            break;
        case 3:                                   /* fade in; remember that the sequence has begun */
            state(x).skipLocked = 1;
            if (s.phase == 0) { request(2, 30, 4095, 4094); timer() = 0; }
            if (skip() != 0) { s.arg0++; timer() = 0; }
            break;
        case 4:                                   /* like the second step, going to step 5 */
            if (skip() != 0) { skipShow(5); break; }
            if ((int32_t)timer() < 61) timer() = timer() + 1;
            else { request(1, 30, 4, 0); s.arg0 = 5; timer() = 0; }
            break;
        case 5: case 6: case 8: skip() = 0; break;
        case 7:
            if ((int32_t)timer() < 90) skip() = 0;
            if ((int32_t)timer() >= 301 || skip() != 0) { request(1, 30, 3, 0); timer() = 0; s.arg0 = 8; }
            else timer() = timer() + 1;
            break;
        }
    }
    setStepResult(x);
}
static SwapReg reg_LogoSequence({"libtitle.LogoSequence", MODULE, off(0x08effd94), swap_LogoSequence});

/* FUN_08f03b90: the title demo/attract screen (screen code 5). arg0 (0..12) is the step; the table at 0x08f23cb8 maps it to:
 * start (pick a random jingle, fall into the fade), wait for a button or 990 frames, then the list and language steps. */
static void swap_TitleScreen(SwapCtx &x) {
    State &s = state(x);
    const uint32_t pad = x.callNidKeepRegs(nid::GetInput);
    const uint32_t pressed = x.at<ehsys::PadState>(pad).pressed;
    if (slotQuery(x, 0) == 89) x.callLocal(fn::SlotSetRange, 0, 2, 90, 129);
    auto timer = [&]() -> uint32_t & { return state(x).transition; };
    const uint32_t step = (uint16_t)s.arg0;
    if (step < 13) {
        bool fade = false;   // falls from the start step into the fade step
        if (step == 1) {   /* the step that picks the title sound and starts the fade */
            if ((int32_t)x.callNidKeepRegs(nid::GetLanguage) >= 0) {
                const int32_t r = (int32_t)x.callNidKeepRegs(nid::RandomNumber);
                const uint32_t sound = (uint32_t)(r % 43) + 25025;
                state(x).soundHandle = sound;
                x.callNid(nid::PlaySound, sound);
            }
            s.arg0++;
            fade = true;
        }
        if (fade || step == 2) {
            if (x.callLocal(fn::StepColourFade, kColourFade) != 0) { timer() = 0; }
            else {
                bool press = (pressed & 8) != 0;
                if (!press) { const uint32_t mask = x.callNid(nid::GetButtonMask, 0) & 0xFFFF; press = (pressed & mask) != 0; }
                if (press) {
                    x.callNid(nid::PlaySound, 25107);
                    x.callLocal(fn::SlotLoad, 1, 2, 120, 164);
                    x.callLocal(fn::StartColourFade, kColourFade, kMenuColours, 0xFF404040u, 15);
                    x.callLocal(fn::ActivateColourFade, kColourFade);
                    const int16_t cur = s.arg0;
                    s.pendingArg0 = 4;
                    s.arg0 = (int16_t)(cur + 1);
                } else if ((int32_t)timer() >= 990) {
                    if ((x.at<uint8_t>(kFlagsAddr) >> 3) & 1) x.callLocal(fn::RequestScreen16, 1, 32, 0);
                    else x.callLocal(fn::RequestScreen16, 1, 3, 0);
                    s.arg0 = 0;
                    timer() = 0;
                    x.callNid(nid::SetAttractTimer, 600);
                }
            }
        } else switch (step) {
        case 3:
            if (x.callLocal(fn::StepColourFade, kColourFade) == 0) { x.callLocal(fn::InitTitleList); s.arg0 = (int16_t)s.pendingArg0; }
            break;
        case 4: x.callLocal(fn::TitleMenuInput); break;
        case 5: case 6: x.callLocal(fn::LanguageFlow); break;
        case 7: x.callLocal(fn::NameEntryScreenStep); break;
        case 8: x.callLocal(fn::NameEntryLoadStep); break;
        case 11:
            if (s.arg1 == 0) { x.callLocal(fn::RequestScreen16, 1, 31, 0); s.arg1++; }
            break;
        }
    }
    setStepResult(x);
}
static SwapReg reg_TitleScreen({"libtitle.TitleScreen", MODULE, off(0x08f03b90), swap_TitleScreen});

/* FUN_08efd334: input step of the opening menu (window block at 0x09B29B18): while the slot-13 resource is not ready (progress < 3), run the list
 * controller (FUN_08f019ac) and turn its answer (-3..8, table at 0x08f23a0c) into the next fade/screen request. */
static void swap_OpeningMenuInput(SwapCtx &x) {
    State &s = state(x);
    const uint32_t p = kTitleList;
    MenuList &m = menu(x, p);
    m.openFadeColour = 0xFFFFFFFFu;
    m.normalColour = 0xFFFFFFFFu;
    m.selectedColour = 0xFF000000u;
    const int32_t progress = (int32_t)x.callLocal(fn::Slot13Progress);
    if (progress >= 3) return;
    if (s.arg1 != 0) return;
    const uint32_t result = x.callLocal(fn::ListUpdate, p, progress != 0 ? 1 : 0);
    if (result + 3 >= 12) return;
    auto next = [&](uint32_t screen, uint32_t pending) { x.callLocal(fn::RequestScreen16, 1, screen, pending); s.arg1++; };
    switch ((int32_t)result) {   /* -2 = cancelled, 0.. = the item chosen (MainMenuItem) */
    case -2: {
        m.blink = 0x80;   /* two bit fields: low six bits 0, top two bits 10 */
        x.callNid(nid::SetAttractTimer, 600);
        const int16_t cur = s.arg1;
        x.callLocal(fn::RequestScreen16, 1, 5, 0);
        s.transition = 0;
        s.arg1 = (int16_t)(cur + 1);
        break;
    }
    case kStoryMode: x.callLocal(fn::BuildSubList3); break;
    case kFreeDuel: next(12, 0); break;
    case kDeckMenu: x.callLocal(fn::BuildSubList5); break;
    case kDatabase: x.callLocal(fn::BuildSubList6); break;
    case kNetworkMode: next(14, 0); break;
    case kOptions: graphics.active = false; next(17, 0); break;
    case kGraphics: graphics.active = true; graphics.cursor = 0; graphics.shown = false; next(17, 0); break;
    case kLanguageSettings: next(19, 0); break;
    case 8: x.callNid(nid::SetAttractTimer, 600); next(13, 0); break;   /* the ninth, hidden item */
    }
}
static SwapReg reg_OpeningMenuInput({"libtitle.OpeningMenuInput", MODULE, off(0x08efd334), swap_OpeningMenuInput});

