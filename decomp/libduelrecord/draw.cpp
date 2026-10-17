/* Duel Ranking: drawing. Everything here records GE display lists through ehsys; coordinates are in screen pixels (480x272) and
 * most boxes are built from a corner/edge/centre sprite set cut out of the screen's texture sheets (keys 0x1002 and 0x1003). */
#include "duelrecord.h"
#include "../ehsys.h"
#include "records.h"
#include <math.h>
#include <string.h>

namespace {
constexpr uint32_t kTexTabs = 0x1002;        /* window frame and tab headers */
constexpr uint32_t kTexParts = 0x1003;       /* panels, arrows, scroll bar, header */
constexpr uint32_t kTexTexts = 0x1020;       /* string table */
constexpr uint32_t kWhite = 0xffffffff;
constexpr uint32_t kFirstTabLabel = 0x10d;   /* string index of the first tab's name */
constexpr uint32_t kHeaderLabel = 1, kHeaderCaption = 0xfc;
}  // namespace

uint32_t DuelRecord::FillBackground(uint32_t cur) {
    cur = ehsys::SetClearColor(x_, cur, 0xff603b60);
    cur = ehsys::SetClearDepth(x_, cur, 0);
    cur = ehsys::SetClearStencil(x_, cur, 0);
    return ehsys::Clear(x_, cur, 0x17);
}

uint32_t DuelRecord::DrawHeader(uint32_t cur) {
    const uint32_t tex = GetTexture(kTexParts);
    if (tex == 0) return cur;
    uint32_t u = ehsys::BindTexture(x_, cur, tex);
    u = ehsys::SetColor(x_, u, kWhite);
    u = ehsys::SetTextureFilter(x_, u, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 0xc, 0x17, 0xc, 0x17);
    u = ehsys::DrawSprite(x_, u, 0, 0, 0, 0, 0x14, 0, 0);
    u = ehsys::DrawSprite(x_, u, 0x1d4, 0, 0, 0xd, 0x14, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 1, 0x17, 0x1c8, 0x17);
    u = ehsys::DrawSprite(x_, u, 0xc, 0, 0, 0xc, 0x14, 0, 0);
    ehsys::SetFontSize(x_, 0xe, 0xe);
    /* the caption box on the right is as wide as its text */
    const int32_t captionWidth = ehsys::TextWidth(x_, ehsys::GetText(x_, GetTexture(kTexTexts), kHeaderCaption)) >> 6;
    const int32_t left = -(captionWidth + 0x2e);
    u = ehsys::SetSpriteSize(x_, u, 4, 0x10, 4, 0x10);
    u = ehsys::DrawSprite(x_, u, left + 0x1db, 3, 0, 0x40, 0, 0, 0);
    const int32_t right = left + 0x1db + captionWidth + 0x2e;
    u = ehsys::DrawSprite(x_, u, right - 4, 3, 0, 0x4c, 0, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 1, 0x10, captionWidth + 0x26, 0x10);
    u = ehsys::DrawSprite(x_, u, left + 0x1df, 3, 0, 0x44, 0, 0, 0);
    ehsys::SetFontSize(x_, 0xe, 0xe);
    ehsys::SetTextColor(x_, kWhite);
    ehsys::BeginText(x_, u);
    ehsys::DrawText(x_, 0x200, 0x100, 0x7600, ehsys::GetText(x_, GetTexture(kTexTexts), kHeaderLabel));
    {   /* the percent sign, copied to the stack as the original does */
        GuestStack scratch(x_, 16);
        memcpy(x_.mem(scratch.addr()), x_.mem(x_.addr(0x6600)), 4);
        ehsys::DrawText(x_, (right - 0xd) * 0x40, 0x100, (0x1e0 - (right - 0xd)) * 0x40, scratch.addr());
    }
    ehsys::SetFontSize(x_, 0xc, 0xe);
    DrawNumber(right - 0x16, 4, state().completionPercent, 3, 9, false);
    ehsys::SetFontSize(x_, 0xe, 0xe);
    ehsys::DrawTextAligned(x_, (left + 0x1dc) * 0x40, 0x100, (captionWidth + 4) * 0x40, ehsys::GetText(x_, GetTexture(kTexTexts), kHeaderCaption));
    return ehsys::EndText(x_);
}

uint32_t DuelRecord::DrawTabs(uint32_t cur, int32_t x, int32_t y, int32_t w, int32_t h) {
    const uint32_t tex = GetTexture(kTexTabs);
    if (tex == 0) return cur;
    const uint32_t current = state().tabInfo & 0xf;
    uint32_t u = ehsys::BindTexture(x_, cur, tex);
    u = ehsys::SetColor(x_, u, kWhite);
    u = ehsys::SetTextureFilter(x_, u, 0, 0);
    const int32_t sheetV = (int32_t)current * 0x20;   /* each tab has its own frame colour on the sheet */
    const int32_t top = y + 0xf, bottom = y + h - 6, right = x + w - 0xe;
    u = ehsys::SetSpriteSize(x_, u, 6, 6, 6, 6);
    u = ehsys::DrawSprite(x_, u, x, top, 0, 0x40, sheetV, 0, 0);
    u = ehsys::DrawSprite(x_, u, x, bottom, 0, 0x40, sheetV + 0xb, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 0xe, 6, 0xe, 6);
    u = ehsys::DrawSprite(x_, u, right, top, 0, 0x62, sheetV, 0, 0);
    u = ehsys::DrawSprite(x_, u, right, bottom, 0, 0x62, sheetV + 0x1a, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 1, 6, w - 0x14, 6);
    u = ehsys::DrawSprite(x_, u, x + 6, top, 0, 0x46, sheetV, 0, 0);
    u = ehsys::DrawSprite(x_, u, x + 6, bottom, 0, 0x46, sheetV + 0xb, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 6, 1, 6, h - 0x1b);
    u = ehsys::DrawSprite(x_, u, x, y + 0x15, 0, 0x40, sheetV + 6, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 0xe, 1, 0xe, h - 0x1b);
    u = ehsys::DrawSprite(x_, u, right, y + 0x15, 0, 0x62, sheetV + 6, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 1, 1, w - 0x14, h - 0x1b);
    cur = ehsys::DrawSprite(x_, u, x + 6, y + 0x15, 0, 0x46, sheetV + 6, 0, 0);
    ehsys::SetFontSize(x_, 0xd, 0xd);

    /* the tab headers share the width; the selected one is bright and raised, the others dim and lowered */
    uint32_t count = state().tabInfo >> 4;
    int32_t tabV = 0;
    uint32_t previous = cur;
    for (uint32_t i = 0; i < count;) {
        const int32_t width = w / (int32_t)(count - i);
        w -= width;
        int32_t leftU = i == 0 ? 0 : 0x10;
        int32_t rightU = i == count - 1 ? 0x30 : 0x20;
        int32_t lower = 0;
        if (i == current) {
            u = ehsys::SetColor(x_, previous, kWhite);
            ehsys::SetTextColor(x_, kWhite);
        } else {
            leftU += 8;
            rightU += 8;
            lower = 2;
            u = ehsys::SetColor(x_, previous, 0x80ffffff);
            ehsys::SetTextColor(x_, 0xff808080);
        }
        u = ehsys::BindTexture(x_, u, tex);
        u = ehsys::SetTextureFilter(x_, u, 0, 0);
        u = ehsys::SetSpriteSize(x_, u, 8, 0x12, 8, 0x12);
        u = ehsys::DrawSprite(x_, u, x, y, 0, leftU, tabV, 0, 0);
        u = ehsys::DrawSprite(x_, u, x + width - 8, y, 0, rightU, tabV, 0, 0);
        u = ehsys::SetSpriteSize(x_, u, 1, 0x12, width - 0x10, 0x12);
        u = ehsys::DrawSprite(x_, u, x + 8, y, 0, leftU + 4, tabV, 0, 0);
        if (i != current) {
            u = ehsys::SetScissor(x_, u, x, y + lower, width - 8, 0xd);
            u = ehsys::EnableState(x_, u, 2);
        }
        ehsys::BeginText(x_, u);
        ehsys::DrawText(x_, (x + 4) * 0x40, (y + 3 + lower) * 0x40, (width - 8) * 0x40,
                        ehsys::GetText(x_, GetTexture(kTexTexts), i + kFirstTabLabel));
        u = ehsys::EndText(x_);
        cur = ehsys::DisableState(x_, u, 2);
        i++;
        x += width;
        count = state().tabInfo >> 4;
        tabV += 0x20;
        previous = cur;
    }
    return cur;
}

uint32_t DuelRecord::DrawPanel(uint32_t cur, int32_t x, int32_t y, int32_t w, int32_t h) {
    const uint32_t tex = GetTexture(kTexParts);
    if (tex == 0) return cur;
    uint32_t u = ehsys::BindTexture(x_, cur, tex);
    u = ehsys::SetColor(x_, u, kWhite);
    u = ehsys::SetTextureFilter(x_, u, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 6, 6, 6, 6);
    u = ehsys::DrawSprite(x_, u, x, y, 0, 0x19, 0x2f, 0, 0);
    const int32_t bottom = y + h - 6;
    u = ehsys::DrawSprite(x_, u, x, bottom, 0, 0x19, 0x3a, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 6, 6, 6, 6);
    const int32_t right = x + w - 6;
    u = ehsys::DrawSprite(x_, u, right, y, 0, 0x24, 0x2f, 0, 0);
    u = ehsys::DrawSprite(x_, u, right, bottom, 0, 0x24, 0x3a, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 1, 6, w - 0xc, 6);
    u = ehsys::DrawSprite(x_, u, x + 6, y, 0, 0x1f, 0x2f, 0, 0);
    u = ehsys::DrawSprite(x_, u, x + 6, bottom, 0, 0x1f, 0x3a, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 6, 1, 6, h - 0xc);
    u = ehsys::DrawSprite(x_, u, x, y + 6, 0, 0x19, 0x35, 0, 0);
    u = ehsys::DrawSprite(x_, u, right, y + 6, 0, 0x24, 0x35, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 1, 1, w - 0xc, h - 0xc);
    return ehsys::DrawSprite(x_, u, x + 6, y + 6, 0, 0x1f, 0x35, 0, 0);
}

void DuelRecord::DrawNumber(int32_t x, int32_t y, int32_t value, uint32_t digits, uint32_t advance, bool padZeros) {
    GuestStack scratch(x_, 16);
    uint16_t *text = (uint16_t *)x_.mem(scratch.addr());
    text[0] = text[1] = 0;
    digits &= 0xffff;
    advance &= 0xffff;
    if (digits == 0) return;
    int32_t px = x << 6;
    for (uint32_t n = 0; n < digits;) {
        text[0] = (uint16_t)((int16_t)(value % 10) + 0x30);
        ehsys::DrawText(x_, px, y << 6, (0x1e0 - x) * 0x40, scratch.addr());
        px -= (int32_t)advance * 0x40;
        x -= (int32_t)advance;
        value /= 10;
        if (!padZeros && value == 0) return;
        n++;
    }
}

uint32_t DuelRecord::DrawValue(uint32_t cur, int32_t x, int32_t y, uint32_t rowAddress) {
    const uint32_t tex = GetTexture(kTexParts);
    if (tex == 0) return cur;
    RankingRow &row = x_.at<RankingRow>(rowAddress);
    uint32_t u = ehsys::BindTexture(x_, cur, tex);
    u = ehsys::SetColor(x_, u, kWhite);
    u = ehsys::SetTextureFilter(x_, u, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 0xc, 0xc, 0xc, 0xc);
    u = ehsys::DrawSprite(x_, u, x, y + 1, 0, 0, 0, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 4, 0xd, 4, 0xd);
    u = ehsys::DrawSprite(x_, u, x + 0xd, y, 0, 0x40, 0x10, 0, 0);
    u = ehsys::DrawSprite(x_, u, x + 0x43, y, 0, 0x4c, 0x10, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 1, 0xd, 0x32, 0xd);
    u = ehsys::DrawSprite(x_, u, x + 0x11, y, 0, 0x44, 0x10, 0, 0);
    ehsys::BeginText(x_, u);
    ehsys::SetTextColor(x_, kWhite);
    if ((int32_t)row.value > 999999999) row.value = 999999999;   /* the box shows nine digits at most */
    const int32_t value = (int32_t)row.value;
    if ((state().tabInfo & 0xf) == kTabHighScore && row.item == 0x25) {   /* item 37 is a duration: minutes : seconds */
        ehsys::SetFontSize(x_, 0xc, 0xe);
        DrawNumber(x + 0x3d, y, value % 60, 2, 9, true);
        {
            GuestStack scratch(x_, 16);
            memcpy(x_.mem(scratch.addr()), x_.mem(x_.addr(0x6604)), 4);
            ehsys::DrawText(x_, (x + 0x2e) * 0x40, y << 6, (0x1e0 - (x + 0x2e)) * 0x40, scratch.addr());
        }
        DrawNumber(x + 0x22, y, value / 60, 2, 9, false);
    } else if (value < 1000000) {
        ehsys::SetFontSize(x_, 0xc, 0xe);
        DrawNumber(x + 0x3d, y, value, 6, 9, false);
    } else {   /* seven to nine digits: a narrower font */
        ehsys::SetFontSize(x_, 8, 0xe);
        DrawNumber(x + 0x40, y, value, 9, 6, false);
    }
    return ehsys::EndText(x_);
}

uint32_t DuelRecord::DrawRow(uint32_t cur, int32_t x, int32_t y, uint32_t rowAddress) {
    const uint32_t tex = GetTexture(kTexParts);
    if (tex == 0) return cur;
    RankingRow &row = x_.at<RankingRow>(rowAddress);
    uint32_t u = ehsys::BindTexture(x_, cur, tex);
    u = ehsys::SetColor(x_, u, kWhite);
    u = ehsys::SetTextureFilter(x_, u, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 0xc, 0x15, 0xc, 0x15);
    u = ehsys::DrawSprite(x_, u, x, y + 1, 0, 0, 0x2b, 0, 0);
    u = ehsys::DrawSprite(x_, u, x + 0x1c, y + 1, 0, 0xd, 0x2b, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 1, 0x15, 0x10, 0x15);
    u = ehsys::DrawSprite(x_, u, x + 0xc, y + 1, 0, 0xc, 0x2b, 0, 0);
    const int32_t height = (row.heightBits & 0x3f) * 0x18;
    u = DrawPanel(u, x + 0x2a, y, 0x1a3, height - 1);
    ehsys::SetTextFlags(x_, ehsys::GetLanguage(x_) == 0 ? 0x280 : 0x180);
    ehsys::BeginText(x_, u);
    ehsys::SetFontSize(x_, 0xc, 0xe);
    ehsys::SetTextColor(x_, kWhite);
    DrawNumber(x + 0x19, y + 4, row.order + 1, 3, 8, true);   /* row number */
    ehsys::SetFontSize(x_, 0xe, 0xe);
    ehsys::SetTextColor(x_, 0xff000000);
    const bool hasValue = (row.flags & 2) != 0;
    const int32_t labelWidth = hasValue ? 0x14a : 0x198;
    const uint32_t texts = GetTexture(kTexTexts);
    const uint32_t tab = state().tabInfo & 0xf;
    const uint32_t label = LabelIndex((const uint16_t *)x_.mem(x_.addr(kLabelTables[tab].offset)), kLabelTables[tab].labels, row.item);
    ehsys::DrawTextBox(x_, (x + 0x2f) * 0x40, (y + 4) * 0x40, labelWidth << 6, (height - 7) * 0x40, ehsys::GetText(x_, texts, label), kWhite);
    ehsys::SetTextFlags(x_, 0);
    u = ehsys::EndText(x_);
    if (hasValue) u = DrawValue(u, x + 0x17f, y + 5, rowAddress);
    if ((row.flags & 1) && (x_.at<uint32_t>(ehsys::FrameCounter(x_)) & 8)) {   /* the "new" mark blinks */
        u = ehsys::BindTexture(x_, u, tex);
        u = ehsys::SetColor(x_, u, kWhite);
        u = ehsys::SetTextureFilter(x_, u, 0, 0);
        u = ehsys::SetSpriteSize(x_, u, 0x16, 10, 0x16, 10);
        u = ehsys::DrawSprite(x_, u, x + 0x1b1, y + 2, 0, 0xd, 0, 0, 0);
    }
    return u;
}

uint32_t DuelRecord::DrawArrow(uint32_t cur, int32_t x, int32_t y, bool down, uint32_t period, bool blink, bool solid, uint32_t frame, uint32_t texture) {
    const uint32_t half = period >> 1;
    const int32_t sheetV = down ? 8 : 0;
    uint32_t u = ehsys::BindTexture(x_, cur, texture);
    u = ehsys::SetTextureFilter(x_, u, 0, 0);
    if (solid) {   /* the dim, always visible arrow */
        u = ehsys::SetColor(x_, u, kWhite);
        u = ehsys::SetSpriteSize(x_, u, 8, 8, 8, 8);
        u = ehsys::DrawSprite(x_, u, x, y, 0, 0x2e, sheetV + 0x30, 0, 0);
    }
    if (blink) {   /* the bright arrow fades in and out over `period` frames */
        const uint32_t phase = period ? frame % period : frame;
        float f;
        if (phase < half) f = (float)(int32_t)phase / (float)(int32_t)half;
        else f = (float)(int32_t)(period - phase) / (float)(int32_t)half;
        if (f < 0.0f) f = 0.0f;
        if (!(f <= 1.0f)) f = 1.0f;
        f *= 255.0f;
        const uint32_t alpha = f >= 2147483648.0f ? (uint32_t)(int32_t)(f - 2147483648.0f) | 0x80000000u : (uint32_t)(int32_t)f;
        u = ehsys::SetColor(x_, u, (alpha << 24) | 0xffffff);
        u = ehsys::SetSpriteSize(x_, u, 8, 8, 8, 8);
        u = ehsys::DrawSprite(x_, u, x, y, 0, 0x2e, sheetV, 0, 0);
    }
    return u;
}

uint32_t DuelRecord::DrawScrollBar(uint32_t cur, int32_t x, int32_t y, int32_t h, int32_t position, int32_t visible, int32_t total) {
    const uint32_t tex = GetTexture(kTexParts);
    if (tex == 0) return cur;
    const int32_t pos = position <= total ? position : total;
    int32_t thumb = (visible * (h - 2)) / (total + visible);
    if (thumb < 10) thumb = 10;
    int32_t thumbOffset = 0;
    if (total != 0) {
        thumbOffset = (pos * ((h - 2) - thumb)) / total;
        const int32_t room = (h - 2) - thumb;
        if (thumbOffset < 0) thumbOffset = 0;
        if (room < thumbOffset) thumbOffset = room;
    }
    uint32_t u = ehsys::SetTextureFilter(x_, cur, 0, 0);
    u = ehsys::SetColor(x_, u, kWhite);
    u = ehsys::BindTexture(x_, u, tex);
    u = ehsys::SetSpriteSize(x_, u, 8, 5, 8, 5);
    u = ehsys::DrawSprite(x_, u, x, y, 0, 0x37, 0, 0, 0);
    u = ehsys::DrawSprite(x_, u, x, y + h - 5, 0, 0x37, 0xf, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 8, 5, 8, h - 10);
    u = ehsys::DrawSprite(x_, u, x, y + 5, 0, 0x37, 5, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 4, 5, 4, 5);
    const int32_t top = y + thumbOffset;
    u = ehsys::DrawSprite(x_, u, x + 2, top + 2, 0, 0x37, 0x14, 0, 0);
    u = ehsys::DrawSprite(x_, u, x + 2, thumb + top - 3, 0, 0x37, 0x23, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 4, 5, 4, thumb - 10);
    u = ehsys::DrawSprite(x_, u, x + 2, top + 7, 0, 0x37, 0x19, 0, 0);
    uint32_t period = x_.at<uint32_t>(ehsys::FrameCounter(x_) + 4);
    uint32_t frame = x_.at<uint32_t>(ehsys::FrameCounter(x_));
    u = DrawArrow(u, x, y - 7, false, period, pos > 0, true, frame, tex);
    period = x_.at<uint32_t>(ehsys::FrameCounter(x_) + 4);
    frame = x_.at<uint32_t>(ehsys::FrameCounter(x_));
    return DrawArrow(u, x, y + h + 2, true, period, pos < total, true, frame, tex);
}

void DuelRecord::Draw() {
    RankingState &s = state();
    /* list 0: clear the screen */
    uint32_t cur = ehsys::BeginList(x_, 0);
    cur = ehsys::SetClearColor(x_, cur, 0xff000000);
    cur = ehsys::SetClearDepth(x_, cur, 0);
    cur = ehsys::SetClearStencil(x_, cur, 0);
    cur = ehsys::Clear(x_, cur, 0x17);
    ehsys::EndList(x_, cur);
    if (s.phase == 4 || s.phase == 0) return;

    /* list 4: the screen */
    cur = ehsys::BeginList(x_, 4);
    cur = ehsys::EnableState(x_, cur, 9);
    cur = ehsys::EnableState(x_, cur, 4);
    cur = ehsys::SetTextureFunction(x_, cur, 0, 1);
    cur = ehsys::SetTextureFilter(x_, cur, 0, 0);
    cur = ehsys::SetBlend(x_, cur, 0, 2, 3, 0, 0);
    cur = ehsys::DisableState(x_, cur, 1);
    cur = FillBackground(cur);
    cur = DrawHeader(cur);
    cur = DrawTabs(cur, 0, 0x17, 0x1e0, 0xf9);

    /* heights (in text lines) of the rows that can scroll off the top, and of those already scrolled off: for the scroll bar */
    int32_t scrollable = 0, scrolledOff = 0;
    const uint32_t tab = s.tabInfo & 0xf;
    const int32_t maxScroll = MaxScroll(tab);
    for (int32_t i = 0; i < maxScroll; i++) {
        const uint32_t row = i < (int32_t)s.rowCount[tab] ? s.rows[tab] + i * sizeof(RankingRow) : 0;
        if (row == 0) continue;
        const int32_t lines = x_.at<RankingRow>(row).heightBits & 0x3f;
        scrollable += lines;
        if (i < (int32_t)s.scroll[tab]) scrolledOff += lines;
    }
    cur = DrawScrollBar(cur, 0x1d4, 0x32, 0xd1, scrolledOff, 9, scrollable);

    /* the visible rows, clipped to the window */
    cur = ehsys::SetScissor(x_, cur, 0, 0x29, 0x1e0, 0xe3);
    cur = ehsys::EnableState(x_, cur, 2);
    int32_t lines = 0;
    for (int32_t i = 0; lines < 9; i++) {
        const int32_t index = s.scroll[tab] + i;
        if (index >= (int32_t)s.rowCount[tab]) break;
        const uint32_t row = s.rows[tab] + index * sizeof(RankingRow);
        if (row != 0) {
            cur = DrawRow(cur, 4, lines * 0x18 + 0x30, row);
            lines += x_.at<RankingRow>(row).heightBits & 0x3f;
        }
    }
    cur = ehsys::DisableState(x_, cur, 2);
    ehsys::EndList(x_, cur);

    /* list 5: the fade overlay */
    cur = ehsys::BeginList(x_, 5);
    cur = ehsys::DisableState(x_, cur, 1);
    {
        GuestStack scratch(x_, 16);
        x_.at<uint32_t>(scratch.addr()) = cur;
        ehsys::DrawFade(x_, scratch.addr());
        cur = x_.at<uint32_t>(scratch.addr());
    }
    ehsys::EndList(x_, cur);
}
