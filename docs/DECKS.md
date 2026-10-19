# Duelist decks and the Better Decks mod (`DecksAprimorados`)

The game ships **351 duelist decks** (`RCP000` … `RCP350`). This document describes how they are stored, how the mod replaces them
without touching game files, and the tools to inspect and tune them. No deck list from the game is reproduced here: you extract
them from **your own copy**.

## Where the decks live

The decks are files `RCPnnn.ydc` inside an EHP archive embedded in `BOOT.BIN` (found with `find(b'EHP', 0x93298)`; `ehp()` in
`gen/ferramentas_decomp/scripts/cartas.py` opens it). Deck names (six languages) are in `recipe_name_{e,f,g,i,j,s}.bin` in the same
archive: a `u32` count, a `u32` table offset, then offsets to UTF-16 strings; the name index equals the `RCP` number.

### `.ydc` format

The reader is `libehsys_85A292D7` (`0884a694`). It ignores the first 8 bytes (a header that is not needed to read a deck), then:

```
u16 n_main   (<= 0x50)   followed by n_main  × u16 card id   (each copy listed separately)
u16 n_extra  (<  0x1f)   followed by n_extra × u16 card id   (fusion monsters)
u16 n_side   (<  0x10)   followed by n_side  × u16 card id
```

It fills a 0x108-byte buffer: `[0]=n_main, [1]=n_side, [2]=n_extra`, main deck at `+0x0c`, side at `+0xac`, extra at `+0xca` (u16 each).

### Who uses which deck

Each duelist structure in a duel setup (0x18c bytes per duelist, base `0x091c3d6c`) has the duelist id at `+0x120` and the deck index
at `+0x124` (values `0x15f…0x162` mean the player's own / saved decks). The main-cast characters have **six decks each, in
consecutive blocks** (a table in `libtitle.prx` marks the first deck of each block: 1, 7, 13, 19, 25, 31, 37). The order inside a block is
the in-game progression, **not** a strength order.

### Deck map (who owns each of the 351 decks)

Solved from the story's own data and written as code in `decomp/common/duelists.h` (`CharacterId`, `OwnerOfDeck`, `LadderDeck`, `SingleDeck`, `IsPlaceholder`).
The story gives a character his decks in `ScrNNNDeck.bin` (NNN = character id − 1) inside `script/st01_t01_brain.ehp` (first year) and
`st02_t01_brain.ehp` (second year); the last `u16` of each 16-byte record is the deck id (the first four are a month/day range, 0 = none).
The first year uses levels 1 and 2 plus the June duel (level 6); the second year uses levels 3, 4 and 5. Character names are in `BOOT.BIN`
at `0x71cc0` (full name, short name, in id order from 2).

| Decks | Owner | Rule |
|---|---|---|
| RCP000 | the player | model of the personal deck |
| RCP001 … RCP228 | characters 2 … 39 | ladder of 6: `deck = 1 + 6 × (character − 2) + (level − 1)`. Jaden … Zane are 2 … 8, Crowler 9, Banner 10, Atticus 11 … Sheppard 39 |
| RCP240 … RCP341 | characters 41 … 142 | one deck each: `deck = character + 199` (Colin, 85, is the exception: his slot RCP284 is a dummy) |
| RCP229, RCP230 | Colin (85) | special decks named by his script |
| everything else | nobody | the **placeholder** (same cards as RCP051, 171 decks): the game fills every unused slot with it |

Exceptions the scripts do on purpose: Nightshroud (40) duels with Atticus's level 1 (RCP055), Blair's script also names RCP109 (Belowski's level 1 by the
rule), and the characters who do not really duel (Doctor, Researchers, many fillers) get RCP235, a placeholder. Many ladders use only some levels
(`LadderUsedMask`); the main cast and Crowler use all six.

`python tools/game/decks_mapa.py` rebuilds `decomp/common/duelist_ids.inc` (ids and masks only, no game text) and writes `gen/decks_db/MAPA_DECKS.md` /
`mapa_decks.json` (the readable map with deck names, archetype and the story phase of each deck; game data, outside Git). It also prints every
deck whose script owner differs from the rule, so a different game version shows up at once.

## Extracting a database

```sh
python tools/game/decks_extrair.py        # needs gen/ferramentas_decomp (module dump) from your ISO
```

writes `gen/decks_db/{cartas,decks,protagonistas}.json` and a readable `DECKS.md` (all outside Git: it is game data).

## The mod

`plugins/src/DecksAprimorados` hooks the deck reader (`0884a694`). For every index that has a file
`mods/DecksAprimorados/decks/RCPnnn.txt` it returns that deck; for all others it runs the original. It therefore works in duels,
Free Duel, the recipe viewer and Story Mode, and turning the mod off restores the original decks. File format:

```
# comments start with '#'
principal: 4219 4219 5066 ...      # card ids, one per copy (up to 80)
extra: 6397 6397 ...               # up to 30
side:                              # up to 15
```

`TF_DECKS_OFF=1` disables the mod (useful to compare in the arena).

## Tools

| Tool | Purpose |
|---|---|
| `tools/game/decks_lib.py` | load/edit/validate/write decks by card **name**. Validation: legal copy limits are taken from the game itself — a card may appear at most as many times as in any original duelist deck, and cards no duelist uses are treated as forbidden |
| `tools/game/decks_v1.py`, `tools/game/decks_final.py` | the tuning recipes and the generator of the final mod files |
| `tools/game/decks_ab.py` | A/B two decks in the arena (both sides, same seeds) |
| `tools/game/decks_analise.py` | per-deck win rates and which cards the CPU rarely plays when drawn |
| `tools/ai/ia_uso_cartas.py` | the same card-usage statistic over all arena logs |

### Test arena with any two decks

`TF_DECK_J0=<n>` and `TF_DECK_J1=<n>` (set for the **CPU vs CPU** mod) force the deck of each side in the arena. The swap has to
happen before the game loads the duel (hook on `libehsys_F11F0D9A`, `0883f4f8`): swapping later, at engine start, crashes the game
because models and textures were already loaded for the original decks.

## Findings so far

Tuning against the original decks, with the arena AI on both sides, **removing cards the CPU does not know how to use** and adding
the trap cards it plays well gave the largest gains (for example +12 percentage points on one character's six decks). Cards the CPU
almost never plays when drawn (for example discard-heavy or fusion-dependent support) are the best candidates to replace.
