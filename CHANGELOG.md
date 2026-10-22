# Changelog

Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Versioning: [SemVer](https://semver.org/).

## [Unreleased]

### Changed
- **Cheats is now part of the recomp, not a mod.** It lives in the exe (`runtime/mods/cheats.cpp`) and no longer needs `mods/Cheats`; the launcher's Cheats page and the in-game keys are unchanged. Settings moved to `cheats.ini` next to `tagforce_recomp.ini` (the old `mods/Cheats/config.ini` and its on/off switch are imported on the first start; an old `mods/Cheats` folder is ignored).

### Added
- **Exclusive fullscreen** (Direct3D 11). The launcher's Window mode now has three choices: Windowed, Fullscreen (exclusive) and Fullscreen borderless (the previous "Fullscreen"; saved settings keep working). If the driver refuses exclusive mode, or with Vulkan, the game falls back to borderless. Alt+Tab leaves exclusive mode and comes back with the window.

## [0.4.1] - 2026-10-22

### Added
- Modding: a minimal `HelloMod` template (`plugins/examples/HelloMod`), a list of the game functions mods can hook (`docs/HOOKS.md`), a `TFMOD_REQUIRE` version check for plugins, and `tools/test/sdk_check.sh`, which freezes the plugin ABI and checks that every mod still builds and loads after an SDK change.

### Changed
- Mod folders have English names: `BoosterBox`, `BetterDecks`, `BetterAI`, `MonstersOnField` (were `CaixaDeBoosters`, `DecksAprimorados`, `IAAprimorada`, `MonstrosNoCampo`). An older folder is renamed automatically at startup, keeping its settings and on/off state.

## [0.4.0] - 2026-10-21

### Added
- **Player Deck Randomizer** and **Opponent Deck Randomizer** mods: every duel, your deck and/or the opponent's becomes a random whole recipe of the game, and the deck's own name shows at the start of the duel. Each works alone or together.
- Card Randomizer mod: refills every duel deck with random cards from the whole card pool.
- Fast-forward cheat (game speed 1x to 8x): in game with L + R + Left / Right, or a starting speed on the launcher's Cheats page. The music is time-stretched, so it speeds up without changing pitch.

### Changed
- **The main download no longer contains any mods** (closest to the original game). All behavior mods are in a second file of the release, `TagForceRecomp-Mods-<version>.zip`; each starts switched off. Copy its `mods` folder next to the exe and turn them on in the launcher's Mods page.
- The launcher is more compact (smaller fonts and rows).

### Removed
- MSAA anti-aliasing (it cost performance and was barely visible).
- The game window's menu bar (File / View / Mods / Cheats / Help): every setting lives in the launcher now. F3, F5, F11, Alt+Enter and the in-game cheat combos still work. Plugin sliders (API v3 `add_slider`) are accepted but no longer shown.

### Fixed
- Deck Edit no longer freezes when moving a card from the Deck to the Trunk (a rewritten window helper ignored one of its arguments).

## [0.3.3] - 2026-10-21

### Fixed
- Audio on slower PCs: late frames no longer cause crackling or lost game time; the audio reserve adapts to the machine.
- Audio output rewritten (WASAPI with a continuous resampler) for cleaner, steadier sound.
- 60 FPS no longer stays suspended when a heavy scene is over; it backs off and recovers.
- Smoother frame pacing (D3D11 timing, three back buffers).
- Faster startup after the first run: compiled code is cached.
- Sharper application icon at every window size, correct at any display scaling.

## [0.3.2] - 2026-10-18

### Fixed
- Fixed the public build crashing when starting Story mode from Part 1.
- Fixed the public build crashing when entering the tutorial.

## [0.3.1] - 2026-10-18

### Added
- Yu-Gi-Oh! GX Tag Force logo as the application icon.

### Improved
- More stable 120 FPS pacing while exploring the map.
- Smoother audio transitions in demanding scenes.

### Fixed
- Fixed the public build closing immediately on startup.
- Fixed startup when the package is extracted to a path containing accented characters.

## [0.3.0] - 2026-10-18

### Added
- **Mods** page in the launcher: turn mods on/off and change their priority order (`mods/load_order.txt`).
- **Cheats** page in the launcher; "Unlock everything" also on the F2 key.
- Window mode (windowed / fullscreen) in the launcher and in the Graphics menu.
- Quick marks on the controller, no note box: L3 = graphics glitch, R3 = performance, Start+Select = audio (saves the last 8 s as a WAV).
- Continuous monitor recording to hunt flicker (`tools/test/jogar_gravando.bat`).

### Changed
- Cheats, CPU vs CPU and Monsters on the Field no longer appear in the mod list (they have their own menu).
- Smoother music/audio transition between areas.
- The mouse cursor hides after 3 s when a controller is on.
- The F8 mark shows the real FPS mode (120/60/30).

### Fixed
- 120 FPS: text letters appearing gray or sliding when a message changes; NPC head detached from the body; menu item bar bending; map character freezing or clipping when changing pose.

## [0.2.0] - 2026-10-17

### Added
- 120 FPS option (interpolation with measured pacing) and smoother 60 FPS during the whole session.
- HD textures compressed to BC7 (a quarter of the memory), with a disk cache and background loading.
- Title menu with Continue, New Game and Exit to Desktop (asks before leaving); player name typed inside the game.
- Graphics option inside the game and in the launcher, with an Original/Enhanced texture switch (F5).
- "Unlock everything" cheat: cards, packs and Free Duel duelists.
- Single problem mark F8 (or F7 / L3+R3): records graphics and CPU state with one note.
- `tools/test/tf.py`: plays the game by itself (buttons, captures, memory reads) for automated tests.
- Readable C++ decomp of the title, boot, story mode, map and duel result screen.
- Map of the 351 decks by duelist.

### Changed
- Menus, dialog boxes and the Duel Result screen redrawn in navy/gold in the Enhanced Textures package.
- The HD font is now part of Enhanced Textures (no option of its own).
- Only one instance per save folder.

### Fixed
- Lines and seams on the head of HD portraits; line through the middle of card art at high resolution.
- Polymerization cards disappearing; wrong deck in Free Duel (Zane got Jaden's).
- Flicker on message change and character entrance with 60/120 FPS; slowdowns on first run.
- Crash on the story map; wrong character schedule (player and director) on the map.

### Removed
- "Integer scale" and "Texture Upscaling" options (replaced by the Enhanced package).
