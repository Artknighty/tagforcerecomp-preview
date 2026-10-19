/* Glue between the PSP calling convention and the native functions of libduel_mgr's Duel Result screen. */
#include "../common/swap.h"
#include "result_screen.h"

#define MODULE "libduel_mgr"

using namespace duel_mgr;

static constexpr uint32_t off(uint32_t address) { return address - kModuleBase; }

/* A draw call writes display-list commands after its cursor and the drawing state kept in ehsys globals: compared when verifying. */
static void drawRegions(SwapCtx &x, Regions &out) {
    const uint32_t base = x.at<uint32_t>(0x08903e88), size = x.at<uint32_t>(0x08903e78);
    if (base && size && size <= 0x200000) out.push_back({base, size});
    out.push_back({0x08903e70, 0xe0});
    const uint32_t ctx = x.at<uint32_t>(0x088ed190);
    if (ctx) out.push_back({ctx + 0x50c10, 0x50});
    out.push_back({0x08bf0ccc, 0x20});
    out.push_back({ehsys::FrameCounter(x), 16});
    const uint32_t screen = x.at<uint32_t>(kResultScreenPointer);
    if (screen) out.push_back({screen + offsetof(ResultScreen, resources), sizeof(ResultScreen::resources)});   /* the resource handles are cached on first use */
}

/* FUN_08f0a420(index) */
static void swap_GetResource(SwapCtx &x) { x.ret(DuelResult(x).GetResource((uint16_t)x.arg(0))); }
static SwapReg reg_GetResource({"libduel_mgr.GetResource", MODULE, off(0x08f0a420), swap_GetResource, false, drawRegions});

/* numero_desenhar_decimal(cursor, x, y, value, advance, maxDigits, padZeros) */
static void swap_DrawNumber(SwapCtx &x) {
    x.ret(DuelResult(x).DrawNumber(x.arg(0), (int32_t)x.arg(1), (int32_t)(int16_t)x.arg(2), (int32_t)x.arg(3), x.arg(4), x.arg(5), x.arg(6) != 0));
}
static SwapReg reg_DrawNumber({"libduel_mgr.DrawNumber", MODULE, off(0x08f0d7f4), swap_DrawNumber, false, drawRegions});

/* duelo_resultado_linha_desenhar(cursor, x, y, category) */
static void swap_DrawCategoryRow(SwapCtx &x) { x.ret(DuelResult(x).DrawCategoryRow(x.arg(0), (int32_t)x.arg(1), (int32_t)x.arg(2), (int32_t)x.arg(3))); }
static SwapReg reg_DrawCategoryRow({"libduel_mgr.DrawCategoryRow", MODULE, off(0x08f0b5f8), swap_DrawCategoryRow, false, drawRegions});

/* duelo_resultado_categorias_desenhar(cursor, slide) */
static void swap_DrawCategories(SwapCtx &x) { x.ret(DuelResult(x).DrawCategories(x.arg(0), (int32_t)x.arg(1))); }
static SwapReg reg_DrawCategories({"libduel_mgr.DrawCategories", MODULE, off(0x08f0ae04), swap_DrawCategories, false, drawRegions});

/* FUN_08f0a498(cursor) */
static void swap_DrawBackground(SwapCtx &x) { x.ret(DuelResult(x).DrawBackground(x.arg(0))); }
static SwapReg reg_DrawBackground({"libduel_mgr.DrawBackground", MODULE, off(0x08f0a498), swap_DrawBackground, false, drawRegions});

/* FUN_08f0a51c(cursor) */
static void swap_DrawTitleBar(SwapCtx &x) { x.ret(DuelResult(x).DrawTitleBar(x.arg(0))); }
static SwapReg reg_DrawTitleBar({"libduel_mgr.DrawTitleBar", MODULE, off(0x08f0a51c), swap_DrawTitleBar, false, drawRegions});

/* ---- the rest of the drawing and the screen's helpers ---- */
/* FUN_08f0ac74(cursor) */
static void swap_DrawPages(SwapCtx &x) { x.ret(DuelResult(x).DrawPages(x.arg(0))); }
static SwapReg reg_DrawPages({"libduel_mgr.DrawPages", MODULE, off(0x08f0ac74), swap_DrawPages, false, drawRegions});

/* FUN_08f0a7e8(cursor) */
static void swap_DrawFooter(SwapCtx &x) { x.ret(DuelResult(x).DrawFooter(x.arg(0))); }
static SwapReg reg_DrawFooter({"libduel_mgr.DrawFooter", MODULE, off(0x08f0a7e8), swap_DrawFooter, false, drawRegions});

/* FUN_08f092c0() */
static void swap_DrawScreen(SwapCtx &x) { DuelResult(x).DrawScreen(); }
static SwapReg reg_DrawScreen({"libduel_mgr.DrawScreen", MODULE, off(0x08f092c0), swap_DrawScreen, false, drawRegions});

/* FUN_08f0a2bc() */
static void swap_LoadArchive(SwapCtx &x) { x.ret(DuelResult(x).LoadArchive()); }
static SwapReg reg_LoadArchive({"libduel_mgr.LoadArchive", MODULE, off(0x08f0a2bc), swap_LoadArchive});

/* FUN_08f0a3c4(archive, result): the read's completion callback */
static void swap_OnArchiveLoaded(SwapCtx &x) { x.ret(DuelResult(x).OnArchiveLoaded(x.arg(0), (int32_t)x.arg(1))); }
static SwapReg reg_OnArchiveLoaded({"libduel_mgr.OnArchiveLoaded", MODULE, off(0x08f0a3c4), swap_OnArchiveLoaded});

/* FUN_08f094ac(state) */
static void swap_SetTitleBarState(SwapCtx &x) { DuelResult(x).SetTitleBarState((uint16_t)x.arg(0)); }
static SwapReg reg_SetTitleBarState({"libduel_mgr.SetTitleBarState", MODULE, off(0x08f094ac), swap_SetTitleBarState});

/* FUN_08f0955c() */
static void swap_TitleBarAnimationDone(SwapCtx &x) { x.ret(DuelResult(x).TitleBarAnimationDone() ? 1 : 0); }
static SwapReg reg_TitleBarAnimationDone({"libduel_mgr.TitleBarAnimationDone", MODULE, off(0x08f0955c), swap_TitleBarAnimationDone});

/* FUN_08f09ddc() */
static void swap_MeasureEntryWidths(SwapCtx &x) { DuelResult(x).MeasureEntryWidths(); }
static SwapReg reg_MeasureEntryWidths({"libduel_mgr.MeasureEntryWidths", MODULE, off(0x08f09ddc), swap_MeasureEntryWidths});

/* FUN_08f09fb4(kind) */
static void swap_CreateQuestionWindow(SwapCtx &x) { DuelResult(x).CreateQuestionWindow((int16_t)x.arg(0)); }
static SwapReg reg_CreateQuestionWindow({"libduel_mgr.CreateQuestionWindow", MODULE, off(0x08f09fb4), swap_CreateQuestionWindow});

/* FUN_08f09938() */
static void swap_HandlePageInput(SwapCtx &x) { DuelResult(x).HandlePageInput(); }
static SwapReg reg_HandlePageInput({"libduel_mgr.HandlePageInput", MODULE, off(0x08f09938), swap_HandlePageInput});

/* FUN_08f093ec() */
static void swap_Free(SwapCtx &x) { DuelResult(x).Free(); }
static SwapReg reg_Free({"libduel_mgr.Free", MODULE, off(0x08f093ec), swap_Free});

/* ---- the state machine and its small helpers ---- */
/* FUN_08f09d48() */
static void swap_UpdateSlide(SwapCtx &x) { DuelResult(x).UpdateSlide(); }
static SwapReg reg_UpdateSlide({"libduel_mgr.UpdateSlide", MODULE, off(0x08f09d48), swap_UpdateSlide});

/* FUN_08f0979c() */
static void swap_GetDuelOutcome(SwapCtx &x) { x.ret((uint32_t)(int32_t)DuelResult(x).GetDuelOutcome()); }
static SwapReg reg_GetDuelOutcome({"libduel_mgr.GetDuelOutcome", MODULE, off(0x08f0979c), swap_GetDuelOutcome});

/* FUN_08f09710() */
static void swap_IsBannerFinished(SwapCtx &x) { x.ret(DuelResult(x).IsBannerFinished() ? 1 : 0); }
static SwapReg reg_IsBannerFinished({"libduel_mgr.IsBannerFinished", MODULE, off(0x08f09710), swap_IsBannerFinished});

/* FUN_08f095cc(kind) */
static void swap_StartBanner(SwapCtx &x) { DuelResult(x).StartBanner((int16_t)x.arg(0)); }
static SwapReg reg_StartBanner({"libduel_mgr.StartBanner", MODULE, off(0x08f095cc), swap_StartBanner});

/* FUN_08f0a25c(kind, argument) */
static void swap_SendNetworkStep(SwapCtx &x) { DuelResult(x).SendNetworkStep((uint8_t)x.arg(0), (uint8_t)x.arg(1)); }
static SwapReg reg_SendNetworkStep({"libduel_mgr.SendNetworkStep", MODULE, off(0x08f0a25c), swap_SendNetworkStep});

/* FUN_08f08a1c(): the per-frame update */
static void swap_Update(SwapCtx &x) { x.ret(DuelResult(x).Update()); }
static SwapReg reg_Update({"libduel_mgr.Update", MODULE, off(0x08f08a1c), swap_Update});

/* ---- the detail pages and the pieces they are built from ---- */
#define PAGE_SWAP(NAME, ADDRESS, CALL)     static void swap_##NAME(SwapCtx &x) { DuelResult d(x); x.ret(CALL); }     static SwapReg reg_##NAME({"libduel_mgr." #NAME, MODULE, off(ADDRESS), swap_##NAME, false, drawRegions})
PAGE_SWAP(DrawDuelBonusPage, 0x08f0b8d4, d.DrawDuelBonusPage(x.arg(0), (int32_t)x.arg(1)));
PAGE_SWAP(DrawHighScorePage, 0x08f0bd0c, d.DrawHighScorePage(x.arg(0), (int32_t)x.arg(1)));
PAGE_SWAP(DrawChallengePage, 0x08f0c350, d.DrawChallengePage(x.arg(0), (int32_t)x.arg(1)));
PAGE_SWAP(DrawDuelBonusEntry, 0x08f0ba90, d.DrawDuelBonusEntry(x.arg(0), (int32_t)x.arg(1), (int32_t)x.arg(2), (int32_t)x.arg(3)));
PAGE_SWAP(DrawHighScoreEntry, 0x08f0bf20, d.DrawHighScoreEntry(x.arg(0), (int32_t)x.arg(1), (int32_t)x.arg(2), (int32_t)x.arg(3)));
PAGE_SWAP(DrawChallengeEntry, 0x08f0c50c, d.DrawChallengeEntry(x.arg(0), (int32_t)x.arg(1), (int32_t)x.arg(2), (int32_t)x.arg(3)));
PAGE_SWAP(DrawPageFrame, 0x08f0cdcc, d.DrawPageFrame(x.arg(0), (int32_t)x.arg(1), (int32_t)x.arg(2), (int32_t)x.arg(3)));
PAGE_SWAP(DrawPageSummary, 0x08f0d240, d.DrawPageSummary(x.arg(0), (int32_t)x.arg(1), (int32_t)x.arg(2)));
PAGE_SWAP(DrawBox, 0x08f0d530, d.DrawBox(x.arg(0), (int32_t)x.arg(1), (int32_t)x.arg(2), (int32_t)x.arg(3), (int32_t)x.arg(4), (int32_t)x.arg(5), (int32_t)x.arg(6)));
PAGE_SWAP(PageScrollBar, 0x08f0dbc4, d.DrawScrollBar(x.arg(0), (int32_t)x.arg(1), (int32_t)x.arg(2), (int32_t)x.arg(3), (int32_t)x.arg(4), (int32_t)x.arg(5), (int32_t)x.arg(6)));
/* the ninth argument (the sheet) is passed on the guest stack */
PAGE_SWAP(PageArrow, 0x08f0d940, d.DrawArrow(x.arg(0), (int32_t)x.arg(1), (int32_t)x.arg(2), (x.arg(3) & 0xff) == 1, x.arg(4), x.arg(5) != 0, x.arg(6) != 0, x.arg(7), x.at<uint32_t>(x.c->r[29])));
PAGE_SWAP(DrawBanner, 0x08f0a714, d.DrawBanner(x.arg(0)));
PAGE_SWAP(DrawOutcomePages, 0x08f0c764, d.DrawOutcomePages(x.arg(0)));
PAGE_SWAP(DrawNamePlate, 0x08f0df54, d.DrawNamePlate(x.arg(0), (int32_t)x.arg(1), (int32_t)x.arg(2), x.arg(3) != 0, x.arg(4)));
PAGE_SWAP(DrawResultBadge, 0x08f0e10c, d.DrawResultBadge(x.arg(0), (int32_t)x.arg(1), (int32_t)x.arg(2), (int16_t)x.arg(3)));

/* FUN_08f0de9c(x, y, width, fontWidth, fontHeight, text) */
static void swap_DrawFittedText(SwapCtx &x) { DuelResult(x).DrawFittedText((int32_t)x.arg(0), (int32_t)x.arg(1), (int32_t)x.arg(2), x.arg(3) & 0xff, x.arg(4) & 0xff, x.arg(5)); }
static SwapReg reg_DrawFittedText({"libduel_mgr.DrawFittedText", MODULE, off(0x08f0de9c), swap_DrawFittedText, false, drawRegions});

/* ---- the start and the end ---- */
/* duelo_recompensa_calcular() */
static void swap_Start(SwapCtx &x) { DuelResult(x).Start(); }
static SwapReg reg_Start({"libduel_mgr.Start", MODULE, off(0x08f08534), swap_Start});

/* FUN_08efd3e8() */
static void swap_Finish(SwapCtx &x) { DuelResult(x).Finish(); }
static SwapReg reg_Finish({"libduel_mgr.Finish", MODULE, off(0x08efd3e8), swap_Finish});
