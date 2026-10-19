# `libduel_draw`: duel presentation

`libduel_draw` is loaded at `0x09A15300` with `libduel_eng` and `libduel_mgr` for a duel. It draws the field, the cards, the effects and the HUD from the rules engine's state. The module has about 3700 functions; this directory covers the first small helpers.

## Current C++ swaps (`helpers.cpp`, names in `draw_data.h`)

| C++ | Original | What it does |
|---|---|---|
| `SetDrawFlags` | `FUN_09a1779c` | Clears (mode 0), sets (mode > 0) or only queries (mode < 0) bits of the drawing-option word; returns the bits that were set before. |
| `FindSortedTableIndex` | `FUN_09a16494` | Binary search of an unsigned key in a table of records sorted by their first word; index or -1. |
| `GetSceneRecord` | `FUN_09a21638` | Address of one slot's scene record (0xB0 bytes) of one player; slots 0xB..0xD live in the player block named by a lookup table. |
| `GetSideWord14`, `GetSideWord1C` | `FUN_09a76514`, `FUN_09a7652c` | The engine's words at +0x14 / +0x1C of the player record behind a screen side (meaning not mapped yet). |
| `IsSideTargetAllowed`, `IsSideActing` | `FUN_09a76808`, `FUN_09a76644` | The engine's target-allowed / can-act answers for the player behind a screen side. |
| `IsDrawStateWordSet` | `FUN_09a7ab70` | Tests a state word for "not zero" (meaning not mapped yet). |
| `GetNeutralTurnCode` | `FUN_09a765a4` | 2 when the engine reports the neutral turn player, else 0. |

## Verification

Chosen from `tools/decomp/decomp_cobertura.sh` on the Free Duel session `sessao_20261014_205658` (frames 0..6000): 426 of the module's functions run there; these are the hottest tiny ones. A complete Free Duel played by the CPU on both sides (`python tools/test/tf.py duelo --auto --fim --lento`) ends identically with and without the swaps (frame 28926, J0 = 0 LP, J1 = 800 LP, turn 24). `TF_DECOMP_VERIFY` was not run clean on this module yet: the verify run of the recorded session diverges on a `libtitle.InitTitleList` mismatch before the duel starts, and the duel route of `tf.py` did not finish under verify.
