# Contributing

Thanks for your interest! Bug reports, mods, fixes and ideas are welcome.

## The rules that matter

1. **No game data in the repository** — no textures, video, audio, ISO excerpts, card database, deck lists or code generated from the game.
   Everything derived from your copy of the game lives in `gen/`, `ms0/`, `mods/` and `ref/`, which are ignored by Git.
2. **Textures and videos go in their own mods** (`mods/<Mod>`), never inside third-party packs. Do not modify other people's texture packs.
3. **No code from PPSSPP** (GPL): it may be used as a read-only reference for hardware behavior and file formats, not copied.
4. Keep the project a **PC port**: native input, controller, fullscreen, dark UI — not an imitation of the PSP system UI.
5. Never write to a real save in tests: use the replay tools (they work on a copy), and keep tests on their own `.ini` and log folder.

By contributing you agree that your contribution is licensed under the project's [MIT license](LICENSE).

## Reporting a bug

Attach the session log from `gen/logs/` (`sessao_*.log`). A recorded session (`.inputs` + `_ms0/`) lets us replay the exact problem with
`tools/replay.sh`. For graphics problems, the **diagnostic capture mode** (F9, and F8 on the frame that looks wrong) produces
screenshots we can analyze: see [`docs/TESTING.md`](docs/TESTING.md).

## Before sending a change

- Renderer or frame-pacing changes: run `sh tools/regressao.sh` (it checks that the image does not change).
- AI or deck changes: measure with paired arena duels, dozens per arm (see `docs/TESTING.md` and `docs/AI.md`); say what you measured.
- New behavior mods: follow `docs/MODS.md` and keep the plugin API backward compatible (it only grows).
- Part of the code base still has Portuguese comments and log messages (the maintainers' first language); new comments, user-visible
  text and all documentation should be in **English**. See [`docs/PREVIEW.md`](docs/PREVIEW.md) for what is still being translated.

## Environment

Windows 10/11, MSYS2 UCRT64 (gcc/g++), Git Bash for the `sh tools/*.sh` scripts, Python 3 with Pillow and numpy. Edit files with an
editor, not through shell heredocs: backslashes inside inline strings get corrupted on Windows.
