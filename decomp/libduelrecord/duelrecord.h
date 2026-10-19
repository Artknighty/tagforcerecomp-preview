/* libduelrecord: the Duel Ranking screen. The game keeps its state in the module's data (guest memory), so this class is a typed view
 * over it plus the screen's behaviour; each method names the original function it replaces. */
#pragma once
#include "../common/swap.h"
#include "ranking_state.h"

class DuelRecord {
public:
    explicit DuelRecord(SwapCtx &x) : x_(x) {}

    void Init(uint32_t heap);                 /* libduelrecord_6ADB95DB (module entry point, offset 0x8) */
    bool Update();                            /* libduelrecord_420F9405 (offset 0xfc): once per frame, true when the screen has closed */
    void BuildRows();                         /* FUN_000020b0: builds the rows of every tab from the save data */
    void Draw();                              /* libduelrecord_8D53DC42 (offset 0x690): records the screen's display lists, once per frame */
    uint32_t FillBackground(uint32_t cur);                 /* FUN_000009dc */
    uint32_t DrawHeader(uint32_t cur);                     /* FUN_00000a20: title bar with the completion percentage */
    uint32_t DrawTabs(uint32_t cur, int32_t x, int32_t y, int32_t w, int32_t h);   /* FUN_00000d00: the window frame and the tab headers */
    uint32_t DrawRow(uint32_t cur, int32_t x, int32_t y, uint32_t row);            /* FUN_000011e8 */
    uint32_t DrawPanel(uint32_t cur, int32_t x, int32_t y, int32_t w, int32_t h);  /* FUN_0000150c: a framed box */
    void DrawNumber(int32_t x, int32_t y, int32_t value, uint32_t digits, uint32_t advance, bool padZeros);   /* FUN_00001770: digit by digit, right to left */
    uint32_t DrawValue(uint32_t cur, int32_t x, int32_t y, uint32_t row);          /* FUN_00001880: the value box of a row */
    uint32_t DrawArrow(uint32_t cur, int32_t x, int32_t y, bool down, uint32_t period, bool blink, bool solid, uint32_t frame, uint32_t texture);   /* FUN_00001b5c */
    uint32_t DrawScrollBar(uint32_t cur, int32_t x, int32_t y, int32_t h, int32_t position, int32_t visible, int32_t total);   /* FUN_00001de0 */
    void HandleInput();                       /* FUN_000026bc: tabs, scrolling and leaving the screen */
    void InitTextureSlots();                  /* FUN_0000028c */
    bool Unload();                            /* FUN_00000204 */
    void FreeDatabase();                      /* FUN_00000398 */
    bool LoadStep();                          /* FUN_000002f0: true once the database is loaded */
    uint32_t StartDatabaseLoad();             /* FUN_000003d8: the loaded database, or 0 (and starts loading it) */
    uint32_t GetTexture(uint16_t key);        /* FUN_00000488 */
    bool OnDatabaseLoaded(uint32_t buffer, int32_t result);   /* FUN_00000574: completion callback of the load */

private:
    RankingState &state() { return x_.at<RankingState>(x_.addr(kRankingStateOffset)); }
    TextureSlot *slots() { return (TextureSlot *)x_.mem(x_.addr(kTextureSlotsOffset)); }
    int32_t MaxScroll(uint32_t tab);
    uint32_t NewRows(uint32_t count);
    void BuildStatusRows(uint32_t tab, uint32_t saveOffset, uint32_t plainStyle, int32_t lineHeight, int32_t &shown, int32_t &total);
    uint8_t RowHeight(uint32_t style, uint16_t tab, uint16_t item, int32_t lineHeight);
    SwapCtx &x_;
};
