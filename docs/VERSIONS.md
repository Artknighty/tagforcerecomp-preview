# Game versions

The recompiled code is valid for **one build of the game**: every PRX module and `BOOT.BIN` has different code and
addresses in each release. So each supported version has its own generated C code, its own installed data folder and its
own module table. The executable is the same for every version. Nothing of this is distributed; everything is produced on your machine from your own ISO.

| Version | Game ID | Generated code | Data folder | Module table |
|---|---|---|---|---|
| European (default) | `ULES-00600` | `gen/` | `gen/jogo/` | `tools/translator/modules.tsv` |
| American, and translations built on it (e.g. the PT-BR patch) | `ULUS-10136` | `gen/ULUS10136/` | `gen/jogo_ULUS10136/` | `tools/translator/modules_ULUS10136.tsv` |

The version is read from `UMD_DATA.BIN` on the disc (`tools/game/id_jogo.py`, `runtime/core/gameid.cpp`).

## Using another version

```sh
sh tools/build.sh && sh tools/install_dev.sh "<path to the ISO>"
```

The script detects the version from the ISO, installs the data in the right folder and translates the modules (needs a
`tools/translator/modules_<ID>.tsv`; it stops with a message if the version is not supported yet).
Several versions can coexist: just run it once per ISO.

At run time you can open any ISO with the same executable: the launcher and `--iso`/`--data` read the game ID and use the code
translated for that version.
`gen/tagforce_recomp.ini` keeps the last ISO; saves go to `ms0/PSP/SAVEDATA/<ID>0001`, so versions do not share saves
(the language field inside the save is version specific, so a save from another version is not loadable).

There is one build for every version: the game version is whatever was installed from the ISO (`sh tools/build.sh`, then install the ISO of that version).
Tests: `TF_ISO=<ISO> sh tools/test/play.sh ...` (use `TF_FRESH_SAVE=1`; the European save is not valid).

## Adding a new version

1. Copy `tools/translator/modules.tsv` to `tools/translator/modules_<ID>.tsv` (ID without the dash, e.g. `ULUS10136`).
2. `sh tools/install_dev.sh <ISO>`. The first load of each module tells you if its base is wrong
   (`[modulo] ... nenhuma base recompilada livre` / `regiao ocupada` in the log).
3. Check that the modules that are loaded together do not overlap: a module's recompiled code is placed at the base in the
   table, and its size (`<name>_memsz` in the generated `.c`) can differ from the other versions. Example: in `ULUS-10136`
   `libduel_draw` is bigger, so `libduel_eng` was moved from `09E6B800` to `09E6D000`.
4. Modules that exist only in that version (debug/viewer modules) are listed too; give them any free base.

## What is version specific

* Code, module bases, function addresses: generated per version (above).
* Plugins/mods (`plugins/src/*`) hook function addresses of the **European** build. On other versions they must not be used
  until their addresses are ported (see `plugins/sdk/tfmod.h`).
* The C++ decomp swaps (`decomp/`) name **European** function addresses: on other versions they are switched off at start-up
  (log line `[decomp] disabled: ...`), so those versions run the translated code only and miss the PC changes made there (the in-game
  Graphics screen, the title menu changes...).
* `runtime/gpu/pc_text.cpp` replaces the English "Memory Stick" texts by PC texts; other languages keep the original text. Hiding the
  loading icon (`hle_ge.cpp`) is European-only too.
