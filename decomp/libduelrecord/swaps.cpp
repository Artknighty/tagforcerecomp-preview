/* Glue between the PSP calling convention and the native functions of libduelrecord. */
#include "../swap.h"
#include "records.h"
#include "../ehsys.h"
#include <stddef.h>

#define MODULE "libduelrecord_08eeb400"

/* FUN_00000614(key, table, count, stride) */
static void swap_FindEntryIndex(SwapCtx &x) {
    x.ret((uint32_t)FindEntryIndex((uint16_t)x.arg(0), x.mem(x.arg(1)), (int32_t)x.arg(2), (int32_t)x.arg(3)));
}
static SwapReg reg_FindEntryIndex({"libduelrecord.FindEntryIndex", MODULE, 0x614, swap_FindEntryIndex, true});

/* FUN_0000299c(tab) */
static void swap_MaxScrollPosition(SwapCtx &x) {
    const uint32_t tab = x.arg(0) & 0xffff;
    const uint32_t state = x.addr(kRankingStateOffset);
    const uint32_t count = x.at<uint16_t>(state + offsetof(RankingState, rowCount) + 2 * tab);
    const uint32_t rows = x.at<uint32_t>(state + offsetof(RankingState, rows) + 4 * tab);
    x.ret((uint32_t)MaxScrollPosition(count ? (const RankingRow *)x.mem(rows) : nullptr, count));
}
static SwapReg reg_MaxScrollPosition({"libduelrecord.MaxScrollPosition", MODULE, 0x299c, swap_MaxScrollPosition, true});

/* FUN_000028e0(tab, item): the label tables sit in the module data, one per tab */
static void swap_LabelIndex(SwapCtx &x) {
    static const struct { uint32_t offset, count; } kTables[kTabCount] = {{0x6608, 0x28}, {0x6658, 0x53}, {0x6700, 0x75}};
    const int16_t tab = (int16_t)x.arg(0);
    if (tab < 0 || tab >= kTabCount) { x.ret(0); return; }
    x.ret(LabelIndex((const uint16_t *)x.mem(x.addr(kTables[tab].offset)), kTables[tab].count, (uint16_t)x.arg(1)));
}
static SwapReg reg_LabelIndex({"libduelrecord.LabelIndex", MODULE, 0x28e0, swap_LabelIndex, true});

/* ---- lifecycle: the module's state machine, texture cache and database loading ---- */
#include "duelrecord.h"

static void swap_Init(SwapCtx &x) { DuelRecord(x).Init(x.arg(0)); }
static SwapReg reg_Init({"libduelrecord.Init", MODULE, 0x8, swap_Init});

static void swap_Update(SwapCtx &x) { x.ret(DuelRecord(x).Update()); }
static SwapReg reg_Update({"libduelrecord.Update", MODULE, 0xfc, swap_Update});

static void swap_InitTextureSlots(SwapCtx &x) { DuelRecord(x).InitTextureSlots(); }
static SwapReg reg_InitTextureSlots({"libduelrecord.InitTextureSlots", MODULE, 0x28c, swap_InitTextureSlots});

static void swap_Unload(SwapCtx &x) { x.ret(DuelRecord(x).Unload()); }
static SwapReg reg_Unload({"libduelrecord.Unload", MODULE, 0x204, swap_Unload});

static void swap_FreeDatabase(SwapCtx &x) { DuelRecord(x).FreeDatabase(); }
static SwapReg reg_FreeDatabase({"libduelrecord.FreeDatabase", MODULE, 0x398, swap_FreeDatabase});

static void swap_LoadStep(SwapCtx &x) { x.ret(DuelRecord(x).LoadStep()); }
static SwapReg reg_LoadStep({"libduelrecord.LoadStep", MODULE, 0x2f0, swap_LoadStep});

static void swap_StartDatabaseLoad(SwapCtx &x) { x.ret(DuelRecord(x).StartDatabaseLoad()); }
static SwapReg reg_StartDatabaseLoad({"libduelrecord.StartDatabaseLoad", MODULE, 0x3d8, swap_StartDatabaseLoad});

static void swap_GetTexture(SwapCtx &x) { x.ret(DuelRecord(x).GetTexture((uint16_t)x.arg(0))); }
static SwapReg reg_GetTexture({"libduelrecord.GetTexture", MODULE, 0x488, swap_GetTexture});

static void swap_OnDatabaseLoaded(SwapCtx &x) { x.ret(DuelRecord(x).OnDatabaseLoaded(x.arg(0), (int32_t)x.arg(1))); }
static SwapReg reg_OnDatabaseLoaded({"libduelrecord.OnDatabaseLoaded", MODULE, 0x574, swap_OnDatabaseLoaded});

static void swap_HandleInput(SwapCtx &x) { DuelRecord(x).HandleInput(); }
static SwapReg reg_HandleInput({"libduelrecord.HandleInput", MODULE, 0x26bc, swap_HandleInput});

static void swap_BuildRows(SwapCtx &x) { DuelRecord(x).BuildRows(); }
static SwapReg reg_BuildRows({"libduelrecord.BuildRows", MODULE, 0x20b0, swap_BuildRows});

/* ---- drawing ---- */
/* A draw call writes display-list commands after its cursor and the drawing state kept in ehsys globals: compared when verifying. */
static void drawRegions(SwapCtx &x, Regions &out) {
    const uint32_t base = x.at<uint32_t>(0x08903e88), size = x.at<uint32_t>(0x08903e78);
    if (base && size && size <= 0x200000) out.push_back({base, size});
    out.push_back({0x08903e70, 0xe0});
    const uint32_t ctx = x.at<uint32_t>(0x088ed190);
    if (ctx) out.push_back({ctx + 0x50c10, 0x50});
    out.push_back({0x08bf0ccc, 0x20});   /* fade progress: advanced by the fade overlay call */
    out.push_back({ehsys::FrameCounter(x), 16});   /* the frame number changes while a frame is recorded */
}

static void swap_Draw(SwapCtx &x) { DuelRecord(x).Draw(); }
static SwapReg reg_Draw({"libduelrecord.Draw", MODULE, 0x690, swap_Draw, false, drawRegions});

/* The drawing helpers are only called from Draw (natively), but are swapped too so every function of the module has its C++ counterpart
 * and any of them can be selected with TF_DECOMP. */
#define DRAW_SWAP(NAME, OFFSET, CALL) \
    static void swap_##NAME(SwapCtx &x) { DuelRecord d(x); x.ret(CALL); } \
    static SwapReg reg_##NAME({"libduelrecord." #NAME, MODULE, OFFSET, swap_##NAME})
DRAW_SWAP(FillBackground, 0x9dc, d.FillBackground(x.arg(0)));
DRAW_SWAP(DrawHeader, 0xa20, d.DrawHeader(x.arg(0)));
DRAW_SWAP(DrawTabs, 0xd00, d.DrawTabs(x.arg(0), x.arg(1), x.arg(2), x.arg(3), x.arg(4)));
DRAW_SWAP(DrawRow, 0x11e8, d.DrawRow(x.arg(0), x.arg(1), x.arg(2), x.arg(3)));
DRAW_SWAP(DrawPanel, 0x150c, d.DrawPanel(x.arg(0), x.arg(1), x.arg(2), x.arg(3), x.arg(4)));
DRAW_SWAP(DrawValue, 0x1880, d.DrawValue(x.arg(0), x.arg(1), x.arg(2), x.arg(3)));
/* the ninth argument (the texture) is passed on the guest stack */
DRAW_SWAP(DrawArrow, 0x1b5c, d.DrawArrow(x.arg(0), x.arg(1), x.arg(2), (x.arg(3) & 0xff) == 1, x.arg(4), x.arg(5) != 0, x.arg(6) != 0, x.arg(7), x.at<uint32_t>(x.c->r[29])));
DRAW_SWAP(DrawScrollBar, 0x1de0, d.DrawScrollBar(x.arg(0), x.arg(1), x.arg(2), x.arg(3), x.arg(4), x.arg(5), x.arg(6)));

static void swap_DrawNumber(SwapCtx &x) { DuelRecord(x).DrawNumber(x.arg(0), x.arg(1), x.arg(2), x.arg(3), x.arg(4), x.arg(5) != 0); }
static SwapReg reg_DrawNumber({"libduelrecord.DrawNumber", MODULE, 0x1770, swap_DrawNumber});

/* ---- small ones ---- */
/* 0x0: nothing to do (the module has no exit work) */
static void swap_NoOp(SwapCtx &x) { x.ret(0); }
static SwapReg reg_NoOp({"libduelrecord.NoOp", MODULE, 0x0, swap_NoOp, true});

/* 0x3a0: FreeDatabase is a jump to here; both entries do the same */
static SwapReg reg_FreeDatabaseBody({"libduelrecord.FreeDatabaseBody", MODULE, 0x3a0, swap_FreeDatabase});

/* 0x604: the comparison used to sort the texture slots by key */
static void swap_CompareTextureSlots(SwapCtx &x) {
    x.ret((uint32_t)((int32_t)x.at<uint16_t>(x.arg(0)) - (int32_t)x.at<uint16_t>(x.arg(1))));
}
static SwapReg reg_CompareTextureSlots({"libduelrecord.CompareTextureSlots", MODULE, 0x604, swap_CompareTextureSlots, true});
