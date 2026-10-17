/* Typed view over the title flow's state in guest memory. The flow (Kanomi/publisher logos, opening movie, title menu and its screens) keeps
 * one block of globals in the game's data area (not in the module image), found by reading libtitle's generated code. The block is 13512
 * bytes: the flow counters first, then the three dialog windows, the panel set, the two menu lists and the flags of the screens. */
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace libtitle {

constexpr uint32_t kStateAddr = 0x09B29730;
/* The screens of the title flow that are understood (State::screen); the others are numbers in the tables of Tick and SwitchScreen. */
enum Screen : int16_t {
    kScreenTitle = 5,        /* the title / attract screen */
    kScreenMainMenu = 8,
    kScreenLists = 9,        /* the title lists: Free Duel, sub lists, limit list */
    kScreenCardList = 12,    /* card list / Free Duel settings */
    kScreenNetwork = 14,     /* Network Mode: not supported on PC yet */
    kScreenOptions = 17,
    kScreenLanguage = 19,
    kScreenEnd = 41,         /* the flow is over */
};

constexpr uint16_t kNone = 0xFFF;        /* "no pending value" in the pending* fields */
constexpr uint16_t kIncrement = 0xFFE;   /* "add one to the argument" instead of replacing it */

struct Window {   /* a 0x120-byte window block of the engine (frame, text, buttons); the first 0x4C bytes are its item set */
    uint32_t slot[16];   /* +0x0 window ids taken from the engine pool (differ between two runs) */
    uint32_t count;   /* +0x40 */
    uint32_t sheet;   /* +0x44 texture sheet of the frames (window01.gim) */
    uint32_t setHeap;   /* +0x48 */
    uint32_t styleKind;   /* +0x4C */
    uint8_t pad50[4];
    uint8_t frameKind;   /* +0x54 frame look: 0, 2 */
    uint8_t unknown85;   /* +0x55 */
    uint8_t pad56[2];
    uint16_t x;   /* +0x58 */
    uint16_t y;   /* +0x5A */
    uint8_t pad5C[4];
    uint32_t unknown96;   /* +0x60 */
    uint16_t unknown100;   /* +0x64 */
    uint8_t pad66[6];
    uint16_t width;   /* +0x6C */
    uint16_t height;   /* +0x6E */
    uint32_t colour;   /* +0x70 ARGB of the frame */
    uint8_t pad74[4];
    uint32_t unknown120;   /* +0x78 */
    uint32_t unknown124;   /* +0x7C */
    uint8_t pad80[4];
    uint32_t text;   /* +0x84 guest address of the text shown */
    uint32_t unknown136;   /* +0x88 */
    uint8_t pad8C[4];
    uint16_t margin[4];   /* +0x90 inner margins, in the order the engine reads them */
    uint8_t textSize;   /* +0x98 font size of the text */
    uint8_t pad99[3];
    uint32_t textColour;   /* +0x9C ARGB */
    uint32_t unknown160;   /* +0xA0 */
    uint8_t highlightTextSize;   /* +0xA4 (guess) */
    uint8_t padA5[3];
    uint32_t highlightColour;   /* +0xA8 (guess) ARGB */
    uint32_t animationA;   /* +0xAC open/close animation kind (1, 2) */
    uint32_t animationB;   /* +0xB0 set together with animationA */
    uint8_t padB4[16];
    uint32_t heap;   /* +0xC4 the title heap */
    uint8_t flags;   /* +0xC8 bit fields (see the callers) */
    uint8_t flags2;   /* +0xC9 */
    uint8_t flags3;   /* +0xCA low nibble: button layout */
    uint8_t padCB[1];
    uint16_t unknown204;   /* +0xCC */
    uint8_t unknown206;   /* +0xCE */
    uint8_t padCF[1];
    uint16_t animationFrame;   /* +0xD0 */
    uint8_t phase;   /* +0xD2 low 5 bits: animation phase */
    uint8_t selectedButton;   /* +0xD3 low nibble: button that starts selected */
    uint8_t unknown212;   /* +0xD4 */
    uint8_t padD5[11];
    uint16_t unknown224;   /* +0xE0 */
    uint8_t padE2[6];
    uint8_t stateFlags;   /* +0xE8 */
    uint8_t state;   /* +0xE9 0 cleared, 1 laid out, 6 closed */
    uint8_t padEA[6];
    uint16_t textWidth;   /* +0xF0 */
    uint16_t textY;   /* +0xF2 */
    uint16_t unknown244;   /* +0xF4 */
    uint16_t unknown246;   /* +0xF6 */
    uint16_t textRight;   /* +0xF8 */
    uint16_t unknown250;   /* +0xFA */
    uint32_t drawCallbackB;   /* +0xFC second draw callback */
    uint8_t pad100[12];
    uint32_t drawCallback;   /* +0x10C draws the window's content */
    uint32_t unknown272;   /* +0x110 */
    uint8_t pad114[12];
};
static_assert(sizeof(Window) == 0x120, "Window layout");
static_assert(offsetof(Window, slot) == 0x0, "Window layout");
static_assert(offsetof(Window, count) == 0x40, "Window layout");
static_assert(offsetof(Window, sheet) == 0x44, "Window layout");
static_assert(offsetof(Window, setHeap) == 0x48, "Window layout");
static_assert(offsetof(Window, styleKind) == 0x4C, "Window layout");
static_assert(offsetof(Window, frameKind) == 0x54, "Window layout");
static_assert(offsetof(Window, unknown85) == 0x55, "Window layout");
static_assert(offsetof(Window, x) == 0x58, "Window layout");
static_assert(offsetof(Window, y) == 0x5A, "Window layout");
static_assert(offsetof(Window, unknown96) == 0x60, "Window layout");
static_assert(offsetof(Window, width) == 0x6C, "Window layout");
static_assert(offsetof(Window, height) == 0x6E, "Window layout");
static_assert(offsetof(Window, colour) == 0x70, "Window layout");
static_assert(offsetof(Window, unknown120) == 0x78, "Window layout");
static_assert(offsetof(Window, unknown124) == 0x7C, "Window layout");
static_assert(offsetof(Window, text) == 0x84, "Window layout");
static_assert(offsetof(Window, unknown136) == 0x88, "Window layout");
static_assert(offsetof(Window, margin) == 0x90, "Window layout");
static_assert(offsetof(Window, textSize) == 0x98, "Window layout");
static_assert(offsetof(Window, textColour) == 0x9C, "Window layout");
static_assert(offsetof(Window, unknown160) == 0xA0, "Window layout");
static_assert(offsetof(Window, highlightTextSize) == 0xA4, "Window layout");
static_assert(offsetof(Window, highlightColour) == 0xA8, "Window layout");
static_assert(offsetof(Window, animationA) == 0xAC, "Window layout");
static_assert(offsetof(Window, animationB) == 0xB0, "Window layout");
static_assert(offsetof(Window, heap) == 0xC4, "Window layout");
static_assert(offsetof(Window, flags) == 0xC8, "Window layout");
static_assert(offsetof(Window, flags2) == 0xC9, "Window layout");
static_assert(offsetof(Window, flags3) == 0xCA, "Window layout");
static_assert(offsetof(Window, unknown204) == 0xCC, "Window layout");
static_assert(offsetof(Window, unknown206) == 0xCE, "Window layout");
static_assert(offsetof(Window, animationFrame) == 0xD0, "Window layout");
static_assert(offsetof(Window, phase) == 0xD2, "Window layout");
static_assert(offsetof(Window, selectedButton) == 0xD3, "Window layout");
static_assert(offsetof(Window, unknown212) == 0xD4, "Window layout");
static_assert(offsetof(Window, unknown224) == 0xE0, "Window layout");
static_assert(offsetof(Window, stateFlags) == 0xE8, "Window layout");
static_assert(offsetof(Window, state) == 0xE9, "Window layout");
static_assert(offsetof(Window, textWidth) == 0xF0, "Window layout");
static_assert(offsetof(Window, textY) == 0xF2, "Window layout");
static_assert(offsetof(Window, unknown244) == 0xF4, "Window layout");
static_assert(offsetof(Window, unknown246) == 0xF6, "Window layout");
static_assert(offsetof(Window, textRight) == 0xF8, "Window layout");
static_assert(offsetof(Window, unknown250) == 0xFA, "Window layout");
static_assert(offsetof(Window, drawCallbackB) == 0xFC, "Window layout");
static_assert(offsetof(Window, drawCallback) == 0x10C, "Window layout");
static_assert(offsetof(Window, unknown272) == 0x110, "Window layout");

/* A menu list block (0x7C bytes in the game's data area): a column of items with a cursor, drawn by DrawList and driven by ListUpdate.
 * Every title menu (main menu, sub lists, language list) is one of these; the title uses two, kTitleList and kTitleList2.
 * `itemState[i]` is the state of item i: kItemEnabled, kItemDisabled (drawn dark, the cursor skips it) or kItemAlternate. */
constexpr uint8_t kItemEnabled = 0, kItemDisabled = 1, kItemAlternate = 2;
/* What a list is doing (MenuList::state), visited by ListUpdate in this order. */
enum ListState : uint8_t {
    kListStart = 0,          /* just built: starts the opening animation */
    kListOpening,            /* the opening animation runs */
    kListSkipDisabled,       /* open: moves the cursor off a disabled item */
    kListIdle,               /* open and waiting for buttons */
    kListChosen,             /* an item was confirmed: the closing animation */
    kListAnimatingOut,       /* the blink after a confirm or cancel (then stateAfterAnimation) */
    kListCancelled,          /* cancelled: reports -2 */
    kListGone,               /* finished */
};
struct MenuList {
    uint8_t state;                 /* +0   a ListState */
    uint8_t stateAfterAnimation;   /* +1 */
    uint8_t chosen;                /* +2   what was chosen (0 stay on the list, 1/3 animate out) */
    uint8_t stateAfterConfirm;     /* +3 */
    uint8_t openAnimation;         /* +4   0..3 */
    uint8_t confirmBehaviour;      /* +5 */
    uint8_t cancelBehaviour;       /* +6 */
    uint8_t unknown7;              /* +7 */
    uint8_t itemCount;             /* +8 */
    uint8_t unknown9;              /* +9 */
    uint16_t fadeLength;           /* +10 frames each item takes to fade in */
    uint16_t fadeDelay;            /* +12 frames between two items starting */
    uint16_t fadeFrame;            /* +14 current frame of the fade-in */
    uint16_t fadeEnd;              /* +16 frame at which the fade-in is over */
    uint8_t unknown18[2];
    uint32_t itemColour[10];       /* +20  per item, used while the list opens (openAnimation 2) */
    uint8_t itemState[10];         /* +60  per item: kItemEnabled / kItemDisabled / kItemAlternate */
    uint16_t x, y, width, rowHeight;   /* +70 */
    uint8_t spriteCell;            /* +78  which cell of the button sheet draws a normal row */
    uint8_t spriteCellAlternate;   /* +79  ... a row in state kItemAlternate */
    uint8_t selected;              /* +80  the item under the cursor */
    uint8_t spriteCellPressed;     /* +81  ... the highlighted row */
    uint8_t flags;                 /* +82  bit 0: wrap around, bit 1: auto-repeat buttons */
    uint8_t hasMarkedItem;         /* +83 */
    uint8_t markedItem;            /* +84  an item drawn with its own cell (the language in use) */
    uint8_t spriteCellMarked;      /* +85 */
    uint8_t unknown86;             /* +86 */
    uint8_t pad87;
    uint16_t unknown88;            /* +88 */
    uint8_t pad90[2];
    uint32_t openFadeColour;       /* +92  colour of the open fade, also the normal text colour while drawing */
    uint32_t normalColour;         /* +96 */
    uint32_t selectedColour;       /* +100 */
    uint32_t disabledTextColour;   /* +104 used by DrawList */
    uint32_t disabledColour;       /* +108 used by ItemColor */
    uint8_t blink;                 /* +112 bits 0-5: frame counter, bits 6-7: 0 fade, 1 confirm blink */
    uint8_t pad113[7];
    uint32_t drawCallback;         /* +120 draws the label of one row (list, item, x, y, width, height) */
};
static_assert(offsetof(MenuList, fadeLength) == 10 && offsetof(MenuList, itemColour) == 20, "MenuList layout");
static_assert(sizeof(MenuList) == 0x7C, "MenuList layout");
static_assert(offsetof(MenuList, itemState) == 60 && offsetof(MenuList, x) == 70 && offsetof(MenuList, selected) == 80, "MenuList layout");
static_assert(offsetof(MenuList, openFadeColour) == 92 && offsetof(MenuList, blink) == 112 && offsetof(MenuList, drawCallback) == 120, "MenuList layout");


/* A panel is a framed box of cells in a record of 44 bytes in the data area (several panels live at 0x09B29CC8 and 0x09B29F44). */
struct Panel {
    uint8_t unknown0, unknown1;
    int16_t x, y, w, h;                  /* the box (+2) */
    uint8_t unknown10, unknown11;
    uint32_t colour, colour2;            /* +12, +16 */
    uint8_t frameKind, frameVariant;     /* +20, +21: which frame the box is drawn with */
    uint8_t flags;                       /* +22: bit 0 no box, bit 2 no cursor, bit 3 no cell frames, bit 5 no scroll bar / big corners */
    uint8_t unknown23;
    uint8_t cursor;                      /* +24: the selected cell */
    uint8_t unknown25;
    uint8_t cells;                       /* +26 */
    uint8_t columns;                     /* +27: 0 counts as 1; cells fill column by column */
    int16_t gridX, gridY, cellW, cellH;  /* +28: where the cells start and how big one is */
    uint32_t label;                      /* +36: text drawn over the grid, 0 = none */
    uint32_t drawCell;                   /* +40: guest function that draws one cell, 0 = none */
};
static_assert(sizeof(Panel) == 44 && offsetof(Panel, cursor) == 24 && offsetof(Panel, gridX) == 28 && offsetof(Panel, drawCell) == 40, "Panel layout");

/* One of the four tab headers of a panel set: a small box of the strip above the info box. */
struct TabRecord {
    uint8_t unknown0, unknown1;
    int16_t x, y, w, h;                  /* +2 */
    uint8_t shape;                       /* +10 low nibble: edge shape (4 first tab, 9 middle, 0xE last) */
    uint8_t unknown11;
    uint32_t colour[4];                  /* +12 white, grey, white, grey */
    uint32_t unknown28, unknown32;
};
static_assert(sizeof(TabRecord) == 36 && offsetof(TabRecord, shape) == 10 && offsetof(TabRecord, unknown32) == 32, "TabRecord layout");

/* The panel set (kPanelBlock): a strip of tab headers, an info box under it and the window that shows the text, laid out by InitPanelSet. */
struct PanelSet {
    TabRecord tab[4];                    /* +0x000 */
    Panel infoBox;                       /* +0x090 */
    Window window;                       /* +0x0BC */
    uint8_t unknown1DC[0x2FC - 0x1DC];
    uint8_t flags;                       /* +0x2FC low 3 bits: 5 */
    uint8_t pad2FD;
    int16_t x, y, w, h;                  /* +0x2FE the box the strip and info box are laid out in */
    uint8_t selectedTab;                 /* +0x306 */
    uint8_t tabCount;                    /* +0x307 */
    uint8_t pad308[4];
    Panel cardBox;                       /* +0x30C the box of the card list screens */
    uint8_t unknown338[0x364 - 0x338];
    uint8_t unknown364, unknown365;      /* +0x364 */
    uint8_t pad366[2];
};
static_assert(sizeof(PanelSet) == 0x368 && offsetof(PanelSet, infoBox) == 144 && offsetof(PanelSet, window) == 188, "PanelSet layout");
static_assert(offsetof(PanelSet, flags) == 764 && offsetof(PanelSet, x) == 766 && offsetof(PanelSet, tabCount) == 775 && offsetof(PanelSet, cardBox) == 0x30C && offsetof(PanelSet, unknown364) == 868, "PanelSet layout");

/* One line of text in the title text box: where it is drawn and which part of the text it shows. */
struct TextRow {
    uint32_t x;            /* +0 */
    uint32_t y;            /* +4 */
    uint32_t unknown8;
    uint32_t height;       /* +12 */
    uint32_t textStart;    /* +16 first character of the text on this row */
    uint32_t textLength;   /* +20 */
};

/* The title's text box (the typed hint/help text on the title menus): a window set and its style record, the parameters it was started with,
 * three text rows and the slide-in animation. 384 bytes at 0x09B2CBF8, cleared by SetupMenuInput. The style record at +0x54 has the layout of
 * Window's fields +0x4C..+0xC4 (use `Window` at kTextBoxWindow for them); the fields from +0xCC on are the box's own. */
struct TitleTextBox {
    uint32_t changeRequested;   /* +0x00 RequestChange sets it to 1: the screen's drawing must be redone */
    uint32_t unknown4;
    uint8_t windowSet[0x4C];    /* +0x08 the engine's window set (AddWindow / FreeWindowSet) */
    uint8_t style[0x78];        /* +0x54 style record, see Window */
    uint32_t heap;              /* +0xCC */
    uint32_t paramX;            /* +0xD0 */
    uint32_t paramWidth;        /* +0xD4 */
    uint32_t paramFontSize;     /* +0xD8 */
    uint32_t rowCount;          /* +0xDC 2 or 3 depending on the UI mode */
    uint32_t rowWidth;          /* +0xE0 */
    uint32_t text;              /* +0xE4 guest address of the 16-bit text, 0 when nothing is shown */
    uint32_t messageSpeed;      /* +0xE8 */
    uint32_t paramD;            /* +0xEC */
    uint32_t paramE;            /* +0xF0 */
    TextRow row[3];             /* +0xF4 */
    uint32_t unknown13C;        /* +0x13C */
    uint32_t firstChar;         /* +0x140 where the first row starts in the text */
    uint32_t unknown144;
    uint32_t unknown148, unknown14C, unknown150, unknown154;
    uint32_t wideFlag;          /* +0x158 0 or 1 (UI mode): passed to the engine's wide-screen flag */
    uint32_t unknown15C;        /* +0x15C cleared when the box is cleared */
    uint8_t pad160[8];
    float x1, y1;               /* +0x168 slide-in animation: current position (y starts off screen at 300) */
    float x2, y2;               /* +0x170 */
    float x3, y3;               /* +0x178 target (y = 210) */
};
static_assert(sizeof(TitleTextBox) == 0x180, "TitleTextBox layout");
static_assert(offsetof(TitleTextBox, style) == 0x54 && offsetof(TitleTextBox, heap) == 0xCC && offsetof(TitleTextBox, row) == 0xF4, "TitleTextBox layout");
static_assert(offsetof(TitleTextBox, wideFlag) == 0x158 && offsetof(TitleTextBox, x1) == 0x168 && offsetof(TitleTextBox, y3) == 0x17C, "TitleTextBox layout");

/* ---- the work blocks of the options / card list screens ---- */

/* One of the four columns of the card list: what it shows and where its data is (set by ConfigureColumn).
 * The SAME 16 bytes are read back by the Free Duel start (FUN_08f186f0, called by FUN_08f02bc0) as the duel's participants, so some fields have
 * two names. Per column, as the card list sees it / as the Free Duel start sees it:
 *   kind    = which list the column shows: 1 and 41 = the player's cards / the character id of the duelist whose list it is (2 = Jaden ... 8 = Zane,
 *             see duelists.h)
 *   sourceA = (save kind source) / unused by the Free Duel start
 *   sourceB = where the data comes from / the slot type (0..3: how the deck is chosen)
 *   count   = number of entries / the DECK ID of the participant (duelists.h: 37 = Zane's first deck). It comes from LastTypedEntryValue: the
 *             last card of the duelist's list in the save, which for a duelist is his most advanced unlocked deck. */
struct ColumnRecord {
    int16_t kind;          /* +0  card list kind / character id of the participant */
    int16_t sourceA;       /* +2 */
    int16_t sourceB;       /* +4  selects where the data comes from (0 save records, 1 card data, 2 and 3 none) / slot type */
    int16_t count;         /* +6  entries / deck id of the participant (the one the duel will load) */
    uint32_t value;        /* +8 set by SetOptionsEntry */
    uint32_t data;         /* +12 guest address of the column's data */
};
static_assert(sizeof(ColumnRecord) == 16, "ColumnRecord layout");

/* The options / card list work block (108 bytes from the title heap, pointer at kOptionsBlockPtr). */
struct OptionsWork {
    uint32_t heap;         /* +0 */
    uint32_t archive;      /* +4 texture archive of the screen */
    uint32_t unknown8;
    uint32_t mode;         /* +12 4 for kinds 0..7, 2 for kinds 8 and 9 */
    uint8_t networkScene;  /* +16 (GetNetworkScene when the block was made) */
    uint8_t kind;          /* +17 which options screen: 0..10 */
    uint8_t pad18[2];
    ColumnRecord column[4];   /* +20 */
    uint8_t unknown84[108 - 84];
};
static_assert(sizeof(OptionsWork) == 108 && offsetof(OptionsWork, column) == 20, "OptionsWork layout");

/* One card (or deck slot) of the card list tables. */
struct CardEntry {
    uint16_t classPosition;   /* +0 bits 0-1 the card's class, bits 2-11 its position in the list */
    uint16_t number;          /* +2 the card (or save slot) number */
    uint8_t copies;           /* +4 how many copies are owned */
    uint8_t pad5;
};
static_assert(sizeof(CardEntry) == 6, "CardEntry layout");

/* The card list screen's tables (kCardListTables): three lists of cards and the buffers they were read from. */
struct CardListTables {
    uint32_t kind;                 /* +0 which card list was built (1 or 41 = every owned card) */
    uint8_t unknown4[12];
    uint32_t mainBuffer;           /* +16 */
    uint32_t secondBuffer;         /* +20 */
    uint8_t bufferFlags;           /* +24 bits 1-4: a buffer was taken from the heap */
    uint8_t pad25[3];
    uint32_t mainSize;             /* +28 */
    uint32_t mainRecords;          /* +32 how many 0x208-byte records the main buffer holds */
    uint8_t unknown36[64];
    CardEntry deckSlot[51];        /* +100 the 51 deck records (slot 0 is the deck being edited) */
    CardEntry owned[351];          /* +406 every owned card (ids 1..350) */
    CardEntry second[256];         /* +2512 the records of the second table */
    uint16_t count;                /* +4048 how many entries of `owned` are in use */
};
static_assert(offsetof(CardListTables, mainBuffer) == 16 && offsetof(CardListTables, deckSlot) == 100 && offsetof(CardListTables, owned) == 406, "CardListTables layout");
static_assert(offsetof(CardListTables, second) == 2512 && offsetof(CardListTables, count) == 4048, "CardListTables layout");

/* Three small values the card list screen keeps in front of its tables (0x09B2B294). */
struct CardListFilters {
    uint8_t unknown0;
    uint8_t first;         /* +1 */
    uint8_t second;        /* +2 */
    uint8_t third;         /* +3 */
};

/* The answers of the Free Duel questions (0x09B2AFC4): the dialog writes bits of two bytes. */
struct FreeDuelSettings {
    uint8_t answers;       /* +0 bit 5: question 0 (picks the next screen: 4 when set, else 3), bit 6: question 1, bit 7: question 2 */
    uint8_t picks;         /* +1 low nibble: question 3, high nibble: question 4 */
    uint8_t unknown2[14];
};
static_assert(sizeof(FreeDuelSettings) == 16, "FreeDuelSettings layout");

/* One player's slot of the duel setup. */
struct DuelSlot {
    uint16_t character;    /* +0 which character plays this slot (6 = the special one); character id as in duelists.h */
    uint16_t isSpecial;    /* +2 1 when `character` is 6 */
    uint16_t deck;         /* +4 DECK id = the n of RCPnnn that the duel loads for this slot (duelists.h; 0 = none). Not the character id. */
    uint8_t unknown6[0x18C - 6];
};
static_assert(sizeof(DuelSlot) == 0x18C, "DuelSlot layout");

/* The engine's duel setup block (GetSettingsBlock): who plays, with what, and how the duel was started. Only the fields the title touches. */
struct DuelSetup {
    uint8_t unknown0[0x110];
    uint32_t seed;         /* +0x110 */
    uint32_t coinToss;     /* +0x114 0 or 1 */
    uint8_t kind;          /* +0x118 how the duel was started: 5 = random duel, 2 = the one the story asked for */
    uint8_t ruleA;         /* +0x119 (2 = the wide layout of the card list) */
    uint8_t ruleB;         /* +0x11A */
    uint8_t stage;         /* +0x11B */
    uint8_t firstPlayer;   /* +0x11C 1 or 3 */
    uint8_t unknown11D;
    uint8_t unknown11E, unknown11F;
    DuelSlot slot[2];      /* +0x120 */
};
static_assert(offsetof(DuelSetup, seed) == 0x110 && offsetof(DuelSetup, kind) == 0x118 && offsetof(DuelSetup, slot) == 0x120, "DuelSetup layout");

/* A colour that moves from one ARGB value to another over `frames` frames (StartColourFade / StepColorFade): the colour of the title's menu text. */
struct ColourFade {
    uint32_t target;     /* +0  guest address of the colour that is written */
    uint32_t from;       /* +4 */
    uint32_t to;         /* +8 */
    uint16_t frames;     /* +12 */
    uint16_t step;       /* +14 */
    uint16_t active;     /* +16  1 while running */
    uint8_t pad18[2];
};

/* What the game was started with (the engine's launch record: which scene asked for the title and with what parameter). */
struct LaunchInfo {
    uint16_t mode;       /* +0  2 or 3 select how the first screen is chosen */
    uint16_t sub;        /* +2 */
    uint16_t unknown4;   /* +4 */
    uint16_t param;      /* +6 */
};

/* The flow is a list of screens (logos, movie, menus). `screen` is the one running; `nextScreen` is the one asked for. Each of the
 * three arguments has a pending value that is committed (replace, or +1 when kIncrement) when the flow reaches a quiet point. */
struct State {
    uint32_t frame;            /* +0x00 frame counter, wraps at 0x10000 */
    uint32_t transition;       /* +0x04 frames since the screen started (cleared when a screen is started) */
    int16_t screen;            /* +0x08 */
    int16_t nextScreen;        /* +0x0A kNone when nothing is requested */
    int16_t arg0;              /* +0x0C */
    uint16_t pendingArg0;      /* +0x0E */
    int16_t arg1;              /* +0x10 */
    uint16_t pendingArg1;      /* +0x12 */
    int16_t arg2;              /* +0x14 */
    uint16_t pendingArg2;      /* +0x16 */
    int16_t returnArg;         /* +0x18 the list screen to come back to: the main menu item + 3 */
    uint8_t returnFlags;       /* +0x1A bit 0: come back to returnArg after the screen */
    uint8_t pad1B;
    int16_t publishedScreen;   /* +0x1C copy of `screen` for the draw dispatcher (set by Tick) */
    int16_t publishedArg0;     /* +0x1E copy of `arg0` for the drawing */
    uint16_t phase;            /* +0x20 2 = the running screen is ready to be replaced */
    uint8_t pad22[2];
    uint32_t heap;             /* +0x24 the 12 MB title heap */
    LaunchInfo launch;         /* +0x28 */
    uint32_t padState;         /* +0x30 guest address of the pad state read this frame */
    uint8_t pad34[8];
    uint32_t skipRequested;    /* +0x3C the intro was skipped */
    uint32_t menuColour[2];    /* +0x40 text colours of the title menu (they fade between white and grey) */
    uint8_t pad48[8];
    ColourFade colourFade;     /* +0x50 */
    uint8_t pad64[0xA0 - 0x64];
    uint8_t handles[0x1A8 - 0xA0];   /* +0xA0 one u32 per slot: a loaded resource, 0 when empty (kHandleTable) */
    Window windowA;            /* +0x1A8 dialog and main menu window */
    Window windowB;            /* +0x2C8 */
    Window windowC;            /* +0x3E8 */
    PanelSet panelSet;         /* +0x508 four tab headers, an info box and a window (InitPanelSet) */
    uint32_t savedSelection;   /* +0x870 item to put the cursor on when a sub list opens */
    uint32_t subListCount;     /* +0x874 */
    uint8_t unknown878;
    uint8_t subListFound;      /* +0x879 */
    uint8_t pad87A[2];
    MenuList titleList;        /* +0x87C the main title list (and the Options / language rows) */
    MenuList titleList2;       /* +0x8F8 the sub lists */
    uint8_t cardWork[0x11F0];  /* +0x974 work block of the card list window (cleared by ClearWorkBlock) */
    uint8_t pad1B64[0x3078 - 0x1B64];
    uint8_t previousLanguage;  /* +0x3078 language before the language screen changed it */
    uint8_t charaListLoaded;   /* +0x3079 the character list module is up */
    uint8_t pad307A[2];
    uint32_t soundHandle;      /* +0x307C */
    uint32_t menuBuffer[11];   /* +0x3080 buffers taken for the menu (one per row kind) */
    uint32_t moduleHandle;     /* +0x30AC handle of the helper module while it is loaded */
    uint8_t skipLocked;        /* +0x30B0 */
    uint8_t pad30B1[0x34C8 - 0x30B1];
};
static_assert(offsetof(State, screen) == 0x8 && offsetof(State, phase) == 0x20 && offsetof(State, heap) == 0x24, "State layout");
static_assert(offsetof(State, launch) == 0x28 && offsetof(State, colourFade) == 0x50 && offsetof(State, windowA) == 0x1A8, "State layout");
static_assert(offsetof(State, windowC) == 0x3E8 && offsetof(State, savedSelection) == 0x870 && offsetof(State, titleList) == 0x87C, "State layout");
static_assert(offsetof(State, titleList2) == 0x8F8 && offsetof(State, previousLanguage) == 0x3078 && offsetof(State, soundHandle) == 0x307C, "State layout");
static_assert(offsetof(State, moduleHandle) == 0x30AC && sizeof(State) == 13512, "State layout");

/* Addresses of the blocks inside the state */
constexpr uint32_t kHandleTable = kStateAddr + offsetof(State, handles);
constexpr uint32_t kWindowA = kStateAddr + offsetof(State, windowA), kWindowB = kStateAddr + offsetof(State, windowB), kWindowC = kStateAddr + offsetof(State, windowC);
constexpr uint32_t kPanelBlock = kStateAddr + offsetof(State, panelSet);
constexpr uint32_t kInfoBox = kPanelBlock + offsetof(PanelSet, infoBox), kCardBox = kPanelBlock + offsetof(PanelSet, cardBox);
constexpr uint32_t kCardWork = kStateAddr + offsetof(State, cardWork);
constexpr uint32_t kTitleList = kStateAddr + offsetof(State, titleList), kTitleList2 = kStateAddr + offsetof(State, titleList2);
constexpr uint32_t kMenuColours = kStateAddr + offsetof(State, menuColour), kColourFade = kStateAddr + offsetof(State, colourFade);
constexpr uint32_t kLaunchInfo = kStateAddr + offsetof(State, launch);
constexpr uint32_t kCardListTables = 0x09B2B29C, kCardListFilters = 0x09B2B294, kFreeDuelSettings = 0x09B2AFC4;
constexpr uint32_t kOptionsBlockPtr = 0x09B2CF30;   /* u32: pointer to the OptionsWork block */

/* other globals the small functions touch */
constexpr uint32_t kFlagsAddr = 0x09B2C7D8;               /* byte: bits 0-1 = fade mode, bit 2 = flow locked */
constexpr uint32_t kFlagsTouchedAddr = 0x09B2CBF2;        /* u16, cleared with the fade mode */
constexpr uint32_t kLogoIndexAddr = 0x09B2C7A4;           /* u32 */
constexpr uint32_t kLogoTableAddr = 0x09B2C7BC;           /* u32 per index */
constexpr uint32_t kLogoBlockAddr = 0x09B2C7EC;           /* u32: the 5 MB block taken while a logo screen runs */
constexpr uint32_t kTextBox = 0x09B2CBF8;                 /* TitleTextBox */
constexpr uint32_t kTextBoxWindow = kTextBox + 8;         /* its window set, followed by the style record (Window fields up to +0xC4) */
/* Sprite records in the module image (read-only data of libtitle).
 * LogoSprite: 152-byte records at kLogoSprites. Each names the image it comes from (the first is "konami_logo.gim"; the logo screens, the
 * menu lists and the menu decorations all use them) and one rectangle of it: drawn at (x, y) with size width x height, cut from (u, v) with
 * size sourceWidth x sourceHeight (the image is stretched when the two sizes differ). */
struct LogoSprite {
    uint32_t unknown0;
    char fileName[128];
    int16_t x, y, width, height;              /* +132 where and how big it is drawn */
    int16_t u, v, sourceWidth, sourceHeight;  /* +140 the piece of the image */
    int16_t mirrorU, mirrorV;                 /* +148 */
};
static_assert(sizeof(LogoSprite) == 152 && offsetof(LogoSprite, x) == 132 && offsetof(LogoSprite, mirrorU) == 148, "LogoSprite layout");
constexpr uint32_t kLogoSprites = 0x08f25c40;
constexpr uint32_t kLogoScreenSprites = 0x08f27424;   /* two u32: the LogoSprite numbers the logo screen draws */
constexpr uint32_t kListRowSprites = 0x08f27440;      /* three u32: the LogoSprite numbers of a list row (left cap, stretched middle, right cap) */

/* FramePart: 22-byte records at kFrameParts, the pieces of the window frames and the small menu decorations (the cursor arrow, the name
 * plate): u/v is where the piece sits in its sheet, width/height its size. Each frame kind is a run of 1, 3, 9 or 12 consecutive records
 * (corners, edges, middle). */
struct FramePart {
    int16_t unknown[5];
    int16_t u, v, width, height, mirrorU, mirrorV;
};
static_assert(sizeof(FramePart) == 22, "FramePart layout");
constexpr uint32_t kFrameParts = 0x08f27838;

}  // namespace libtitle
