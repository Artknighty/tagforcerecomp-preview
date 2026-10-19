/* libfield: the message box: a window at the bottom of the screen that types a text out line by line and waits for the confirm button to go on. It
 * slides in from below, shows up to two lines (three on the other layouts) and, when asked to, the "more" arrow. */
#include "../common/swap.h"
#include "../common/ehsys.h"
#include "../boot/window.h"
#include "field_data.h"
#include "field_engine.h"
#include "field_functions.h"
#include <stddef.h>
#include <string.h>

#define MODULE "libfield"

namespace {
using namespace field;
namespace gu = ehsys;

enum MessageState : int32_t { kMessageAway = 0, kMessageOpening = 1, kMessageShown = 2, kMessageClosing = 3 };
struct MessageLine {
    int32_t x, y;           /* +0x00 where the line starts (pixels) */
    int32_t chars;          /* +0x08 how many characters of the line are typed so far */
    int32_t height;         /* +0x0C the height of a line of text */
    int32_t start;          /* +0x10 the index of the line's first character in the text */
    int32_t length;         /* +0x14 how many characters fit on the line */
};
static_assert(sizeof(MessageLine) == 0x18, "MessageLine layout");
/* 0x0999380C .. 0x0999398C */
struct Message {
    int32_t state;                 /* +0x000 MessageState */
    float t;                       /* +0x004 */
    uint8_t windowSet[0x4C];       /* +0x008 the window set (InitWindowSet) */
    boot::WindowStyle style;       /* +0x054 the box's own window */
    uint32_t heap;                 /* +0x0CC 0x099938D8 */
    int32_t xParameter;            /* +0x0D0 */
    int32_t widthParameter;        /* +0x0D4 */
    int32_t fontParameter;         /* +0x0D8 */
    int32_t lineCount;             /* +0x0DC 2, or 3 on the other layouts */
    int32_t textWidth;             /* +0x0E0 width of a line in pixels */
    uint32_t text;                 /* +0x0E4 -> UTF-16 text; 0 = none */
    int32_t speed;                 /* +0x0E8 the player's message speed setting */
    int32_t interactive;           /* +0x0EC waits for the confirm button */
    int32_t frameKind;             /* +0x0F0 */
    MessageLine lines[3];          /* +0x0F4 */
    int32_t currentLine;           /* +0x13C */
    int32_t nextStart;             /* +0x140 index of the first character of the current page */
    int32_t pages;                 /* +0x144 pages typed so far */
    int32_t pageDone;              /* +0x148 */
    int32_t finished;              /* +0x14C */
    int32_t pagePending;           /* +0x150 */
    int32_t pageStart;             /* +0x154 */
    int32_t wideFlag;              /* +0x158 */
    uint32_t arrowSprite;          /* +0x15C */
    int32_t arrowCounter;          /* +0x160 */
    int32_t locked;                /* +0x164 */
    float position[2];             /* +0x168 */
    float from[2];                 /* +0x170 */
    float to[2];                   /* +0x178 */
    uint32_t unknown180;
};
constexpr uint32_t kMessage = 0x0999380C;
Message &message(SwapCtx &x) { return datum<Message>(x, kMessage); }
uint32_t messageAddress(SwapCtx &x, size_t member) { return guest(x, kMessage) + (uint32_t)member; }
constexpr int32_t kScreenHeight = 0x110;
constexpr float kSlideStep = 0.1f;
constexpr uint32_t kPageSound = 0x6219;
int16_t s16(SwapCtx &x, uint32_t address) { return datum<int16_t>(x, address); }

/* ---- FUN_091d6e2c(heap, x, width, font, interactive, frameKind): makes the message box ---- */
void swap_InitMessage(SwapCtx &x) {
    const uint32_t heap = x.arg(0), xParameter = x.arg(1), width = x.arg(2), font = x.arg(3), interactive = x.arg(4), frameKind = x.arg(5);
    Message &m = message(x);
    memset(&m, 0, 0x180);
    m.heap = heap; m.xParameter = (int32_t)xParameter; m.widthParameter = (int32_t)width; m.fontParameter = (int32_t)font;
    m.interactive = (int32_t)interactive; m.frameKind = (int32_t)frameKind;
    x.callLocal(fn::BuildMessageWindow);
}
SwapReg reg_InitMessage({"libfield.InitMessage", MODULE, offsetOf(0x091D6E2C), swap_InitMessage});

/* ---- FUN_091d7840: sets the box's window up (frame, text margins, colours) and hands it to the window set ---- */
void swap_BuildMessageWindow(SwapCtx &x) {
    Message &m = message(x);
    x.callNid(gu::nid::InitWindowSet, messageAddress(x, offsetof(Message, windowSet)), m.heap);
    boot::WindowStyle &style = m.style;
    memset(&style, 0, sizeof(style));
    style.visible = 1;
    style.hasTitleText = 1;
    style.uiFlag4 = 1;
    style.titleVariant = 2;
    style.textTop = 2;
    style.frameKind = (uint8_t)m.frameKind;
    style.titleStrip = 0;
    style.y = 0xD2;
    style.x = (int16_t)m.xParameter;
    style.text = 0;
    style.title = 0;
    style.height = 0x3C;
    style.colour = 0xFFFFFFFFu;
    style.titleColour = 0xFFFFFFFFu;
    style.width = (int16_t)m.widthParameter;
    style.textColour = 0xFF000000u;
    style.textLeft = 4;
    style.fontSize = (uint8_t)m.fontParameter;
    style.textRight = 0x14;
    style.titleFontSize = style.fontSize;
    x.callNid(gu::nid::AddWindow, messageAddress(x, offsetof(Message, windowSet)), messageAddress(x, offsetof(Message, style)));
    x.callLocal(fn::ResetMessageState, (uint32_t)m.xParameter, (uint32_t)m.widthParameter, (uint32_t)m.fontParameter, (uint32_t)m.interactive, (uint32_t)m.frameKind);
}
SwapReg reg_BuildMessageWindow({"libfield.BuildMessageWindow", MODULE, offsetOf(0x091D7840), swap_BuildMessageWindow});

/* ---- FUN_091d6ed0(x, width, font, interactive, frameKind): back to the start: no text, the box below the screen ---- */
void swap_ResetMessageState(SwapCtx &x) {
    const int32_t px = (int32_t)x.arg(0), font = (int32_t)x.arg(2), interactive = (int32_t)x.arg(3), frameKind = (int32_t)x.arg(4);
    Message &m = message(x);
    m.lineCount = 0;
    m.text = 0;
    m.speed = (int32_t)x.callNid(gu::nid::GetMessageSpeed);
    m.interactive = interactive;
    m.frameKind = frameKind;
    memset(m.lines, 0, sizeof(m.lines));
    m.currentLine = 0; m.nextStart = 0; m.pageDone = 0; m.finished = 0; m.pagePending = 0; m.pageStart = 0; m.wideFlag = 0;
    m.position[0] = (float)px; m.position[1] = 300.0f;
    m.from[0] = m.position[0]; m.from[1] = 300.0f;
    m.to[0] = m.position[0]; m.to[1] = 210.0f;
    m.style.x = (int16_t)px;
    const int32_t mode = (int32_t)x.callNid(gu::nid::GetUiMode);
    m.lineCount = mode == 0 ? 2 : 3;
    m.wideFlag = mode == 0 ? 1 : 0;
    x.callLocal(fn::SetMessageFont, (uint32_t)font);
    x.callLocal(fn::StartMessage, 0, 0);
    x.callLocal(fn::RelayoutMessage);
    m.state = kMessageAway;
}
SwapReg reg_ResetMessageState({"libfield.ResetMessageState", MODULE, offsetOf(0x091D6ED0), swap_ResetMessageState});

void swap_SetMessageSprite(SwapCtx &x) { message(x).arrowSprite = x.arg(0); }
SwapReg reg_SetMessageSprite({"libfield.SetMessageSprite", MODULE, offsetOf(0x091D76B4), swap_SetMessageSprite});
void swap_SetMessageFont(SwapCtx &x) { message(x).style.fontSize = (uint8_t)x.arg(0); message(x).style.titleFontSize = (uint8_t)x.arg(0); }
SwapReg reg_SetMessageFont({"libfield.SetMessageFont", MODULE, offsetOf(0x091D76E4), swap_SetMessageFont});
void swap_ShowMessage(SwapCtx &x) { message(x).state = kMessageOpening; }
SwapReg reg_ShowMessage({"libfield.ShowMessage", MODULE, offsetOf(0x091D778C), swap_ShowMessage});

/* ---- FUN_091d7704(text, title): a new text (and the title strip when there is a title) ---- */
void swap_StartMessage(SwapCtx &x) {
    const uint32_t text = x.arg(0), title = x.arg(1);
    Message &m = message(x);
    m.style.titleStrip = title != 0 ? 1 : 0;
    m.finished = 0; m.pageDone = 0; m.pagePending = 0; m.pageStart = 0; m.pages = 0;
    m.style.title = title;
    m.text = text;
    x.callLocal(fn::LayoutMessage);
    x.callLocal(fn::MeasureMessageLines);
    if (m.state == kMessageAway) x.callLocal(fn::ShowMessage);
}
SwapReg reg_StartMessage({"libfield.StartMessage", MODULE, offsetOf(0x091D7704), swap_StartMessage});

/* ---- FUN_091d7e3c / FUN_091d7f18: the text settings the message uses (and putting the previous font flags back) ---- */
void swap_BeginMessageText(SwapCtx &x) {
    const uint32_t flags = x.callNid(gu::nid::GetFontFlags);
    const int32_t layoutMode = (int32_t)x.callNid(gu::nid::GetUiMode);
    const uint32_t lineAdvance = layoutMode == 1 || layoutMode == 0 ? 1 : 3;
    const Message &m = message(x);
    if (x.callNid(gu::nid::GetUiMode) == 0) {
        gu::SetUiFlag1(x, true);
        gu::SetWideScreenFlag(x, (uint32_t)m.wideFlag);
    } else {
        gu::SetUiFlag8(x, true);
        gu::SetWideScreenFlag(x, 0);
    }
    gu::SetUiFlag2(x, true);
    gu::SetFontSize(x, m.style.fontSize, m.style.fontSize);
    gu::SetUiFlag4(x, m.style.uiFlag4 != 0);
    gu::SetTextColor(x, m.style.textColour);
    gu::SetTextFlags(x, lineAdvance << 6);
    x.ret(flags & 0xFFFFu);
}
SwapReg reg_BeginMessageText({"libfield.BeginMessageText", MODULE, offsetOf(0x091D7E3C), swap_BeginMessageText});
void swap_EndMessageText(SwapCtx &x) {
    x.callNid(gu::nid::SetFontFlags, x.arg(0));
    gu::SetTextFlags(x, 0);
}
SwapReg reg_EndMessageText({"libfield.EndMessageText", MODULE, offsetOf(0x091D7F18), swap_EndMessageText});

/* ---- FUN_091d7b08: lays the lines out inside the window's content area, from the first character ---- */
void swap_LayoutMessage(SwapCtx &x) {
    Message &m = message(x);
    const uint32_t flags = x.callLocal(fn::BeginMessageText) & 0xFFFF;
    const int32_t layoutMode = (int32_t)x.callNid(gu::nid::GetUiMode);
    const int32_t advance = layoutMode == 1 || layoutMode == 0 ? 1 : 3;
    m.currentLine = 0;
    m.nextStart = 0;
    GuestStack rect(x, 32);
    const uint32_t outX = rect.addr(), outY = rect.addr() + 4, outW = rect.addr() + 8, outH = rect.addr() + 12;
    x.callNid(gu::nid::GetWindowContentRect, messageAddress(x, offsetof(Message, windowSet)), messageAddress(x, offsetof(Message, style)), outX, outY, outW, outH);
    m.textWidth = *(int32_t *)x.mem(outW);
    const int32_t lineHeight = gu::GetLineHeight(x);
    int32_t offsetY = 0, offsetShift = 0;
    for (int32_t i = 0; i < m.lineCount; i++) {
        MessageLine &line = m.lines[i];
        line.x = *(int32_t *)x.mem(outX);
        line.y = *(int32_t *)x.mem(outY) + offsetY;
        line.chars = 0;
        line.height = lineHeight >> 6;
        line.start = 0;
        if (x.callNid(gu::nid::GetUiMode) != 0) line.y = line.y + offsetShift;
        offsetY += lineHeight >> 6;
        offsetShift += advance;
    }
    x.callLocal(fn::EndMessageText, flags);
}
SwapReg reg_LayoutMessage({"libfield.LayoutMessage", MODULE, offsetOf(0x091D7B08), swap_LayoutMessage});

/* ---- FUN_091d7c40: the same while the box slides: the lines follow the window ---- */
void swap_RelayoutMessage(SwapCtx &x) {
    Message &m = message(x);
    const uint32_t flags = x.callLocal(fn::BeginMessageText) & 0xFFFF;
    const int32_t layoutMode = (int32_t)x.callNid(gu::nid::GetUiMode);
    const int32_t advance = layoutMode == 1 || layoutMode == 0 ? 1 : 3;
    m.style.x = (int16_t)truncate(m.position[0]);
    m.style.y = (int16_t)truncate(m.position[1]);
    GuestStack rect(x, 32);
    const uint32_t outX = rect.addr(), outY = rect.addr() + 4, outW = rect.addr() + 8, outH = rect.addr() + 12;
    x.callNid(gu::nid::GetWindowContentRect, messageAddress(x, offsetof(Message, windowSet)), messageAddress(x, offsetof(Message, style)), outX, outY, outW, outH);
    const int32_t lineHeight = gu::GetLineHeight(x);
    int32_t offsetY = 0, offsetShift = 0;
    for (int32_t i = 0; i < m.lineCount; i++) {
        MessageLine &line = m.lines[i];
        line.x = *(int32_t *)x.mem(outX);
        line.y = *(int32_t *)x.mem(outY) + offsetY;
        if (x.callNid(gu::nid::GetUiMode) != 0) line.y = line.y + offsetShift;
        offsetY += (lineHeight >> 6) + 1;
        offsetShift += advance;
    }
    x.callLocal(fn::EndMessageText, flags);
}
SwapReg reg_RelayoutMessage({"libfield.RelayoutMessage", MODULE, offsetOf(0x091D7C40), swap_RelayoutMessage});

/* ---- FUN_091d7f3c: measures how many characters fit on each line from the current page's start ---- */
void swap_MeasureMessageLines(SwapCtx &x) {
    Message &m = message(x);
    if (m.text == 0) return;
    const uint32_t flags = x.callLocal(fn::BeginMessageText) & 0xFFFF;
    gu::SetTextColor(x, 0);
    int32_t start = m.nextStart;
    for (int32_t i = 0; i < m.lineCount; i++) {
        MessageLine &line = m.lines[i];
        line.start = start;
        line.length = (int32_t)x.callNid(gu::nid::DrawTextBox, line.x << 6, line.y << 6, m.textWidth << 6, (line.height + 2) * 0x40, m.text + start * 2, 0xFFFFFFFFu);
        const uint32_t total = x.callNid(gu::nid::StringLength, m.text);
        int32_t length = line.length;
        if ((uint32_t)(start + length) < total) {
            if (*(int16_t *)x.mem(m.text + (start + length) * 2) == 0x20) line.length++;
            length = line.length;
        }
        start += length;
    }
    x.callLocal(fn::EndMessageText, flags);
}
SwapReg reg_MeasureMessageLines({"libfield.MeasureMessageLines", MODULE, offsetOf(0x091D7F3C), swap_MeasureMessageLines});

/* ---- FUN_091d7068: one frame of the message box: slides in and out, types the text, goes on at the confirm button. Returns whether it is finished ---- */
void swap_UpdateMessage(SwapCtx &x) {
    Message &m = message(x);
    const uint32_t pad = gu::GetInput(x);
    const uint32_t pressed = view<uint32_t>(x, pad)[1];
    m.speed = (int32_t)x.callNid(gu::nid::GetMessageSpeed);
    if (m.state == kMessageClosing) {
        x.c->f[12].f = m.t;
        x.callNid(engine::nid::Lerp2D, messageAddress(x, offsetof(Message, position)), messageAddress(x, offsetof(Message, from)), messageAddress(x, offsetof(Message, to)));
        if (m.t <= 0.0f) m.state = kMessageAway;
        else { m.t = m.t - (m.t * kSlideStep + kSlideStep); if (m.t < 0.0f) m.t = 0.0f; }
    } else if (m.state != kMessageShown) {
        if (m.state == kMessageOpening) {
            x.c->f[12].f = m.t;
            x.callNid(engine::nid::Lerp2D, messageAddress(x, offsetof(Message, position)), messageAddress(x, offsetof(Message, from)), messageAddress(x, offsetof(Message, to)));
            if (1.0f <= m.t) m.state = kMessageShown;
            else { m.t = m.t + (kSlideStep + m.t * kSlideStep); if (1.0f < m.t) m.t = 1.0f; }
        }
    } else {
        if (m.speed == 2) { for (int32_t i = 0; i < m.lineCount; i++) x.callLocal(fn::TypeNextCharacter); }
        else x.callLocal(fn::TypeNextCharacter);
        x.callLocal(fn::UpdateMessageArrow);
        if (m.interactive != 0) {
            bool skipHeld = false;
            if (x.callNid(ehsys::nid::SaveFlag, 7) != 0) {
                if ((view<uint32_t>(x, gu::GetInput(x))[0] & 0x200) != 0) {
                    const uint32_t padHeld = view<uint32_t>(x, gu::GetInput(x))[0];
                    const uint32_t confirm = x.callNid(gu::nid::GetButtonMask, 0);
                    if ((padHeld & confirm & 0xFFFF) != 0 && *view<uint32_t>(x, gu::FrameCounter(x)) % 3 == 0) skipHeld = true;
                }
            }
            const int32_t typeStep = datum<int32_t>(x, 0x091FA20C);
            if (m.locked == 0 && ((pressed & x.callNid(gu::nid::GetButtonMask, 0) & 0xFFFF) != 0 || skipHeld)) {
                if (m.pageDone == 0) {
                    if (m.pagePending == 0) {
                        for (int32_t i = 0; i < m.lineCount; i++) {
                            m.lines[m.currentLine].chars += typeStep;
                            x.callLocal(fn::TypeNextCharacter);
                            if (m.pageDone != 0 || m.pagePending != 0) break;
                        }
                    } else {
                        x.callLocal(fn::LayoutMessage);
                        m.nextStart = m.pageStart;
                        x.callLocal(fn::MeasureMessageLines);
                        m.pagePending = 0;
                        m.pages++;
                        gu::PlaySound(x, kPageSound);
                    }
                } else if (m.finished == 0) {
                    m.finished = 1;
                    gu::PlaySound(x, kPageSound);
                }
            }
        }
        if (m.text == 0) m.finished = 1;
    }
    if (m.state != kMessageAway) x.callLocal(fn::RelayoutMessage);
    x.ret((uint32_t)m.finished);
}
SwapReg reg_UpdateMessage({"libfield.UpdateMessage", MODULE, offsetOf(0x091D7068), swap_UpdateMessage});


/* ---- FUN_091d73a0(cursor address): draws the box, then the typed part of each line, clipped to its line, and the "more" arrow ---- */
void swap_DrawMessage(SwapCtx &x) {
    const uint32_t cursor = x.arg(0);
    Message &m = message(x);
    if (m.state == kMessageAway) return;
    uint32_t &list = *view<uint32_t>(x, cursor);
    x.callNid(gu::nid::SetWideScreenFlag, 0);
    x.callNid(engine::nid::DrawWindowSet, cursor, messageAddress(x, offsetof(Message, windowSet)));
    if (m.text != 0) {
        const uint32_t flags = x.callLocal(fn::BeginMessageText) & 0xFFFF;
        for (int32_t i = 0; i <= m.currentLine; i++) {
            const MessageLine &line = m.lines[i];
            const int32_t layoutMode = (int32_t)x.callNid(gu::nid::GetUiMode);
            int32_t top, height, advance;
            if (layoutMode == 1 || layoutMode == 0) { height = line.height + 1; top = line.y; advance = 1; }
            else { advance = 3; top = line.y - 2; height = line.height + 4; }
            if (line.chars > 0) {
                if (top < kScreenHeight) {
                    int32_t clip = top + height < kScreenHeight ? height : kScreenHeight - top;
                    list = gu::SetScissor(x, list, line.x, top, line.chars + 1, clip);
                    list = gu::EnableState(x, list, gu::kScissorTest);
                }
                if (top < kScreenHeight) {
                    const int32_t lineHeight = gu::GetLineHeight(x);
                    const float lines = (float)m.lineCount, pages = (float)m.pages;
                    GuestStack rect(x, 32);
                    const uint32_t outX = rect.addr(), outY = rect.addr() + 4, outW = rect.addr() + 8, outH = rect.addr() + 12;
                    x.callNid(gu::nid::GetWindowContentRect, messageAddress(x, offsetof(Message, windowSet)), messageAddress(x, offsetof(Message, style)), outX, outY, outW, outH);
                    gu::BeginText(x, list);
                    const int32_t rectX = *(int32_t *)x.mem(outX), rectY = *(int32_t *)x.mem(outY), rectW = *(int32_t *)x.mem(outW);
                    gu::DrawTextBox(x, rectX << 6, (rectY - truncate(pages * (lines * ((float)lineHeight / 64.0f + (float)advance)))) * 0x40, rectW << 6, 0x4400, m.text, 0xFFFFFFFFu);
                    list = gu::EndText(x);
                }
                list = gu::DisableState(x, list, gu::kScissorTest);
            }
        }
        x.callLocal(fn::EndMessageText, flags);
    }
    if (m.interactive != 0 && x.callLocal(fn::MessageBusyA) == 0 && x.callLocal(fn::MessageBusyB) == 0 && m.arrowSprite != 0) {
        const int32_t boxX = m.style.x, boxWidth = m.style.width, boxY = m.style.y, boxHeight = m.style.height;
        list = gu::SetColor(x, list, 0xFFFFFFFF);
        list = gu::BindTexture(x, list, m.arrowSprite);
        const uint32_t phase = (uint32_t)((((uint64_t)(uint32_t)m.arrowCounter * 0xCCCCCCCDULL) >> 32) & 0xF) >> 3;
        GuestStack ninth(x, 16);   /* the ninth argument of the call goes on the stack */
        *(uint32_t *)x.mem(ninth.addr()) = phase;
        x.callLocal(fn::DrawMessageArrow, cursor, (uint32_t)(boxX + boxWidth + datum<int32_t>(x, 0x091FA218)), (uint32_t)(boxY + boxHeight + datum<int32_t>(x, 0x091FA21C)), 0,
                    (uint32_t)(int32_t)s16(x, 0x091FA210), (uint32_t)(int32_t)s16(x, 0x091FA212), (uint32_t)(int32_t)s16(x, 0x091FA214), (uint32_t)(int32_t)s16(x, 0x091FA216));
    }
}
SwapReg reg_DrawMessage({"libfield.DrawMessage", MODULE, offsetOf(0x091D73A0), swap_DrawMessage});

}  // namespace
