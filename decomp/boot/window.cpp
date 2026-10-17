/* ehsys window drawing (BOOT): how a window's frame, body text and title reach the display list. */
#include "../swap.h"
#include "../ehsys.h"
#include "../pc_style.h"
#include "boot.h"
#include "gfx_options.h"   /* runtime/: gfx_modern_menus() */
#include "window.h"
#include <math.h>

#define MODULE "BOOT"

namespace {
using namespace boot;
using boot::WindowItem;
using boot::WindowStyle;

constexpr uint32_t kSpritePrimitive = 6;          /* GU_SPRITES: two vertices per rectangle */
constexpr uint32_t kFrameVertexType = 0x80011E;   /* 16-bit texture coordinates, 8888 colour, 16-bit positions, no transform */
constexpr uint32_t kVertexColourOffset = 4;       /* after the two 16-bit texture coordinates */
constexpr uint32_t kWhite = 0xFFFFFFFFu;

/* ---- 0884368c ---- */

void swap_PlaceRect(SwapCtx &x) {
    float *origin = (float *)x.mem(x.arg(0)), *end = (float *)x.mem(x.arg(1));
    const int32_t px = (int32_t)x.arg(2), py = (int32_t)x.arg(3), width = (int32_t)x.arg(4), height = (int32_t)x.arg(5);
    const uint32_t anchor = x.arg(6);
    placeRect(origin, end, px, py, width, height, anchor);
}
void regions_PlaceRect(SwapCtx &x, Regions &out) { out.push_back({x.arg(0), 8}); out.push_back({x.arg(1), 8}); }
SwapReg reg_PlaceRect({"BOOT.PlaceRect", MODULE, fn::PlaceRect, swap_PlaceRect, false, regions_PlaceRect});

/* ---- 08842808 ---- */

/* The state the window's sprites are drawn in: alpha-tested, blended, no depth, texture modulated by the vertex colour, no filtering. The depth
 * buffer is cleared first (fast clear). */
uint32_t beginFrameDrawing(SwapCtx &x, uint32_t cur, uint32_t texture) {
    constexpr uint32_t kClearDepthFast = 0x14, kBlendAdd = 0, kBlendSrcAlpha = 2, kBlendOneMinusSrcAlpha = 3, kGreater = 6, kAlways = 7;
    constexpr uint32_t kTextureFormat8888 = 3, kModulate = 0, kRepeat = 0, kNearest = 0;
    cur = ehsys::SetClearStencil(x, cur, 0);
    cur = ehsys::Clear(x, cur, kClearDepthFast);
    cur = ehsys::SetBlend(x, cur, kBlendAdd, kBlendSrcAlpha, kBlendOneMinusSrcAlpha, 0, 0);
    cur = ehsys::EnableState(x, cur, ehsys::kBlend);
    cur = ehsys::AlphaFunc(x, cur, kGreater, 0, 0xFF);
    cur = ehsys::EnableState(x, cur, ehsys::kAlphaTest);
    cur = ehsys::DepthFunc(x, cur, kAlways);
    cur = ehsys::EnableState(x, cur, ehsys::kDepthTest);
    cur = ehsys::DepthMask(x, cur, 0);
    cur = ehsys::EnableState(x, cur, ehsys::kTexture2D);
    cur = ehsys::SetTextureMode(x, cur, kTextureFormat8888, 0, 0, 0);
    cur = ehsys::SetTextureFunction(x, cur, kModulate, 1);
    cur = ehsys::SetTextureWrap(x, cur, kRepeat, kRepeat);
    cur = ehsys::SetColor(x, cur, kWhite);
    cur = ehsys::SetTextureFilter(x, cur, kNearest, kNearest);
    return ehsys::BindTexture(x, cur, texture);
}

/* The window's body text, in the box inside the frame, with the window's own font size, flags and colour (the flags are put back afterwards). */
void drawBodyText(SwapCtx &x, const WindowStyle &style, const float *origin, uint32_t colour) {
    const int16_t left = x.at<int16_t>(kInsetLeft), top = x.at<int16_t>(kInsetTop), right = x.at<int16_t>(kInsetRight), bottom = x.at<int16_t>(kInsetBottom);
    const uint32_t savedUiFlag = ehsys::GetUiFlag4(x), savedWide = ehsys::GetWideScreenFlag(x);
    ehsys::SetFontSize(x, style.fontSize, style.fontSize);
    ehsys::SetUiFlag4(x, style.uiFlag4);
    ehsys::SetWideScreenFlag(x, style.wideScreenFlag);
    ehsys::SetTextColor(x, colour);
    const int32_t boxX = (int32_t)((float)(left + 1) + (origin[0] + (float)style.textLeft));
    const int32_t boxY = (int32_t)((float)(top + 1) + (origin[1] + (float)style.textTop));
    const int32_t boxW = ((-2 - right) - left) - style.textRight - style.textLeft + style.width;
    const int32_t boxH = ((-2 - bottom) - top) - style.textBottom - style.textTop + style.height;
    ehsys::DrawTextBox(x, boxX * 64, boxY * 64, boxW * 64, boxH * 64, style.text, kWhite);
    ehsys::SetUiFlag4(x, savedUiFlag);
    ehsys::SetWideScreenFlag(x, savedWide);
}

/* The title strip's text, on one line above the window's top edge. */
void drawTitleText(SwapCtx &x, const WindowStyle &style, const float *origin) {
    const int16_t top = x.at<int16_t>(kTitleInsetTop), left = x.at<int16_t>(kTitleInsetLeft), right = x.at<int16_t>(kTitleInsetRight);
    const uint32_t savedUiFlag = ehsys::GetUiFlag4(x), savedWide = ehsys::GetWideScreenFlag(x);
    ehsys::SetFontSize(x, style.titleFontSize, style.titleFontSize);
    ehsys::SetUiFlag4(x, style.uiFlag4);
    ehsys::SetWideScreenFlag(x, 0);
    ehsys::SetTextColor(x, style.titleColour);
    const int32_t textX = (int32_t)(2.0f + origin[0] + (float)left);
    const int32_t textY = (int32_t)((float)top + (origin[1] - (float)style.titleBaseline));
    const int32_t textW = ((style.titleWidth - 4) - left) - right;
    ehsys::DrawText(x, textX * 64, textY * 64, textW * 64, style.title);
    ehsys::SetUiFlag4(x, savedUiFlag);
    ehsys::SetWideScreenFlag(x, savedWide);
}

}  // namespace

namespace boot {
/* Which point of the rectangle (x, y) names: its top left corner, the top right / bottom left / bottom right corners, the middle of the top edge, the
 * middle of the left edge, or its centre. */
enum Anchor : uint32_t { kTopLeft, kTopRight, kBottomLeft, kBottomRight, kTopCentre, kLeftCentre, kCentre };

/* FUN_0884368c(origin, end, x, y, width, height, anchor): the corners of the rectangle in float pixels, `origin` the top left and `end` the bottom
 * right. An anchor beyond the list leaves `origin` as it was. */
void placeRect(float *origin, float *end, int32_t x, int32_t y, int32_t width, int32_t height, uint32_t anchor) {
    switch (anchor) {
    case kTopLeft: origin[0] = (float)x; origin[1] = (float)y; break;
    case kTopRight: origin[0] = (float)(x - width); origin[1] = (float)y; break;
    case kBottomLeft: origin[0] = (float)x; origin[1] = (float)(y - height); break;
    case kBottomRight: origin[0] = (float)(x - width); origin[1] = (float)(y - height); break;
    case kTopCentre: origin[0] = (float)x - (float)width * 0.5f; origin[1] = (float)y; break;
    case kLeftCentre: origin[0] = (float)x; origin[1] = (float)y - (float)height * 0.5f; break;
    case kCentre: origin[0] = (float)x - (float)width * 0.5f; origin[1] = (float)y - (float)height * 0.5f; break;
    }
    end[0] = origin[0] + (float)width;
    end[1] = origin[1] + (float)height;
}

/* A window of the PC style drawn flat, for the screens where libtitle's glass sheet is not loaded (the duel map's "End game?", the save messages, the
 * story map's place list...): a border rectangle and a fill rectangle drawn without a texture. The colours say which kind: dark blue for a window that
 * carries text (kDarkFill), the Free Duel's light glass for a list window (kLightFill). The frame's own mesh is left alone, so switching the style
 * off in the middle of a window still shows the game's frame. */
bool drawFlatWindow(SwapCtx &x, uint32_t &list, uint32_t texture, const float *origin, const float *end, uint32_t fade, const uint8_t *fill, const uint8_t *border) {
    const int32_t left = (int32_t)lroundf(origin[0]), top = (int32_t)lroundf(origin[1]), width = (int32_t)lroundf(end[0]) - left, height = (int32_t)lroundf(end[1]) - top;
    if (width <= 2 * kPanelBorderWidth || height <= 2 * kPanelBorderWidth) return false;
    auto abgr = [&](const uint8_t *c) { return fade << 24 | (uint32_t)c[2] << 16 | (uint32_t)c[1] << 8 | c[0]; };
    list = beginFrameDrawing(x, list, texture);   /* the texture is not used: only the state setup is wanted (alpha blending, no depth) */
    list = ehsys::DisableState(x, list, ehsys::kTexture2D);
    list = ehsys::SetBlendColor(x, list, abgr(border));
    list = ehsys::SetSpriteSize(x, list, 1, 1, width, height);
    list = ehsys::DrawSprite(x, list, left, top, 0, 0, 0, 0, 0);
    list = ehsys::SetBlendColor(x, list, abgr(fill));
    list = ehsys::SetSpriteSize(x, list, 1, 1, width - 2 * kPanelBorderWidth, height - 2 * kPanelBorderWidth);
    list = ehsys::DrawSprite(x, list, left + kPanelBorderWidth, top + kPanelBorderWidth, 0, 0, 0, 0, 0);
    list = ehsys::EnableState(x, list, ehsys::kTexture2D);
    list = ehsys::DisableState(x, list, ehsys::kDepthTest);
    return true;
}

/* FUN_08842808(list, item, block): draw one window of a block: its frame (the item's sprite mesh, and an optional second mesh over it), then the
 * body text and the title. `list` is the address of the display list cursor; `block` supplies the frame sheet. */
void drawWindowItem(SwapCtx &x, uint32_t listAddress, uint32_t itemAddress, uint32_t blockAddress) {
    const WindowItem &item = x.at<WindowItem>(itemAddress);
    const WindowStyle &style = x.at<WindowStyle>(item.style);
    const WindowBlock &block = x.at<WindowBlock>(blockAddress);
    uint32_t &list = x.at<uint32_t>(listAddress);

    float origin[2], end[2];
    placeRect(origin, end, style.x, style.y, (int32_t)((float)style.width * style.scaleX), (int32_t)((float)style.height * style.scaleY), style.anchor);

    /* the PC style draws a glass panel over the frame's rectangle instead of the frame's sprites; the first vertex's colour carries the fade */
    const uint32_t fade = item.spriteCount ? x.at<uint32_t>(item.sprites + kVertexColourOffset) >> 24 : 0xFF;
    const uint32_t sheet = style.texture ? style.texture : block.sheet;
    /* while the window opens or closes (phases 1 and 3) the layout hides the texts and the title strip and keeps them in the item: the animation must
     * look like the finished window, so a window with those saved texts is a dialog too (otherwise it flashed back to the game's colours) */
    const bool animating = item.phase == 1 || item.phase == 3;
    const bool hasText = style.text != 0 || (animating && item.savedText != 0), hasStrip = style.titleStrip != 0 || (animating && item.savedTitleStrip != 0);
    /* a title strip is part of the game's frame art and a tail (speech bubble) points somewhere: those keep the game's frame */
    const bool flatOk = gfx_modern_menus() && style.tail == 0 && !hasStrip;
    const bool dark = hasText && (pcStyleWindow(x, listAddress, origin[0], origin[1], end[0], end[1], fade, true) || (flatOk && drawFlatWindow(x, list, sheet, origin, end, fade, kDarkFill, kDarkBorder)));
    const bool restyled = dark || pcStyleWindow(x, listAddress, origin[0], origin[1], end[0], end[1], fade, false)
                          || (!hasText && flatOk && drawFlatWindow(x, list, sheet, origin, end, fade, kLightFill, kLightBorder));
    if (!restyled) {
        list = beginFrameDrawing(x, list, sheet);
        if (item.hasOverlay) list = ehsys::DrawArray(x, list, kSpritePrimitive, kFrameVertexType, item.overlayCount, 0, item.overlaySprites);
        list = ehsys::DrawArray(x, list, kSpritePrimitive, kFrameVertexType, item.spriteCount, 0, item.sprites);
        list = ehsys::DisableState(x, list, ehsys::kDepthTest);
    }

    ehsys::BeginText(x, list);
    if (style.text != 0) drawBodyText(x, style, origin, dark ? kWhite : style.textColour);   /* the dark panel needs light text, whatever the game's frame wanted */
    if (style.title != 0 && style.titleStrip != 0) drawTitleText(x, style, origin);
    list = ehsys::EndText(x);
}
}  // namespace boot

namespace {
void swap_DrawWindowItem(SwapCtx &x) {
    const uint32_t listAddress = x.arg(0), itemAddress = x.arg(1), blockAddress = x.arg(2);
    drawWindowItem(x, listAddress, itemAddress, blockAddress);
    x.ret(x.at<uint32_t>(listAddress));
}
SwapReg reg_DrawWindowItem({"BOOT.DrawWindowItem", MODULE, fn::DrawWindowItem, swap_DrawWindowItem});

}  // namespace
