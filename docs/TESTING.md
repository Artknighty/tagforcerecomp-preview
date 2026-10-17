# Testing, replays, the arena and the diagnostic capture mode

Every run records the key presses, a copy of the starting save and a log into `gen/logs/` (`sessao_<date>_<time>.log/.inputs`,
`..._ms0/`). That recording is the basis of all tests: **a session can be replayed bit-for-bit** and compared.

## Replays

```sh
sh tools/replay.sh <session> [capture_step=1000] [seconds=900]
```

Runs a recorded session in an isolated folder (`gen/replay/<session><TAG>/`) without touching any real save. Useful variables:

| Variable | Effect |
|---|---|
| `ENDF=n` | last frame (default: last key press + 600) |
| `SNAPS=a,b,c` | exact frames to capture (BMP + contact sheet) |
| `MODDIR=folder` | use this mods folder in place (read-only) |
| `INPUTS=file` | use another key-press file instead of the session's |
| `PACE=1` | real-time pacing (measures load delays, real performance) |
| `TAG=_x` | suffix of the result folder (several replays side by side) |
| `TF_ISO` | path to your ISO (required) |

By default test runs are **hidden, silent and light**: window hidden and never activated (`TF_SEGUNDO_PLANO=1`, so it cannot steal the focus
of whatever you are doing), no audio (`NO_AUDIO=1`) and — the biggest saving — no in-RAM copy of the ISO (`TF_NO_ISO_CACHE=1`) with
the software renderer (`TF_GPU=0`): about **140 MB per duel instead of ~1 GB**, with identical results and speed.
`PRIMEIRO_PLANO=1` shows the window, `COM_SOM=1` enables sound, and `PESADO=1` (or `PACE=1`, or `TF_CAPTURAS`) keeps the full GPU
configuration for texture/scene/performance tests.

> **Recorded sessions are user data.** `tools/arena.sh`, `tools/bench.sh` and `tools/regressao.sh` refer to the maintainers' own recorded sessions
> (`gen/logs/sessao_*`), which are not in the repository. Play once to record your own (every session is recorded automatically), then
> replace the session id in those scripts. For the arena, record a session that ends right at the start of a Free Duel and save its key
> presses as `gen/arena/inputs_fd.txt` (the script needs an **absolute** path to it).

## The arena: CPU vs CPU

The `CPUvsCPU` mod makes the CPU play both sides and logs every move (`[mon]` lines: board at each turn, cards played) and the winner
(`[arena] FIM ...`). `tools/arena.sh <label> <seed> <side> ...` replays a recorded Free Duel with a fixed seed, so a duel is reproducible.

- `TF_RNG_SEED`, `TF_PRIMEIRO` (who starts), `TF_IA_LADO` (which side uses the Better AI), `TF_DECK_J0/J1` (deck per side, see
  [`DECKS.md`](DECKS.md)), `TF_ARENA_SAIR=1` (exit at the end of the duel).
- Measure with **paired** designs (same decks and seed, swapping which side has the change) and **dozens of duels** per arm: one duel
  says almost nothing, 30 duels still leave about ±9 percentage points of noise.
- Do not run more duels in parallel than your RAM and cores allow (6 is a good default in light mode).

## Batch scripts

All of them run several duels in parallel (default 6; each one is a separate hidden `run.exe`) and write one result file per duel to
`gen/arena/res_<label>_*.txt`:

| Script | Purpose |
|---|---|
| `tools/torneio.sh <label> <pairs-file> <mods-dir> [parallel]` | one duel per line `deckJ0 deckJ1 seed` (decks forced with `TF_DECK_J0/J1`) |
| `tools/ab_lote.sh <label> <side> <mods-dir> <seed-from> <seed-to> [parallel]` | same seeds, Better AI features on one side only |
| `tools/ab_valor.sh <label> <pairs-file> <mods-dir> [parallel]` | paired A/B of the learned board evaluation (`deckJ0 deckJ1 seed side`) |
| `tools/dados_lote.sh`, `tools/dados_modelo.sh` | collect training data (random exploration; with the rules or with the model deciding) |
| `tools/decks_ab.py` | wraps `torneio.sh` for an A/B of two decks and prints the win rate with its confidence interval |

Light mode keeps each duel around 140 MB, so the limit is mostly the number of CPU cores.

## Graphics diagnostic capture mode

For texture/scene bugs, the runtime can record screenshots **during normal play** so you can analyze them afterwards.

| Key / variable | Effect |
|---|---|
| `F9` or `TF_CAPTURAS=<seconds>` | toggle the mode (default interval 4 s) |
| `F8` | "this looks wrong": first pauses the game with a text box for a note (**Esc or an empty note discards the mark**, nothing is saved); with a note it saves the current frame **full size** (PNG, never deleted by the ring) plus two more 10 and 20 frames later (to catch flicker), the black box of the ~1.5 s presented before F8 (`seq_<n>/`, see `tools/seq_analisar.py`), the graphics settings and the textures drawn in the last 3 frames (hash, size, replacement pending/none). `python tools/capturas_analisar.py` writes `gen/marcas/<session>_graf.md` with all of it. Env `TF_MARCAG_QUADRO=<frames>` triggers it in replays, `TF_MARCA_NOTA_AUTO="text"` answers the box (empty = discard), `TF_MARCA_SEM_NOTA=1` saves without asking |
| `F7` / L3+R3 | the same general mark as F8: one note saves the graphics side above **and** the duel side (board state, last turns, plugin decisions; `tools/marcas_cpu.py`). `TF_MARCA_QUADRO=<frames>` triggers it in replays |
| automatic | periodic (JPEG ≤ 960 px; static scenes are skipped, at most one per minute), on scene change (module load), on a frame slower than 250 ms |
| `TF_CAPTURAS_MAX` | size of the ring (default 900 images) |

Files go to `gen/logs/<session>_capturas/` with an `indice.tsv` (file, frame, ms, tag, size). Then:

```sh
python tools/capturas_analisar.py [folder|ultima]
```

builds **contact sheets** (4×3 thumbnails with number, tag and time) in `folhas/`, writes `suspeitas.tsv` with automatic flags (almost
all black/white, magenta = missing texture, one colour covering the frame, no contrast) and prints a summary. Open the sheets and the
`marca` (F8) frames and look at them.

## Clean-room build test

`tools/teste_ambiente_limpo/` opens Windows Sandbox, installs MSYS2 and the build tools from scratch, builds the project with your ISO and checks that
the game boots. It is the way to verify the instructions in the README on a machine with nothing installed (see its README).

## Other tools

`tools/regressao.sh` (visual regression across renderers), `tools/bench.sh` (frame-time benchmark), `tools/interp_metrics.py`
(60 FPS interpolation metrics), `tools/limpar.sh` (clean test artifacts in `gen/`).
