# libtitle (the title flow and its menus)

`libtitle` is the game module that runs everything between the Konami logo and the first duel or story scene: the logos, the opening movie, the
title screen and the menus (main menu, Free Duel settings, card list, Options, language). It is split by topic (one file each; the shared table of game functions and helpers is [internal.h](internal.h), the state
layout is [title_state.h](title_state.h)):

| File | What is in it |
|---|---|
| [flow.cpp](flow.cpp) | small helpers, the per-frame screen dispatcher, widget initialisers, resource slots, the logo steps |
| [loading.cpp](loading.cpp) | loading the title menu archives, flow initialisation, the per-screen step functions, fades, the title menu text layout |
| [screen_switch.cpp](screen_switch.cpp) | the main title list, text alignment, the language-change steps, the screen switch, the boot entry |
| [lists.cpp](lists.cpp) | the panel set, the list controller, list drawing (classic and PC-style buttons) |
| [menu_helpers.cpp](menu_helpers.cpp) | module start, main menu parts, styled text, the title sub lists, the limit list |
| [main_menu.cpp](main_menu.cpp) | the card list / options screen setup (screen 9) and the main menu (screen 8) |
| [name_box.cpp](name_box.cpp) | the in-game name box (not in the original game) |
| [panels.cpp](panels.cpp) | window frames and panels, and the PC style glass panels (`pcStyleWindow`) |
| [sub_lists.cpp](sub_lists.cpp) | sub list rows, the duel settings step, the card list entries, small steps of screen 9 |
| [options_screen.cpp](options_screen.cpp) | the Options screen and the Graphics screen |
| [language_screen.cpp](language_screen.cpp) | the card database language screen and the last functions |

A helper used by more than one file lives in [internal.h](internal.h) (declared there, defined in the file that owns it). Method, tools and rules: [docs/DECOMP.md](../../docs/DECOMP.md).

## Status

Every function of the module that the recorded sessions run is a Swap (192 of them): `tools/decomp/decomp_cobertura.sh <session> libtitle <last frame>` lists
nothing left for sessions `100343 102706 133113 154411 161505 161708 163734`. The module has 1704 functions in total; the rest belong to screens
no session has opened yet (they stay generated code until a session goes through them).

## How a frame runs

1. `Main` (`FUN_08efca34`) sets the flow up and calls `Tick` (`FUN_08eff908`) every frame until it reports the end.
2. `Tick` reads the pad, lets the screen's step function run (`...Step` swaps below) and publishes the screen code and `arg0` for the drawing.
3. `RunScreen` (`FUN_08f00238`) opens a display list, calls the screen's draw function (`Draw...`; each takes the list and returns the new one),
   closes the list and nudges the flow when a screen reports that it is finished.
4. `UpdateFlow` (`FUN_08efefa8`) commits the pending arguments and switches screen (`SwitchScreen`, `FUN_08eff100`) when a screen asked for it.

The flow state is a small block in the game's data area ([title_state.h](title_state.h)): the running `screen`, the requested `nextScreen` and three
arguments `arg0..arg2`, each with a pending value. Screens are state machines on `arg0` / `arg1` / `arg2` (their meaning is in each swap's comment).

## Screens

| Code | Screen | Step | Draw |
|---|---|---|---|
| 2 / 4 | logos, opening movie | `LogoSequence` | `DrawLogos` |
| 5 | title (demo) and its list: Continue, New Game, Exit to Desktop (`TitleMenuItem`; the last one is new, the first two swapped places) | `TitleScreen`, `TitleMenuInput` | `DrawTitleItem` |
| 8 | main menu | `MainMenuStep`, `MainMenuOwnStep` | `DrawMainMenu` |
| 9 | title lists (Free Duel, sub lists 3, 5, 6, limit list) | `EnterListScreen`, `MenuScreenStep`, `SubList3Input`, `SubList5Input`, `SubList6Input`, `LimitListScreen` | `DrawList`, `DrawSubList*Row` |
| 12 | card list / Free Duel settings | `OptionsScreenStep` (`EnterCardListScreen`, `EnterOptionsKind`, `DuelSettingsStep`) | `DrawOptionsScreen` > `DrawCardListPage` |
| 17 | Options | `OptionsStep`, `OptionsInput` | `DrawOptions` > `DrawOptionsRows` > `DrawOptionRow` |
| 17 | Graphics (new, not in the original game) | `OptionsStep`, `graphicsInput` | `DrawOptions` > `drawGraphicsScreen` |
| 19 | language settings | `LanguageSelectStep` | `DrawLanguageSelect` > `DrawLanguageRow` |

## Drawing building blocks

- **Display list**: ehsys threads one value through all drawing calls (each takes the list value and returns the next one). Every `Draw...` swap
  takes it as its first argument and returns the updated one in `r2`.
- **Windows** (`kWindowA/B/C`, 0x120 bytes) and **lists** (`kTitleList`, `kTitleList2`): blocks the engine draws and updates itself
  (`InitWindow`, `ListUpdate`, `DrawList`). The swaps fill their fields (see the comments of `InitList`, `InitWindow`, `ListUpdate`).
- **Menu lists** (`struct MenuList`, `enum MainMenuItem`): every title menu is one 0x7C-byte block with named fields (`itemState[i]`, `selected`, `x/y/width/rowHeight`, colours, `drawCallback`). To grey out a main menu item, set `m.itemState[kNetworkMode] = kItemDisabled` in `BuildTitleList`.
- **Panels** (`struct Panel`): a framed box of cells; `DrawPanel` draws the frame, a frame per cell, the label, the scroll bar and each cell through a
  callback. **Frames** (`DrawFrame`, `struct FramePart`) are nine-slice boxes cut from a sheet of 22-byte part records in the module image. The cursor box around a cell (`DrawFramedBox`, `drawCursorBox`) is a frame that pulses by the frame counter and blinks while a fade mode is on. The Free Duel settings page's Details list draws its cells with `DrawCardCell` (the player's answer to each question, from `FreeDuelSettings`).
- **Graphics screen** (new): the main menu's 7th item (the slot "Save Data" had, which only refreshed the list) opens an in-game version of the window's Graphics menu. It runs as screen 17 with `graphics.active` set (same fades, input step and return path as Options), draws the Options frame/sprites/font around one box (`drawGroupFrame`, `drawHighlightBar`, `drawHelpFrame`, shared with the Options screen) and texts the game does not have with `drawAscii`. The options come from `runtime/gpu/gfx_options.h` (label, help, choices, get/set), which applies and saves them. To add an option: add it to `GfxOption` and to the table in `runtime/gpu/gfx.cpp`; the screen lists it by itself (the box fits about 10 rows).
- **Name box** (new): the PSP typed the player's name on its own keyboard (`StartNameEntry` / `NameEntryStep` / the sceUtilityOsk call, still generated code). Here the name stage (screen 8, `arg0` 2) draws the main menu background, the name plate with the typed letters and a blinking caret, and the game's own message bar (`DrawMainMenu` > `nameBox`, `stepNameBox`, `closeNameBox`). The keys come from `runtime/ui/text_input.cpp` (`text_input_live`, `text_input_key`).
- **Options screen state** (`struct OptionsScreen`): 48 bytes from the heap; the four setting bytes are documented next to the struct.

## Conventions

- Readability (2026-10-11): engine calls (`nid::`), game functions (`fn::`), the flow state and every block the code touches have names (`State`, `Window`, `MenuList` + `ListState`, `TitleTextBox`, `PanelSet`, `OptionsWork`, `CardListTables`, `OptionSettings`, `DuelSetup`, `PadState`...); jump tables are `switch`es on the screen / step number, screens that are understood have names (`Screen`), the main menu items are `MainMenuItem`. What is left: (1) `fn::Original_<address>` and a few named functions (`StartFreeDuel`, `CardListKind4Step`...) are generated code of screens no recorded session has opened (story, duel start, deck, network...): they become Swaps when a session goes through them; (2) fields called `unknownNN` are not understood yet; (3) engine names marked `(guess)`; (4) two stack-argument reads (`DrawSprite`, `DrawStyledText`) and the numbers of the screens that are not named yet (`case 2`, `case 3`, `case 4`, `case 6`, `case 13`, ...). New code must follow the readability rule of `docs/DECOMP.md`.

- Names say what a function does; `FUN_` addresses are in each comment (the per-version address table will replace them later).
- Field widths come from the generated code (`LHU` / `LW` / `LBU`), never from Ghidra's pseudo-C; hidden arguments of ehsys calls come from
  the registers set before the call (`tools/decomp/decomp_args.py`).
- Registered Swaps that are only called from other Swaps (for example `DrawOptionRow`) are never reached through the hook any more: the C++ calls
  each other directly. They stay registered so that no generated copy of the function can run.

## Checking a change

```
sh tools/build.sh
sh tools/decomp/decomp_verify.sh <last frame> <session>      # TF_DECOMP_VERIFY: original and swap compared on every call
SNAPSTEP=60 sh tools/decomp/decomp_compare.sh <last frame> <session>   # captures and log with swaps on vs off must be identical
SNAPLIST=1900,2100 sh tools/decomp/decomp_compare.sh 2600 <session>    # exact frames (to look at one screen)
TF_TEST_NETWORK_ENABLED=1 sh tools/decomp/decomp_compare.sh ...         # keep Network Mode selectable: the recorded sessions move the cursor over it, so with it greyed out their replay takes another path
# note: the main menu's 7th label (Graphics here, Save Data with swaps off) is the one expected difference: a 78 x 17 pixel box at (202, 210)
```
