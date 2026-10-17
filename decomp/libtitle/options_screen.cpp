/* libtitle swaps: The Options screen (Main Menu > Options) and the Graphics screen. */
#include "internal.h"

/* ---- the Options screen (Main Menu > Options) ---- */

/* The Options screen keeps its state in a 48-byte block taken from the heap (pointer at 0x09B2CD78): the settings twice (being edited, and as
 * they were when the screen was entered: Cancel puts them back) and what the drawing needs. Rows: 0 Tutorial, 1 Message Speed, 2-4 the volumes,
 * 5 Card Animation, 6 Duel Effects, 7 Activation Confirm, 8 Auto Draw. */
/* The settings of the Options screen: one byte of switches and three volumes (what the engine keeps; ReadSettings / WriteSettings move them). */
struct OptionSettings {
    uint8_t tutorial : 1;          /* row 0: 1 = ON */
    uint8_t messageSpeed : 2;      /* row 1: 0..2 (slow .. very fast) */
    uint8_t cardAnimation : 2;     /* row 5: 0..3 */
    uint8_t duelEffects : 1;       /* row 6 */
    uint8_t autoDraw : 1;          /* row 8 */
    uint8_t activationConfirm : 1; /* row 7 */
    uint8_t voiceVolume;           /* row 2, 0..15 (only listed for the first language) */
    uint8_t musicVolume;           /* row 3 (B.G.M.) */
    uint8_t effectsVolume;         /* row 4 (S.E.) */
};
static_assert(sizeof(OptionSettings) == 4, "OptionSettings layout");

struct OptionsScreen {
    int32_t mode;               /* 0 = from the main menu, 1 = only the last two groups (from a duel) */
    int32_t cursor;             /* selected row 0..8 */
    OptionSettings current;     /* +8 the settings being edited */
    OptionSettings saved;       /* +12 the settings when the screen was entered (Cancel puts them back) */
    uint8_t shown;              /* +16 the highlight bar is drawn this frame */
    uint8_t unknown17[3];
    uint32_t heap;              /* +20 */
    uint32_t archive;           /* +24 the screen's texture archive */
    uint32_t panelTexture;      /* +28 */
    uint32_t stripTexture;      /* +32 */
    uint32_t blink;             /* +36 counts frames, 24 bits */
    uint32_t colourNormal;      /* +40 */
    uint32_t colourSelected;    /* +44 */
};
static_assert(sizeof(OptionsScreen) == 48 && offsetof(OptionsScreen, heap) == 20 && offsetof(OptionsScreen, blink) == 36, "OptionsScreen layout");
constexpr uint32_t kOptionsScreenPtr = 0x09B2CD78;
constexpr uint32_t kOptionsSprites = 0x08f27588;     /* 27 rectangles u, v, w, h (4 int16 each) of the screen's sprite sheets */
constexpr uint32_t kOptionsRowCounts = 0x08f27674;   /* int16 per group: rows in the group */
constexpr uint32_t kOptionsRowIcons = 0x08f27670;    /* byte per group: row of the strip sprites */
constexpr uint32_t kOptionsGroupIcons = 0x08f27660;  /* word per group: icon cell of the group's frame */
constexpr uint32_t kPadLeft = 0x80, kPadUp = 0x10, kPadRight = 0x20, kPadDown = 0x40;

static OptionsScreen &options(SwapCtx &x) { return x.at<OptionsScreen>(x.at<uint32_t>(kOptionsScreenPtr)); }

/* FUN_08f074a0(list, texture): the drawing state for the screen's sprites (no clear, no filter, blending on), then bind `texture` */
static uint32_t setupSprites(SwapCtx &x, uint32_t list, uint32_t texture) {
    uint32_t cur = ehsys::SetClearColor(x, list, 0);
    cur = ehsys::SetClearStencil(x, cur, 0);
    cur = ehsys::SetClearDepth(x, cur, 0);
    cur = ehsys::SetTextureFilter(x, cur, 0, 0);
    cur = ehsys::EnableState(x, cur, 9);
    cur = ehsys::DisableState(x, cur, 1);
    cur = ehsys::DisableState(x, cur, 10);
    cur = ehsys::EnableState(x, cur, 4);
    cur = ehsys::SetBlend(x, cur, 0, 2, 3, 0, 0);
    cur = ehsys::SetBlendColor(x, cur, 0xFFFFFFFFu);
    return ehsys::BindTexture(x, cur, texture);
}
static void swap_SetupSprites(SwapCtx &x) { x.ret(setupSprites(x, x.arg(0), x.arg(1))); }
static SwapReg reg_SetupSprites({"libtitle.SetupSprites", MODULE, off(0x08f074a0), swap_SetupSprites});

/* FUN_08f07554 / 08f07684(list, x, y, width, text, centre): draw a message of option_msg_?.bin (one file per language, 0x40 bytes apart at
 * 0x08f276ac); with `centre` a text narrower than `width` is centred in it. Returns the new list. */
static uint32_t optionText(SwapCtx &x, uint32_t list, int32_t px, int32_t py, int32_t width, uint32_t index, bool centre) {
    const OptionsScreen &o = options(x);
    const uint32_t file = x.addr(off(0x08f276ac)) + ehsys::GetLanguage(x) * 0x40;
    const uint32_t text = ehsys::GetText(x, ehsys::FindTexture(x, o.archive, file, 0), index);
    if (centre) {
        const int32_t textWidth = ehsys::TextWidth(x, text) >> 6;
        if (textWidth < width) {
            int32_t spare = width - textWidth;
            if (spare < 0) spare += 1;
            px += spare >> 1;
            width = textWidth;
        }
    }
    ehsys::BeginText(x, list);
    ehsys::DrawTextAligned(x, px << 6, py << 6, (width + 2) * 0x40, text);
    return ehsys::EndText(x);
}
static void swap_OptionTextCentred(SwapCtx &x) { x.ret(optionText(x, x.arg(0), (int32_t)x.arg(1), (int32_t)x.arg(2), (int32_t)x.arg(3), x.arg(4), x.arg(5) != 0)); }
static SwapReg reg_OptionTextCentred({"libtitle.OptionTextCentred", MODULE, off(0x08f07554), swap_OptionTextCentred});
static void swap_OptionText(SwapCtx &x) { x.ret(optionText(x, x.arg(0), (int32_t)x.arg(1), (int32_t)x.arg(2), (int32_t)x.arg(3), x.arg(4), false)); }
static SwapReg reg_OptionText({"libtitle.OptionText", MODULE, off(0x08f07684), swap_OptionText});

struct SpriteRect { int16_t u, v, w, h; };

/* FUN_08f07074(list, options, value, x, y): the volume bar, 15 ticks 7 pixels apart, the first `value` of them lit */
static uint32_t drawVolumeBar(SwapCtx &x, uint32_t list, const OptionsScreen &o, uint32_t value, int16_t px, int16_t py) {
    const SpriteRect *rect = (const SpriteRect *)x.mem(x.addr(off(0x08f2767c)));   /* [0] a lit tick, [1] an unlit one */
    for (int16_t i = 0; i < 15; i++) {
        const SpriteRect &tick = (i < (int32_t)(value & 0xFF)) ? rect[0] : rect[1];
        list = setupSprites(x, list, o.stripTexture);
        list = ehsys::SetSpriteSize(x, list, tick.w, tick.h, rect[0].w, rect[0].h);
        list = ehsys::DrawSprite(x, list, px, py, 0, tick.u, tick.v, 0, 0);
        px = (int16_t)(px + 7);
    }
    return list;
}

/* FUN_08f071c4(list, options, x, y, value, last): the two arrows beside a choice, 0x44 pixels apart; the left one only when value is not the
 * first, the right one only when it is not `last`. They blink with the screen's counter (visible when bit 3 of it is set). */
static uint32_t drawArrows(SwapCtx &x, uint32_t list, const OptionsScreen &o, int16_t px, int16_t py, char value, char last, int16_t spacing = 0x44) {
    const SpriteRect *rect = (const SpriteRect *)x.mem(x.addr(off(0x08f2768c)));   /* [0] left arrow, [1] right arrow */
    for (int16_t i = 0; i < 2; i++) {
        const SpriteRect &arrow = rect[i];
        const bool show = i == 0 ? value != 0 : value != last;
        if (show) {
            const uint32_t alpha = (o.blink & 0xF) >> 3 ? 0xFF : 0;
            list = setupSprites(x, list, o.stripTexture);
            list = ehsys::SetBlendColor(x, list, (alpha << 24) | 0xFFFFFFu);
            list = ehsys::SetSpriteSize(x, list, arrow.w, arrow.h, rect[0].w, rect[0].h);
            list = ehsys::DrawSprite(x, list, px, py, 0, arrow.u, arrow.v, 0, 0);
        }
        px = (int16_t)(px + spacing);
    }
    return list;
}

/* FUN_08f07394(list, options, onSide, value, x, y): the on/off marker: sprite A when on, B when off, drawn at x - 2 or, when `value` is 0, x + 33 */
static uint32_t drawToggle(SwapCtx &x, uint32_t list, const OptionsScreen &o, int32_t onSide, char value, int16_t px, int16_t py) {
    const SpriteRect *rect = (const SpriteRect *)x.mem(x.addr(off(0x08f2769c)));
    const SpriteRect &sprite = onSide == 0 ? rect[1] : rect[0];
    int16_t drawX = (int16_t)(px - 2);
    if (value == 0) drawX = (int16_t)(px + 0x21);
    list = setupSprites(x, list, o.stripTexture);
    list = ehsys::SetSpriteSize(x, list, sprite.w, sprite.h, rect[0].w, rect[0].h);
    return ehsys::DrawSprite(x, list, drawX, py, 0, sprite.u, sprite.v, 0, 0);
}

constexpr int32_t kModernGroupGap = 8;
constexpr int32_t kHeadingFontSize = 12;   /* the heading of a box (the rest of the text is 13) */

/* FUN_08f06c20(list, options, group, row, selected, x, y): one row of the Options screen: its label and its widget. Groups: 0 = row of one
 * on/off, 1 = a three-way choice, 2 = three volume bars (row 0 is the heading), 3 = on/off switches and a four-way choice. */
static uint32_t drawOptionRow(SwapCtx &x, uint32_t list, uint32_t optionsAddr, int32_t group, int32_t row, bool selected, int32_t boxX, int32_t boxY) {
    OptionsScreen &o = x.at<OptionsScreen>(optionsAddr);
    const OptionSettings &settings = o.current;
    int32_t labelX, labelY, labelWidth = 0, labelIndex = 0, widget = 0;
    if (row == 0) { labelX = boxX + 8; labelY = boxY + 5; }
    else { labelX = boxX + 10; labelY = boxY + 5 + row * 0x11; }
    if (group == 3) {
        labelWidth = 0x7A;
        if (row == 4) labelIndex = 0x12;
        else if (row == 3) labelIndex = 0xB;
        else if (row == 2) labelIndex = 9;
        else if (row == 1) labelIndex = 8;
        else { labelIndex = 7; labelWidth = 0xCA; }
    } else if (group == 2) {
        labelIndex = row + 3;
        labelWidth = 0xCA;
        if (ehsys::GetLanguage(x) != 0 && labelIndex > 3) labelIndex = row + 4;
        if (labelIndex > 3) labelWidth = 0x5C;
    } else if (group == 1) { labelIndex = row + 2; labelWidth = 0x7B; }
    else if (group == 0) { labelIndex = row + 1; labelWidth = 0x78; }
    const uint32_t colour = (!selected || o.shown == 0) ? o.colourNormal : o.colourSelected;
    const bool heading = row == 0 && group >= 2;   /* the audio and duel groups' first row is the heading of the box */
    ehsys::SetFontSize(x, heading ? kHeadingFontSize : 13, heading ? kHeadingFontSize : 13);
    ehsys::SetTextColor(x, colour);
    ehsys::SetUiFlag4(x, true);
    list = optionText(x, list, labelX, labelY, labelWidth, (uint32_t)labelIndex, false);
    if (group == 3) { if (row == 1) widget = 2; else if (row > 1) widget = 1; }
    else if (group == 2) { if (row != 0) widget = 3; }
    else if (group == 1) widget = 2;
    else if (group == 0) widget = 1;
    ehsys::SetUiFlag4(x, true);
    if (widget == 3) {
        uint32_t value = 0;
        if (ehsys::GetLanguage(x) == 0) {
            if (row == 3) value = settings.effectsVolume; else if (row == 2) value = settings.musicVolume; else if (row == 1) value = settings.voiceVolume;
        } else {
            if (row == 2) value = settings.effectsVolume; else if (row == 1) value = settings.musicVolume;
        }
        list = drawVolumeBar(x, list, o, value, (int16_t)(boxX + 0x6A), (int16_t)labelY);
    } else if (widget == 2) {
        uint32_t last = 3, choice, message;
        if (group == 1) { last = 2; choice = settings.messageSpeed; message = choice + 0xF; }
        else { choice = settings.cardAnimation; message = choice + 0xE; }
        list = optionText(x, list, boxX + 0x90, labelY, 0x36, message, true);
        if (selected) list = drawArrows(x, list, o, (int16_t)(boxX + 0x84), (int16_t)labelY, (char)choice, (char)last);
    } else if (widget == 1) {
        uint32_t onSide, value = 0;
        if (group == 0) { onSide = 1; value = settings.tutorial; }
        else {
            onSide = 0;
            if (row == 4) value = settings.autoDraw;
            else if (row == 3) value = settings.activationConfirm;
            else if (row == 2) value = settings.duelEffects;
        }
        list = drawToggle(x, list, o, (int32_t)onSide, (char)value, (int16_t)(boxX + 0x89), (int16_t)labelY);
        list = optionText(x, list, boxX + 0x88, labelY, 0x1E, 0xC, true);
        list = optionText(x, list, boxX + 0xAB, labelY, 0x1E, 0xD, true);
    }
    return list;
}
static void swap_DrawOptionRow(SwapCtx &x) {
    x.ret(drawOptionRow(x, x.arg(0), x.arg(1), (int32_t)x.arg(2), (int32_t)x.arg(3), (char)x.arg(4) != 0, (int32_t)x.arg(5), (int32_t)x.arg(6)));
}
static SwapReg reg_DrawOptionRow({"libtitle.DrawOptionRow", MODULE, off(0x08f06c20), swap_DrawOptionRow});

static void swap_DrawVolumeBar(SwapCtx &x) { x.ret(drawVolumeBar(x, x.arg(0), x.at<OptionsScreen>(x.arg(1)), x.arg(2), (int16_t)x.arg(3), (int16_t)x.arg(4))); }
static SwapReg reg_DrawVolumeBar({"libtitle.DrawVolumeBar", MODULE, off(0x08f07074), swap_DrawVolumeBar});
static void swap_DrawArrows(SwapCtx &x) { x.ret(drawArrows(x, x.arg(0), x.at<OptionsScreen>(x.arg(1)), (int16_t)x.arg(2), (int16_t)x.arg(3), (char)x.arg(4), (char)x.arg(5))); }
static SwapReg reg_DrawArrows({"libtitle.DrawArrows", MODULE, off(0x08f071c4), swap_DrawArrows});
static void swap_DrawToggle(SwapCtx &x) { x.ret(drawToggle(x, x.arg(0), x.at<OptionsScreen>(x.arg(1)), (int32_t)x.arg(2), (char)x.arg(3), (int16_t)x.arg(4), (int16_t)x.arg(5))); }
static SwapReg reg_DrawToggle({"libtitle.DrawToggle", MODULE, off(0x08f07394), swap_DrawToggle});

/* FUN_08f057ec(settings): read the four setting bytes from the engine (ehsys getters) */
static void readSettings(SwapCtx &x, uint32_t block) {
    OptionSettings &o = x.at<OptionSettings>(block);
    o.tutorial = x.callNid(nid::GetTutorial) & 1;
    o.messageSpeed = x.callNid(nid::GetMessageSpeed) & 3;
    o.voiceVolume = (uint8_t)x.callNid(nid::GetVolume, 2);
    o.musicVolume = (uint8_t)x.callNid(nid::GetVolume, 0);
    o.effectsVolume = (uint8_t)x.callNid(nid::GetVolume, 1);
    o.cardAnimation = x.callNid(nid::GetCardAnimation) & 3;
    o.activationConfirm = x.callNid(nid::GetDuelOption, 2) & 1;
    o.duelEffects = x.callNid(nid::GetDuelOption, 0) & 1;
    o.autoDraw = x.callNid(nid::GetDuelOption, 3) & 1;
}
static void swap_ReadSettings(SwapCtx &x) { readSettings(x, x.arg(0)); }
static SwapReg reg_ReadSettings({"libtitle.ReadSettings", MODULE, off(0x08f057ec), swap_ReadSettings});

/* FUN_08f058f8(settings): hand the four setting bytes to the engine (ehsys setters) */
static void writeSettings(SwapCtx &x, uint32_t block) {
    const OptionSettings o = x.at<OptionSettings>(block);
    x.callNid(nid::SetTutorial, o.tutorial);
    x.callNid(nid::SetMessageSpeedSetting, o.messageSpeed);
    x.callNid(nid::SetVolume, 2, o.voiceVolume);
    x.callNid(nid::SetVolume, 0, o.musicVolume);
    x.callNid(nid::SetVolume, 1, o.effectsVolume);
    x.callNid(nid::SetCardAnimation, o.cardAnimation);
    x.callNid(nid::SetDuelOption, 2, o.activationConfirm);
    x.callNid(nid::SetDuelOption, 0, o.duelEffects);
    x.callNid(nid::SetDuelOption, 3, o.autoDraw);
}
static void swap_WriteSettings(SwapCtx &x) { writeSettings(x, x.arg(0)); }
static SwapReg reg_WriteSettings({"libtitle.WriteSettings", MODULE, off(0x08f058f8), swap_WriteSettings});

/* FUN_08f0573c(heap, archive, mode): take the Options block from the heap, clear it and read the settings twice (edited copy and saved copy) */
static void swap_InitOptionsScreen(SwapCtx &x) {
    const uint32_t heap = x.arg(0), archive = x.arg(1), mode = x.arg(2);
    const uint32_t block = ehsys::Alloc(x, heap, 0x30);
    x.at<uint32_t>(kOptionsScreenPtr) = block;
    memset(x.mem(block), 0, 0x30);
    OptionsScreen &o = x.at<OptionsScreen>(block);
    o.heap = heap;
    o.archive = archive;
    o.mode = (int32_t)mode;
    readSettings(x, block + 8);
    readSettings(x, block + 12);
    if (mode == 0) { o.cursor = 0; o.colourNormal = 0xFFFFFFFFu; o.colourSelected = 0xFF000000u; }
    else { o.cursor = 3; o.colourNormal = 0xFF000000u; o.colourSelected = 0xFF000000u; }
}
static SwapReg reg_InitOptionsScreen({"libtitle.InitOptionsScreen", MODULE, off(0x08f0573c), swap_InitOptionsScreen});

/* FUN_08f059a4: give the Options block back to the heap */
static void swap_FreeOptionsScreen(SwapCtx &x) {
    const uint32_t block = x.at<uint32_t>(kOptionsScreenPtr);
    ehsys::Free(x, x.at<OptionsScreen>(block).heap, block);
}
static SwapReg reg_FreeOptionsScreen({"libtitle.FreeOptionsScreen", MODULE, off(0x08f059a4), swap_FreeOptionsScreen});

/* FUN_08f05504(screen, arg0): enter the Options screen: reset the flow, build its state and start the three background images */
static void swap_EnterOptionsScreen(SwapCtx &x) {
    State &s = state(x);
    const uint32_t screen = x.arg(0), arg0 = x.arg(1);
    x.callLocal(fn::RequestScreen16, 0, 0xFFF, 0xFFE);
    s.screen = (int16_t)screen;
    s.arg0 = (int16_t)arg0;
    s.arg1 = 0;
    s.transition = 0;
    state(x).menuColour[0] = 0xFFFFFFFFu;
    state(x).menuColour[1] = 0xFFFFFFFFu;
    const uint32_t archive = x.callLocal(fn::CurrentLogoResource);
    if (graphics.active) graphics.archive = archive;   /* the Graphics screen needs no block: its state is `graphics` */
    else x.callLocal(fn::InitOptionsScreen, state(x).heap, archive, 0);
    x.callLocal(fn::SlotLoad, 5, 2, 0, 480);
    x.callLocal(fn::SlotLoad, 6, 2, 0, 480);
    x.callLocal(fn::SlotLoad, 13, 2, 0, 16);
}
static SwapReg reg_EnterOptionsScreen({"libtitle.EnterOptionsScreen", MODULE, off(0x08f05504), swap_EnterOptionsScreen});

/* FUN_08f059b4: one frame of Options input. Returns 2 when the screen is done (confirm or cancel), 1 otherwise.
 * Up/down move the cursor (skipping row 2 outside the first language), left/right change the row's setting. */
static uint32_t optionsInput(SwapCtx &x) {
    OptionsScreen &o = options(x);
    OptionSettings &settings = o.current;
    const uint32_t pad = x.callNid(nid::GetInput);
    o.shown = 1;
    const int32_t firstRow = o.mode == 1 ? 3 : 0;
    const uint32_t repeat = x.at<ehsys::PadState>(pad).pressedOrRepeating, pressed = x.at<ehsys::PadState>(pad).pressed;
    if (repeat & x.callNid(nid::GetButtonMask, 0) & 0xFFFF) {                 /* confirm: keep the changes */
        x.callNid(nid::PlaySound, kSoundConfirm);
        writeSettings(x, x.at<uint32_t>(kOptionsScreenPtr) + 8);
        return 2;
    }
    if (repeat & x.callNid(nid::GetButtonMask, 1) & 0xFFFF) {                 /* cancel: put the saved settings back */
        x.callNid(nid::PlaySound, kSoundCancel);
        o.current = o.saved;
        writeSettings(x, x.at<uint32_t>(kOptionsScreenPtr) + 8);
        return 2;
    }
    auto step = [&](uint32_t sound) { x.callNid(nid::PlaySound, sound); };
    auto setVolume = [&](uint8_t &v, int engineChannel, bool up, uint32_t sound) {
        if (up ? v < 0xF : v != 0) { step(sound); v = (uint8_t)(up ? v + 1 : v - 1); }
        x.callNid(nid::SetVolume, (uint32_t)engineChannel, v);
    };
    if (repeat & kPadLeft) {
        switch (o.cursor) {
        case 0: step(kSoundCursor); settings.tutorial = 1; break;
        case 1: if (settings.messageSpeed != 0) { step(kSoundCursor); settings.messageSpeed = settings.messageSpeed - 1; } break;
        case 2: setVolume(settings.voiceVolume, 2, false, kSoundVolume); break;
        case 3: setVolume(settings.musicVolume, 0, false, kSoundCursor); break;
        case 4: setVolume(settings.effectsVolume, 1, false, kSoundCursor); break;
        case 5: if (settings.cardAnimation != 0) { step(kSoundCursor); settings.cardAnimation = settings.cardAnimation - 1; } break;
        case 6: step(kSoundCursor); settings.duelEffects = 1; break;
        case 7: step(kSoundCursor); settings.activationConfirm = 1; break;
        case 8: step(kSoundCursor); settings.autoDraw = 1; break;
        }
    } else if (repeat & kPadRight) {
        switch (o.cursor) {
        case 0: step(kSoundCursor); settings.tutorial = 0; break;
        case 1: if (settings.messageSpeed < 2) { step(kSoundCursor); settings.messageSpeed = settings.messageSpeed + 1; } break;
        case 2: setVolume(settings.voiceVolume, 2, true, kSoundVolume); break;
        case 3: setVolume(settings.musicVolume, 0, true, kSoundCursor); break;
        case 4: setVolume(settings.effectsVolume, 1, true, kSoundCursor); break;
        case 5: if (settings.cardAnimation < 3) { step(kSoundCursor); settings.cardAnimation = settings.cardAnimation + 1; } break;
        case 6: step(kSoundCursor); settings.duelEffects = 0; break;
        case 7: step(kSoundCursor); settings.activationConfirm = 0; break;
        case 8: step(kSoundCursor); settings.autoDraw = 0; break;
        }
    } else {
        const uint32_t upPressed = pressed & kPadUp, downPressed = pressed & kPadDown;
        if (upPressed != 0 || (repeat & kPadUp) != 0) {
            if (firstRow < o.cursor) {
                step(kSoundCursor);
                o.cursor -= o.cursor == 3 ? 2 : 1;       /* row 3 skips row 2 when row 2 is not shown */
            } else if (upPressed != 0) {
                step(kSoundCursor);
                o.cursor = 8;
            }
        } else if (downPressed != 0 || (repeat & kPadDown) != 0) {
            if (o.cursor < 8) {
                step(kSoundCursor);
                o.cursor++;
                if (ehsys::GetLanguage(x) != 0 && o.cursor == 2) o.cursor = 3;
            } else if (downPressed != 0) {
                step(kSoundCursor);
                o.cursor = firstRow;
            }
        }
    }
    uint32_t blink = o.blink + 1;
    o.blink = ((int32_t)blink < 0 && (blink & 0xFFFFFF) == 0) ? 0 : (blink & 0xFFFFFF);
    return 1;
}
static void swap_OptionsInput(SwapCtx &x) { x.ret(optionsInput(x)); }
static SwapReg reg_OptionsInput({"libtitle.OptionsInput", MODULE, off(0x08f059b4), swap_OptionsInput});

/* One frame of Graphics input (not a game function). Returns 2 when the screen is done (confirm or cancel), 1 otherwise. Up/down move the
 * cursor (wrapping), left/right change the option's choice and apply it at once. */
static uint32_t graphicsInput(SwapCtx &x) {
    const uint32_t pad = x.callNid(nid::GetInput);
    graphics.shown = true;
    graphics.blink = (graphics.blink + 1) & 0xFFFFFF;
    const uint32_t repeat = x.at<ehsys::PadState>(pad).pressedOrRepeating, pressed = x.at<ehsys::PadState>(pad).pressed;
    if (repeat & x.callNid(nid::GetButtonMask, 0) & 0xFFFF) { x.callNid(nid::PlaySound, kSoundConfirm); return 2; }
    if (repeat & x.callNid(nid::GetButtonMask, 1) & 0xFFFF) { x.callNid(nid::PlaySound, kSoundCancel); return 2; }
    const int32_t choice = gfx_option_get(graphics.cursor), choices = gfx_option_choice_count(graphics.cursor);
    if ((repeat & kPadLeft) && choice > 0) {
        x.callNid(nid::PlaySound, kSoundCursor);
        gfx_option_set(graphics.cursor, choice - 1);
    } else if ((repeat & kPadRight) && choice < choices - 1) {
        x.callNid(nid::PlaySound, kSoundCursor);
        gfx_option_set(graphics.cursor, choice + 1);
    } else if (!(repeat & (kPadLeft | kPadRight))) {
        if ((pressed | repeat) & kPadUp) {
            if (graphics.cursor > 0) { x.callNid(nid::PlaySound, kSoundCursor); graphics.cursor--; }
            else if (pressed & kPadUp) { x.callNid(nid::PlaySound, kSoundCursor); graphics.cursor = GFXOPT_COUNT - 1; }
        } else if ((pressed | repeat) & kPadDown) {
            if (graphics.cursor < GFXOPT_COUNT - 1) { x.callNid(nid::PlaySound, kSoundCursor); graphics.cursor++; }
            else if (pressed & kPadDown) { x.callNid(nid::PlaySound, kSoundCursor); graphics.cursor = 0; }
        }
    }
    return 1;
}

/* FUN_08f055c8: the Options screen's step by arg0: 0 -> 1, 2 -> start the slot-13 animation, 3 -> run the input until it finishes (then fade out),
 * 5 -> free the screen and go back to the main menu */
static void swap_OptionsStep(SwapCtx &x) {
    State &s = state(x);
    switch (s.arg0) {
    case 0: s.arg0 = 1; break;
    case 2:
        if (x.callLocal(fn::SlotQuery, 13) != 16) return;
        x.callLocal(fn::SlotSetRange, 13, 2, 17, 96);
        s.arg0++;
        break;
    case 3:
        if ((graphics.active ? graphicsInput(x) : x.callLocal(fn::OptionsInput)) == 2) {
            x.callLocal(fn::RequestScreen16, 1, 0xFFF, 5);
            s.arg0 = 4;
        }
        break;
    case 5:
        if (graphics.active) {
            graphics.active = false;
            x.callLocal(fn::SwitchScreen, 9, 9);   /* back to the main menu with Graphics selected (Options returns with 8: its own item) */
            break;
        }
        x.callLocal(fn::FreeOptionsScreen);
        x.callLocal(fn::SwitchScreen, 9, 8);
        break;
    }
}
static SwapReg reg_OptionsStep({"libtitle.OptionsStep", MODULE, off(0x08f055c8), swap_OptionsStep});

constexpr int16_t kOptionsBoxWidth = 0xDA;   /* the Options groups are 218 pixels wide */

/* The frame of one group of rows: 3 pieces of the sprite sheet T for a single row, 9 (corners, edges, middle stretched to the box) for more.
 * A group with a heading (rows > 1) also gets the strip of 3 small sprites at its top. `icon` picks the frame's colour cell, `stripRow` the strip's. */
/* A colour of the sheet's cells, `percent` of the way from black (0) to the cell colour (100) */
static Rgb shaded(Rgb base, int32_t percent) { return {base.r * percent / 100, base.g * percent / 100, base.b * percent / 100}; }
/* ... and the same towards white */
static Rgb lightened(Rgb base, int32_t percent) { return {base.r + (255 - base.r) * percent / 100, base.g + (255 - base.g) * percent / 100, base.b + (255 - base.b) * percent / 100}; }

constexpr int32_t kOptionsRowHeight = 17, kOptionsHeadingHeight = 13;

/* The PC style's group frame: a dark glass box in the group's colour (`icon` is its cell of the sheet) and, for a group with a heading, a lighter
 * glass strip over its top row. */
static uint32_t drawModernGroupFrame(SwapCtx &x, uint32_t list, const int16_t *T, uint32_t panelTexture, int32_t rows, int16_t boxX, int16_t boxY,
                                     int16_t boxWidth, int32_t icon) {
    const Rgb base = kSheetCellColours[icon % std::size(kSheetCellColours)];
    const int32_t height = rows == 1 ? T[3] : (rows - 1) * kOptionsRowHeight + T[15];
    list = setupSprites(x, list, panelTexture);
    list = drawGlassPieces(x, list, 0xFFFFFFFFu, boxX, boxY, boxWidth, height, {false, shaded(base, 38), lightened(base, 45)});
    if (rows > 1) list = drawGlassPieces(x, list, 0xFFFFFFFFu, boxX + 4, boxY + 4, boxWidth - 8, kOptionsHeadingHeight, {false, shaded(base, 78), lightened(base, 60)});
    return list;
}

static uint32_t drawGroupFrame(SwapCtx &x, uint32_t list, const int16_t *T, uint32_t panelTexture, uint32_t stripTexture, int32_t rows,
                               int16_t boxX, int16_t boxY, int16_t boxWidth, int32_t icon, uint8_t stripRow) {
    if (gfx_modern_menus()) return drawModernGroupFrame(x, list, T, panelTexture, rows, boxX, boxY, boxWidth, icon);
    const int16_t middleWidth = (int16_t)(boxWidth - (T[2] + T[10]));
    const int16_t stripWidth = (int16_t)(boxWidth - 9 - (T[38] + T[46]));
    const int32_t parts = rows == 1 ? 3 : 9;
    for (int32_t k = 0; k < parts; k++) {
        list = setupSprites(x, list, panelTexture);
        int16_t dstX, dstW, dstY;
        int32_t dstH;
        switch (k % 3) {
        case 2: dstX = (int16_t)(boxX + boxWidth - T[4 * k + 2]); dstW = T[4 * k + 2]; break;
        case 1: dstX = (int16_t)(T[4 * k] + boxX); dstW = middleWidth; break;
        default: dstW = T[4 * k + 2]; dstX = (int16_t)(T[4 * k] + boxX); break;
        }
        switch (k / 3) {
        case 1: dstH = T[4 * k + 3]; dstY = (int16_t)(boxY + (rows - 1) * 0x11); break;
        case 2: dstY = (int16_t)(boxY + 10); dstH = (int16_t)(rows * 0x11 - 10); break;
        default: dstY = boxY; dstH = T[4 * k + 3]; break;
        }
        list = ehsys::SetSpriteSize(x, list, T[4 * k + 2], T[4 * k + 3], dstW, (int16_t)dstH);
        list = ehsys::DrawSprite(x, list, dstX, dstY, 0, (int16_t)(T[4 * k] + (int16_t)(icon % 6) * 0x14), (int16_t)(T[4 * k + 1] + (int16_t)(icon / 6) * 0x18), 0, 0);
    }
    if (rows > 1) {
        const int16_t stripY = (int16_t)(boxY + 4);
        for (int32_t n = 0; n < 3; n++) {
            const int32_t e = 9 + n;
            list = setupSprites(x, list, stripTexture);
            int16_t dstX, dstW;
            switch (n % 3) {
            case 2: dstX = (int16_t)((boxX + boxWidth - 5) - T[4 * e + 2]); dstW = T[4 * e + 2]; break;
            case 1: dstX = (int16_t)(boxX + 4 + T[4 * e]); dstW = stripWidth; break;
            default: dstW = T[4 * e + 2]; dstX = (int16_t)(boxX + 4 + T[4 * e]); break;
            }
            list = ehsys::SetSpriteSize(x, list, T[4 * e + 2], T[4 * e + 3], dstW, T[4 * e + 3]);
            list = ehsys::DrawSprite(x, list, dstX, stripY, 0, T[4 * e], (int16_t)(stripRow * 0x10 + T[4 * e + 1]), 0, 0);
        }
    }
    return list;
}

/* the highlight bar behind the selected row (`cursorRow` counts from the top of the group's box) */
static uint32_t drawHighlightBar(SwapCtx &x, uint32_t list, const int16_t *T, uint32_t panelTexture, int16_t boxX, int16_t boxY, int16_t boxWidth, int32_t cursorRow) {
    if (gfx_modern_menus()) {   /* gold glass, like the cursor row of the lists */
        list = setupSprites(x, list, panelTexture);
        return drawGlassPieces(x, list, 0xFFFFFFFFu, boxX + 4, boxY + 4 + cursorRow * kOptionsRowHeight, boxWidth - 9, 14, {false, {255, 196, 72}, {255, 238, 160}});
    }
    list = setupSprites(x, list, panelTexture);
    list = ehsys::SetSpriteSize(x, list, T[50], T[51], boxWidth - 9, 0xE);
    return ehsys::DrawSprite(x, list, (int16_t)(boxX + 4), (int16_t)(boxY + 4 + cursorRow * 0x11), 0, T[48], T[49], 0, 0);
}

/* the help text is white on the PC style's dark panel, black on the game's light one */
static uint32_t helpTextColour() { return gfx_modern_menus() ? 0xFFFFFFFFu : 0xFF000000u; }

/* the help panel at the bottom: a 3x3 frame and the small icon on its left; the caller draws the text (at 29, 251, 436 wide) */
static uint32_t drawHelpFrame(SwapCtx &x, uint32_t list, const int16_t *T, uint32_t panelTexture, uint32_t stripTexture) {
    int16_t middle = 0, stretchedHeight = 0, dstX = 0, dstW = 0, dstY = 0;
    int32_t dstH = 0;
    const bool modern = gfx_modern_menus();   /* the PC style draws one glass panel over the rectangle the nine pieces cover */
    const int16_t left = (int16_t)(T[70] + 8);
    for (int32_t i = 0; i < 9; i++) {
        const int32_t e = 0x11 + i;
        if (i % 3 == 0) { dstW = T[4 * e + 2]; dstX = 8; }
        if (i % 3 == 1) {
            dstW = (int16_t)((0x1CE - T[4 * e - 2]) - T[4 * e + 6]);
            dstX = left;
            middle = dstW;
            if (dstW < 0) { dstW = 0; middle = 0; }
        }
        if (i % 3 == 2) { dstW = T[4 * e + 2]; dstX = (int16_t)(left + middle); }
        if (i < 3) { dstH = T[4 * e + 3]; dstY = 0xF6; }
        else if (i < 6) {
            dstY = (int16_t)(T[71] + 0xF6);
            stretchedHeight = (int16_t)((0x14 - T[4 * e - 1]) - T[4 * e + 7]);
            if (stretchedHeight < 0) stretchedHeight = 0;
            dstH = stretchedHeight;
        } else { dstH = T[4 * e + 3]; dstY = (int16_t)(T[71] + 0xF6 + stretchedHeight); }
        if (modern) {
            if (i == 8) {   /* the last piece closes the rectangle */
                list = setupSprites(x, list, panelTexture);
                list = drawGlassPieces(x, list, 0xFFFFFFFFu, 8, 0xF6, dstX + dstW - 8, dstY + dstH - 0xF6, kDarkPanelStyle);
            }
            continue;
        }
        list = setupSprites(x, list, stripTexture);
        list = ehsys::SetSpriteSize(x, list, T[4 * e + 2], T[4 * e + 3], dstW, (int16_t)dstH);
        list = ehsys::DrawSprite(x, list, dstX, (int16_t)dstY, 0, T[4 * e], T[4 * e + 1], 0, 0);
    }
    list = setupSprites(x, list, stripTexture);
    list = ehsys::SetSpriteSize(x, list, T[106], T[107], T[106], T[107]);
    return ehsys::DrawSprite(x, list, 0xC, 0xFA, 0, T[104], T[105], 0, 0);
}

/* FUN_08f05fc0(list): draw the Options screen's four groups of rows and the help text at the bottom. Groups are boxes made of 3 or 9 pieces
 * of the sprite sheet (kOptionsSprites) stacked down the screen; each group has a strip of 3 small sprites and its rows (DrawOptionRow). */
static uint32_t drawOptionsScreen(SwapCtx &x, uint32_t list) {
    OptionsScreen &o = options(x);
    const uint32_t optionsAddr = x.at<uint32_t>(kOptionsScreenPtr);
    const int16_t *T = (const int16_t *)x.mem(x.addr(off(kOptionsSprites)));
    int16_t rows[4];
    memcpy(rows, x.mem(x.addr(off(kOptionsRowCounts))), sizeof(rows));
    uint8_t rowIcon[4];
    memcpy(rowIcon, x.mem(x.addr(off(kOptionsRowIcons))), sizeof(rowIcon));
    int32_t groupIcon[4];
    memcpy(groupIcon, x.mem(x.addr(off(kOptionsGroupIcons))), sizeof(groupIcon));

    int32_t firstGroup = 0;
    ehsys::GetLanguage(x);
    if (o.mode == 1) firstGroup = 2;
    uint32_t cursor = (uint8_t)o.cursor;
    const uint32_t language = ehsys::GetLanguage(x);
    if (language != 0) {
        rows[2] = 3;
        if (o.cursor > 2) cursor = (uint32_t)(o.cursor - 1) & 0xFF;
    }
    const uint32_t names = x.addr(off(o.mode == 1 ? 0x08f27508 : 0x08f27488));
    o.panelTexture = ehsys::FindTexture(x, o.archive, names, 0);
    o.stripTexture = ehsys::FindTexture(x, o.archive, names + 0x40, 0);

    /* which group and row the cursor is in */
    int16_t cursorGroup = 0, cursorRow = 0, counted = 0;
    bool found = false;
    for (int g = 0; g < 4; g++) {
        if (rows[g] < 2) {
            cursorRow = 0;
            if (cursor == (uint32_t)(int32_t)counted) break;
            cursorRow = 1;
            counted++;
        } else {
            const int32_t selectable = rows[g] - 1;
            cursorRow = 1;
            for (int32_t i = 0; i < selectable; i++) {
                if (cursor == (uint32_t)(int32_t)counted) { found = true; break; }
                cursorRow++;
                counted++;
            }
        }
        if (found) break;
        cursorGroup++;
    }

    /* where each group goes */
    int16_t rowStart[4] = {0, 0, 0, 0}, boxX[4] = {0, 0, 0, 0}, boxY[4] = {0, 0, 0, 0};
    const int32_t groupGap = gfx_modern_menus() ? kModernGroupGap : 6;   /* 6 px between the PSP's boxes; the glass boxes sit closer, so a little more there */
    for (int32_t g = firstGroup; g < 4; g++) {
        rowStart[g] = g == firstGroup ? 0 : (int16_t)(rowStart[g - 1] + rows[g - 1]);
        boxX[g] = 0x83;
        boxY[g] = (int16_t)(g * groupGap + rowStart[g] * 0x11 + (language != 0 ? 0x2A : 0x22));
    }
    for (int32_t g = firstGroup; g < 4; g++) {
        list = drawGroupFrame(x, list, T, o.panelTexture, o.stripTexture, rows[g], boxX[g], boxY[g], kOptionsBoxWidth, groupIcon[g], rowIcon[g]);
        if (o.shown != 0 && g == cursorGroup) list = drawHighlightBar(x, list, T, o.panelTexture, boxX[g], boxY[g], kOptionsBoxWidth, cursorRow);
        for (int32_t row = 0; row < rows[g]; row++) {
            list = drawOptionRow(x, list, optionsAddr, g, row, g == cursorGroup && row == cursorRow, boxX[g], boxY[g]);
        }
    }

    list = drawHelpFrame(x, list, T, o.panelTexture, o.stripTexture);   /* the help panel at the bottom, then the text of the selected row */
    uint32_t message = (uint32_t)o.cursor + 0x13;
    if (o.cursor == 4) message = 0x16;
    else if (o.cursor == 3) message = 0x17;
    ehsys::SetFontSize(x, 13, 13);
    ehsys::SetTextColor(x, helpTextColour());
    ehsys::SetUiFlag4(x, true);
    list = optionText(x, list, 0x1D, 0xFB, 0x1B4, message, false);
    o.shown = 0;
    return list;
}
static void swap_DrawOptionsRows(SwapCtx &x) { x.ret(drawOptionsScreen(x, x.arg(0))); }
static SwapReg reg_DrawOptionsRows({"libtitle.DrawOptionsRows", MODULE, off(0x08f05fc0), swap_DrawOptionsRows});

/* The Graphics screen (not a game function): the Options screen's frame, sprites and font around one box with a heading and one row per
 * graphics option (label on the left, current choice between two arrows on the right) and the option's help text in the bottom panel. */
static uint32_t drawGraphicsScreen(SwapCtx &x, uint32_t list) {
    const int16_t *T = (const int16_t *)x.mem(x.addr(off(kOptionsSprites)));
    const uint8_t headedGroupRowIcon = x.at<uint8_t>(x.addr(off(kOptionsRowIcons)) + 2);       /* the volume group has a heading, like this box */
    const int32_t headedGroupIcon = x.at<int32_t>(x.addr(off(kOptionsGroupIcons)) + 8);
    const uint32_t names = x.addr(off(0x08f27488));
    const uint32_t panelTexture = ehsys::FindTexture(x, graphics.archive, names, 0);
    const uint32_t stripTexture = ehsys::FindTexture(x, graphics.archive, names + 0x40, 0);

    if (state(x).arg0 >= 3) {   /* the title, where the other screens have a sprite with theirs (colours are ABGR: yellow) */
        ehsys::SetFontSize(x, 22, 22);
        ehsys::SetTextColor(x, 0xFF00E8FFu);
        ehsys::SetUiFlag4(x, true);
        list = drawAscii(x, list, 0, 6, 480, "GRAPHICS", true);
    }
    constexpr int16_t boxX = 40, boxY = 36, boxWidth = 400;
    constexpr int32_t labelWidth = 220, valueLeft = 256, valueWidth = 104, arrowLeft = 244, arrowSpacing = 128;
    constexpr uint32_t white = 0xFFFFFFFFu, black = 0xFF000000u;
    const int32_t rows = GFXOPT_COUNT + 1;   /* row 0 is the heading */
    list = drawGroupFrame(x, list, T, panelTexture, stripTexture, rows, boxX, boxY, boxWidth, headedGroupIcon, headedGroupRowIcon);
    if (graphics.shown) list = drawHighlightBar(x, list, T, panelTexture, boxX, boxY, boxWidth, graphics.cursor + 1);

    OptionsScreen arrowSprites = {};   /* drawArrows reads the strip texture and the blink counter from an Options block */
    arrowSprites.stripTexture = stripTexture;
    arrowSprites.blink = graphics.blink;
    ehsys::SetFontSize(x, kHeadingFontSize, kHeadingFontSize);
    ehsys::SetUiFlag4(x, true);
    ehsys::SetTextColor(x, white);
    list = drawAscii(x, list, boxX + 8, boxY + 5, boxWidth - 16, "GRAPHICS", false);
    for (int32_t option = 0; option < GFXOPT_COUNT; option++) {
        const int32_t labelY = boxY + 5 + (option + 1) * 0x11;
        const bool selected = option == graphics.cursor && graphics.shown;
        const int32_t choice = gfx_option_get(option), last = gfx_option_choice_count(option) - 1;
        ehsys::SetFontSize(x, 13, 13);
        ehsys::SetTextColor(x, selected ? black : white);
        ehsys::SetUiFlag4(x, true);
        list = drawAscii(x, list, boxX + 10, labelY, labelWidth, gfx_option_label(option), false);
        list = drawAscii(x, list, boxX + valueLeft, labelY, valueWidth, gfx_option_choice_name(option, choice), true);
        if (selected) list = drawArrows(x, list, arrowSprites, (int16_t)(boxX + arrowLeft), (int16_t)labelY, (char)choice, (char)last, arrowSpacing);
    }

    list = drawHelpFrame(x, list, T, panelTexture, stripTexture);
    ehsys::SetFontSize(x, 13, 13);
    ehsys::SetTextColor(x, helpTextColour());
    ehsys::SetUiFlag4(x, true);
    return drawAscii(x, list, 0x1D, 0xFB, 0x1B4, gfx_option_help(graphics.cursor), false);
}

/* FUN_08f056c4(list): draw the Options screen: the two background images, the cursor arrow once it is up, then the rows; counts the frame */
static void swap_DrawOptions(SwapCtx &x) {
    State &s = state(x);
    uint32_t list = x.callLocal(fn::SlotDraw, x.arg(0), 6);
    list = x.callLocal(fn::SlotDraw, list, 5);
    if (s.arg0 >= 2) {
        list = x.callLocal(fn::SlotDraw, list, 13);
        list = graphics.active ? list : x.callLocal(fn::DrawMenuArrow, list);   /* it draws the screen's title text (a sprite per screen); Graphics draws its own */
    }
    list = graphics.active ? drawGraphicsScreen(x, list) : drawOptionsScreen(x, list);
    s.transition++;
    x.ret(list);
}
static SwapReg reg_DrawOptions({"libtitle.DrawOptions", MODULE, off(0x08f056c4), swap_DrawOptions});

