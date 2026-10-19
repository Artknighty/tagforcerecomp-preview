# libduelrecord: the Duel Ranking screen

Main Menu > Duel Ranking. A tabbed list of statistics ("High Score", "Bonus", and a third tab shown only once save flag 7 is set), each row with a number, a label and optionally a value, plus a completion percentage in the title bar and a scroll bar.

All 27 functions of the module are replaced by C++ here (`TF_DECOMP`, see `docs/DECOMP.md`). The game keeps the screen's state in the module's data, so `RankingState` (`ranking_state.h`) is a typed view over it.

## Life cycle (`duelrecord.cpp`)

`Init` sets up the state, takes a heap (given by the caller, or a private 2 MB kernel pool when none is given) and sorts the texture slots. `Update` runs once per frame and moves through phases stored in `RankingState::phase`:

| phase | what happens |
|---|---|
| 0 | load `database/duelrecord.ehp` (asynchronously: `StartDatabaseLoad` requests it and `OnDatabaseLoaded` is the completion callback), build the rows, start the fade in |
| 1 | wait for the fade in |
| 2 | `HandleInput`: tabs with left/right/L/R/square, scrolling with up/down (a fresh press at an end wraps around), circle/cross leave |
| 3 | wait for the fade out |
| 4 | `Unload`: free the database and the rows, give back the kernel pool; `Update` returns true and the screen is over |

`GetTexture` is a small cache of three texture sheets looked up by key (a binary search over the sorted slots) and created on first use from file names that contain the language letter.

## Rows (`BuildRows`)

Each tab has a static table in the module data: one u16 per item, bit 0 = the item exists. `BuildRows` turns it into 16-byte `RankingRow`s taken from the heap:

- High Score: every item that exists, with its number from the save data and a "new" mark.
- Bonus and the third tab: only items whose status byte in the save is non-zero (1 = just unlocked, shown with the blinking "new" mark once, then set to 2). In Bonus the first three items show a number: cards fully owned, player level, and the number of rows in the third tab.
- The height of a row is the width of its label divided by the line height; the scroll bar and the number of visible rows are counted in these units.
- The completion percentage is rows built over items that exist.

## Drawing (`draw.cpp`)

`Draw` records three display lists: 0 clears the screen, 4 is the screen (background, header, window frame and tabs, scroll bar, then the visible rows clipped by a scissor), 5 is the fade overlay. Panels and frames are built from a corner/edge/centre sprite set (`DrawPanel`, `DrawTabs`, `DrawScrollBar`); numbers are drawn one digit at a time from the right (`DrawNumber`); one row, `Time played`, shows minutes and seconds.

## How it was checked

`tools/decomp/decomp_check.sh` replays a recorded session that goes through the screen: with every swap on and `TF_DECOMP_VERIFY=1` the swap and the original are run from the same state and their results compared (for `Draw`, the whole display-list buffer and the drawing state), then the captures are compared with the generated code's.
