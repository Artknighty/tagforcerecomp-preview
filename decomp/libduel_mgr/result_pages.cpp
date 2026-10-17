/* Duel Result: the three detail pages (what the duel earned in each category, ten lines at a time) and the pieces they are built from.
 * Page 0 of the screen is DrawCategories (result_draw.cpp); here are the pages the player reaches with L / R. */
#include "result_internal.h"
#include <string.h>

namespace duel_mgr {

namespace {
constexpr int32_t kListTop = 0x3d, kLineHeight = 0x10, kListLines = 10;   /* the list area: ten 16-pixel lines from y = 0x3d */
constexpr int32_t kListLeft = 0x17, kListWidth = 0x1aa;
}  // namespace

uint32_t DuelResult::DrawBox(uint32_t cur, int32_t x, int32_t y, int32_t w, int32_t h, int32_t sheetU, int32_t sheetV) {
    if (h == 0xe) {   /* a single line: left cap, right cap, stretched middle */
        uint32_t u = ehsys::SetSpriteSize(x_, cur, 1, 0xe, 1, 0xe);
        u = ehsys::DrawSprite(x_, u, x, y, 0, sheetU, sheetV, 0, 0);
        u = ehsys::DrawSprite(x_, u, x + w - 1, y, 0, sheetU + 2, sheetV, 0, 0);
        u = ehsys::SetSpriteSize(x_, u, 1, 0xe, w - 2, 0xe);
        return ehsys::DrawSprite(x_, u, x + 1, y, 0, sheetU + 1, sheetV, 0, 0);
    }
    uint32_t u = ehsys::SetSpriteSize(x_, cur, 2, 2, 2, 2);   /* corners, edges, centre */
    u = ehsys::DrawSprite(x_, u, x, y, 0, sheetU, sheetV, 0, 0);
    const int32_t right = x + w - 2, bottom = y + h - 2;
    u = ehsys::DrawSprite(x_, u, right, y, 0, sheetU + 1, sheetV, 0, 0);
    u = ehsys::DrawSprite(x_, u, x, bottom, 0, sheetU, sheetV + 0xc, 0, 0);
    u = ehsys::DrawSprite(x_, u, right, bottom, 0, sheetU + 1, sheetV + 0xc, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 1, 2, w - 4, 2);
    u = ehsys::DrawSprite(x_, u, x + 2, y, 0, sheetU + 1, sheetV, 0, 0);
    u = ehsys::DrawSprite(x_, u, x + 2, bottom, 0, sheetU + 1, sheetV + 0xc, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 2, 1, 2, h - 4);
    u = ehsys::DrawSprite(x_, u, x, y + 2, 0, sheetU, sheetV + 1, 0, 0);
    u = ehsys::DrawSprite(x_, u, right, y + 2, 0, sheetU + 1, sheetV + 1, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 1, 1, w - 4, h - 4);
    return ehsys::DrawSprite(x_, u, x + 2, y + 2, 0, sheetU + 1, sheetV + 1, 0, 0);
}

uint32_t DuelResult::DrawArrow(uint32_t cur, int32_t x, int32_t y, bool down, uint32_t period, bool blink, bool solid, uint32_t frame, uint32_t sheet) {
    const uint32_t half = period >> 1;
    const int32_t sheetV = down ? 8 : 0;
    uint32_t u = ehsys::BindTexture(x_, cur, sheet);
    u = ehsys::SetTextureFilter(x_, u, 0, 0);
    if (solid) {   /* the dim, always visible arrow */
        u = ehsys::SetColor(x_, u, kWhite);
        u = ehsys::SetSpriteSize(x_, u, 8, 8, 8, 8);
        u = ehsys::DrawSprite(x_, u, x, y, 0, 0xee, sheetV + 0x30, 0, 0);
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
        u = ehsys::DrawSprite(x_, u, x, y, 0, 0xee, sheetV, 0, 0);
    }
    return u;
}

uint32_t DuelResult::DrawScrollBar(uint32_t cur, int32_t x, int32_t y, int32_t h, int32_t position, int32_t visible, int32_t total) {
    const int32_t inner = h - 2;
    const uint32_t sheet = GetResource(kTableSheet);
    if (sheet == 0) return cur;
    int32_t thumb, thumbOffset;
    if (visible < total) {
        thumb = (visible * inner) / total;
        if (thumb < 10) thumb = 10;
        thumbOffset = (position * (inner - thumb)) / (total - visible);
        if (thumbOffset < 0) thumbOffset = 0;
        if (inner - thumb < thumbOffset) thumbOffset = inner - thumb;
    } else {
        thumbOffset = 0;
        thumb = inner;
    }
    uint32_t u = ehsys::SetTextureFilter(x_, cur, 0, 0);
    u = ehsys::SetColor(x_, u, kWhite);
    u = ehsys::BindTexture(x_, u, sheet);
    u = ehsys::SetSpriteSize(x_, u, 8, 5, 8, 5);   /* the track: both ends and the stretched middle */
    u = ehsys::DrawSprite(x_, u, x, y, 0, 0xf7, 0, 0, 0);
    u = ehsys::DrawSprite(x_, u, x, y + h - 5, 0, 0xf7, 0xf, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 8, 5, 8, h - 10);
    u = ehsys::DrawSprite(x_, u, x, y + 5, 0, 0xf7, 5, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 4, 5, 4, 5);   /* the thumb */
    const int32_t top = y + thumbOffset;
    u = ehsys::DrawSprite(x_, u, x + 2, top + 2, 0, 0xf7, 0x28, 0, 0);
    u = ehsys::DrawSprite(x_, u, x + 2, thumb + top - 3, 0, 0xf7, 0x37, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 4, 5, 4, thumb - 10);
    u = ehsys::DrawSprite(x_, u, x + 2, top + 7, 0, 0xf7, 0x2d, 0, 0);
    /* the arrows above and below blink when there is more to scroll to */
    const uint32_t counter = ehsys::FrameCounter(x_);
    u = DrawArrow(u, x, y - 7, false, x_.at<uint32_t>(counter + 4), position > 0, true, x_.at<uint32_t>(counter), sheet);
    return DrawArrow(u, x, y + h + 2, true, x_.at<uint32_t>(counter + 4), position < total - visible, true, x_.at<uint32_t>(counter), sheet);
}

void DuelResult::DrawFittedText(int32_t x, int32_t y, int32_t width, uint32_t fontWidth, uint32_t fontHeight, uint32_t text) {
    ehsys::SetFontSize(x_, fontWidth, fontHeight);
    ehsys::SetFontSize(x_, ehsys::FitFontSize(x_, (uint32_t)(width << 6), text) & 0xff, fontHeight);
    int32_t room = width - ((ehsys::TextWidth(x_, text) >> 6) + 1);
    if (room < 0) room++;
    x += room >> 1;
    ehsys::DrawText(x_, x * 0x40, y << 6, (kScreenWidth - x) * 0x40, text);
}

uint32_t DuelResult::DrawPageFrame(uint32_t cur, int32_t slide, int32_t sheetU, int32_t sheetV) {
    const uint32_t sheet = GetResource(kTableSheet);
    if (sheet == 0) return cur;
    uint32_t u = BindSheet(x_, cur, sheet);
    const int32_t left = slide + 0x11;
    u = ehsys::SetSpriteSize(x_, u, 9, 0x14, 9, 0x14);   /* the frame: top row, bottom row, side walls, inner corners */
    u = ehsys::DrawSprite(x_, u, left, 0x28, 0, sheetU, sheetV, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 0xd, 0x14, 0xd, 0x14);
    u = ehsys::DrawSprite(x_, u, slide + 0x1c3, 0x28, 0, sheetU + 10, sheetV, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 1, 0x14, 0x1ab, 0x14);
    u = ehsys::DrawSprite(x_, u, slide + 0x1a, 0x28, 0, sheetU + 0xb, sheetV, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 7, 0x1a, 7, 0x1a);
    u = ehsys::DrawSprite(x_, u, left, 0xdd, 0, sheetU, sheetV + 0x15, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 0xe, 0x1a, 0xe, 0x1a);
    u = ehsys::DrawSprite(x_, u, slide + 0x1c2, 0xdd, 0, sheetU + 9, sheetV + 0x15, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 1, 0x1a, 0x1aa, 0x1a);
    u = ehsys::DrawSprite(x_, u, slide + 0x18, 0xdd, 0, sheetU + 7, sheetV + 0x15, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 7, 1, 7, 0xa1);
    u = ehsys::DrawSprite(x_, u, left, 0x3c, 0, sheetU, sheetV + 0x14, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 0xd, 1, 0xd, 0xa1);
    u = ehsys::DrawSprite(x_, u, slide + 0x1c3, 0x3c, 0, sheetU + 10, sheetV + 0x14, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 2, 2, 2, 2);
    u = ehsys::DrawSprite(x_, u, slide + 0x15, 0x3b, 0, sheetU + 0x17, sheetV + 0x11, 0, 0);
    u = ehsys::DrawSprite(x_, u, slide + 0x15, 0xdb, 0, sheetU + 0x17, sheetV + 0x1b, 0, 0);
    u = ehsys::DrawSprite(x_, u, slide + 0x1c1, 0x3b, 0, sheetU + 8, sheetV + 0x1e, 0, 0);
    u = ehsys::DrawSprite(x_, u, slide + 0x1c1, 0x3b, 0, sheetU + 0x21, sheetV + 0x11, 0, 0);
    u = ehsys::DrawSprite(x_, u, slide + 0x1c1, 0xdb, 0, sheetU + 8, sheetV + 0x1e, 0, 0);
    u = ehsys::DrawSprite(x_, u, slide + 0x1c1, 0xdb, 0, sheetU + 0x21, sheetV + 0x1b, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 1, 2, 0x1aa, 2);
    u = ehsys::DrawSprite(x_, u, slide + 0x17, 0x3b, 0, sheetU + 0x19, sheetV + 0x11, 0, 0);
    u = ehsys::DrawSprite(x_, u, slide + 0x17, 0xdb, 0, sheetU + 0x19, sheetV + 0x11, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 2, 1, 2, 0x9e);
    u = ehsys::DrawSprite(x_, u, slide + 0x15, 0x3d, 0, sheetU + 0x17, sheetV + 0x13, 0, 0);
    u = ehsys::DrawSprite(x_, u, slide + 0x1c1, 0x3d, 0, sheetU + 0x17, sheetV + 0x13, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 1, 1, 0x1aa, 0x9e);
    u = ehsys::DrawSprite(x_, u, slide + 0x17, 0x3d, 0, sheetU + 0x19, sheetV + 0x13, 0, 0);
    return DrawBox(u, slide + 0x18a, 0xe4, 0x37, 0xe, sheetU + 0x1a, sheetV);
}

uint32_t DuelResult::DrawPageSummary(uint32_t cur, int32_t slide, int32_t page) {
    ResultScreen &s = screen();
    const EntryList &list = s.lists[page + 1];
    ehsys::SetFontSize(x_, 0xe, 0xe);
    ehsys::BeginText(x_, cur);
    ehsys::SetTextColor(x_, kWhite);
    const int32_t textWidth = (kScreenWidth - (slide + 0x1a)) * 0x40;
    const int32_t textLeft = (slide + 0x1a) * 0x40;
    ehsys::DrawText(x_, textLeft, 0xac0, textWidth, Text(2));
    ehsys::DrawText(x_, textLeft, 0x3940, textWidth, Text(x_.at<uint16_t>(moduleAddress(kPageTitleStrings) + page * 2u)));
    ehsys::SetTextColor(x_, 0xff00ffff);
    ehsys::SetFontSize(x_, 0xb, 0xb);
    ehsys::DrawText(x_, (slide + 0x1af) * 0x40, 0x39c0, (kScreenWidth - (slide + 0x1af)) * 0x40, moduleAddress(kDpLabel));
    uint32_t u = ehsys::EndText(x_);
    ehsys::SetFontSize(x_, 0xc, 0xd);
    ehsys::SetTextColor(x_, kWhite);
    u = DrawNumberFrom(0x08f0b8b8, u, slide + 0x1af, 0xe5, s.rewards[page], 9, 5, false);
    int32_t linesAbove = 0, linesTotal = 0;   /* the scroll bar works in text lines, not entries */
    for (int i = 0; i < list.count; i++) {
        const int32_t lines = x_.at<uint16_t>(list.widths + i * 2u);
        linesTotal += lines;
        if (i < list.scroll) linesAbove += lines;
    }
    u = DrawScrollBar(u, slide + 0x1c5, 0x45, 0x8e, linesAbove, 10, linesTotal);
    if (slide == 0) {   /* the page arrows, swinging with the frame counter */
        int16_t swing[4];
        for (int i = 0; i < 4; i++) swing[i] = x_.at<int16_t>(moduleAddress(kSummaryArrowOffsets) + i * 2u);
        u = ehsys::SetColor(x_, u, kWhite);
        u = ehsys::SetTextureFilter(x_, u, 0, 0);
        u = ehsys::SetSpriteSize(x_, u, 0xf, 0x17, 0xf, 0x17);
        u = ehsys::DrawSprite(x_, u, swing[(x_.at<uint32_t>(ehsys::FrameCounter(x_)) >> 2) & 2] + 1, 0x84, 0, 0x90, 0x18, 0, 0);
        u = ehsys::DrawSprite(x_, u, 0x1d2 - swing[(x_.at<uint32_t>(ehsys::FrameCounter(x_)) >> 2) & 2], 0x84, 0, 0x9f, 0x18, 0, 0);
    }
    return u;
}

namespace {
/* The part every detail page shares: clip to the list area (the area slides with the page) and draw ten lines of entries. */
template <class DrawEntry>
uint32_t DrawEntryList(SwapCtx &x, uint32_t cur, int32_t slide, const EntryList &list, DrawEntry drawEntry) {
    int32_t clip = slide + kListLeft;
    if (clip < 0) clip = 0;
    if (clip > kScreenWidth) clip = kScreenWidth;
    int32_t width = kListWidth;
    if (kScreenWidth - clip < kListWidth) width = kScreenWidth - clip;
    uint32_t u = cur;
    if (clip > 0 && clip < kScreenWidth) {
        u = ehsys::SetScissor(x, u, clip, kListTop, width, 0xa0);
        u = ehsys::EnableState(x, u, ehsys::kScissorTest);
    }
    int32_t n = 0;
    for (int32_t row = 0; row < kListLines; n++) {
        int32_t entry = list.scroll + n;
        if (entry >= list.count) entry = -1;
        u = drawEntry(u, slide + kListLeft, row * kLineHeight + kListTop, entry);
        row += entry < 0 ? 1 : x.at<uint16_t>(list.widths + entry * 2u);
    }
    return ehsys::DisableState(x, u, ehsys::kScissorTest);
}
}  // namespace

uint32_t DuelResult::DrawDuelBonusPage(uint32_t cur, int32_t slide) {
    uint32_t u = DrawPageFrame(cur, slide, 0x48, 0);
    u = DrawBox(u, slide + 0x1a, 0x2b, 0x177, 0xe, 0x50, 3);
    u = DrawBox(u, slide + 0x194, 0x2b, 0x2d, 0xe, 0x65, 0);
    ehsys::SetFontSize(x_, 0xe, 0xe);
    ehsys::SetTextColor(x_, 0xff000000);
    ehsys::BeginText(x_, u);
    ehsys::DrawText(x_, (slide + 0x1a0) * 0x40, 0xb00, (kScreenWidth - (slide + 0x1a0)) * 0x40, moduleAddress(kDpLabel));
    u = ehsys::EndText(x_);
    u = DrawPageSummary(u, slide, 0);
    return DrawEntryList(x_, u, slide, screen().lists[1], [&](uint32_t at, int32_t x, int32_t y, int32_t entry) { return DrawDuelBonusEntry(at, x, y, entry); });
}

uint32_t DuelResult::DrawHighScorePage(uint32_t cur, int32_t slide) {
    uint32_t u = DrawPageFrame(cur, slide, 0x24, 0);
    u = DrawBox(u, slide + 0x1a, 0x2b, 0x143, 0xe, 0x2c, 3);
    u = DrawBox(u, slide + 0x15f, 0x2b, 0x3a, 0xe, 0x3e, 0);
    u = DrawBox(u, slide + 0x19b, 0x2b, 0x26, 0xe, 0x41, 0);
    ehsys::SetTextColor(x_, kWhite);
    ehsys::BeginText(x_, u);
    DrawFittedText(slide + 0x15f, 0x2b, 0x3a, 0xe, 0xe, Text(0xf2));
    ehsys::SetFontSize(x_, 0xe, 0xe);
    ehsys::SetTextColor(x_, 0xff000000);
    ehsys::DrawText(x_, (slide + 0x1a4) * 0x40, 0xb00, (kScreenWidth - (slide + 0x1a4)) * 0x40, moduleAddress(kDpLabel));
    u = ehsys::EndText(x_);
    u = DrawPageSummary(u, slide, 1);
    return DrawEntryList(x_, u, slide, screen().lists[2], [&](uint32_t at, int32_t x, int32_t y, int32_t entry) { return DrawHighScoreEntry(at, x, y, entry); });
}

uint32_t DuelResult::DrawChallengePage(uint32_t cur, int32_t slide) {
    uint32_t u = DrawPageFrame(cur, slide, 0x6c, 0);
    u = DrawBox(u, slide + 0x1a, 0x2b, 0x17f, 0xe, 0x74, 3);
    u = DrawBox(u, slide + 0x19b, 0x2b, 0x26, 0xe, 0x89, 0);
    ehsys::SetFontSize(x_, 0xe, 0xe);
    ehsys::SetTextColor(x_, 0xff000000);
    ehsys::BeginText(x_, u);
    ehsys::DrawText(x_, (slide + 0x1a4) * 0x40, 0xb00, (kScreenWidth - (slide + 0x1a4)) * 0x40, moduleAddress(kDpLabel));
    u = ehsys::EndText(x_);
    u = DrawPageSummary(u, slide, 2);
    return DrawEntryList(x_, u, slide, screen().lists[3], [&](uint32_t at, int32_t x, int32_t y, int32_t entry) { return DrawChallengeEntry(at, x, y, entry); });
}

/* The name of an entry, vertically centred in its row, and the "DP" label that follows; y becomes the row's centre. Returns the text cursor. */
uint32_t DuelResult::DrawEntryLabel(uint32_t cur, int32_t x, int32_t &y, int32_t height, uint32_t nameTable, uint32_t item, int32_t nameWidth) {
    ehsys::SetTextFlags(x_, 0x40);
    ehsys::SetFontSize(x_, 0xe, 0xe);
    ehsys::BeginText(x_, cur);
    ehsys::SetTextColor(x_, 0xff000000);
    const uint16_t label = x_.at<uint16_t>(moduleAddress(nameTable) + item * 2u);
    ehsys::DrawTextBox(x_, (x + 2) * 0x40, y << 6, nameWidth, (height + 2) * 0x40, Text(label >> 1), kWhite);
    if (height < 0) height++;
    y += height >> 1;
    ehsys::SetTextColor(x_, 0xff00ffff);
    ehsys::SetFontSize(x_, 0xb, 0xb);
    ehsys::DrawText(x_, (x + 0x198) * 0x40, (y - 4) * 0x40, (kScreenWidth - (x + 0x198)) * 0x40, moduleAddress(kDpLabel));
    return ehsys::EndText(x_);
}

uint32_t DuelResult::DrawDuelBonusEntry(uint32_t cur, int32_t x, int32_t y, int32_t entry) {
    ResultScreen &s = screen();
    const uint32_t sheet = GetResource(kTableSheet);
    if (sheet == 0) return cur;
    uint32_t u = BindSheet(x_, cur, sheet);
    const EntryList &list = s.lists[1];
    int32_t height = entry < 0 ? 0xe : x_.at<uint16_t>(list.widths + entry * 2u) * 0x10 - 2;
    const uint32_t item = x_.at<uint16_t>(list.items + entry * 2u);
    u = DrawBox(u, x, y, 0x17a, height, 0x5f, 0);
    cur = DrawBox(u, x + 0x17c, y, 0x2e, height, 0x62, 0);
    if (entry < 0) return cur;
    const uint32_t afterText = DrawEntryLabel(cur, x, y, height, kEntryNames[0], item, 0x5d80);
    ehsys::SetFontSize(x_, 0xc, 0xd);
    ehsys::SetTextColor(x_, kWhite);
    int32_t points = statistics().bonusPoints[item];   /* the DP this entry earned */
    if (points > 999) points = 999;
    cur = DrawNumberFrom(0x08f0bcd8, afterText, x + 0x198, y - 6, points, 9, 3, false);
    ehsys::SetTextFlags(x_, 0);
    return cur;
}

uint32_t DuelResult::DrawHighScoreEntry(uint32_t cur, int32_t x, int32_t y, int32_t entry) {
    ResultScreen &s = screen();
    const uint32_t sheet = GetResource(kTableSheet);
    if (sheet == 0) return cur;
    uint32_t u = BindSheet(x_, cur, sheet);
    const EntryList &list = s.lists[2];
    int32_t height = entry < 0 ? 0xe : x_.at<uint16_t>(list.widths + entry * 2u) * 0x10 - 2;
    const uint32_t item = x_.at<uint16_t>(list.items + entry * 2u);
    u = DrawBox(u, x, y, 0x146, height, 0x3b, 0);
    u = DrawBox(u, x + 0x148, y, 0x3a, height, 0x3e, 0);
    u = ehsys::SetSpriteSize(x_, u, 0xc, 0xc, 0xc, 0xc);
    u = ehsys::DrawSprite(x_, u, x + 0x14a, y + 1, 0, 0x3b, 0x1d, 0, 0);
    cur = DrawBox(u, x + 0x184, y, 0x26, height, 0x3e, 0);
    if (entry < 0) return cur;
    uint32_t afterText = DrawEntryLabel(cur, x, y, height, kEntryNames[1], item, 0x5080);
    ehsys::SetFontSize(x_, 0xc, 0xd);
    ehsys::SetTextColor(x_, kWhite);
    afterText = DrawNumberFrom(0x08f0c1a0, afterText, x + 0x198, y - 6, 0x14, 9, 3, false);   /* every high score earns 20 DP */
    ehsys::SetTextFlags(x_, 0);
    ehsys::SetTextColor(x_, kWhite);
    int32_t record = statistics().highScores[item];   /* the record itself (the number, or a time in seconds for entry 0x25) */
    if (record > 999999999) record = 999999999;
    if (item == 0x25) {
        uint16_t separator[2];
        separator[0] = x_.at<uint16_t>(moduleAddress(kTimeSeparator));
        separator[1] = x_.at<uint16_t>(moduleAddress(kTimeSeparator) + 2);
        ehsys::SetFontSize(x_, 0xb, 0xd);
        afterText = DrawNumberFrom(0x08f0c248, afterText, x + 0x180, y - 6, record % 0x3c, 8, 2, true);   /* seconds */
        GuestStack text(x_, 16);
        memcpy(x_.mem(text.addr()), separator, 4);
        ehsys::BeginText(x_, afterText);
        ehsys::DrawText(x_, (x + 0x16b) * 0x40, (y - 7) * 0x40, (kScreenWidth - (x + 0x16b)) * 0x40, text.addr());
        afterText = ehsys::EndText(x_);
        return DrawNumberFrom(0x08f0c2a8, afterText, x + 0x168, y - 6, record / 0x3c, 8, 2, false);   /* minutes */
    }
    if (record < 100000) {
        ehsys::SetFontSize(x_, 0xb, 0xd);
        return DrawNumberFrom(0x08f0c2f4, afterText, x + 0x180, y - 6, record, 8, 5, false);
    }
    ehsys::SetFontSize(x_, 6, 0xd);
    return DrawNumberFrom(0x08f0c32c, afterText, x + 0x182, y - 6, record, 5, 9, false);
}

uint32_t DuelResult::DrawChallengeEntry(uint32_t cur, int32_t x, int32_t y, int32_t entry) {
    ResultScreen &s = screen();
    const uint32_t sheet = GetResource(kTableSheet);
    if (sheet == 0) return cur;
    uint32_t u = BindSheet(x_, cur, sheet);
    const EntryList &list = s.lists[3];
    int32_t height = entry < 0 ? 0xe : x_.at<uint16_t>(list.widths + entry * 2u) * 0x10 - 2;
    const uint16_t item = x_.at<uint16_t>(list.items + entry * 2u);
    u = DrawBox(u, x, y, 0x182, height, 0x83, 0);
    cur = DrawBox(u, x + 0x184, y, 0x26, height, 0x86, 0);
    if (entry < 0) return cur;
    const uint32_t afterText = DrawEntryLabel(cur, x, y, height, kEntryNames[2], item, 0x5f80);
    ehsys::SetFontSize(x_, 0xc, 0xd);
    ehsys::SetTextColor(x_, kWhite);
    cur = DrawNumberFrom(0x08f0c730, afterText, x + 0x198, y - 6, 0x1e, 9, 2, false);   /* every challenge earns 30 DP */
    ehsys::SetTextFlags(x_, 0);
    return cur;
}

/* One duelist's name on a plate of the sheet: left cap, right cap, stretched middle, a small mark (which side of the board) and the name. */
uint32_t DuelResult::DrawNamePlate(uint32_t cur, int32_t x, int32_t y, bool lower, uint32_t name) {
    const uint32_t sheet = GetResource(kTableSheet);
    if (sheet == 0) return cur;
    uint32_t u = BindSheet(x_, cur, sheet);
    u = ehsys::SetSpriteSize(x_, u, 3, 0x12, 3, 0x12);
    u = ehsys::DrawSprite(x_, u, x, y, 0, 0xd8, 0, 0, 0);
    u = ehsys::DrawSprite(x_, u, x + 0x7f, y, 0, 0xe1, 0, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 1, 0x12, 0x7c, 0x12);
    u = ehsys::DrawSprite(x_, u, x + 3, y, 0, 0xdb, 0, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 0xb, 0x11, 0xb, 0x11);
    u = ehsys::DrawSprite(x_, u, x + 4, y + 1, 0, lower ? 0xad : 0xa2, 0x2f, 0, 0);
    ehsys::SetFontSize(x_, 0xe, 0xe);
    ehsys::SetTextColor(x_, 0xff000000);
    ehsys::BeginText(x_, u);
    ehsys::DrawTextAligned(x_, (x + 0x11) * 0x40, (y + 3) * 0x40, 0x1bc0, name);
    return ehsys::EndText(x_);
}

/* A result badge (win, lose or draw): a small frame with the picture on top and the caption under it. */
uint32_t DuelResult::DrawResultBadge(uint32_t cur, int32_t x, int32_t y, int16_t kind) {
    const uint32_t sheet = GetResource(kTableSheet);
    if (sheet == 0) return cur;
    uint32_t u = BindSheet(x_, cur, sheet);
    u = ehsys::SetSpriteSize(x_, u, 2, 2, 2, 2);   /* corners, edges, centre */
    u = ehsys::DrawSprite(x_, u, x, y, 0, 0x17, 0x1c, 0, 0);
    u = ehsys::DrawSprite(x_, u, x + 0x35, y, 0, 0x18, 0x1c, 0, 0);
    u = ehsys::DrawSprite(x_, u, x, y + 0x24, 0, 0x17, 0x1d, 0, 0);
    u = ehsys::DrawSprite(x_, u, x + 0x35, y + 0x24, 0, 0x18, 0x1d, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 1, 2, 0x33, 2);
    u = ehsys::DrawSprite(x_, u, x + 2, y, 0, 0x18, 0x1c, 0, 0);
    u = ehsys::DrawSprite(x_, u, x + 2, y + 0x24, 0, 0x18, 0x1d, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 2, 1, 2, 0x22);
    u = ehsys::DrawSprite(x_, u, x, y + 2, 0, 0x17, 0x1d, 0, 0);
    u = ehsys::DrawSprite(x_, u, x + 0x35, y + 2, 0, 0x18, 0x1d, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 1, 1, 0x33, 0x22);
    u = ehsys::DrawSprite(x_, u, x + 2, y + 2, 0, 0x18, 0x1d, 0, 0);
    int32_t pictureU, captionV;
    if (kind == 3) { pictureU = 0xc0; captionV = 0x36; }
    else if (kind == 2) { pictureU = 0xa8; captionV = 0x6c; }
    else if (kind == 1) { pictureU = 0x90; captionV = 0; }
    else return u;
    u = ehsys::SetSpriteSize(x_, u, 0x18, 0x18, 0x18, 0x18);
    u = ehsys::DrawSprite(x_, u, x + 0xf, y, 0, pictureU, 0, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 0x36, 0x10, 0x36, 0x10);
    return ehsys::DrawSprite(x_, u, x + 1, y + 0x16, 0, captionV, 0x2f, 0, 0);
}

/* Phases 6..9 (story duels): the outcome board. Two (or four) name plates, and for each of the three rounds the badge of each side. */
uint32_t DuelResult::DrawOutcomePages(uint32_t cur) {
    const uint32_t sheet = GetResource(kTableSheet);
    if (sheet == 0) return cur;
    uint32_t u = BindSheet(x_, cur, sheet);
    u = ehsys::SetSpriteSize(x_, u, 10, 0x14, 10, 0x14);   /* the board's frame: top, bottom, side walls, inner corners */
    u = ehsys::DrawSprite(x_, u, 0x52, 0x4a, 0, 0, 0, 0, 0);
    u = ehsys::DrawSprite(x_, u, 0x185, 0x4a, 0, 0xd, 0, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 1, 0x14, 0x129, 0x14);
    u = ehsys::DrawSprite(x_, u, 0x5c, 0x4a, 0, 10, 0, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 10, 0x12, 10, 0x12);
    u = ehsys::DrawSprite(x_, u, 0x52, 0xa9, 0, 0, 0x1d, 0, 0);
    u = ehsys::DrawSprite(x_, u, 0x185, 0xa9, 0, 0xd, 0x1d, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 1, 0x12, 0x129, 0x12);
    u = ehsys::DrawSprite(x_, u, 0x5c, 0xa9, 0, 10, 0x1d, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 4, 1, 4, 0x4b);
    u = ehsys::DrawSprite(x_, u, 0x52, 0x5e, 0, 0, 0x14, 0, 0);
    u = ehsys::DrawSprite(x_, u, 0x18b, 0x5e, 0, 0x13, 0x14, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 1, 1, 0x135, 0x27);
    u = ehsys::DrawSprite(x_, u, 0x56, 0x5e, 0, 10, 0x14, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 1, 1, 0x135, 0x1c);
    u = ehsys::DrawSprite(x_, u, 0x56, 0x8e, 0, 10, 0x14, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 3, 9, 3, 9);
    u = ehsys::DrawSprite(x_, u, 0x56, 0x85, 0, 4, 0x14, 0, 0);
    u = ehsys::DrawSprite(x_, u, 0x188, 0x85, 0, 0x10, 0x14, 0, 0);
    u = ehsys::SetSpriteSize(x_, u, 1, 9, 0x12f, 9);
    u = ehsys::DrawSprite(x_, u, 0x59, 0x85, 0, 7, 0x14, 0, 0);
    ehsys::BeginText(x_, u);   /* the title and the three round captions */
    ehsys::SetFontSize(x_, 0xe, 0xe);
    ehsys::SetTextColor(x_, kWhite);
    ehsys::DrawText(x_, 0x1700, 0x1340, 0x6100, Text(0xfd));
    ehsys::SetFontSize(x_, 0xe, 0xe);
    DrawFittedText(0xdc, 0x4d, 0x37, 0xe, 0xe, Text(0xfe));
    ehsys::SetFontSize(x_, 0xe, 0xe);
    DrawFittedText(0x117, 0x4d, 0x37, 0xe, 0xe, Text(0xff));
    ehsys::SetFontSize(x_, 0xe, 0xe);
    DrawFittedText(0x152, 0x4d, 0x37, 0xe, 0xe, Text(0x100));
    u = ehsys::EndText(x_);
    const uint32_t first = (ehsys::GetNetworkScene(x_) && ehsys::IsNetworkHost(x_)) ? 1 : 0;   /* in a network duel the host's side is the other one */
    const uint32_t other = (first + 1) & 1;
    auto player = [&](uint32_t n) { return ehsys::GetSettingsBlock(x_) + kPlayerRecordBase + n * kPlayerRecordSize; };
    if (settings().teamMode == kTeamModeFour) {   /* two duelists per side */
        u = DrawNamePlate(u, 0x57, 0x5f, true, player(other));
        u = DrawNamePlate(u, 0x57, 0x73, true, player(other + 2));
        u = DrawNamePlate(u, 0x57, 0x8e, false, player(first));
        u = DrawNamePlate(u, 0x57, 0xa2, false, player(first + 2));
    } else {
        u = DrawNamePlate(u, 0x57, 0x69, true, player(other));
        u = DrawNamePlate(u, 0x57, 0x98, false, player(first));
    }
    for (int32_t i = 0; i < 3; i++) {
        const int16_t result = request().results[i];
        const int16_t opposite = result == 2 ? 1 : result == 1 ? 2 : result;   /* the upper side sees the opposite result */
        const int32_t x = i * 0x3b + 0xdc;
        u = DrawResultBadge(u, x, 0x5f, opposite);
        u = DrawResultBadge(u, x, 0x8e, result);
    }
    return u;
}

uint32_t DuelResult::DrawBanner(uint32_t cur) {
    const int16_t banner = screen().banner;
    if (banner < 0) return cur;
    const uint32_t sprites = GetResource((uint16_t)banner);
    if (sprites == 0) return cur;
    uint32_t u = PlaySprites(cur, sprites);
    u = ehsys::EnableState(x_, u, ehsys::kBlend);
    u = ehsys::SetTextureFunction(x_, u, 0, 1);
    return ehsys::SetBlend(x_, u, 0, 2, 3, 0, 0);
}

}  // namespace duel_mgr
