# Mods — Tag Force Recomp

A mod is a folder inside `mods/`, next to `TagForceRecomp.exe` (menu **Mods → Open mods folder** creates it if missing). There are four
kinds of content, and one mod can mix them:

1. **File replacement** — files that the game reads instead of the ones on the ISO.
2. **Replacement textures** — PNGs of any resolution (GPU renderers only).
3. **Replacement movie video** — HD H.264 versions of the game's movies.
4. **Behavior (plugins)** — a `plugin.dll` that hooks game functions.

The ISO is never modified; turning a mod off restores the original behavior.

## Folder layout

Files go **at the same path they have on the ISO**:

```
mods/
  HDCards/
    mod.txt                                  (optional)
    PSP_GAME/USRDIR/card/cardm_e.cip         (replaces the ISO file with the same path)
  MyTranslation/
    PSP_GAME/USRDIR/duelsys/cardinfo_eng.ehp
```

`mod.txt` (optional, UTF-8), one key per line:

```
name=HD Cards
author=Your Name
version=1.0
description=Redrawn card thumbnails
```

Some sub-folders are **not** treated as ISO files: `textures/`, `movie/`, `llm/` (model server of the Better AI mod), `decks/`
(deck files of the Better Decks mod), `sprites/` (Monsters on the field), and the files at the top of the mod folder (`disabled`,
`mod.txt`, `plugin.dll`, `config.ini`, `readme*`...).

## Turning mods on and off, and order

- **Mods** menu: enabled mods show a check mark; clicking toggles and offers to restart (changes apply from game start).
  Disabling creates `mods/<folder>/disabled`; deleting that file enables the mod again.
- Priority: `mods/load_order.txt` lists the mod folders from lowest to highest priority. If two mods change the same file,
  texture, movie or hook, the **last** one wins (the log warns). Folders not in the file go at the end, alphabetically; the
  texture packages always come first. The launcher's **Mods** page toggles mods and moves them up/down (top of the list =
  highest priority); changes apply on the next start.

## Rules and limits for file replacement

- **Data files only.** Code modules (`BOOT.BIN`, `*.prx`) are ignored: the game's code is recompiled and the image must match it
  (behavior mods are plugins, see below).
- The game opens most files by **disc sector**, not by name, and sometimes reads only part of a package; the runtime handles
  this itself (the replacement is indexed by the file's position on the ISO).
- **Size** may differ from the original — the runtime fixes the size the game reads from directory records (tested with a larger
  `.ehp`). Packages with internal offset tables still have to be consistent (use `tools/ehp.py` to repack EHP files). The log warns
  when the size differs.
- The PC-specific texts (the "Memory Stick" wording, see `runtime/pc_text.cpp`) are not applied to files coming from mods.

## Finding which file to replace

- The session log (`gen/logs/sessao_*.log`; "sessao" = session) records every file read from a mod (`[mods] reading ...`).
- `IO_TRACE=1` lists all disc opens and reads (sector and size).
- Known files: `card/cardm_*.cip` (64x64 DXT1 card thumbnails, one per language), `card/cardh_*.cip` (larger version),
  `duelsys/cardinfo_*.ehp` (card texts), `movie/*.pmf` (PSMF movies), `title_menu/title_menu_*.ehp` (title menu).
  For EHP archives: `python tools/ehp.py list|extract|pack` (for example `extract iso:PSP_GAME/USRDIR/title_menu/title_menu_e.ehp folder`,
  edit, `pack folder out.ehp`; the format is described in the script header).

## Log

At start-up the log lists each mod: `[mods] <name>: N file(s) replaced`, `disabled`, files that do not exist on the ISO (ignored)
and conflicts between mods.

## Replacement textures (GPU renderers only)

A mod can replace game **textures** with PNGs of any resolution (2x, 4x, 8x…). No restart is needed to try a new texture: the PNG
is read in the background the first time the texture appears (the original is used meanwhile).

- Put the PNGs in `mods/<Mod>/textures/`, **or** point to an existing folder with `textures=C:\path\to\folder` in `mod.txt`
  (no copy; many GB of PPSSPP packs work this way).
- **Identification** is by the hash of the texture data (XXH64, seed 0xBACD7814, low 32 bits) — the same one PPSSPP texture packs
  use, so PPSSPP packs (`textures.ini` with `[hashes]` and 24-hex keys) work **without conversion**. Without `textures.ini`, any
  `<hash8>.png` (8 hex) or `<key24>.png` in any sub-folder is accepted.
- **Dumping**: menu Mods → *Dump used textures* (or `TF_TEX_DUMP=folder`) writes one `<hash8>.png` per texture used (plus
  `index.txt` with size/format/address). Edit or upscale it and move the PNG, with the **same name**, to `mods/<Mod>/textures/`.
- The aspect ratio of the replacement can differ (the texture is stretched), but keeping the original's is best. Mipmaps are
  generated automatically.
- They do not apply to textures the game draws in real time (render-to-texture, movies) or to the software renderer.
- For paletted textures (CLUT) the PPSSPP pack tells variants apart by palette. PPSSPP's palette hash is not reproduced here: when one
  texture has several different PNGs (card frames per card type, for example), the one whose average colour is closest to the original
  texture drawn with the current palette is used; in practice almost all variants point to the same PNG.

## Replacement HD movie video (`movie/*.pmf`)

A mod can replace a movie's **video** with a high-resolution version (for example an AI upscale). Put in the mod folder:

```
mods/HDOpening/movie/opening_a.h264
```

- The name is the original `.pmf` name (`opening_a`, `exodia_finish`, `staffroll_a`, `*_summon`, `*_attack`...).
- Format: **H.264 Annex B** (`.h264`), **1920x1088**, **no B-frames**, with AU delimiters and **the same number of frames** as the
  original movie (frame N of the HD video replaces frame N of the original). Audio and timing remain the original's.
- GPU rendering only (D3D11/Vulkan): during the movie the window shows the HD frame instead of the game's 480x272 frame.
- Decoding runs on its own thread (ahead of time); a ~90 s opening at 1080p costs ~220 MB of RAM and almost no game CPU.
- To generate: `sh tools/movie_hd.sh opening_a` (extracts the PMF from the ISO, upscales it with `realesrgan-ncnn-vulkan` and encodes;
  see the script header). Set `TF_ISO` to your ISO path.

## Behavior mods (plugins)

A behavior mod is a DLL in the mod folder:

```
mods/
  BoosterBox/
    mod.txt          name=, author=, version=, description=
    plugin.dll       exports tfmod_init (API in mods_sdk/tfmod.h)
```

It is switched on and off in the **Mods** menu like any other mod (applies on restart). The game loads the `plugin.dll` of the
enabled mods at start-up, in alphabetical folder order, and calls `tfmod_init(api)`.

### The API (`mods_sdk/tfmod.h`, C only, version 5)

The game code is recompiled, so a code mod does not "patch" the ISO: it **intercepts game functions by their PSP address**. Every
recompiled function checks a bitmap on entry; if there is a hook, it runs with the PSP registers (`reg(c, 4..7)` = arguments, `r2` =
result, `r31` = return address) and returns `0` = continue with the original (the hook only observed/prepared) or `1` = handled
(the original does not run).

| Function | Purpose |
|---|---|
| `hook(addr, fn)` | install a hook (several mods can hook the same address; the first to return 1 ends the chain) |
| `call_original(c, addr)` | run the original body of the hooked function ("wrap" it) |
| `call(c, addr)` | call another game function |
| `reg` / `set_reg` | read/write PSP registers |
| `mem(addr)` | pointer to PSP memory |
| `buttons()`, `frame()`, `module_loaded(name)` | input, frame counter, module state |
| `message_box(...)` | show a message box over the game (a line `"=N/M"` becomes a bar of M segments with N lit) |
| `log(fmt, ...)` | write to the session log, tagged with the mod name |
| `guest_alloc(n)` (v2) | reserve PSP memory for data the game will read (for example UTF-16 text shown by the game's own message window) |
| `add_slider(title, unit, min, max, def, get, set)` (v3) | register an integer setting; the runtime adds a **Mods** menu item that opens a dark modal slider dialog (`runtime/slider_dialog.cpp`) |
| `add_toggle(title, vk, get, set)` (v4) | register an on/off setting: a checkable **Mods** menu item and, optionally, an in-game key (Windows virtual-key code, e.g. `VK_F6`). Keys that belong to the game are refused (the item stays menu-only): the window's F-keys (F3, F5, F7, F8, F9, F11, listed in Help > Controls), F10, Esc, Enter and the keyboard keys of the PSP buttons in both layouts (arrows, W A S D, Z X C E F R Q, Space, Backspace, Tab, Shift, Ctrl) |
| `panel(corner, title, lines, n)` (v4) | information panel in a corner of the screen (0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right); call it every frame it should stay visible; it never blocks the game. Plain text works; an optional markup (documented in `tfmod.h`) gives section headers with counters (`"#Hand\t5"`), colored card rows (`"@ed{M4}Drillago\t*4\tATK\|1600\tDEF\|1100"`: frame color, attribute dot, badge, table-aligned columns, level star) and a title icon/right text (`"@eOpponent\tLP 8000"`) |
| `sprite(id, rgba, w, h, x, y, dw, dh, alpha)` (v5) | RGBA image over the game (GPU renderers only), in 480x272 PSP pixels; call every frame it should stay; redrawn on a thread (1-3 frames of latency). Example: `mods_src/MonstrosNoCampo` |
| `quad_near(x, y, tol, wmin, wmax)` (v5) | 1 if the game drew a textured 3D quad (a card on the field...) centered near (x, y) in the frame on screen — tells whether a field slot is really visible (not hidden by a cut-in) |
| `use_menu(menu)` (v4) | which window menu the following `add_slider`/`add_toggle` go to: `TFMOD_MENU_MODS` (default) or `TFMOD_MENU_CHEATS` (the **Cheats** menu, separate from Mods) |

The API only grows: a plugin built for version N runs on runtimes with version ≥ N. Use `api->version >= N` before calling
newer functions. Everything runs on the game thread inside the frame: never block or wait.

Optional export: `void tfmod_marca(int n)` is called (on the game thread, with the game paused) when the player saves the "the CPU made
a mistake" mark number `n` (F7 or L3+R3, after typing the note): a good place to dump to the log whatever explains the move
(`mods_src/CPUvsCPU` dumps the board; `tools/marcas_cpu.py` builds the report).

When a hook calls game functions (`call`) before letting the original run, save and restore **all** registers around the call: the PSP
passes up to 8 arguments in a0–a3 and t0–t3, and `r2` holds the original's result when the hook wraps it (see `chama()` in
`mods_src/Cheats/cheats.cpp`).

### Creating a mod

Copy `mods_src/CaixaDeBoosters` (a small, well-commented example) to `mods_src/MyMod`, edit the `.cpp` and `mod.txt` and run
`sh tools/build_plugins.sh MyMod` (MSYS2 UCRT64; `package_release.py` and the first `build.sh` build all of them). The result goes to `gen/mods/MyMod/`
(`plugin.dll` plus the other files of the folder). The DLL is statically linked: it only depends on Windows. To find what to hook,
`TF_HOOK_TRACE=addr,addr` logs (frame, arguments) of each call. Careful: overlapping modules (libfield and libshop share the same
base 0x091C4600) — check `module_loaded("libshop")` inside the hook.

### Bundled behavior mods

- **Booster box** (`mods_src/CaixaDeBoosters`): in the card shop, on the pack selection screen, press **R** (Tab on the keyboard, or W
  in the classic layout; RB on a controller): the game's own message window asks "Buy all N packs for X DP?" and YES buys every remaining pack of that booster
  at once (limited by DP). The text is placed in PSP memory with `guest_alloc` and a hook on the game's string reader delivers it when
  the window is built.
- **Cheats** (`mods_src/Cheats`, see its README): optional cheats, each off by default, in their own **Cheats** menu (separate from Mods).
  **See the opponent's cards** (F6): a panel lists the CPU's hand and its face-down cards, one row per card in the color of its frame
  (Normal, Effect, Fusion, Ritual, Spell, Trap) with attribute, level and ATK/DEF (or the Spell/Trap type, e.g. Quick-Play); it only
  reads the duel state. **DP multiplier** (formerly the separate "DP multiplier" mod): 1x (default) to 10x the DP earned at
  the end of each duel, set in the Cheats menu or in game by holding **L + R** and pressing **Up/Down**; a hook on the duel reward
  routine measures the DP actually earned, adds the extra with the game's own "add DP" function (which respects the 999,999,999 cap)
  and scales the parcels the result screen reads. The value saved by the old mod is imported.
- **CPU vs CPU** (`mods_src/CPUvsCPU`): the CPU also controls your side (see [`AI.md`](AI.md)); doubles as the test arena.
- **Better AI** (`mods_src/IAAprimorada`) and **Better Decks** (`mods_src/DecksAprimorados`): see [`AI.md`](AI.md) and [`DECKS.md`](DECKS.md).

## Other bundled helpers

- **Xbox buttons**: replaces PSP button icons with Xbox ones (A = ×, B = ○, X = □, Y = △, Start, View = SELECT, LB/RB = L/R). The
  textures are generated by `python tools/mod_xbox_buttons.py` from images you place in the mod folder; the mod wins over a
  texture pack because it comes later in alphabetical order.
- **Title screen (Press Start)**: `tools/mod_titulo_hd.py` redraws the "PRESS START BUTTON" text and its red bar at 8x (default: Arial Black on the original box; `original` keeps the original letter shapes). Input: the original textures dumped with `TF_TEX_DUMP` into `gen/dump_tex/titulo/`. Output: `gen/mods/TituloHD/textures/`; the name sorts after `PPSSPP_MikeyMK`, so it wins.

## Texture packages (Original and Enhanced)

- **Graphics > Textures** is the in-game selector. **Original** means the unmodified PSP textures (no package); **Enhanced** loads the external `mods/EnhancedTextures` package (next to the executable; `gen/mods/EnhancedTextures` in a source build). F5 is the PC shortcut for the same selector.
- **Enhanced** (`gen/mods/EnhancedTextures`, built by `python tools/montar_enhanced.py`): 8201 PNGs, upscaled 2x with Real-ESRGAN (`realesrgan-x4plus-anime`, 4x then Lanczos to 2x; alpha is kept out of the network) and capped at 2048 px. It remains an external asset package; it is intentionally hidden from the host **Mods** menu because its enable/disable control is in the game's Graphics screen. The in-game switch reloads the index immediately; a replay switching from Original to Enhanced loaded all 14,163 indexed textures. Do not delete the folder if Enhanced visuals are wanted.
- The legacy `TexturasPC` package name is also hidden from the host Mods menu; the built-in Original choice now uses the PSP textures directly. Keep texture packages separate from other enabled mods; later mods win over earlier ones in alphabetical order.
