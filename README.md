# Tag Force Recomp

Static recompilation of **Yu-Gi-Oh! GX Tag Force** (PSP, European release ULES00600 v1.01) to **native Windows**. The game's MIPS
(Allegrex) code is translated to C and runs on a custom runtime: HLE PSP kernel, GE (graphics chip) in software and on the GPU,
audio, video, I/O and windowing. The goal is a real **PC version** and a foundation for **mods**, including mods that change how
the game's AI plays and which decks the duelists use.

> **Legal notice.** This project is not affiliated with Konami and **contains no game data**: no code, textures, audio or video
> from the game. You need **your own legitimate copy** of the game (the ISO). It is read **once** to install the files into a
> local folder; everything derived from it (installed data, generated C code, executable, saves, logs) stays on your machine,
> outside Git. Yu-Gi-Oh! is a trademark of Konami and its licensors. See also [`THIRD_PARTY.md`](THIRD_PARTY.md).

> **Preview release.** This is an early public preview, not a finished product. It plays the game from start to finish in our
> testing, but it has had limited testing (mostly one machine, one game version and one save), some features are marked
> *experimental*, and **APIs, file formats, settings and folder names may still change without notice**. There are no prebuilt
> binaries: you build it yourself from your own ISO (see [Build and play](#build-and-play)). Expect rough edges and please
> [report them](CONTRIBUTING.md#reporting-a-bug). See [`docs/PREVIEW.md`](docs/PREVIEW.md) for what is and is not covered.

## What works

The game runs end to end as far as it has been tested: title, New Game/Continue, Story Mode (3D field, island map, dialogue,
days, autosaves, story duels), full duels, Free Duel, Deck Edit, shop, Database, Options and Save Data. Full audio (SAS and
ATRAC3/3+) and H.264 movies through FFmpeg. Network/ad-hoc modes are not supported.

**Built as a PC game, not an imitation of the PSP UI:**
- Resizable window, fullscreen (F11 or Alt+Enter), dark theme, XInput controller, keyboard, native text box instead of the virtual keyboard.
- **Renderers**: Software (CPU), **Direct3D 11** or **Vulkan**, with **internal resolution from 1x to 8x**, **MSAA** up to 8x, configurable filter and aspect ratio.
- **High-resolution textures** (PPSSPP-format packs) and a **high-resolution font**.
- **60 FPS by interpolation** (game logic stays at 30; the runtime creates the in-between frames) with **early presentation** to cut
  latency, precise frame pacing and stable audio. Experimental.
- Configurable **sync mode** (default, VSync or lowest latency).
- Atomic saves, a log plus input recording of every session so problems can be replayed, and an optional
  **graphics diagnostic mode** that records screenshots during play (see [`docs/TESTING.md`](docs/TESTING.md)).

## Mods

Every mod is a folder in `mods/`, switched on or off in the window's **Mods** menu (applies on restart). There are three kinds:

| Kind | What it does |
|---|---|
| Files | replaces files from the disc (text, data, movies) |
| Textures | replacement texture packs (PPSSPP format) |
| **Behavior (plugins)** | a `plugin.dll` that hooks game functions by address; stable, versioned API in [`mods_sdk/tfmod.h`](mods_sdk/tfmod.h) |

Behavior mods in this repository (sources in [`mods_src/`](mods_src/)):

| Mod | What it does |
|---|---|
| **Better AI** (`IAAprimorada`) | makes CPU duelists play better: adaptive posture, smarter summons/attacks/Dark Hole, and an optional **small language model** (bundled `llama.cpp` server) that picks among legal moves. See [`docs/AI.md`](docs/AI.md) |
| **Better Decks** (`DecksAprimorados`) | replaces the decks of duelists with edited ones, without touching the game files. See [`docs/DECKS.md`](docs/DECKS.md) |
| **CPU vs CPU** (`CPUvsCPU`) | lets the CPU play your side too; also the engine of the AI/deck test arena |
| **Booster box** (`CaixaDeBoosters`) | buy all remaining packs of a booster at once in the shop |
| **Cheats** (`Cheats`) | optional cheats in their own **Cheats** menu: see the CPU's cards (F6), DP multiplier (1x to 10x) |
| **Monsters on the field** (`MonstrosNoCampo`) | *experimental prototype*: shows your face-up monsters larger over their zone |

Texture, video and button packs (HD menus, HD opening movie, EU cards, Xbox buttons) are separate mods distributed outside the repository.

The plugin API can read and write PSP memory and registers, call game functions, show message boxes over the screen and register
slider settings in the menu. See [`docs/MODS.md`](docs/MODS.md) to build your own.

## Download and play

1. Download `TagForceRecomp-<version>-win64.zip` from the [Releases](../../releases) page and extract it anywhere (not inside `Program Files`).
2. Run `TagForceRecomp.exe`, click **Select ISO...** and pick **your own** copy of the game (European release, ULES-00600).
3. Click **Install and play**. The first run copies the game files and translates the game's code on your machine (a few minutes,
   only once; the ISO is not needed afterwards). From then on just click **Play**.

Nothing needs to be installed: the zip carries the runtime, FFmpeg, a small C compiler (TinyCC) and the translator. It contains **no
game data and no game code**: the game's code is translated from your copy on your computer (`gen/`) and compiled in memory each time
the game starts (about 4 s).

**Controls.** Keyboard: arrow keys; Z = ✕, X = ○, A = □, S = △; Q/W = L/R; Enter = START; Shift = SELECT. XInput controllers work too.
F11 / Alt+Enter: fullscreen · F3: FPS · F9: graphics diagnostic capture on/off · F8: "this looks wrong" (asks for a note, then saves full-size screenshots and the last ~1.5 s of frames; Esc or an empty note discards it) · F7 / L3+R3: "the CPU made a mistake" (note + report) ·
Esc: leave fullscreen / asks before closing. The **Help > Controls** menu lists the PC keyboard and mouse layout.

Saves live in `ms0/` next to the executable. Each run writes a log, the key presses and a copy of the save to `logs/`; any session can be
replayed with `tools/replay.sh` (developers).

## Build from source (developers)

Requirements: Windows 10/11, [MSYS2](https://www.msys2.org/) UCRT64 (`gcc`, `g++`, `windres`, `glslc`, FFmpeg and Vulkan headers),
Python 3 with Pillow and numpy, Git Bash. **No Ghidra or any decompiler is needed.**

```sh
sh tools/recomp_tudo.sh "D:/path/to/Tag Force.iso"   # first time: installs the data, translates the modules (Allegrex -> C) and builds (~2 min)
sh tools/build_run.sh                                 # afterwards: incremental rebuild
python tools/package_release.py                       # builds the release zip described above (gen/release/)
```

```powershell
.\gen\run.exe       # launcher: "Install and play" the first time (ISO); afterwards just "Play" (the ISO is no longer needed)
```

To check that the documented build works on a **clean Windows**, there is a one-click test in [Windows Sandbox](tools/teste_ambiente_limpo/README.md).

`run.exe` can also install by itself (`run.exe --install <iso>`) and verify the copy (`--verify <iso>`). What the project
translates is the code **of your copy**, on your machine; none of it is distributed.

## How it works

1. `tools/` reads the game's modules and translates each Allegrex function to C (`recomp.py`, `allegrex.py`, `pspmod.py`).
2. The generated code, the runtime (`runtime/`) and FFmpeg are built into a `run.exe`.
3. The runtime implements in HLE what the PSP firmware would do (threads, files, audio, graphics, fonts) and presents the frames.
4. Plugins and mods connect through hooks on the recompiled functions and through file and texture replacement.

More detail in [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

## Repository layout

| Folder | Contents |
|---|---|
| `tools/` | generator (Allegrex → C), build, test, AI-training and deck tools |
| `runtime/` | C++ runtime: HLE kernel, GE, GPU backends, audio, video, window, mods |
| `mods_sdk/` | plugin API (`tfmod.h`) |
| `mods_src/` | sources of behavior mods |
| `docs/` | documentation ([preview status](docs/PREVIEW.md), [mods](docs/MODS.md), [AI](docs/AI.md), [decks](docs/DECKS.md), [testing](docs/TESTING.md), [architecture](docs/ARCHITECTURE.md), [game versions](docs/VERSIONS.md)) |
| `gen/` | everything that comes from your ISO (**not in Git**) |

## Contributing

Contributions are welcome: bug reports (with the log from `gen/logs/`), mods, fixes and ideas. See [`CONTRIBUTING.md`](CONTRIBUTING.md).
The most important rule: **no game data in the repository**.

## Credits

- HD texture pack used by many players: **MikeyMK** and **Filipe Ramos**.
- [PPSSPP](https://www.ppsspp.org/): reference for hardware behavior and the texture pack format (read-only reference, no code copied).
- [FFmpeg](https://ffmpeg.org/): video and ATRAC audio.
- [llama.cpp](https://github.com/ggml-org/llama.cpp): runs the small language model of the Better AI mod.

## License

[MIT](LICENSE) — you can use, fork, modify and redistribute this project freely, including for your own mods. The license covers the code and
documentation in this repository only; it does **not** cover the game, its data, or the third-party components listed in [`THIRD_PARTY.md`](THIRD_PARTY.md).
