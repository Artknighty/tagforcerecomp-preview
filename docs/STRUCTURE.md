# Repository layout

A map of the repository. For how the pieces fit together at run time, see [`ARCHITECTURE.md`](ARCHITECTURE.md).

```
assets/       branding and icon sources
decomp/       readable C++ rewrite of the game, one folder per game module
docs/         guides (this folder)
plugins/      source of the behavior mods (`src/`, one folder per mod) and the header they build against (`sdk/`)
runtime/      the native runtime: HLE PSP kernel, GPU renderers, audio, UI
tools/        build, test, analysis and translator scripts
```

Folders such as `gen/`, `jogo/`, `ms0/`, `mods/` (installed mods: compiled plugins and data mods such as textures) and `ref/` are created locally (translated code, installed game data, saves,
installed mods, reference material) and are never committed.

## `runtime/`

The generic executable. Everything is compiled with the include path set to each subfolder, so headers are included by bare
name (`#include "gpu.h"`).

| Folder | What it holds |
|---|---|
| `core/` | Program start (`main_run`), the HLE kernel, ISO and installed-folder access, game installation and first-run translation (`prepare`), the TinyCC JIT that compiles the translated C, the module loader, diagnostics, `psp_rt.h` (the one header the generated code includes) |
| `hle/` | High-level emulation of the PSP system calls: graphics engine (`hle_ge`), audio, MPEG video, fonts, I/O, utility dialogs, and the VFPU math unit |
| `gpu/` | Renderers (`gpu_d3d11`, `gpu_vulkan`, software `gfx`), the 60/120 FPS interpolation (`interp`), frame pacing, texture replacement and BC7 compression, the high-resolution font, GLSL `shaders/`, and the graphics diagnostic capture include |
| `ui/` | The window (`display`), the launcher, menus, mouse and text input, the dark theme, and the mod panels |
| `audio/` | Audio output and the FFmpeg decoders |
| `mods/` | Loading of mod folders and plugin DLLs, and the hook table (`hooks.h`) |

`app.ico` and `app.manifest` (the executable's icon and manifest) stay at the top of `runtime/`.

## `tools/`

Entry points stay at the top; everything else is grouped by purpose.

| Path | What it does |
|---|---|
| `build.sh` | The one build: incrementally builds `TagForceRecomp.exe` into `gen/pkg/TagForceRecomp/` |
| `install_dev.sh` | First-time setup: installs the game data from your ISO |
| `package_release.py` | Builds the release zip |
| `check_docs.sh` | Size guard for the maintainer notes (used by the commit hook) |
| `build/` | Helper build steps: compile one file, Vulkan shaders, the slim FFmpeg build, plugin DLLs |
| `translator/` | The static translator (Allegrex MIPS to C), the module tables, and the NID list. This folder is shipped inside the release as `tools/translator/`, where the runtime runs it with the embedded Python |
| `decomp/` | Helpers for the decomp: compare, coverage, call graphs, function printing |
| `game/` | Reading the game's own data: ISO access, packages (EHP), card database, duelist decks |
| `ai/` | Analysis of the CPU's play, A/B runs, and the training pipelines for the AI mods |
| `test/` | Running the game for tests: replays, the closed-loop driver `tf.py`, the arena, regression and benchmark scripts |
| `analysis/` | Frame-pacing, flicker and capture analysis, and the screen recorders |
| `mods/` | Generators for texture and data mods, and the mod panel preview |
| `python/`, `githooks/` | Embedded Python used by the translator, and the commit hook |

## `decomp/`

A function-by-function C++ rewrite of the game, one folder per game module (`boot`, `libtitle`, `libfield`, ...), each with a
README. `common/` holds the headers they share: the byte-swap helpers, the engine call names (`ehsys.h`), the PC-style
drawing hook, and the duelist and deck tables. `decomp.cpp` registers the rewritten functions. See [`DECOMP.md`](DECOMP.md).

## `plugins/`

`plugins/` holds source code only; the built mods live in `mods/` (see below). Each folder in `plugins/src/` is one behavior mod (CPU vs CPU, Better AI, Better Decks, Booster box, Monsters on the
field) with its `README.md`, `mod.txt` and C++ source. `tools/build/build_plugins.sh` compiles them to `mods/<Mod>/plugin.dll`
against `plugins/sdk/tfmod.h`. See [`MODS.md`](MODS.md).

## `assets/`

`branding/` has the README banner and the launcher image. `icon/` has the script that produces `runtime/app.ico` from the logo
(its source images are not committed).

## `docs/`

| File | Topic |
|---|---|
| `ARCHITECTURE.md` | How the translator, runtime and mods fit together |
| `DECOMP.md` | The decomp plan and conventions |
| `MODS.md` | Writing and using mods |
| `AI.md`, `DECKS.md` | The Better AI and Better Decks mods and their tooling |
| `TESTING.md` | Replays, the arena and the diagnostic capture mode |
| `VERSIONS.md` | Supported game versions |
| `PREVIEW.md` | What the preview release does and does not cover |
