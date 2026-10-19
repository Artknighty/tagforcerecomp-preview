# Decomp plan

Goal: replace the generated Recomp code with readable hand-written C++, function by function, keeping the game playable at every step.

## Rules

- Functional, not byte-matching. English identifiers, comments and docs.
- No game data and no raw Ghidra output in the repo; assets come from the user's own ISO.
- EU version (ULES-00600) first. Code names functions and structures once; today the addresses are the EU ones (`fn::`, `off()`), and `decomp_init` switches every swap off on other versions. A per-version address table is the plan before any other version gets swaps.
- New code goes in `decomp/<module>/`. Native typed objects are the target; typed views over PSP memory are the transitional stage.
- **Readable by anyone (the point of the work).** A reader must not need a debugger or this document to understand a line. In `decomp/`:
  - no bare engine ids: `x.callNid(nid::PlaySound, ...)`, never `x.callNid(0x2AE3C0AD, ...)`. Every NID has a name in `decomp/ehsys.h` (`namespace nid`); a name that is a guess says `(guess)`.
  - no bare function addresses: `x.callLocal(fn::SetFlowArgs, ...)`, never `x.callLocal(off(0x08f01550), ...)`. A function the code calls gets a Swap (and so a name in `namespace fn`).
  - no `x.at<T>(base + 76)`: guest memory is read through a struct with named fields (`MenuList`, `Window`, `State`, `Panel`, `OptionsScreen`...); a field that is not understood yet is called `unknownNN` (its offset), never given a made-up name.
  - a list of choices is an enum or a named constant (`MainMenuItem`, `kItemDisabled`), not a number.
  - names say what the thing does for the player or the engine, not how it was found (`StartColourFade`, not `StartIterator`). A wrong name found later is renamed everywhere.
  - Where the code is not yet like this, `decomp/libtitle/README.md` lists it under "Readability debt".
- A Swap is a runtime hook (`runtime/mods/hooks.h`) that can be switched on and off, so the original and the new function can be compared in the same binary.

## Definition of done (per Module)

1. Every own function of the Module is swapped.
2. Recorded sessions (`tools/test/replay.sh`) and regression captures (`tools/test/regressao.sh`) stay identical without the Module's generated code.
3. A short summary of what the Module does and how its functions relate is committed next to the code.

The status table distinguishes a fully completed Module from coverage of recorded sessions: so far, `libduelrecord` is complete as a whole; the other modules still contain generated functions outside their verified paths.

## Order and current path

1. **Pilot completed: `libduelrecord`** (Duel Ranking; the initial estimate was 21 own functions). It proved the Swap/Oracle workflow end to end.
2. Follow the player's path and decompile the functions reached by recorded sessions: title/menu (`libtitle`), story character selection (`libcharalist`), story flow (`libstory`), then the field/map (`libfield`).
3. Continue into unvisited screens and modules. The current requested focus is the duel core: use the complete Free Duel as the Oracle, starting with `libduel_eng`, then `libduel_mgr` and `libduel_draw`. `BOOT` text/graphics helpers also remain substantial generated-code areas.
4. **Phase 2**: replace the HLE runtime with a native platform layer. This is separate from the current C++ swaps and has not started.

## Pilot (done)

`libduelrecord` (Main Menu > Duel Ranking) was the pilot: 27 own functions, swapped leaf-first, each compared on/off with `TF_DECOMP_VERIFY` and with captures. Baseline session `gen/logs/sessao_20261011_014117`. Call graph of any module: `python tools/decomp/decomp_grafo.py <module>`.

## Swap mechanism (`decomp/swap.h`, `decomp/decomp.cpp`)

A swap is a hook on the function entry. A `SwapReg` maps the PSP registers to a hand-written C++ function in `decomp/<module>/`. It only fires while its Module is loaded, because several Modules share the same base address at different times.

- `TF_DECOMP=off` disables all swaps; `TF_DECOMP=a,b` enables only the named ones; default is all registered.
- `TF_DECOMP_VERIFY=1` runs eligible swaps (`pure` or with declared writable regions) and logs `[decomp] MISMATCH` if the original and replacement differ.
- The single build is `gen/pkg/TagForceRecomp/TagForceRecomp.exe`, with registered swaps enabled by default. Build an isolated test executable without touching it: `OUT=gen/pkg/TagForceRecomp/test.exe sh tools/build.sh`, then `RUN_EXE=gen/pkg/TagForceRecomp/test.exe sh tools/test/replay.sh <session>`.

## Swaps done

| Module | Functions | Status |
|---|---|---|
| `libtitle` (logo, opening, title and menus) | 192 swaps in `decomp/libtitle/*.cpp`: all functions reached by seven recorded sessions (`100343 102706 133113 154411 161505 161708 163734`), including the main menu, Free Duel/card lists, Options, Graphics, language screen, dialogs and drawing. The module has 1704 functions total. | No generated `libtitle` functions remain on those seven recorded paths. Captures/logs matched for the relevant swaps; unvisited screens remain generated. Summary: `decomp/libtitle/README.md`. |
| `BOOT` (resident engine / ehsys) | 26 of 2291 in `decomp/boot/`: the window path—drawing, layout, animation, input/state (`UpdateWindow`) and colour fade. | Window path done; captures matched with these swaps on/off in 11 sessions. `FadeColour` matched the generated version on 100,000 random inputs. Text and graphics-list wrappers remain generated. Summary: `decomp/boot/README.md`. |
| `libduelrecord` (Duel Ranking) | 27 of 27 in `decomp/libduelrecord/`. | Pilot complete: original and swaps compared on every call with `TF_DECOMP_VERIFY` (for `Draw`, the full display-list buffer); captures also matched. Summary: `decomp/libduelrecord/README.md`. |
| `libcharalist` (story character selection) | 13 of 57 in `decomp/libcharalist/`: functions used to build character lists and choose the character/picture at story start. | Replay chose the same character and picture, with the same list sizes and capture; pure lookup functions also pass `TF_DECOMP_VERIFY`. Other functions have not been reached by recorded sessions. Summary: `decomp/libcharalist/README.md`. |
| `libstory` (story flow up to map entry) | 117 functions exercised by `sessao_20261012_190727`: calendar, character state, archives, place menu, module loading, sound scenes and the story state machine. | Swaps-on and generated-code runs reached the map with 16 matching captures; a separate verify run found no unexpected mismatch. Unvisited story paths stay generated. Summary: `decomp/libstory/README.md`. |
| `libfield` (story map and field scene) | All 193 functions the story-to-map replay executes (~51 KB) are C++ swaps; 186 were exercised. They cover indicators, scene/NPC updates, player, walkers, loader, HUD, camera, messages, the place menu, follower, effects, panel, banner and fade. | The boundary trace (all engine calls and module-function entries, 171,722 events) is identical with `TF_DECOMP_SKIP=libfield.`, using `tools/decomp/decomp_calls.sh`. Not exercised: the 7 `map_grid.cpp` swaps and the active paths of follower/effects/panel/banner/dialog (the replay never walks or opens them). Summary: `decomp/libfield/README.md`. |
| `libduel_eng` (duel rules and state) | Twenty-five swaps in `decomp/libduel_eng/`, covering field-card flags, effect-list queries, identity resolution, attribute/category checks, restrictions, active-card counts, and target/zone/monster-availability legality. | All 25 were exercised; targeted verify runs reported no mismatch (through frames 12000, 6000 or 5000, depending on the function). Incremental full-duel compares kept all 13 captures identical. Most of the module remains generated. Summary: `decomp/libduel_eng/README.md`. |
| `libduel_mgr` (the Duel Result screen) | Swaps in `decomp/libduel_mgr/`: the state machine, input, loading, the question window, the start (rewards and DP) and the end, and the screen's drawing: the table page, the three detail pages, footer, title capsule, background. | Captures of the result screens, the page slide and the question are pixel-identical with the swaps on and off (`tools/decomp/decomp_resultado.sh`, 14/14); per-function `TF_DECOMP_VERIFY` reports no difference. The outcome board of story duels (`08f0c764` and its helpers) is C++ too, checked with `tools/decomp/decomp_resultado_banner.sh` (identical in two- and four-duelist modes); nothing of the screen's own code is generated any more. Summary: `decomp/libduel_mgr/README.md`. |
| `libduel_draw` (duel presentation) | Nine swaps in `decomp/libduel_draw/`: the drawing-flag word, a sorted-table search, scene-record addresses and thin wrappers over `libduel_eng` getters (hottest tiny functions of the Free Duel). | A full CPU-vs-CPU Free Duel ends identically with the swaps on and off; a clean `TF_DECOMP_VERIFY` run is still pending. Summary: `decomp/libduel_draw/README.md`. |

## Lessons

- **Ghidra's pseudo-C hides arguments** of the ehsys functions (for example the heap Free call shows one parameter but takes two, which crashed the first attempt). Always check the registers set before each call in the generated C: `python tools/decomp/decomp_args.py gen/<module>.c` lists them per import.
- Field widths come from the generated code (`LHU`/`LW`/`LBU`), not from the pseudo-C: the state phase looked like a word and is a halfword.
- `tools/decomp/decomp_check.sh` builds, replays with swaps on (verify) and off, and compares captures; `tools/decomp/decomp_try.sh "<list>"` tries one selection without rebuilding.
- Natives call each other directly, so `TF_DECOMP=<list>` selects entry points, not call-by-call behaviour.
- Verifying a function with side effects: list the memory it writes (`Swap::regions`); the harness primes caches with a first run, then runs the original (with no swap firing inside it) and the swap from the same state and compares. Include every global the code touches (the frame counter and the fade progress both caused false mismatches until listed). Verify mode runs functions several times, so captures taken in verify mode are not comparable; `decomp_check.sh` compares captures in a separate run.

## Decomp build and play-order workflow

- There is a single build, `TagForceRecomp.exe`: the decomp swaps are compiled into it and are on by default (`TF_DECOMP=off` runs only the generated code; the parts not decompiled yet stay as generated C). Rebuild: `sh tools/build.sh`. Replays use `TagForceRecomp_bg.exe`, the copy of the exe the build makes.
- Modules are tackled in the order the player meets them: Konami start screen, opening cutscene, title, then menus, story and duels. The owner records one session per stage; each new session becomes the baseline of the next Module.

## Coverage of a recorded session

`sh tools/decomp/decomp_cobertura.sh <session> <Module> [last_frame]` lists which functions of a Module actually run in a session (hook trace on every function; a replay with the trace is slow for hot Modules such as BOOT).

Session `sessao_20261011_100343` (Konami start screen to the end of the opening movie, 1000 frames): `libtitle` runs 57 of 1704 functions (3246 calls); `BOOT` runs 593 of 2291 (915 k calls, mostly engine/library helpers).

## libtitle (title flow) notes

- Module name for the swap table is plain `libtitle` (only one instance); offsets are `address - 0x08efca00` (`off()` in the code).
- The flow keeps its state in the game's data area at `0x09B29730` (`decomp/libtitle/title_state.h`: current/next screen, three arguments with a pending value each, phase). `Tick` (`FUN_08eff908`) runs every frame: reads the pad, runs `UpdateFlow` (commits the pending values and switches screens through `SwitchScreen`, `FUN_08eff100`) and the running screen's logic step; `RunScreen` (`FUN_08f00238`) draws it. `Main` (`FUN_08efca34`) is the module's loop.
- Most screen routines are state machines on `arg0`/`arg1` with a jump table in the module data. The swaps read the table at run time and map each target address to the arm (so a wrong table address shows up as a different behaviour, not as a crash); screens never visited in the recorded sessions are still called as generated (`callLocal`).
- Still unexercised: alternate branches of the language flow, the demo timeout, and most of `SwitchScreen`'s targets. The common language-screen path is swapped; these branches need sessions that go through them.
- Sprite records of the module image are structs in `title_state.h`: `LogoSprite` (152 bytes at `kLogoSprites`: image file name + drawn rectangle + source rectangle; the logo screen, the list buttons and the menu decorations) and `FramePart` (22 bytes at `kFrameParts`: window frames, cursor arrow, name plate). Read them with `logoSprite(x, i)` / `logoSpriteName(x, i)`, never as `entry + 132`. The image archive of the language in use is `currentArchive(x)`; the game's sound numbers are `kSound*` in `internal.h`.
- Main menu drawing (`drawList`): one row = `drawClassicButton` (the game's three LogoSprites) or `drawModernButton` (PC style, Enhanced package: two neutral half-height pieces in a free corner of the button sheet, tinted with the row cell's colour; the game's cells are shared with other screens, so the PC art must not live in them), the cursor row's highlight alpha is `highlightAlpha` (pulse 207 -> 255 -> 207 over 60 frames, or the quick flash after a confirm).

## Method notes (learned the hard way)

- **Hex of a decimal offset**: `0x8f20000 + N` appears in the generated code with N in decimal; convert it with a tool (a table at `0x8f23a0c` was typed as `0x8f2390c` and only the Duel Ranking session noticed).
- A swap that calls back into the game (`callLocal`/`callNid`) clobbers `r4..r11`: read the arguments first. For ehsys functions that Ghidra shows without parameters, the original passes whatever is in `r4..r11`: use `callNidKeepRegs`.
- `swap_hook` restores `s0-s7/gp/sp/fp/ra` after every swap (a swap must behave like the function it replaces).
- `TF_HOOK_TRACE=<guest addresses>` shows the real arguments of ehsys functions (it found a wrong string address in a few minutes); `TF_DECOMP_SKIP=<name prefixes>` leaves swaps out to bisect a regression.
- Void functions: the result register is not compared by the owner's game, but `TF_DECOMP_VERIFY` compares it. Return what the original leaves (often a `memset` result or `0`) so real mismatches stay visible.
- **Verify runs a function several times** (prime, original, swap, original), so a swap that allocates/frees heap, starts screens or otherwise is not repeatable must not declare `regions` (`UpdateFlow`, `LeaveLogoScreen`); declared regions must be safe to read at entry (a garbage pointer crashed the replay and hid every later check, so look at the `swaps exercised` count and `CRASH` in the verify log).
- Reading arguments after a call is a trap: `x.callNid(.., x.arg(0)); x.callNid(.., x.arg(0))` reads a clobbered `r4` the second time, and C++ argument evaluation order is unspecified; copy `x.arg()` values into locals first. A callee may also read registers Ghidra does not show (the module-start function takes the handle in `r5`, `EF9B5D06` reads the block in `r5`); pass the original registers on.
- **Read the generated C in full before trusting a filtered view**: `grep` for `S[BHW](` hides the lines that compute a field value (`r3 = r3 + 5; SH(+28, r3)`); a panel setup was written from such a view and moved the "Next" label by 5 px (found by the capture diff, fixed from Ghidra's pseudo-C). `python`-print the whole function when a value is not a plain constant.
- **Hidden arguments** also come from earlier compares: `FUN_08f01550(1, 0xfff)` in Ghidra is `(1, 0xfff, 2)` because `r6` still holds the constant of the previous `== 2` test. Always look at the registers set before each call in the generated C.
- A function Ghidra calls `void` may return the new display-list cursor in `r2` (`FUN_08f0a928`, the frame drawer): look at what the callers do with `r2` before dropping it.
- Some swaps intentionally change the game (for example, the in-game Graphics screen and title-menu choices). For a new swap, compare with `TF_DECOMP_SKIP=<new swap>` rather than disabling every swap; a full `decomp_compare.sh` may include expected differences from other swaps.
- Tools: `tools/decomp/decomp_cobertura.sh` (which functions run in a session), `tools/decomp/decomp_verify.sh <last_frame> <session...>`, `tools/decomp/decomp_compare.sh <last_frame> <session...>` (captures/logs), and `tools/decomp/decomp_calls.sh <session> <last_frame> <module> <skip prefix> <stub range> [first_frame] [tag_suffix]` (normalized engine-call/function-entry trace; refuses to overwrite existing replay folders). `tools/decomp/decomp_linhas.sh <session> <last_frame> <decomp dir>...` shows which LINES of our C++ the session never runs (gcov on an instrumented, temporary build); those lines are what the trace cannot vouch for, so read them against `tools/decomp/gen_fn.py` output.

## Open points

- Hook granularity for functions with secondary entries (`NAME_e_<end>` in the generated C).
- **Current:** continue the duel core from `sessao_20261012_213918` (50,807 frames; 38 turns). `libduel_eng` tracing was split to fit the Windows environment limit; tracing its hottest second-half addresses timed out at frame 19,445, so those counts are partial. `libduel_mgr` and `libduel_draw` coverage completed. The `libfield` map-grid swaps (and the follower/effects/panel paths) still need a session with player movement.
- Record further sessions for story interactions and unvisited title/engine screens; only functions reached by those paths have been covered so far. `libcharalist`, `libstory` and `libtitle` still have unvisited generated functions.
- Most of `BOOT` outside the window path and the duel/deck/shop modules remain generated C. These are the main Phase 1 scope after the current field path.
- **Phase 2 is not started:** replacing the HLE/PSP-memory platform with native PC audio, video, input and file handling is a separate milestone.
