# `libduel_mgr`: the Duel Result screen

`libduel_mgr` is loaded at `0x08EFCA00` with `libduel_draw` and `libduel_eng` for a duel. Its code that runs after the duel is the **Duel Result** screen: the reward table (pages of categories), the DP total, the outcome banner and the "duel again?" question. The module has 297 own functions; this directory covers the screen's own state machine and its drawing.

The screen keeps its state in one 0x1A4-byte block (`ResultScreen`, `result_screen.h`) allocated when it starts; the global at `0x09A151A4` holds its address. **Ghidra's pseudo-C scales some offsets of that pointer by two** (it types the global as `short *` in some functions and as an integer in others): the offsets in `ResultScreen` were checked against the generated code with `tools/decomp_acessos.py libduel_mgr <address>`, which lists every memory access with its literal offset. Do the same before trusting a field in new code. Arguments that Ghidra hides (a register left from an earlier call) are the other trap: `FUN_08f094ac()` at the start of the answer phase really is `SetTitleBarState(3)` (read the generated code).

## Mods that hook this module

The Cheats mod (DP multiplier) hooks `Start` (08f08534) and `DrawNumber` (08f0d7f4, scaling by the caller's return address). A swapped page must therefore call `DrawNumber` through the guest with the original return address (`DuelResult::DrawNumberFrom`), not as a plain C++ call, or the multiplied DP rows would show the unmultiplied value. The tests run with the same mods as the player's (a x3 multiplier explained a long "10 vs 30" hunt).

## Files

| File | Contents |
|---|---|
| `result_screen.h` | `ResultScreen` layout, the phases, the resource list, `DuelResult` (the screen's behaviour) |
| `result_draw.cpp` | drawing: resources, numbers, the categories page, background, title capsule, page area, footer bar, the screen's display lists |
| `result_pages.cpp` | the three detail pages (duel bonus, high scores, challenges) and the pieces they share: boxes, scroll bar, arrows, fitted text, the outcome banner |
| `result_logic.cpp` | loading the archive, the title capsule's animation, the question window, page input, the per-frame state machine, releasing the memory |
| `swaps.cpp` | the registration of each function as a Swap (names, offsets, verify regions) |

## Swaps

| C++ | Original | What it does |
|---|---|---|
| `GetResource` | `FUN_08f0a420` | handle of a file of the result archive, found on first use |
| `DrawNumber` | `numero_desenhar_decimal` (`08f0d7f4`) | right-aligned decimal number |
| `DrawCategoryRow` | `duelo_resultado_linha_desenhar` (`08f0b5f8`) | one row of the reward table |
| `DrawCategories` | `duelo_resultado_categorias_desenhar` (`08f0ae04`) | the table page: frame, rows, column titles, total |
| `DrawBackground` / `DrawTitleBar` | `08f0a498` / `08f0a51c` | animated background; title capsule and its tabs |
| `DrawPages` | `08f0ac74` | the page(s) of the current phase, with the slide between two pages |
| `DrawFooter` | `08f0a7e8` | the dark bar with the neighbouring pages' names |
| `DrawScreen` | `08f092c0` | records the screen's display lists once per frame |
| `LoadArchive` / `OnArchiveLoaded` | `08f0a2bc` / `08f0a3c4` | starts the read of `duel/result/result_<letter>.ehp`; its callback |
| `SetTitleBarState`, `TitleBarAnimationDone` | `08f094ac`, `08f0955c` | the capsule's animation by state |
| `MeasureEntryWidths` | `08f09ddc` | width of each entry's name in lines (for scrolling) |
| `CreateQuestionWindow` | `08f09fb4` | the "duel again?" window (kind 1..3) |
| `HandlePageInput` | `08f09938` | phase 3: L/R change page, up/down scroll, confirm leaves |
| `UpdateSlide`, `IsBannerFinished`, `StartBanner`, `GetDuelOutcome`, `SendNetworkStep` | `08f09d48`, `08f09710`, `08f095cc`, `08f0979c`, `08f0a25c` | slide, outcome banner, network step |
| `Update` | `08f08a1c` | the state machine: phases 0..10 and the question window |
| `Free` | `08f093ec` | releases the screen's memory |
| `DrawDuelBonusPage`, `DrawHighScorePage`, `DrawChallengePage` | `08f0b8d4`, `08f0bd0c`, `08f0c350` | the detail pages (entries 1..3 of the page table at `0x08F147F4`) |
| `DrawDuelBonusEntry`, `DrawHighScoreEntry`, `DrawChallengeEntry` | `08f0ba90`, `08f0bf20`, `08f0c50c` | one line of a detail page |
| `DrawPageFrame`, `DrawPageSummary`, `DrawBox`, `PageScrollBar`, `PageArrow`, `DrawFittedText`, `DrawBanner` | `08f0cdcc`, `08f0d240`, `08f0d530`, `08f0dbc4`, `08f0d940`, `08f0de9c`, `08f0a714` | the frame, the page's title and total, a box from the sheet, the scroll bar, its arrows, centred shrunk text, the outcome banner |

The start (`Start`, `08f08534`: it computes the rewards and adds the DP; the Cheats mod hooks it too) and the end (`Finish`, `08efd3e8`) are swapped as well (`swaps.cpp`, "the start and the end").

The outcome board shown after story duels (phases 6..9) is in C++ too: `DrawOutcomePages` (`08f0c764`: frame, captions, the name plates and the three rounds' badges), `DrawNamePlate` (`08f0df54`) and `DrawResultBadge` (`08f0e10c`; kind 1 win, 2 lose, 3 draw). Nothing of the screen's own code runs as generated code any more. The board reads the duelists' names from the settings block (`+0x130`, `0x18c` bytes each), `teamMode` (`+0x11a`, 2 = four duelists) and `StartRequest::results`.

## Verification

`sh tools/decomp_resultado_banner.sh [2|4]` covers the outcome board: a Free Duel never reaches it, so the script pokes `settings+0x119 = 2` (result mode with outcome), the three round results (`StartRequest+0x36`: win, lose, draw) and, for `4`, `settings+0x11a = 2`. Six captures through the banner phases, decomp off vs on: pixel-identical in both modes (name plates, badges and captions visible in the captures), no `MISMATCH`, no crash. The 14 captures below stay identical with the new swaps.

`sh tools/decomp_resultado.sh` plays a Free Duel to its end, then forces the second and third reward pages on (`pokectx` writes two rewards into the screen's block), browses all four pages with captures in the middle of each slide, scrolls, confirms and answers the question, three times: decomp off, on, and on with `TF_DECOMP_VERIFY=1`. Captures are compared pixel by pixel (`gen/cmp_decomp/`): all fourteen are identical. The verify run found one `DrawScreen` memory difference at the frame where a capture was taken (the capture reads the list); a 900-frame run without captures reported none, and every drawing function verified alone reports none either.
