/* Typed views over the window records of ehsys, the engine library every Module uses (it lives in BOOT). A window block (`libtitle::Window`,
 * 0x120 bytes) holds up to 16 window items; each item points at a style record that says where the window is, how large, and which text it shows.
 * Fields that are not understood yet are called `unknownNN` (their offset). */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "../swap.h"

namespace boot {

/* A window's look and text: the record `WindowItem::style` points at. Positions are in pixels of the 480 x 272 screen. */
struct WindowStyle {
    uint32_t visible;        /* +0x00 nonzero when the window is drawn */
    uint32_t texture;        /* +0x04 own texture of the window; 0 = the block's frame sheet */
    uint8_t frameKind;       /* +0x08 which frame art (0..7: two tables of eight sprites) */
    uint8_t titleVariant;    /* +0x09 which colour set of the sheet the title strip uses */
    uint8_t tail;            /* +0x0A the little pointer on the frame (speech bubble): 0 none, 1..4 which corner and side */
    uint8_t pad0B;
    int16_t x;               /* +0x0C */
    int16_t y;               /* +0x0E */
    int16_t priority;        /* +0x10 windows are drawn in increasing priority */
    uint8_t pad12[2];
    uint32_t fitToText;      /* +0x14 nonzero: the size follows the body text (within the limits below) */
    uint16_t maxWidth;       /* +0x18 0 = the screen's width */
    uint16_t maxHeight;      /* +0x1A 0 = the screen's height */
    uint16_t minWidth;       /* +0x1C */
    uint16_t minHeight;      /* +0x1E */
    int16_t width;           /* +0x20 */
    int16_t height;          /* +0x22 */
    uint32_t colour;         /* +0x24 ARGB of the frame */
    int16_t tailOffset;      /* +0x28 where the tail sits along the frame */
    uint8_t pad2A[2];
    uint32_t titleStrip;     /* +0x2C nonzero when the title strip is drawn above the frame */
    uint32_t hasTitleText;   /* +0x30 nonzero: the title text sets the strip's width */
    int16_t titleWidth;      /* +0x34 */
    int16_t titleBaseline;   /* +0x36 how far the title's text sits above the window's top edge */
    uint32_t text;           /* +0x38 guest address of the body text, 0 = none */
    uint32_t uiFlag4;        /* +0x3C the value SetUiFlag4 takes while the body text is drawn */
    uint32_t wideScreenFlag; /* +0x40 the same for SetWideScreenFlag */
    int16_t textLeft;        /* +0x44 body text margins inside the frame */
    int16_t textRight;       /* +0x46 */
    int16_t textTop;         /* +0x48 */
    int16_t textBottom;      /* +0x4A */
    uint8_t fontSize;        /* +0x4C */
    uint8_t pad4D[3];
    uint32_t textColour;     /* +0x50 ARGB */
    uint32_t title;          /* +0x54 guest address of the title text, 0 = none */
    uint8_t titleFontSize;   /* +0x58 */
    uint8_t pad59[3];
    uint32_t titleColour;    /* +0x5C ARGB */
    uint32_t openAnimation;  /* +0x60 how the window appears: 0 at once, 1 and 2 over five frames (2 also grows from a quarter of the height) */
    uint32_t closeAnimation; /* +0x64 how it disappears, the same way */
    float scaleX;            /* +0x68 the open/close animation scales the window */
    float scaleY;            /* +0x6C */
    uint32_t anchor;         /* +0x70 which point of the window x, y name (PlaceRect) */
    uint32_t unknown116;     /* +0x74 */
};
static_assert(sizeof(WindowStyle) == 0x78, "WindowStyle layout");
static_assert(offsetof(WindowStyle, x) == 0x0C, "WindowStyle layout");
static_assert(offsetof(WindowStyle, width) == 0x20, "WindowStyle layout");
static_assert(offsetof(WindowStyle, fitToText) == 0x14, "WindowStyle layout");
static_assert(offsetof(WindowStyle, colour) == 0x24, "WindowStyle layout");
static_assert(offsetof(WindowStyle, titleStrip) == 0x2C, "WindowStyle layout");
static_assert(offsetof(WindowStyle, openAnimation) == 0x60, "WindowStyle layout");
static_assert(offsetof(WindowStyle, text) == 0x38, "WindowStyle layout");
static_assert(offsetof(WindowStyle, textLeft) == 0x44, "WindowStyle layout");
static_assert(offsetof(WindowStyle, fontSize) == 0x4C, "WindowStyle layout");
static_assert(offsetof(WindowStyle, title) == 0x54, "WindowStyle layout");
static_assert(offsetof(WindowStyle, scaleX) == 0x68, "WindowStyle layout");
static_assert(offsetof(WindowStyle, anchor) == 0x70, "WindowStyle layout");

/* One entry of a window block's item set. The frame is a mesh of sprites (vertices, count) built by the layout step each frame and drawn with the
 * frame sheet; an optional second mesh goes over it. */
struct WindowItem {
    uint32_t sprites;          /* +0x00 guest address of the frame's sprite vertices */
    uint32_t spriteCount;      /* +0x04 vertex count (two per sprite) */
    uint32_t style;            /* +0x08 guest address of the WindowStyle */
    uint8_t styleCopy[0x78];   /* +0x0C the style as it was at the last layout: a difference means the size must be worked out again */
    uint32_t phase;            /* +0x84 0 new, 1 opening, 2 shown, 3 closing, 4 closed */
    int16_t savedY;            /* +0x88 the window's y while the animation moves it */
    uint8_t pad8A[2];
    uint32_t savedTitleStrip;  /* +0x8C */
    uint32_t savedText;        /* +0x90 the animation hides the texts and puts them back at its end */
    uint32_t savedTitle;       /* +0x94 */
    uint32_t savedTextColour;  /* +0x98 */
    uint32_t savedTitleColour; /* +0x9C */
    uint32_t animationStep;    /* +0xA0 how far the open / close animation is (0..4) */
    uint32_t hasOverlay;       /* +0xA4 nonzero when a second mesh is drawn over the frame (the animation's ghost) */
    uint32_t overlaySprites;   /* +0xA8 */
    uint32_t overlayCount;     /* +0xAC */
    int16_t overlayX;          /* +0xB0 */
    int16_t overlayY;          /* +0xB2 */
    float overlayScaleX;       /* +0xB4 */
    float overlayScaleY;       /* +0xB8 */
    uint32_t overlayColour;    /* +0xBC */
};
static_assert(sizeof(WindowItem) == 0xC0, "WindowItem layout");
static_assert(offsetof(WindowItem, style) == 0x08, "WindowItem layout");
static_assert(offsetof(WindowItem, phase) == 0x84, "WindowItem layout");
static_assert(offsetof(WindowItem, animationStep) == 0xA0, "WindowItem layout");
static_assert(offsetof(WindowItem, hasOverlay) == 0xA4, "WindowItem layout");

/* WindowBlock::flags */
enum WindowFlags : uint8_t {
    kNoInput = 0x01,              /* the window takes no input */
    kCursorByCallback = 0x02,     /* the selected cell is drawn by `cursorCallback` */
    kNoCancel = 0x04,             /* cancel only buzzes */
    kStayOpen = 0x08,             /* a choice does not close the window: it reports the answer and goes on */
    kCancelSelectsDefault = 0x20, /* cancel moves the selection to `defaultSelection` instead of closing */
    kNoConfirm = 0x40,            /* confirm does nothing */
    kNoWrap = 0x80,               /* the selection stops at the first and last cell instead of wrapping */
};

/* WindowBlock::layoutFlags */
enum LayoutFlags : uint8_t { kSkipDisabled = 0x01, kCentreCells = 0x02, kDiscardFrames = 0x04, kCellsSideways = 0x08 };

/* WindowBlock::state */
enum WindowState : uint8_t { kWindowCleared = 0, kWindowLaidOut = 1, kWindowOpening = 2, kWindowOpen = 3, kWindowAnswered = 4, kWindowClosing = 5, kWindowClosed = 6 };

/* The 0x120-byte window block the engine keeps per window set (a menu's boxes, a dialog): the items, the embedded style of the set's own window and
 * the cell grid it can lay out (the rows of a list, the buttons of a dialog). `libtitle::Window` is the same memory seen from the title code. */
struct WindowBlock {
    uint32_t item[16];         /* +0x00 guest addresses of WindowItem, 0 = free slot */
    uint32_t itemCount;        /* +0x40 */
    uint32_t sheet;            /* +0x44 the frame sheet (window01.gim) */
    uint32_t setHeap;          /* +0x48 */
    WindowStyle style;         /* +0x4C the set's own style: x, y, size, margins, text */
    uint32_t heap;             /* +0xC4 */
    uint8_t flags;             /* +0xC8 WindowFlags */
    uint8_t layoutFlags;       /* +0xC9 bit 1: centre the cells; bit 2: discard the frames drawn before the content; bit 3: cells advance sideways
                                *       (else down); bits 3..6 all clear: no cell grid */
    uint8_t buttonFlags;       /* +0xCA low nibble: which dialog buttons (ButtonLayout) */
    uint8_t padCB;
    uint16_t cellCount;        /* +0xCC */
    uint8_t visibleCells;      /* +0xCE how many cells show at once, 0 = all */
    uint8_t padCF;
    uint16_t firstCell;        /* +0xD0 index of the first visible cell (the scroll position) */
    uint8_t selection;         /* +0xD2 low 5 bits: the selected cell among the visible ones */
    uint8_t selectedButton;    /* +0xD3 low nibble: the selected dialog button */
    uint8_t defaultSelection;  /* +0xD4 the cell or button a cancel returns to (with kCancelSelectsDefault) */
    uint8_t padD5[3];
    uint32_t cellMask;         /* +0xD8 bit n set: cell n has a background (guess) */
    uint32_t disabledCells;    /* +0xDC bit n set: cell n cannot be chosen (the selection skips it with kSkipDisabled) */
    int16_t arrowDistance;     /* +0xE0 how far the scroll arrows sit from the cells */
    uint8_t padE2[2];
    uint32_t buttonSheet;      /* +0xE4 sheet of the scroll arrows and dialog buttons */
    uint8_t stateFlags;        /* +0xE8 bit 0: cancelled, bit 1: a cell or button was chosen */
    uint8_t state;             /* +0xE9 WindowState: the animation after an answer (4) lasts nine frames, then the window stays open or starts closing (5), and closes (6) */
    uint8_t padEA[2];
    uint32_t counter;          /* +0xEC bit 1 set: the closing window still draws its buttons */
    uint16_t cellStrideX;      /* +0xF0 */
    uint16_t cellStrideY;      /* +0xF2 */
    uint16_t cellOffsetX;      /* +0xF4 where a cell's own box sits inside its slot */
    uint16_t cellOffsetY;      /* +0xF6 */
    uint16_t cellWidth;        /* +0xF8 */
    uint16_t cellHeight;       /* +0xFA */
    uint32_t cellCallback;     /* +0xFC (cursor, cell, x, y): draws a cell's content */
    uint32_t contentCallback;  /* +0x100 (cursor, x, y, width, height): when set, replaces the whole content */
    uint32_t beforeCallback;   /* +0x104 same arguments: drawn before the cells */
    uint32_t afterCallback;    /* +0x108 same arguments: drawn after the buttons */
    uint32_t cursorCallback;   /* +0x10C (cursor, block, x, y, width, height): the selected cell's highlight */
    uint32_t cursorOverlayCallback;  /* +0x110 same, drawn over the cell's content */
    uint32_t backgroundCallback;     /* +0x114 same: a cell's background */
    uint32_t overlayCallback;        /* +0x118 same: drawn over a cell's content */
    uint32_t inputSource;      /* +0x11C guest address of another pad state to read instead of the real one, 0 = the real one */
};
static_assert(sizeof(WindowBlock) == 0x120, "WindowBlock layout");
static_assert(offsetof(WindowBlock, style) == 0x4C, "WindowBlock layout");
static_assert(offsetof(WindowBlock, flags) == 0xC8, "WindowBlock layout");
static_assert(offsetof(WindowBlock, cellCount) == 0xCC, "WindowBlock layout");
static_assert(offsetof(WindowBlock, cellMask) == 0xD8, "WindowBlock layout");
static_assert(offsetof(WindowBlock, disabledCells) == 0xDC, "WindowBlock layout");
static_assert(offsetof(WindowBlock, buttonSheet) == 0xE4, "WindowBlock layout");
static_assert(offsetof(WindowBlock, state) == 0xE9, "WindowBlock layout");
static_assert(offsetof(WindowBlock, cellStrideX) == 0xF0, "WindowBlock layout");
static_assert(offsetof(WindowBlock, cellCallback) == 0xFC, "WindowBlock layout");
static_assert(offsetof(WindowBlock, overlayCallback) == 0x118, "WindowBlock layout");
static_assert(offsetof(WindowBlock, inputSource) == 0x11C, "WindowBlock layout");

/* window.cpp: the corners of a rectangle: `origin` the top left, `end` the bottom right (the anchor says which point x, y name) */
void placeRect(float *origin, float *end, int32_t x, int32_t y, int32_t width, int32_t height, uint32_t anchor);

/* window_layout.cpp: one frame of a window item's layout (size, animation, frame mesh); `listAddress` is the address of the display list cursor */
void layoutWindowItem(SwapCtx &x, uint32_t listAddress, uint32_t itemAddress, uint32_t blockAddress);

/* window.cpp: one window of a block (the frame, the body text, the title); `listAddress` is the address of the display list cursor. */
void drawWindowItem(SwapCtx &x, uint32_t listAddress, uint32_t itemAddress, uint32_t blockAddress);

}  // namespace boot
