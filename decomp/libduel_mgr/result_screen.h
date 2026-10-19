/* libduel_mgr: the Duel Result screen (shown after a duel: the reward table, the DP total and the "duel again?" question).
 * The game keeps the screen's state in one block allocated when the screen starts; a global holds its address. This header is a typed
 * view over that block plus the screen's behaviour; each method names the original function it replaces. */
#pragma once
#include "../common/swap.h"
#include "../common/ehsys.h"
#include "../libtitle/title_state.h"
#include "duel_records.h"

namespace duel_mgr {

constexpr uint32_t kModuleBase = 0x08efca00;
constexpr uint32_t kResultScreenPointer = 0x09a151a4;   /* global: guest address of the ResultScreen block, 0 while the screen is closed */
constexpr uint32_t kResourceNameTable = 0x08f147a0;     /* 8 pointers to the file names of ResultResource */
constexpr uint32_t kDigitBufferInit = 0x08f14830;       /* two halfwords: the digit buffer's initial contents (digit, terminator) */
constexpr uint32_t kCategoryNameStrings = 0x08f14814;   /* per reward category, the string index of its name in the text table */
constexpr uint32_t kDpLabel = 0x08f147c0;               /* the "DP" label drawn next to every amount */
constexpr uint32_t kPageDrawTable = 0x08f147f4;         /* per page, the function that draws it: (cursor, slide) -> cursor */
constexpr uint32_t kPageNameStrings = 0x08f14804;       /* per page 1..3, the string index of its name */
constexpr uint32_t kArchiveLetters = 0x08f147d0;        /* per language, the letter in the archive name */
constexpr uint32_t kArchiveNameFormat = 0x08f147d8;     /* "duel/result/result_%c.ehp" */
constexpr uint32_t kPageTitleStrings = 0x08f14820;      /* per detail page 0..2, the string index of its name */
constexpr uint32_t kEntryNames[3] = {0x08f145ac, 0x08f1455c, 0x08f14654};   /* per detail page, the table whose halfword >> 1 is the string index of each entry's name */
constexpr uint32_t kTimeSeparator = 0x08f1481c;         /* two halfwords: the separator of a time (minutes:seconds) */
constexpr uint32_t kSummaryArrowOffsets = 0x08f14828;   /* 4 halfwords: how far the detail pages' arrows swing */
constexpr uint32_t kArrowOffsets = 0x08f1480c;
constexpr uint32_t kDuelOutcomeScale = 0x08f147c8;      /* 4 halfwords: the divisor of the bonus, by how many save entries are in state 1 or 3 (see Start) */
constexpr uint32_t kDuelModuleLoaded[4] = {0x09a14a28, 0x09a14a2c, 0x09a14a30, 0x09a14a34};   /* globals: which of the duel's modules and sound banks are loaded */
constexpr uint32_t kDuelSettingsBackup = 0x09a14a3c;    /* 8 bytes the duel setup saved; restored into the save for one kind of duel */          /* 4 halfwords: how far the page arrows swing */

/* The files the screen loads (kResourceNameTable): the sprite resources, the sheets its table is cut from, and the text table. */
enum ResultResource : uint16_t {
    kBackgroundSprites = 0,   /* menu_bg.ehf: the scrolling checkerboard */
    kTitleBarSprites = 1,     /* menu_bar.ehf: the title capsule with its animation */
    kWinBanner = 2,           /* win.ehf */
    kLoseBanner = 3,          /* lose.ehf */
    kDrawBanner = 4,          /* draw.ehf */
    kTableSheet = 5,          /* menu01.gim: corners, edges, rows and arrows of the table (the sprites below index into it) */
    kTabSheet = 6,            /* menu02.gim: the page tabs under the title */
    kTexts = 7,               /* strTbl.bin */
    kResourceCount = 8,
};

enum ResultPhase : uint16_t {
    kPhaseWaitForArchive = 0,   /* waiting for the result archive to load */
    kPhaseFadeIn = 1,
    kPhaseTitleBarOpening = 2,
    kPhasePages = 3,            /* the player browses the reward pages */
    kPhaseSliding = 4,          /* a page slides out and the next slides in */
    kPhaseAnswer = 5,           /* the player confirmed: the outcome banner and, in network duels, the question */
    kPhaseBannerIn = 6,
    kPhaseWaitButton = 7,
    kPhaseBannerOut = 8,
    kPhaseFadeOut = 9,
    kPhaseClosed = 10,
};

/* Functions of this module the C++ calls by address: ones with no C++ counterpart yet (they run as generated code) and callbacks. Names marked (guess) are by use. */
namespace fn {   /* thunks to functions of other modules: the module's own import stubs */
constexpr uint32_t DrawNumber = 0x08f0d7f4;           /* numero_desenhar_decimal: called through the guest so that mod hooks on it still run */
constexpr uint32_t GetSaveState = 0x08f0fc04;          /* save_estado: guest address of the save's state block */
constexpr uint32_t AddPlayerDP = 0x08f0fc44;           /* jogador_dp_somar(amount) */
constexpr uint32_t GetSaveEntryCount = 0x08f0fd74;     /* save_492c: how many entries the save's list holds */
constexpr uint32_t UnloadModule = 0x08f10724;          /* modsys_descarregar(id) */
constexpr uint32_t OnArchiveLoaded = 0x08f0a3c4;        /* the callback of the archive read */
}  // namespace fn

constexpr int kRewardCount = 4;   /* duel bonus, new high score bonus, challenge complete bonus, and the remainder */

/* A list of reward entries: how many, which ones, and how wide each name is. */
struct EntryList {
    uint16_t scroll;   /* first entry shown (the lists are scrolled with up/down) */
    uint16_t count;
    uint32_t items;    /* guest address of count halfwords */
    uint32_t widths;   /* guest address of count halfwords */
};
static_assert(sizeof(EntryList) == 12, "EntryList layout");

/* The screen's state block (0x1a4 bytes). Offsets were checked against the generated code: Ghidra's pseudo-C scales some of them. */
struct ResultScreen {
    uint16_t phase;            /* 0x00 ResultPhase */
    uint16_t step;             /* 0x02 sub-step counter inside a phase */
    uint8_t page;              /* 0x04 the low nibble is the page being shown */
    uint8_t unknown05[3];
    int32_t rewards[kRewardCount];   /* 0x08 DP of each category: duel bonus, high score, challenge, other */
    uint8_t titleState;        /* 0x18 bits 0-3: row of the title capsule's tabs, bits 4-5: capsule animation state */
    uint8_t unknown19;
    int16_t banner;            /* 0x1a the resource of the outcome banner (kWinBanner..kDrawBanner), -1 while there is none */
    uint8_t flags;             /* 0x1c */
    uint8_t unknown1d;
    uint16_t slide;            /* 0x1e how far the pages have slid, in sixteenths of the screen width */
    EntryList lists[4];        /* 0x20 */
    uint32_t stats;            /* 0x50 */
    uint32_t heap;             /* 0x54 where the screen allocates from */
    libtitle::Window window;   /* 0x58 the question window ("duel again?") */
    uint8_t windowState;       /* 0x178 bits 0-1: question state, bit 2: answered */
    uint8_t unknown179;
    uint16_t networkPlayerTarget;   /* 0x17a how many answers a network duel waits for */
    uint8_t flags17c;          /* 0x17c bit 0: network scene, bit 1: archive requested */
    uint8_t unknown17d[3];
    uint32_t archive;          /* 0x180 the loaded result archive */
    uint32_t resources[kResourceCount];   /* 0x184 handles found in the archive, 0 until first used */
};
static_assert(sizeof(ResultScreen) == 0x1a4, "ResultScreen layout");

class DuelResult {
public:
    explicit DuelResult(SwapCtx &x) : x_(x) {}

    uint32_t GetResource(uint16_t index);   /* FUN_08f0a420: the handle of a resource, found in the archive on first use */
    /* numero_desenhar_decimal (08f0d7f4): right-aligned decimal number, one glyph per digit, moving left by advance; returns the cursor */
    uint32_t DrawNumber(uint32_t cur, int32_t x, int32_t y, int32_t value, uint32_t advance, uint32_t maxDigits, bool padZeros);
    /* The same, called as the original does (through the guest, with the original's return address) so a mod hooking it, like the DP multiplier, still sees the call. */
    uint32_t DrawNumberFrom(uint32_t returnAddress, uint32_t cur, int32_t x, int32_t y, int32_t value, uint32_t advance, uint32_t maxDigits, bool padZeros);
    uint32_t DrawCategoryRow(uint32_t cur, int32_t x, int32_t y, int32_t category);   /* duelo_resultado_linha_desenhar (08f0b5f8) */
    uint32_t DrawCategories(uint32_t cur, int32_t slide);                              /* duelo_resultado_categorias_desenhar (08f0ae04) */
    uint32_t DrawBackground(uint32_t cur);                                             /* FUN_08f0a498 */
    uint32_t DrawTitleBar(uint32_t cur);                                               /* FUN_08f0a51c */
    uint32_t DrawPages(uint32_t cur);                                                  /* FUN_08f0ac74: the page(s) of the current phase */
    uint32_t DrawFooter(uint32_t cur);                                                 /* FUN_08f0a7e8: the dark bar with the neighbouring pages' names */
    void DrawScreen();                                                                 /* FUN_08f092c0: records the screen's lists, once per frame */
    uint32_t LoadArchive();                                                            /* FUN_08f0a2bc: the loaded archive, or 0 (and starts loading it) */
    bool OnArchiveLoaded(uint32_t archive, int32_t result);                            /* FUN_08f0a3c4: completion callback of the load */
    void SetTitleBarState(uint16_t state);                                             /* FUN_08f094ac: plays the part of the capsule's animation for state 0..3 */
    bool TitleBarAnimationDone();                                                      /* FUN_08f0955c */
    void MeasureEntryWidths();                                                         /* FUN_08f09ddc: the width of every entry's name, in text lines */
    void CreateQuestionWindow(int16_t kind);                                           /* FUN_08f09fb4 */
    /* the three detail pages (the table at kPageDrawTable, entries 1..3): the entries a duel earned, ten lines at a time */
    uint32_t DrawDuelBonusPage(uint32_t cur, int32_t slide);                           /* FUN_08f0b8d4 */
    uint32_t DrawHighScorePage(uint32_t cur, int32_t slide);                           /* FUN_08f0bd0c */
    uint32_t DrawChallengePage(uint32_t cur, int32_t slide);                           /* FUN_08f0c350 */
    uint32_t DrawEntryLabel(uint32_t cur, int32_t x, int32_t &y, int32_t height, uint32_t nameTable, uint32_t item, int32_t nameWidth);   /* shared by the three entry rows */
    uint32_t DrawDuelBonusEntry(uint32_t cur, int32_t x, int32_t y, int32_t entry);    /* FUN_08f0ba90 */
    uint32_t DrawHighScoreEntry(uint32_t cur, int32_t x, int32_t y, int32_t entry);    /* FUN_08f0bf20 */
    uint32_t DrawChallengeEntry(uint32_t cur, int32_t x, int32_t y, int32_t entry);    /* FUN_08f0c50c */
    uint32_t DrawPageFrame(uint32_t cur, int32_t slide, int32_t sheetU, int32_t sheetV);   /* FUN_08f0cdcc: the frame of a detail page */
    uint32_t DrawPageSummary(uint32_t cur, int32_t slide, int32_t page);               /* FUN_08f0d240: title, total, scroll bar and the page arrows */
    uint32_t DrawBox(uint32_t cur, int32_t x, int32_t y, int32_t w, int32_t h, int32_t sheetU, int32_t sheetV);   /* FUN_08f0d530: a box from the sheet (3 pieces when 14 high, else 9) */
    uint32_t DrawScrollBar(uint32_t cur, int32_t x, int32_t y, int32_t h, int32_t position, int32_t visible, int32_t total);   /* FUN_08f0dbc4 */
    uint32_t DrawArrow(uint32_t cur, int32_t x, int32_t y, bool down, uint32_t period, bool blink, bool solid, uint32_t frame, uint32_t sheet);   /* FUN_08f0d940 */
    void DrawFittedText(int32_t x, int32_t y, int32_t width, uint32_t fontWidth, uint32_t fontHeight, uint32_t text);   /* FUN_08f0de9c: centred, shrunk to fit */
    /* the outcome board (phases 6..9, story duels): the duelists' name plates and a win / lose / draw badge for each side */
    uint32_t DrawOutcomePages(uint32_t cur);                                           /* FUN_08f0c764 */
    uint32_t DrawNamePlate(uint32_t cur, int32_t x, int32_t y, bool lower, uint32_t name);   /* FUN_08f0df54 */
    uint32_t DrawResultBadge(uint32_t cur, int32_t x, int32_t y, int16_t kind);        /* FUN_08f0e10c: kind 1 win, 2 lose, 3 draw */
    uint32_t DrawBanner(uint32_t cur);                                                /* FUN_08f0a714: the outcome banner's sprites, drawn while it plays */
    void UpdateSlide();                                                                /* FUN_08f09d48: phase 4, one step of the page slide */
    int16_t GetDuelOutcome();                                                          /* FUN_08f0979c: 0 none, 1 win, 2 lose, 3 draw */
    bool IsBannerFinished();                                                           /* FUN_08f09710 */
    void StartBanner(int16_t kind);                                                    /* FUN_08f095cc: plays the part of the banner's animation for kind 0..3 */
    void SendNetworkStep(uint8_t kind, uint8_t argument);                              /* FUN_08f0a25c */
    uint32_t Update();                                                                 /* FUN_08f08a1c: once per frame; 0 while open, 1 / 2 when it closes (2: the question was answered) */
    void Start();                                                                      /* duelo_recompensa_calcular (08f08534): computes the rewards, adds the DP, starts loading */
    void Finish();                                                                     /* FUN_08efd3e8: unloads what the duel loaded and ends the scene */
    void HandlePageInput();                                                            /* FUN_08f09938: phase 3, browsing the pages */
    void Free();                                                                       /* FUN_08f093ec: releases the screen's memory */

private:
    DuelStatistics &statistics() { return x_.at<DuelStatistics>(screen().stats); }
    SettingsBlock &settings() { return x_.at<SettingsBlock>(ehsys::GetSettingsBlock(x_)); }
    StartRequest &request() { return x_.at<StartRequest>(ehsys::GetStartRequest(x_)); }
    ResultScreen &screen() { return x_.at<ResultScreen>(x_.at<uint32_t>(kResultScreenPointer)); }
    uint32_t moduleAddress(uint32_t address) { return x_.addr(address - kModuleBase); }
    bool SpriteAnimationDone(uint32_t sprites);
    uint32_t PlaySprites(uint32_t cur, uint32_t sprites);   /* draws a sprite resource at the current frame of the animation */
    uint32_t Text(uint32_t index) { return ehsys::GetText(x_, GetResource(kTexts), index); }
    SwapCtx &x_;
};

}  // namespace duel_mgr
