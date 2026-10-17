# `libfield` — story map and field scene (work in progress)

`libfield` owns the walkable story maps: the terrain and its models, the player and
wandering characters, the place markers, and the field-side resource/sound helpers.
`libstory` passes it the current date, player values and callbacks, then invokes
the field handlers and frame functions.

The readable replacements live here as `SwapReg`s. Their data views are in
`field_data.h`; `field_functions.h` gives semantic names to local functions that
are still generated, and `field_engine.h` names the engine calls used by the
field renderer. `unknownNN` fields are intentionally unnamed until their meaning
is established.

## Files

| File | Responsibility |
|---|---|
| `field_data.h` | Typed views of the field state, map scene/layout, player, walkers, indicators and story arguments. |
| `field_functions.h` | Named offsets for local routines; not every listed routine has a C++ replacement yet. |
| `field_engine.h` | Guessed names/signatures for field-specific matrix, vector and model NIDs. Guesses are marked. |
| `indicators.cpp` | Three place indicators: fade in/out, selection, blinking and drawing. |
| `map_scene.cpp` | Terrain state/drawing, map-area transitions, NPC visibility, ground height and object animation. |
| `player.cpp` | Player object lifecycle, movement update, occupied cells, surface shadow and footsteps. |
| `walkers.cpp` | Wandering characters and fixed map characters. |
| `resources.cpp` | Shared-heap wrappers, lazy `story_sys01.gim` sprite lookup and sound/load gating. |
| `sprite_drawing.cpp` | The scaled and native-size textured-quad helpers; the scaled helper reads its ninth argument from the caller's stack. |
| `map_grid.cpp` | World/grid conversion, ground/walkability arrays, map-specific blocked cells and place reservations. |
| `loader.cpp` | The resource queue: pool, records, requests, jobs and the file-read pump. |
| `resets.cpp` | The memset-style resets of every layer (macro `RESET_BLOCK`) and the screen/shadow resets. |
| `field_state.cpp` | Field state record, story-progress stage, level class, ground surface lookup and the beacon rows. |
| `entry.cpp` | The module's exports: `Start`, `FrameUpdate`, `FrameDraw`, `Invoke`, `HandlerDone`, and the three story handlers. |
| `field_flow.cpp` | The flow state machine that sequences the map UI (menus, lists, drawing). |
| `hud.cpp`, `hud_draw.cpp`, `dp.cpp` | Calendar, clock, counters, badges and the Duel Points counter (slide animations and drawing). |
| `camera.cpp` | Screen shake, 3D camera / look-at / light setup and the full-screen quads used for fades. |
| `message.cpp` | The message box (typing, arrow, layout). |
| `world_map.cpp`, `world_map_draw.cpp` | The place menu ("world map", 14 places): input, movement of the cursor, popup of the main street, drawing. |
| `follower.cpp` | The follower that walks behind the player (steers round people) and its drawing. |
| `effects.cpp` | The effects layer over the map (sorted, three passes with a stencil mask) and the object matrix. |
| `scenes.cpp` | The question dialog, the info panel, the title banner, the screen fade and the ambient sound. |

## Current verification

All 193 functions the story-to-map replay executes (`sessao_20261012_190727`, up to
frame 3520) are now C++ swaps; 186 swaps were exercised. With the swaps on versus
`TF_DECOMP_SKIP=libfield.`, the boundary trace (every engine call with all argument
registers, plus every entry of a module function) is identical over 171,722 events:

```
sh tools/decomp_calls.sh sessao_20261012_190727 3520 libfield libfield. 091ed540-091efc30 0 _tN
```

Lines the replay never runs (`sh tools/decomp_linhas.sh sessao_20261012_190727 3520 decomp/libfield`; about 900 lines, mostly the
walking, dialog, panel, banner, DP-counter, popup and unusual-language paths) were read one by one against the generated C on 2026-10-13. That
review found and fixed: the DP counter's flying number used the wrong slide (`UpdateDpFly` target is the "D" slide, `0x09993724`), one extra
button-mask call in the message box, a float-association difference in the message scroll offset, and two host divisions by zero that the
original leaves undefined. Reading is not running: treat these paths as unverified until a session exercises them.

What this does not prove:

- `map_grid.cpp` (7 swaps: cell conversion, walkability, reservations) is never called by
  this replay, because the player does not walk. Read against the generated C, not run.
- The follower, effects, panel, banner and dialog swaps run only their idle paths in this
  replay (nothing is active on the map-entry screen); their active paths follow the
  generated C but were not executed.
- Some engine NIDs are guesses (`field_engine.h`, marked `(guess)`); the swaps call them
  by NID exactly as the original does, so a wrong name does not change behaviour.

Unswapped functions that remain are the ones the replay never reaches.

## Readability notes

- Map IDs and cell coordinates are values from the game, not stable public IDs;
  names in the code describe their observed use.
- The blocked-cell rules in `map_grid.cpp` are specific to the main island and
  shop maps. They must be checked against a session that moves through those
  areas before further simplification.
- `trace_arity.txt` contains corrections to Ghidra-derived register arities for
  call-trace comparisons. `trace_skip.txt` lists accessor entries omitted from
  that trace because the replacements read their backing data directly.
