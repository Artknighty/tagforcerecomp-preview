/* ehsys window layout (BOOT): works out each window's size, runs its open / close animation and builds the sprite mesh of its frame. */
#include "boot.h"
#include "window.h"
#include <math.h>
#include <string.h>
#include <algorithm>

#define MODULE "BOOT"

namespace {
using namespace boot;

/* a rectangle of a sheet: u, v, width, height */
struct SheetRect { int16_t u, v, width, height; };
/* the two vertices of one sprite in the display list: the top left corner and the bottom right one (16 bytes each; the last two bytes are not written) */
struct SpriteVertex { int16_t u, v; uint32_t colour; int16_t x, y, z; };

constexpr uint32_t kFrameSprites[2] = {0x08873250, 0x088732C8};   /* the frame art of kinds 0..3 and 4..7: eight border pieces, then the filler column, then the tail */
constexpr uint32_t kFrameSheetOffsets[2][2] = {{0x088732C0, 0x088732C4}, {0x08873310, 0x08873314}};   /* per kind: how far its colour set is from the first, in u and in v (ints) */
constexpr uint32_t kTitleSprites = 0x088732A0;   /* the title strip's four pieces */
constexpr uint32_t kFillerWidth = 0x088ED150;    /* width of the filler column in the sheet (a short) */

/* the border pieces of a frame table, in the order the mesh uses them */
enum Piece : int { kTopLeft, kTopRight, kBottomLeft, kBottomRight, kTopEdge, kLeftEdge, kRightEdge, kBottomEdge, kFiller = 8, kTail = 9 };
const SheetRect &piece(SwapCtx &x, uint32_t table, int index) { return x.at<SheetRect>(table + index * sizeof(SheetRect)); }
uint32_t tableOf(uint32_t frameKind) { return kFrameSprites[frameKind < 4 ? 0 : 1]; }

/* ---- 088439cc, 08843808, 08843aa0 ---- */

/* FUN_088439cc(a, b, count): nonzero when the two byte ranges differ */
uint8_t rangesDiffer(const uint8_t *a, const uint8_t *b, uint32_t count) {
    uint8_t difference = 0;
    for (uint32_t i = 0; i < count; i++) difference |= a[i] ^ b[i];
    return difference;
}

/* How a sprite's texture coordinates are flipped: swapping the coordinates of its two vertices. */
enum Flip : int { kNoFlip = 0, kFlipU = 1, kFlipV = 2, kFlipBoth = 3 };

/* FUN_08843808: writes one sprite (two vertices) at `dst`: the piece `rect` of the sheet (offset by `sheetOffset`) drawn at (px, py, z) with the
 * size w x h, a negative size meaning the piece's own. */
void placeSprite(SwapCtx &x, uint32_t dst, const SheetRect &rect, int32_t px, int32_t py, int32_t z, int32_t w, int32_t h, uint32_t colour, const int32_t *sheetOffset, int flip) {
    if (w < 0) w = rect.width;
    if (h < 0) h = rect.height;
    SpriteVertex &first = x.at<SpriteVertex>(dst), &second = x.at<SpriteVertex>(dst + sizeof(SpriteVertex));
    first.u = (int16_t)(rect.u + (int16_t)sheetOffset[0]);
    first.v = (int16_t)(rect.v + (int16_t)sheetOffset[1]);
    first.colour = colour;
    first.x = (int16_t)px;
    first.y = (int16_t)py;
    first.z = (int16_t)z;
    second.u = (int16_t)((int16_t)sheetOffset[0] + rect.u + rect.width);
    second.v = (int16_t)((int16_t)sheetOffset[1] + rect.v + rect.height);
    second.colour = colour;
    second.x = (int16_t)(px + w);
    second.y = (int16_t)(py + h);
    second.z = (int16_t)z;
    if (flip & kFlipU) std::swap(first.u, second.u);
    if (flip & kFlipV) std::swap(first.v, second.v);
}

/* ---- 08843aa0 and the VFPU helpers 0881d3d8, 0881d5c0, 0881d334 ---- */

/* 0881d3d8: a colour as four floats, one per byte (lowest byte first): the byte is spread over a 31-bit integer (b * 0x01010101 >> 1) and scaled by 2^-31 */
void colourToVector(uint32_t colour, float out[4]) {
    for (int i = 0; i < 4; i++) {
        const uint32_t spread = (((colour >> (8 * i)) & 0xFF) * 0x01010101u) >> 1;
        out[i] = (float)(int32_t)spread * (1.0f / 2147483648.0f);
    }
}

/* 0881d334: four floats back to a colour: each is limited to 0..1, scaled by 255 and truncated through a 23-bit fraction (so a value of b / 255 gives b - 1
 * for most bytes: the engine's own rounding) */
uint32_t vectorToColour(const float in[4]) {
    uint32_t colour = 0;
    for (int i = 0; i < 4; i++) {
        const float limited = in[i] <= 0.0f ? 0.0f : (in[i] > 1.0f ? 1.0f : in[i]);
        const double scaled = (double)(limited * 255.0f) * 8388608.0;
        const int32_t fixed = scaled > 2147483647.0 ? 0x7FFFFFFF : (int32_t)trunc(scaled);
        colour |= (uint32_t)((fixed < 0 ? 0 : fixed >> 23) & 0xFF) << (8 * i);
    }
    return colour;
}

/* FUN_08843aa0(from, to, factor): the colour `factor` of the way from `from` to `to`, all four channels (0881d5c0 is the blend: from + (to - from) * factor).
 * The windows use it with `from` = the colour without alpha: the same colour with its alpha scaled. */
uint32_t blendColours(uint32_t from, uint32_t to, float factor) {
    float a[4], b[4], mixed[4];
    colourToVector(from, a);
    colourToVector(to, b);
    for (int i = 0; i < 4; i++) mixed[i] = a[i] + (b[i] - a[i]) * factor;
    return vectorToColour(mixed);
}

/* the animation's half-faded look of a colour: half its alpha */
uint32_t halfFaded(uint32_t colour) { return blendColours(colour & 0xFFFFFFu, colour, 0.5f); }

/* ---- 088435b4 ---- */

/* FUN_088435b4(item, pass): how many vertices the frame mesh has (16 for the eight borders, 8 for the title strip, two per row of the filler column,
 * two for the tail). Pass 1 is the animation's overlay, which has its own vertical scale. */
uint32_t frameVertexCount(SwapCtx &x, const WindowItem &item, int pass) {
    const WindowStyle &style = x.at<WindowStyle>(item.style);
    const float scaleY = pass == 0 ? style.scaleY : item.overlayScaleY;
    const uint32_t table = tableOf(style.frameKind);
    uint32_t count = 0;
    if (style.title != 0 && style.titleStrip != 0) count = 8;
    count += 16;
    const int32_t fillerHeight = ((int32_t)((float)style.height * scaleY) - piece(x, table, kTopLeft).height) - piece(x, table, kBottomLeft).height;
    for (int32_t row = 0; row * 8 < fillerHeight; row++) count += 2;
    if (style.tail != 0 && style.frameKind < 4) count += 2;
    return count;
}

/* ---- 08842c68 ---- */

/* FUN_08842c68(list, item, block, pass): build the frame's mesh in the display list's data: the eight border pieces, the filler column in rows of eight
 * pixels, the tail if there is one and the title strip. Pass 1 builds the overlay mesh from the item's overlay fields. */
void buildFrameMesh(SwapCtx &x, uint32_t listAddress, uint32_t itemAddress, int pass) {
    WindowItem &item = x.at<WindowItem>(itemAddress);
    const WindowStyle &style = x.at<WindowStyle>(item.style);
    const uint32_t count = frameVertexCount(x, item, pass);
    const uint32_t mesh = ehsys::ReserveListData(x, listAddress, count * sizeof(SpriteVertex));
    int32_t originX, originY, colour;
    float scaleX, scaleY;
    if (pass == 0) {
        item.sprites = mesh;
        originX = style.x; originY = style.y; scaleX = style.scaleX; scaleY = style.scaleY; colour = (int32_t)style.colour;
    } else {
        item.overlaySprites = mesh;
        originX = item.overlayX; originY = item.overlayY; scaleX = item.overlayScaleX; scaleY = item.overlayScaleY; colour = (int32_t)item.overlayColour;
    }
    const int32_t width = (int32_t)((float)style.width * scaleX), height = (int32_t)((float)style.height * scaleY);
    float origin[2], end[2];
    placeRect(origin, end, originX, originY, width, height, style.anchor);

    const uint32_t group = style.frameKind < 4 ? 0 : 1, table = kFrameSprites[group];
    const int32_t sheetOffset[2] = {x.at<int32_t>(kFrameSheetOffsets[group][0]) * style.frameKind, x.at<int32_t>(kFrameSheetOffsets[group][1]) * style.frameKind};
    const int32_t titleOffset[2] = {x.at<int32_t>(kFrameSheetOffsets[0][0]) * style.titleVariant, x.at<int32_t>(kFrameSheetOffsets[0][1]) * style.titleVariant};
    const int32_t z = style.priority + 1;
    uint32_t at = mesh;
    auto next = [&]() { const uint32_t current = at; at += 2 * sizeof(SpriteVertex); return current; };
    auto border = [&](int index, int32_t px, int32_t py, int32_t w, int32_t h) { placeSprite(x, next(), piece(x, table, index), px, py, z, w, h, colour, sheetOffset, kNoFlip); };
    const SheetRect &topLeft = piece(x, table, kTopLeft), &topRight = piece(x, table, kTopRight), &bottomLeft = piece(x, table, kBottomLeft), &bottomRight = piece(x, table, kBottomRight);
    const SheetRect &rightEdge = piece(x, table, kRightEdge), &bottomEdge = piece(x, table, kBottomEdge);

    border(kTopLeft, (int32_t)origin[0], (int32_t)origin[1], -1, -1);
    border(kTopRight, (int32_t)(end[0] - (float)topRight.width), (int32_t)origin[1], -1, -1);
    border(kBottomLeft, (int32_t)origin[0], (int32_t)(end[1] - (float)bottomLeft.height), -1, -1);
    border(kBottomRight, (int32_t)(end[0] - (float)bottomRight.width), (int32_t)(end[1] - (float)bottomRight.height), -1, -1);
    border(kTopEdge, (int32_t)(origin[0] + (float)topLeft.width), (int32_t)origin[1], (width - topLeft.width) - topRight.width, -1);
    border(kLeftEdge, (int32_t)origin[0], (int32_t)(origin[1] + (float)topLeft.height), -1, (height - topLeft.height) - bottomLeft.height);
    border(kRightEdge, (int32_t)(end[0] - (float)rightEdge.width), (int32_t)(origin[1] + (float)topRight.height), -1, (height - topRight.height) - bottomRight.height);
    border(kBottomEdge, (int32_t)(origin[0] + (float)bottomLeft.width), (int32_t)(end[1] - (float)bottomEdge.height), (width - bottomLeft.width) - bottomRight.width, -1);

    /* the filler column between the left and right edges, in rows of at most eight pixels */
    const int32_t fillerHeight = (height - topLeft.height) - bottomLeft.height;
    const SheetRect &filler = piece(x, table, kFiller);
    for (int32_t row = 0; row * 8 < fillerHeight; row++) {
        const int32_t rowHeight = fillerHeight < (row + 1) * 8 ? fillerHeight - row * 8 : 8;
        const SheetRect rowRect = {filler.u, filler.v, x.at<int16_t>(kFillerWidth), (int16_t)rowHeight};
        placeSprite(x, next(), rowRect, (int32_t)(origin[0] + (float)topLeft.width), (int32_t)((float)(row * 8) + origin[1] + (float)topLeft.height), z,
                    (width - topLeft.width) - topRight.width, rowHeight, colour, sheetOffset, kNoFlip);
    }

    if (style.tail != 0 && style.frameKind < 4) {   /* the little pointer: above the frame (flipped) for tails 3 and 4, below it for 1 and 2 */
        const SheetRect &tail = piece(x, table, kTail), &topEdge = piece(x, table, kTopEdge);
        int32_t tailY = (int32_t)(end[1] - (float)bottomLeft.height);
        int flip = kNoFlip;
        switch (style.tail) {
        case 4: flip = kFlipBoth; tailY = (int32_t)((float)topEdge.height + (origin[1] - (float)tail.height)); break;
        case 3: flip = kFlipV; tailY = (int32_t)((float)topEdge.height + (origin[1] - (float)tail.height)); break;
        case 2: flip = kFlipU; break;
        case 1: flip = kNoFlip; break;
        }
        placeSprite(x, next(), tail, (int32_t)(origin[0] + (float)style.tailOffset), tailY, z, -1, -1, colour, sheetOffset, flip);
    }

    if (style.title != 0 && style.titleStrip != 0) {   /* the title strip above the frame: left cap, right cap, middle, and the shadow under it */
        const int32_t insetLeft = x.at<int16_t>(kTitleInsetLeft), insetRight = x.at<int16_t>(kTitleInsetRight);
        const float stripTop = origin[1] - (float)style.titleBaseline;
        const int32_t stripY = (int32_t)stripTop;
        const int32_t stripWidth = (style.titleWidth - insetLeft) - insetRight;
        auto strip = [&](int index) -> const SheetRect & { return x.at<SheetRect>(kTitleSprites + index * sizeof(SheetRect)); };
        placeSprite(x, next(), strip(0), (int32_t)(origin[0] + (float)insetLeft), stripY, style.priority, stripWidth, -1, colour, titleOffset, kNoFlip);
        placeSprite(x, next(), strip(1), (int32_t)origin[0], stripY, style.priority, -1, -1, colour, titleOffset, kNoFlip);
        placeSprite(x, next(), strip(2), (int32_t)((origin[0] + (float)style.titleWidth) - (float)insetRight), stripY, style.priority, -1, -1, colour, titleOffset, kNoFlip);
        placeSprite(x, next(), strip(3), (int32_t)(origin[0] + (float)insetLeft), (int32_t)(stripTop + (float)x.at<int16_t>(kTitleInsetTop)), style.priority, stripWidth,
                    (int32_t)x.at<int16_t>(kTitleSprites + 8 + 6) - (int32_t)x.at<int16_t>(kTitleInsetTop), colour, titleOffset, kNoFlip);
    }
    const uint32_t used = (at - mesh) / sizeof(SpriteVertex);
    if (pass == 0) item.spriteCount = used;
    else item.overlayCount = used;
}

/* ---- 08841e9c ---- */

/* The text and title are measured with the window's own font: font size, then the two flags (put back afterwards). */
struct TextSettings {
    SwapCtx &x;
    uint32_t savedUiFlag, savedWide;
    TextSettings(SwapCtx &context, uint32_t fontSize, uint32_t uiFlag4, uint32_t wideScreenFlag) : x(context) {
        ehsys::SetFontSize(x, fontSize, fontSize);
        savedUiFlag = ehsys::GetUiFlag4(x);
        savedWide = ehsys::GetWideScreenFlag(x);
        ehsys::SetUiFlag4(x, uiFlag4);
        ehsys::SetWideScreenFlag(x, wideScreenFlag);
    }
    ~TextSettings() { ehsys::SetUiFlag4(x, savedUiFlag); ehsys::SetWideScreenFlag(x, savedWide); }
};

/* A style that changed since the last layout: work out the size from the body text (within the style's limits) and the title strip's width. */
void measureStyle(SwapCtx &x, WindowStyle &style) {
    constexpr int32_t kScreenWidth = 0x1E0, kScreenHeight = 0x110;
    const int16_t top = x.at<int16_t>(kInsetTop), left = x.at<int16_t>(kInsetLeft), right = x.at<int16_t>(kInsetRight), bottom = x.at<int16_t>(kInsetBottom);
    uint32_t titleOn = style.hasTitleText;
    if (style.fitToText != 0) {
        if (style.text != 0) {
            const int32_t paddingW = style.textRight + 2 + style.textLeft + left + right;
            const int32_t paddingH = style.textBottom + 2 + style.textTop + top + bottom;
            TextSettings settings(x, style.fontSize, style.uiFlag4, style.wideScreenFlag);
            const int32_t roomW = (style.maxWidth == 0 ? kScreenWidth : style.maxWidth) - paddingW;
            const int32_t roomH = (style.maxHeight == 0 ? kScreenHeight : style.maxHeight) - paddingH;
            const uint32_t size = ehsys::MeasureTextBox(x, roomW * 64, roomH * 64, style.text);
            style.width = (int16_t)(int32_t)((float)paddingW + ceilf((float)(size >> 16) / 64.0f));
            style.height = (int16_t)(int32_t)((float)paddingH + ceilf((float)(size & 0xFFFF) / 64.0f));
        }
        if ((int32_t)style.width < (int32_t)style.minWidth) style.width = (int16_t)style.minWidth;
        if ((int32_t)style.height < (int32_t)style.minHeight) style.height = (int16_t)style.minHeight;
        titleOn = style.hasTitleText;
    }
    if (titleOn != 0 && style.title != 0) {
        TextSettings settings(x, style.titleFontSize, style.uiFlag4, 0);
        const int32_t titleWidth = ehsys::TextWidth(x, style.title);
        const int32_t lineHeight = ehsys::GetLineHeight(x);
        const int16_t insetTop = x.at<int16_t>(kTitleInsetTop), insetLeft = x.at<int16_t>(kTitleInsetLeft), insetRight = x.at<int16_t>(kTitleInsetRight);
        style.titleWidth = (int16_t)(int32_t)((float)(int32_t)insetRight + (float)(int32_t)insetLeft + ceilf((float)titleWidth / 64.0f + 1.0f) + 4.0f);
        style.titleBaseline = (int16_t)(int32_t)((float)(int32_t)insetTop + ceilf((float)lineHeight / 64.0f + 1.0f) + -1.0f);
    }
}

/* The animation of a window opening (phase 1) or closing (phase 3) lasts five steps (0..4): the texts are hidden and half-faded, the window grows from
 * (or shrinks to) half its height with a ghost drawn over it (the overlay mesh), and at step 4 the phase moves on. `closing` picks the closing steps. */
void animateOpening(SwapCtx &x, WindowItem &item, WindowStyle &style) {
    const bool grows = style.openAnimation == 2;
    switch (style.openAnimation == 0 || style.openAnimation > 2 ? 99 : item.animationStep) {   /* other animations only count the steps */
    case 4: item.phase = 2; break;
    case 3: style.textColour = item.savedTextColour; style.titleColour = item.savedTitleColour; break;
    case 2:
        style.text = item.savedText;
        style.title = item.savedTitle;
        item.savedTextColour = style.textColour;
        style.textColour = halfFaded(style.textColour);
        item.savedTitleColour = style.titleColour;
        style.titleColour = halfFaded(style.titleColour);
        break;
    case 1:
        if (grows) style.y = item.savedY;
        style.scaleY = 1.0f;
        style.titleStrip = item.savedTitleStrip;
        item.hasOverlay = 0;
        break;
    case 0:
        style.visible = 1;
        if (grows) {
            item.savedY = style.y;
            style.y = (int16_t)(int32_t)((float)style.y + (float)style.height * 0.25f);
        }
        style.anchor = 0;
        style.scaleY = 0.5f;
        item.savedTitleStrip = style.titleStrip;
        style.titleStrip = 0;
        item.hasOverlay = 1;
        item.overlayX = style.x;
        item.overlayY = grows ? (int16_t)(int32_t)((float)item.savedY + (float)style.height * 0.125f) : style.y;
        item.overlayScaleX = 1.0f;
        item.overlayScaleY = 0.75f;
        item.overlayColour = halfFaded(style.colour);
        item.savedText = style.text;
        item.savedTitle = style.title;
        style.text = 0;
        style.title = 0;
        break;
    }
    item.animationStep++;
}

void animateClosing(SwapCtx &x, WindowItem &item, WindowStyle &style) {
    const bool shrinks = style.closeAnimation == 2;
    switch (style.closeAnimation == 0 || style.closeAnimation > 2 ? 99 : item.animationStep) {
    case 4: item.phase = 4; break;
    case 3:
        style.visible = 0;
        if (shrinks) style.y = item.savedY;
        style.titleStrip = item.savedTitleStrip;
        style.text = item.savedText;
        style.title = item.savedTitle;
        break;
    case 2:
        if (shrinks) {
            item.savedY = style.y;
            style.y = (int16_t)(int32_t)((float)style.y + (float)style.height * 0.25f);
        }
        style.anchor = 0;
        style.scaleY = 0.5f;
        item.savedTitleStrip = style.titleStrip;
        style.titleStrip = 0;
        item.hasOverlay = 1;
        item.overlayX = style.x;
        item.overlayY = shrinks ? (int16_t)(int32_t)((float)item.savedY + (float)style.height * 0.125f) : style.y;
        item.overlayScaleX = 1.0f;
        item.overlayScaleY = 0.75f;
        item.overlayColour = halfFaded(style.colour);
        break;
    case 1:
        item.savedText = style.text;
        item.savedTitle = style.title;
        style.text = 0;
        style.title = 0;
        style.textColour = item.savedTextColour;
        style.titleColour = item.savedTitleColour;
        break;
    case 0:
        item.savedTextColour = style.textColour;
        style.textColour = halfFaded(style.textColour);
        item.savedTitleColour = style.titleColour;
        style.titleColour = halfFaded(style.titleColour);
        break;
    }
    item.animationStep++;
}

enum ItemPhase : uint32_t { kNew = 0, kOpening = 1, kShown = 2, kClosing = 3, kClosed = 4 };

/* FUN_08841e9c(list, item, block): one frame of a window item's layout: start it, measure it when its style changed, step its animation, and
 * build the frame's mesh (and the overlay's). */
void layoutItem(SwapCtx &x, uint32_t listAddress, uint32_t itemAddress) {
    WindowItem &item = x.at<WindowItem>(itemAddress);
    WindowStyle &style = x.at<WindowStyle>(item.style);
    bool measure = false;
    switch (item.phase) {
    case kNew:
        if (style.scaleX == 0.0f) style.scaleX = 1.0f;
        if (style.scaleY == 0.0f) style.scaleY = 1.0f;
        style.visible = style.openAnimation == 0 ? 1 : 0;
        item.phase = kShown;
        measure = true;
        break;
    case kShown: case kClosed:
        measure = true;
        break;
    case kOpening:
        animateOpening(x, item, style);
        break;
    case kClosing:
        animateClosing(x, item, style);
        break;
    default:   /* closed windows still get their mesh built */
        break;
    }
    if (measure && rangesDiffer(x.mem(item.style), item.styleCopy, sizeof(WindowStyle))) measureStyle(x, style);
    buildFrameMesh(x, listAddress, itemAddress, 0);
    if (x.at<WindowItem>(itemAddress).hasOverlay != 0) buildFrameMesh(x, listAddress, itemAddress, 1);
}

/* ---- glue ---- */

void swap_FadeColour(SwapCtx &x) { x.ret(blendColours(x.arg(0), x.arg(1), x.c->f[12].f)); }   /* the factor comes in the float register f12 */
SwapReg reg_FadeColour({"BOOT.FadeColour", MODULE, fn::FadeColour, swap_FadeColour, true});


void swap_StyleDiffers(SwapCtx &x) { x.ret(rangesDiffer(x.mem(x.arg(0)), x.mem(x.arg(1)), x.arg(2))); }
SwapReg reg_StyleDiffers({"BOOT.StyleDiffers", MODULE, fn::StyleDiffers, swap_StyleDiffers, true});

void swap_FrameSpriteCount(SwapCtx &x) { x.ret(frameVertexCount(x, x.at<WindowItem>(x.arg(0)), (int)x.arg(1))); }
SwapReg reg_FrameSpriteCount({"BOOT.FrameVertexCount", MODULE, fn::FrameSpriteCount, swap_FrameSpriteCount, true});

void swap_BuildFrameMesh(SwapCtx &x) { buildFrameMesh(x, x.arg(0), x.arg(1), (int)x.arg(3)); }
SwapReg reg_BuildFrameMesh({"BOOT.BuildFrameMesh", MODULE, fn::BuildFrameMesh, swap_BuildFrameMesh});

void swap_LayoutWindowItem(SwapCtx &x) { layoutItem(x, x.arg(0), x.arg(1)); }
SwapReg reg_LayoutWindowItem({"BOOT.LayoutWindowItem", MODULE, fn::LayoutWindowItem, swap_LayoutWindowItem});

}  // namespace

namespace boot {
void layoutWindowItem(SwapCtx &x, uint32_t listAddress, uint32_t itemAddress, uint32_t) { layoutItem(x, listAddress, itemAddress); }
}  // namespace boot
