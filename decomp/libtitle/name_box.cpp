/* libtitle swaps: The name box (not in the original game): the player types the name on the PC keyboard, inside the game. */
#include "internal.h"

/* ---- the name box (not in the original game) ----
 * The PSP typed the player's name on its own keyboard; the PC types it on the keyboard, inside the game: while the name stage (screen 8, arg0 2)
 * runs, the main menu's background, the name plate (with the letters typed so far and a blinking caret) and the message bar are drawn, and the
 * message bar says what to do. The keys come from runtime/ui/text_input.cpp. Without these swaps the runtime opens a small window instead. */
struct NameBox {
    uint32_t message = 0;   /* guest buffer of the message bar's text; 0 = the bar is not set up */
};
static NameBox nameBox;
constexpr uint32_t kNameMessageBytes = 256;

static void writeUtf16(SwapCtx &x, uint32_t guest, const std::wstring &text, uint32_t capacityChars) {
    uint16_t *chars = (uint16_t *)x.mem(guest);
    uint32_t n = 0;
    for (; n < text.size() && n + 1 < capacityChars; n++) chars[n] = (uint16_t)text[n];
    chars[n] = 0;
}

/* One frame of the name stage while the player types: set the message bar up the first time, then let the bar run its opening animation. */
void stepNameBox(SwapCtx &x, const std::wstring &description) {
    State &s = state(x);
    if (nameBox.message == 0) {
        nameBox.message = ehsys::Alloc(x, s.heap, kNameMessageBytes);
        if (nameBox.message == 0) return;
        fadeToBlack(x, 0, 16);   /* the menu faded out when the name was asked for: fade the picture back in (the PSP keyboard covered it) */
        x.callLocal(fn::ResetMainMenuWindow);
        writeUtf16(x, nameBox.message, description + L"  (Enter: OK, Esc: cancel)", kNameMessageBytes / 2);
        x.at<Window>(kWindowA).text = nameBox.message;
    }
    Window &bar = x.at<Window>(kWindowA);
    if (bar.state <= 2) { x.callNid(nid::UpdateWindow, kWindowA); return; }   /* still opening */
    /* Open: the bar is no longer updated (the pad's buttons would close it); the pad's confirm and cancel buttons end the box instead. */
    const uint32_t pressed = x.at<ehsys::PadState>(x.callNidKeepRegs(nid::GetInput)).pressed;
    if (pressed & (x.callNid(nid::GetButtonMask, 0) & 0xFFFF)) text_input_finish(true);
    else if (pressed & (x.callNid(nid::GetButtonMask, 1) & 0xFFFF)) text_input_finish(false);
}

/* The name was confirmed or cancelled: give the main menu its own bar back and free the message. */
void closeNameBox(SwapCtx &x) {
    if (nameBox.message == 0) return;
    x.callLocal(fn::ResetMainMenuWindow);
    ehsys::Free(x, state(x).heap, nameBox.message);
    nameBox.message = 0;
}

/* The name plate's text: what is typed, plus the caret when it is lit and there is room for one. */
static std::wstring nameWithCaret(const std::wstring &typed, bool caretLit, int maxLength) {
    return caretLit && (maxLength <= 0 || (int)typed.size() < maxLength) ? typed + L"_" : typed;
}

/* The main menu's background: three image slots, back to front */
static uint32_t drawMainMenuBackground(SwapCtx &x, uint32_t list) {
    list = x.callLocal(fn::SlotDraw, list, 6);
    list = x.callLocal(fn::SlotDraw, list, 5);
    return x.callLocal(fn::SlotDraw, list, 16);
}

/* The name plate (FramePart 116 at (175, 197)) with a name centred in it, in black: `name` is a guest UTF-16 text, or 0 for the player's name */
static uint32_t drawNamePlate(SwapCtx &x, uint32_t list, uint32_t name) {
    constexpr uint32_t kNamePlatePart = 116;
    list = x.callLocal(fn::DrawSpritePlain, list, 1, kNamePlatePart, 175, 197);
    ehsys::SetFontSize(x, 15, 15);
    ehsys::SetTextColor(x, 0xFF000000u);
    ehsys::SetUiFlag4(x, true);
    if (name == 0) name = x.callNid(nid::GetPlayerName);
    return x.callLocal(fn::DrawCentredText, list, 194, 201, 108, name, 2);
}

/* FUN_08f0913c(list): draw the main menu (screen 8, arg0 1): the three background images, the player's name on the open menu, the cursor
 * arrow, and the window (centred for steps 4, 5, 9, 10; with the dialog look for 6, 7, 8). With the in-game name box open (arg0 2) it draws the
 * same background, the plate with the typed name and the message bar. Returns the new list. */
static void swap_DrawMainMenu(SwapCtx &x) {
    uint32_t list = x.arg(0);
    State &s = state(x);
    std::wstring typed, description; bool caretLit = false; int maxLength = 0;
    if (s.arg0 == 2 && nameBox.message != 0 && text_input_live(&typed, &caretLit, &description, &maxLength)) {
        list = drawMainMenuBackground(x, list);
        GuestStack frame(x, 64);
        writeUtf16(x, frame.addr(), nameWithCaret(typed, caretLit, maxLength), 32);
        list = drawNamePlate(x, list, frame.addr());
        list = x.callLocal(fn::CentreAndRun, list, kWindowA, 0);
        x.ret(list);
        return;
    }
    if (s.arg0 == 1) {
        list = drawMainMenuBackground(x, list);
        if (s.arg1 == 4) list = drawNamePlate(x, list, 0);
        if (s.arg1 > 2) {
            list = x.callLocal(fn::SlotDraw, list, 13);
            list = x.callLocal(fn::DrawMenuArrow, list);
        }
        switch (s.arg1) {
        case 4: case 5: case 9: case 10: list = x.callLocal(fn::CentreAndRun, list, kWindowA, 0); break;
        case 6: case 7: case 8: list = x.callLocal(fn::CentreAndRun, list, kWindowA, 1); break;
        }
    }
    x.ret(list);
}
static SwapReg reg_DrawMainMenu({"libtitle.DrawMainMenu", MODULE, off(0x08f0913c), swap_DrawMainMenu});

