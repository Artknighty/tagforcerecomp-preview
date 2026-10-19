# Preview status

Tag Force Recomp is published as an **early preview**. This page says plainly what that means: what has been tested, what is
experimental, what is missing, and what may change. If something is not listed here, assume it is untested.

## What you get (and do not get)

- **A ready-to-run zip (Windows) and the source code.** Extract the zip, run `TagForceRecomp.exe`, pick your own ISO: nothing to
  install (see the [README](../README.md#download-and-play)). The zip has **no game data and no game code**: the game's code is
  translated from your copy on your machine the first time and compiled in memory at every start. Developers can also build
  everything from source ([README](../README.md#build-from-source-developers)).
- **A working PC port of the game, as far as it has been played.** The whole game has been played through the main modes (see
  [What works](../README.md#what-works)), but by a very small group of testers.
- **A mod platform.** Behavior plugins ([`plugins/sdk/tfmod.h`](../plugins/sdk/tfmod.h)), file/texture/movie replacement and the example
  mods in [`plugins/src/`](../plugins/src/). See [MODS.md](MODS.md).

## Tested configuration

| | |
|---|---|
| OS | Windows 10/11, 64-bit. Other systems are not supported. |
| Game | **European release, ULES-00600 v1.01** (English). This is the reference version. |
| Other versions | The American release `ULUS-10136` (and translations built on it) builds and starts, but has had far less play time. **Plugins are European-only** until their addresses are ported ([VERSIONS.md](VERSIONS.md)). |
| Renderers | Direct3D 11 and Vulkan (the main ones), plus a slow software renderer used for tests. |
| Hardware | Developed and played on a single desktop PC with a modern NVIDIA GPU. Behavior on other GPUs/drivers is largely unknown. |
| Controls | Keyboard + mouse and XInput controllers. |

## Feature status

| Area | Status |
|---|---|
| Story Mode, duels, Free Duel, Deck Edit, shop, Database, Options, Save Data | Works in testing |
| Audio (SAS, ATRAC3/3+) and H.264 movies | Works (a slim FFmpeg ships in the zip) |
| Direct3D 11 / Vulkan renderers, 1x–8x resolution, MSAA, HD font | Works in testing |
| HD replacement textures (PPSSPP-format packs) | Works; the packs themselves are not distributed |
| **60 / 120 FPS by interpolation** | **Experimental.** Game logic stays at 30 FPS; visual glitches in rarely-seen scenes are possible (there is a menu switch to go back to 30) |
| Sync modes / frame pacing | Works; "lowest latency" may tear |
| Native text entry, mouse in menus | Works; **mouse in menus is experimental** |
| Behavior mods: Better AI, Better Decks, CPU vs CPU, Booster box, Cheats | Work with the European version; AI quality is measured statistically in an arena, not guaranteed in every matchup |
| Better AI's optional language model | Off by default (the plain rules played better in our measurements) |
| Mod "Monsters on the field" | **Prototype**, off by default |
| Network / ad-hoc modes | Not supported |
| Some savedata dialog modes, fog and curved surfaces in the GE | Stubbed or not implemented (not seen in the game so far) |
| Exact SAS envelope (ADSR) | Approximated |

## Known limitations of this preview

- **The release zip is ~13 MB** (a slim LGPL FFmpeg with only the ATRAC3/3+ and H.264 decoders).
  Only the European release (ULES-00600) is supported by the zip; other versions need a source build
  ([VERSIONS.md](VERSIONS.md)). First-run preparation takes a few minutes; later starts take a few seconds (the translated code is
  compiled in memory).
- **Building from source needs a developer toolchain** (MSYS2 UCRT64, Python with Pillow and numpy, Git Bash). The
  one-click clean-machine test in [`tools/test/teste_ambiente_limpo`](../tools/test/teste_ambiente_limpo/README.md) exists but has **not
  been run in a public CI**, so those instructions may have gaps. Please report them.
- **No automated test suite in CI.** Regression checking is done with recorded sessions that you generate by playing
  ([TESTING.md](TESTING.md)); the scripts that refer to the maintainers' own sessions need you to substitute yours.
- **Research tools** (`tools/decks_*.py`, `tools/ia_*`, `tools/capturas_*`, parts of `tools/ai/ia_treino`) expect a local module dump and
  card database produced with a decompiler workflow that is not part of this repository
  ([ARCHITECTURE.md](ARCHITECTURE.md#note-on-the-research-tools)). The recompiler and the runtime do not need it.
- **Language.** The documentation and the user interface are in English. Many source comments, log messages, some folder names
  (for example `gen/jogo/`, the installed game data) and some `.ini` keys are still in Portuguese, the maintainers' first language.
  They are being translated gradually; new code should use English.
- **Names and formats can still change.** Only the plugin API ([`tfmod.h`](../plugins/sdk/tfmod.h)) has a compatibility rule (it only
  grows). Folder names, `.ini` keys, mod folder names, log formats and tool command lines may be renamed between preview versions.
- **Single-player only.** Do not expect online features.

## Not affiliated, no game content

This project is independent and not affiliated with or endorsed by Konami. It contains **no game code, data, textures, audio or
video**. Texture packs, HD videos and the Better AI model are distributed (if at all) separately from the source, as mods.
See [THIRD_PARTY.md](../THIRD_PARTY.md).

## Reporting problems

See [CONTRIBUTING.md](../CONTRIBUTING.md#reporting-a-bug). The most useful report is the session log from `gen/logs/` plus, for
graphics problems, a capture made with **F8** (diagnostic mode) on the frame that looks wrong. Every session is recorded, so a bug
can usually be replayed exactly.

## Roadmap (no promises)

- More game versions (the process is described in [VERSIONS.md](VERSIONS.md)).
- Porting the behavior mods to more versions.
- More mods: text/card data tools, more behavior mods.
- A settings window (video / audio / controls / mods).
- Translating the remaining Portuguese comments and names.
