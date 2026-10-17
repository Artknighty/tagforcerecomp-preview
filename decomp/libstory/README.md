# libstory: the story mode ("modstory")

The story code: it loads at `0x08EFCA00` (the same base as libtitle, at other times). It owns the calendar (15-minute ticks, 73 per day from 05:45; months
May to December, tournament on day 92), the 150 characters (ids 2..151, `CharacterState` 0x98 bytes + `CharacterScript` 0x134 bytes), the places (map menu),
the place modules (field, shop, deck editor... loaded one at a time, table at `0x08F160F0`) and the sound scenes that go with them. The swap table name is `libstory`.

| file | what is in it |
|---|---|
| `story_data.h` | `Date`, `CharacterState`, `CharacterScript` and the addresses of the module's globals |
| `story_functions.h` | named offsets of the module's functions (`fn::`); the ones not decompiled yet are `Fun08xxxxxx` / `StepRunnerNN` and are called with `callLocal` |
| `calendar.cpp` | day of the year, dates, holidays, "is today in range", day changes |
| `bindings.cpp` | the `Bind*` / `Reset*` functions: they only store a pointer or clear a block (written with the `STORE` / `ZERO` templates) |
| `characters.cpp` | loading a character's state, event values and images, names, seen/hidden bits, system messages |
| `maps.cpp` | object ids of characters, place text numbers, language index |
| `globals.cpp` | player values, registers, flag bits, date fields, the heaps |
| `loading.cpp` | the archives: open, find an entry by name, script folders, the tables |
| `modules.cpp` | sound scenes (start, stop, voices) and the place modules (`LoadModule`, `SwitchModule`, the field's entry points) |
| `flow.cpp` | the story's flow: field setup, the place menu, entering a map, `AdvanceTime`, `Step` (one step of the module state machine), `ResetAll`, `Start` and `Main` |

The state machine is at `0x091C1D28` `{done, scene, step, callback}`; `Step` loads/unloads the modules of steps `0xD..0x2D`. `Main` is the thread's entry: it resets,
starts, and runs `Step` + `WaitFrame` until the story says it is done.

## How it was checked

Replay of `sessao_20261012_190727` up to the map (frame 3520), three ways: all swaps on, `TF_DECOMP_SKIP=libstory` (the original generated code) and
`TF_DECOMP_VERIFY=1`. The 16 captures are identical between the first two and the verify run has no divergence (the only one is `libtitle.InitTitleList`, which
is intentional). 117 libstory functions are exercised by that session. Quirks of the original are kept on purpose (`FindEventValues` compares the first entry's
place in the second table; the fourth place suffix repeats the first).

## Not done yet

The callees that this session never runs are still generated code (`Fun08xxxxxx`, `StepRunnerNN`). The field module that the story loads (libfield) has its own swaps: `decomp/libfield/README.md`.
