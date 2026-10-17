/* Duel Result: drawing. Everything here records GE display lists through ehsys; coordinates are screen pixels (480x272) and the table
 * is cut from the sheet menu01.gim (kTableSheet): corner, edge and centre sprites addressed by their u/v on the sheet. */
#include "result_internal.h"
#include <string.h>

namespace duel_mgr {

uint32_t DuelResult::GetResource(uint16_t index) {
    ResultScreen &s = screen();
    if (s.resources[index] == 0)
        s.resources[index] = ehsys::FindTexture(x_, s.archive, x_.at<uint32_t>(moduleAddress(kResourceNameTable) + index * 4u), 0);
    return s.resources[index];
}

uint32_t DuelResult::DrawNumberFrom(uint32_t returnAddress, uint32_t cur, int32_t x, int32_t y, int32_t value, uint32_t advance, uint32_t maxDigits, bool padZeros) {
    const uint32_t saved = x_.c->r[31];
    x_.c->r[31] = returnAddress;
    const uint32_t next = x_.callLocal(fn::DrawNumber - kModuleBase, cur, (uint32_t)x, (uint32_t)y, (uint32_t)value, advance, maxDigits, padZeros);
    x_.c->r[31] = saved;
    return next;
}

uint32_t DuelResult::DrawNumber(uint32_t cur, int32_t x, int32_t y, int32_t value, uint32_t advance, uint32_t maxDigits, bool padZeros) {
    GuestStack scratch(x_, 16);
    uint16_t *glyph = (uint16_t *)x_.mem(scratch.addr());
    glyph[0] = x_.at<uint16_t>(moduleAddress(kDigitBufferInit));
    glyph[1] = x_.at<uint16_t>(moduleAddress(kDigitBufferInit) + 2);
    uint32_t left = (uint32_t)x & 0xffff;   /* the position wraps at 16 bits, as the engine's halfwords do */
    const int32_t row = (int16_t)y;
    const bool negative = value < 0;
    if (negative) value = -value;
    ehsys::BeginText(x_, cur);
    int32_t remaining = (int32_t)(maxDigits & 0xffff) - 1;
    while (remaining > -1) {
        left = (left - advance) & 0xffff;
        glyph[0] = (uint16_t)((int16_t)(value % 10) + 0x30);
        ehsys::DrawText(x_, (int32_t)(left << 6), row << 6, (int32_t)(kScreenWidth - left) * 0x40, scratch.addr());
        value /= 10;
        if (!padZeros && value == 0) break;
        remaining--;
    }
    if (negative) {   /* the minus sign goes just left of the last digit */
        const uint32_t signX = (left - (((advance & 0xffff) + 2) & 0xffff)) & 0xffff;
        glyph[0] = (uint16_t)-0xf3;
        ehsys::DrawText(x_, (int32_t)(signX << 6), row << 6, (int32_t)(kScreenWidth - signX) * 0x40, scratch.addr());
    }
    return ehsys::EndText(x_);
}

uint32_t DuelResult::DrawCategoryRow(uint32_t cur, int32_t x, int32_t y, int32_t category) {
    const uint32_t sheet = GetResource(kTableSheet);
    if (sheet == 0) return cur;
    uint32_t u = BindSheet(x_, cur, sheet);
    /* the row: left cap, right cap, stretched middle, then the amount box on the right */
    u = ehsys::SetSpriteSize(x_, u, 1, 0x10, 1, 0x10);
    u = ehsys::DrawSprite(x_, u, x, y, 0, 0x17, 0, 0, 0);
    u = ehsys::DrawSprite(x_, u, x + 0x10c, y, 0, 0x19, 0, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 1, 0x10, 0x10b, 0x10);
    u = ehsys::DrawSprite(x_, u, x + 1, y, 0, 0x18, 0, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 1, 0x10, 1, 0x10);
    u = ehsys::DrawSprite(x_, u, x + 0x10f, y, 0, 0x1a, 0, 0, 0);
    u = ehsys::DrawSprite(x_, u, x + 0x157, y, 0, 0x1c, 0, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 1, 0x10, 0x47, 0x10);
    cur = ehsys::DrawSprite(x_, u, x + 0x110, y, 0, 0x1b, 0, 0, 0);
    if (category < 0 || category >= kRewardCount) return cur;   /* an unused row stays empty */

    ehsys::BeginText(x_, cur);
    ehsys::SetFontSize(x_, 0xe, 0xe);
    ehsys::SetTextColor(x_, 0xff000000);
    const uint16_t nameIndex = x_.at<uint16_t>(moduleAddress(kCategoryNameStrings) + category * 2u);
    ehsys::DrawText(x_, (x + 1) * 0x40, (y + 2) * 0x40, (kScreenWidth - (x + 1)) * 0x40, Text(nameIndex));
    ehsys::SetTextColor(x_, 0xff00ffff);
    ehsys::SetFontSize(x_, 0xc, 0xc);
    ehsys::DrawText(x_, (x + 0x145) * 0x40, (y + 4) * 0x40, (kScreenWidth - (x + 0x145)) * 0x40, moduleAddress(kDpLabel));
    const uint32_t afterText = ehsys::EndText(x_);
    ehsys::SetFontSize(x_, 0xe, 0xf);
    ehsys::SetTextColor(x_, kWhite);
    return DrawNumberFrom(0x08f0b5d8, afterText, x + 0x144, y + 1, screen().rewards[category], 10, 5, false);
}

uint32_t DuelResult::DrawCategories(uint32_t cur, int32_t slide) {
    const uint32_t sheet = GetResource(kTableSheet);
    if (sheet == 0) return cur;
    uint32_t u = BindSheet(x_, cur, sheet);
    const int32_t left = slide + 0x3e;
    /* top row of the frame, then the bottom row, then the side walls and the four inner corners */
    u = ehsys::SetSpriteSize(x_, u, 10, 0x14, 10, 0x14);
    u = ehsys::DrawSprite(x_, u, left, 0x4a, 0, 0, 0, 0, 0);
    u = ehsys::DrawSprite(x_, u, slide + 0x199, 0x4a, 0, 0xd, 0, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 1, 0x14, 0x106, 0x14);
    u = ehsys::DrawSprite(x_, u, slide + 0x48, 0x4a, 0, 10, 0, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 3, 0x14, 3, 0x14);
    u = ehsys::DrawSprite(x_, u, slide + 0x14e, 0x4a, 0, 0xf, 0, 0, 0);
    u = ehsys::DrawSprite(x_, u, slide + 0x151, 0x4a, 0, 0x11, 0, 1, 0);
    u = ehsys::SetSpriteSize(x_, u, 1, 0x14, 0x45, 0x14);
    u = ehsys::DrawSprite(x_, u, slide + 0x154, 0x4a, 0, 10, 0, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 10, 0x1b, 10, 0x1b);
    u = ehsys::DrawSprite(x_, u, left, 0xa9, 0, 0, 0x14, 0, 0);
    u = ehsys::DrawSprite(x_, u, slide + 0x199, 0xa9, 0, 0xd, 0x14, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 1, 0x1b, 0x151, 0x1b);
    u = ehsys::DrawSprite(x_, u, slide + 0x48, 0xa9, 0, 10, 0x14, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 6, 1, 6, 0x4b);
    u = ehsys::DrawSprite(x_, u, left, 0x5e, 0, 0, 0x14, 0, 0);
    u = ehsys::DrawSprite(x_, u, slide + 0x19d, 0x5e, 0, 0x11, 0x14, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 2, 2, 2, 2);
    u = ehsys::DrawSprite(x_, u, slide + 0x42, 0x5d, 0, 0x17, 0x10, 0, 0);
    u = ehsys::DrawSprite(x_, u, slide + 0x19c, 0x5d, 0, 0x21, 0x10, 0, 0);
    u = ehsys::DrawSprite(x_, u, slide + 0x42, 0xa8, 0, 0x17, 0x1a, 0, 0);
    u = ehsys::DrawSprite(x_, u, slide + 0x19c, 0xa8, 0, 0x21, 0x1a, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 1, 2, 0x158, 2);
    u = ehsys::DrawSprite(x_, u, slide + 0x44, 0x5d, 0, 0x19, 0x10, 0, 0);
    u = ehsys::DrawSprite(x_, u, slide + 0x44, 0xa8, 0, 0x19, 0x10, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 2, 1, 2, 0x49);
    u = ehsys::DrawSprite(x_, u, slide + 0x42, 0x5f, 0, 0x17, 0x12, 0, 0);
    u = ehsys::DrawSprite(x_, u, slide + 0x19c, 0x5f, 0, 0x17, 0x12, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 1, 1, 0x158, 0x49);
    u = ehsys::DrawSprite(x_, u, slide + 0x44, 0x5f, 0, 0x19, 0x12, 0, 0);

    if (slide == 0) {   /* the page arrows, swinging with the frame counter */
        int16_t swing[4];
        for (int i = 0; i < 4; i++) swing[i] = x_.at<int16_t>(moduleAddress(kArrowOffsets) + i * 2u);
        u = ehsys::SetColor(x_, u, kWhite);
        u = ehsys::SetTextureFilter(x_, u, 0, 0);
        u = ehsys::SetSpriteSize(x_, u, 0xf, 0x17, 0xf, 0x17);
        u = ehsys::DrawSprite(x_, u, swing[(x_.at<uint32_t>(ehsys::FrameCounter(x_)) >> 2) & 2] + 0x18, 0x7b, 0, 0x90, 0x18, 0, 0);
        u = ehsys::DrawSprite(x_, u, 0x1bb - swing[(x_.at<uint32_t>(ehsys::FrameCounter(x_)) >> 2) & 2], 0x7b, 0, 0x9f, 0x18, 0, 0);
    }

    /* the rows: only the categories that earned something, packed from the top; the total is their sum */
    int32_t shown[kRewardCount] = {-1, -1, -1, -1};
    int32_t count = 0, total = 0;
    for (int category = 0; category < kRewardCount; category++) {
        if (screen().rewards[category] != 0) {
            shown[count++] = category;
            total += screen().rewards[category];
        }
    }
    for (int row = 0; row < kRewardCount; row++) u = DrawCategoryRow(u, slide + 0x44, row * 19 + 0x5f, shown[row]);

    /* the total row */
    u = BindSheet(x_, u, sheet);
    u = ehsys::SetSpriteSize(x_, u, 1, 0x10, 1, 0x10);
    u = ehsys::DrawSprite(x_, u, slide + 0x153, 0xb0, 0, 0x1a, 0, 0, 0);
    u = ehsys::DrawSprite(x_, u, slide + 0x19b, 0xb0, 0, 0x1c, 0, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 1, 0x10, 0x47, 0x10);
    u = ehsys::DrawSprite(x_, u, slide + 0x154, 0xb0, 0, 0x1b, 0, 0, 0);
    ehsys::BeginText(x_, u);
    ehsys::SetFontSize(x_, 0xe, 0xe);
    ehsys::SetTextColor(x_, kWhite);
    ehsys::DrawTextAligned(x_, (slide + 0x47) * 0x40, 0x1340, 0x41c0, Text(2));    /* the column titles */
    ehsys::DrawTextAligned(x_, (slide + 0x155) * 0x40, 0x1340, 0x11c0, Text(3));
    ehsys::DrawTextAligned(x_, (slide + 0x44) * 0x40, 0x2c40, 0x41c0, Text(8));    /* "Total" */
    ehsys::SetTextColor(x_, 0xff00ffff);
    ehsys::SetFontSize(x_, 0xc, 0xc);
    ehsys::DrawText(x_, (slide + 0x189) * 0x40, 0x2d00, (kScreenWidth - (slide + 0x189)) * 0x40, moduleAddress(kDpLabel));
    const uint32_t afterText = ehsys::EndText(x_);
    ehsys::SetFontSize(x_, 0xe, 0xf);
    ehsys::SetTextColor(x_, kWhite);
    return DrawNumberFrom(0x08f0d3b0, afterText, slide + 0x188, 0xb1, total, 10, 5, false);
}

uint32_t DuelResult::PlaySprites(uint32_t cur, uint32_t sprites) {
    GuestStack frameState(x_, 0x60);   /* the animation state: the frame number, the rest zero */
    memset(x_.mem(frameState.addr()), 0, 0x60);
    x_.at<uint32_t>(frameState.addr()) = x_.at<uint32_t>(ehsys::FrameCounter(x_));
    uint32_t u = ehsys::BeginSpriteDraw(x_, cur);
    u = ehsys::DrawSpriteResource(x_, u, sprites, frameState.addr());
    return ehsys::EndSpriteDraw(x_, u);
}

uint32_t DuelResult::DrawBackground(uint32_t cur) {
    const uint32_t sprites = GetResource(kBackgroundSprites);
    return sprites == 0 ? cur : PlaySprites(cur, sprites);
}

uint32_t DuelResult::DrawTitleBar(uint32_t cur) {
    const uint32_t sprites = GetResource(kTitleBarSprites);
    if (sprites == 0) return cur;
    uint32_t u = PlaySprites(cur, sprites);
    u = ehsys::EnableState(x_, u, ehsys::kBlend);
    u = ehsys::SetTextureFunction(x_, u, 0, 1);
    cur = ehsys::SetBlend(x_, u, 0, 2, 3, 0, 0);
    /* the capsule's animation frame decides whether the four tabs under it are drawn, and how: frames 6 and 0x61 are the dim ends */
    const int32_t frame = (int32_t)ehsys::GetSpriteFrameCount(x_, sprites);
    if (frame <= 5 || frame >= 0x62) return cur;
    const bool dim = frame == 6 || frame == 0x61;
    const uint32_t tabs = GetResource(kTabSheet);
    if (tabs == 0) return cur;
    u = ehsys::BindTexture(x_, cur, tabs);
    u = ehsys::SetTextureFilter(x_, u, 1, 1);
    int32_t y;
    if (dim) {
        u = ehsys::SetColor(x_, u, 0x80ffffff);
        cur = ehsys::SetSpriteSize(x_, u, 0x40, 0x15, 0x40, 0xb);
        y = 0xd;
    } else {
        u = ehsys::SetColor(x_, u, kWhite);
        cur = ehsys::SetSpriteSize(x_, u, 0x40, 0x15, 0x40, 0x15);
        y = 8;
    }
    for (int i = 0; i < 4; i++)
        cur = ehsys::DrawSprite(x_, cur, i * 0x40 + 0x70, y, 0, i * 0x40, (screen().titleState & 0xf) * 0x15, 0, 0);
    return cur;
}

}  // namespace duel_mgr

namespace duel_mgr {

uint32_t DuelResult::DrawPages(uint32_t cur) {
    ResultScreen &s = screen();
    auto drawPage = [&](uint32_t page, uint32_t at, int32_t slide) {
        return x_.callAddr(x_.at<uint32_t>(moduleAddress(kPageDrawTable) + page * 4u), at, (uint32_t)slide);
    };
    switch (s.phase) {
    case kPhaseWaitForArchive:
        return cur;
    case kPhaseFadeIn: case kPhaseTitleBarOpening: case kPhasePages: case kPhaseAnswer:
        return drawPage(s.page & 0xf, cur, 0);
    case kPhaseSliding: {   /* the old page slides out of one side while the new one slides in from the other */
        const int32_t slid = (int32_t)((uint32_t)s.slide * kScreenWidth) >> 4;
        const uint32_t direction = (s.flags & 0x7f) >> 5;
        const uint32_t fromPage = (s.flags & 0x1f) >> 1;
        if (direction == 1) {
            const uint32_t u = drawPage(fromPage, cur, -slid);
            return drawPage(s.page & 0xf, u, kScreenWidth - slid);
        }
        if (direction == 0) {
            const uint32_t u = drawPage(s.page & 0xf, cur, slid - kScreenWidth);
            return drawPage(fromPage, u, slid);
        }
        return cur;
    }
    case kPhaseBannerIn: case kPhaseWaitButton: case kPhaseBannerOut: case kPhaseFadeOut:
        return DrawOutcomePages(cur);
    default:
        return cur;
    }
}

uint32_t DuelResult::DrawFooter(uint32_t cur) {
    ResultScreen &s = screen();
    uint32_t u = ehsys::DisableState(x_, cur, ehsys::kTexture2D);
    u = ehsys::SetTextureFunction(x_, u, 4, 0);
    u = ehsys::SetColor(x_, u, 0xa0000000);   /* the dark bar */
    u = ehsys::SetSpriteSize(x_, u, 0, 0, 0x1e0, 0x11);
    u = ehsys::DrawSprite(x_, u, 0, 0xff, 0, 0, 0, 0, 0);
    u = ehsys::EnableState(x_, u, ehsys::kTexture2D);
    u = ehsys::SetTextureFunction(x_, u, 0, 1);
    u = ehsys::SetColor(x_, u, kWhite);

    /* the names of the neighbouring pages (the page before and after the current one that have something to show) and the hint in
     * the middle; half = how much of the bar the middle text may use */
    uint32_t previousText = 0, nextText = 0, middleText = 0;
    int32_t half = 0;
    auto pageName = [&](uint32_t page) { return Text(x_.at<uint16_t>(moduleAddress(kPageNameStrings) + page * 2u)); };
    switch (s.phase) {
    case kPhaseWaitForArchive: case kPhaseSliding:
        return u;
    case kPhaseFadeIn: case kPhaseTitleBarOpening: case kPhasePages: case kPhaseAnswer: {
        const uint32_t page = s.page & 0xf;
        for (int i = 1; i < 4; i++) {
            const uint32_t other = (page + 4 - i) & 3;
            if (other == 0) { previousText = Text(0xf6); break; }
            if (s.rewards[other - 1] > 0) { previousText = pageName(other); break; }
        }
        for (int i = 1; i < 4; i++) {
            const uint32_t other = (page + i) & 3;
            if (other == 0) { nextText = Text(0xf6); break; }
            if (s.rewards[other - 1] > 0) { nextText = pageName(other); break; }
        }
        middleText = Text(0xfa);
        half = 0x78;
        break;
    }
    case kPhaseBannerIn: case kPhaseWaitButton: case kPhaseBannerOut: case kPhaseFadeOut: {
        const int16_t outcome = GetDuelOutcome();
        middleText = Text(outcome == 0 ? 0xfb : 0xfa);
        half = 0x1e0;
        break;
    }
    default:
        return u;
    }
    auto sideWidth = [&]() {   /* (screen width - half) / 2 - 16, in 1/64 pixel (the division rounds toward zero) */
        int32_t w = kScreenWidth - half;
        if (w < 0) w = 0x1e1 - half;
        return ((w >> 1) - 0x10) * 0x40;
    };
    ehsys::SetFontSize(x_, 0xe, 0xe);
    ehsys::SetTextColor(x_, kWhite);
    ehsys::BeginText(x_, u);
    if (previousText != 0) ehsys::DrawTextAligned(x_, 0x400, 0x4040, sideWidth(), previousText);
    if (middleText != 0) {
        const int32_t width = ehsys::TextWidth(x_, middleText);
        int32_t halfText = (width >> 6) + 1;
        if (halfText < 0) halfText = (width >> 6) + 2;
        ehsys::DrawTextAligned(x_, (0xf0 - (halfText >> 1)) * 0x40, 0x4040, half << 6, middleText);
    }
    if (nextText != 0) {
        const int32_t room = sideWidth();
        ehsys::SetFontSize(x_, ehsys::FitFontSize(x_, room, nextText) & 0xff, 0xe);   /* shrunk to fit */
        const int32_t width = ehsys::TextWidth(x_, nextText);
        ehsys::DrawTextAligned(x_, (0x1d0 - ((width >> 6) + 1)) * 0x40, 0x4040, room, nextText);
        ehsys::SetFontSize(x_, 0xe, 0xe);
    }
    u = ehsys::EndText(x_);
    if (previousText == 0 && nextText == 0) return u;
    const uint32_t sheet = GetResource(kTableSheet);
    if (sheet == 0) return u;
    u = BindSheet(x_, u, sheet);
    u = ehsys::SetSpriteSize(x_, u, 9, 0xd, 9, 0xd);
    if (previousText != 0) u = ehsys::DrawSprite(x_, u, 2, 0x101, 0, 0xb8, 0x33, 0, 0);   /* the little arrows beside the names */
    if (nextText != 0) u = ehsys::DrawSprite(x_, u, 0x1d3, 0x101, 0, 0xc1, 0x33, 0, 0);
    return u;
}

void DuelResult::DrawScreen() {
    uint32_t cur = ehsys::BeginList(x_, 0);
    cur = ehsys::SetClearColor(x_, cur, 0xff000000);
    cur = ehsys::SetClearDepth(x_, cur, 0);
    cur = ehsys::SetClearStencil(x_, cur, 0);
    cur = ehsys::Clear(x_, cur, 0x17);
    ehsys::EndList(x_, cur);
    ResultScreen &s = screen();
    if (s.phase == kPhaseClosed || s.phase == kPhaseWaitForArchive) return;

    cur = ehsys::BeginList(x_, 4);
    cur = DrawBackground(cur);
    cur = DrawTitleBar(cur);
    cur = DrawPages(cur);
    if (s.flags & 1) cur = DrawBanner(cur);
    cur = DrawFooter(cur);
    GuestStack cursor(x_, 16);   /* the window and fade calls take the cursor by address and move it */
    x_.at<uint32_t>(cursor.addr()) = cur;
    if (s.windowState & 3) x_.callNid(ehsys::nid::DrawWindow, cursor.addr(), x_.at<uint32_t>(kResultScreenPointer) + offsetof(ResultScreen, window));
    ehsys::DrawFade(x_, cursor.addr());
    ehsys::EndList(x_, x_.at<uint32_t>(cursor.addr()));
}

}  // namespace duel_mgr
