# TF3 Cards (data + behavior mod for Tag Force Recomp)

Cards ported from Yu-Gi-Oh! GX Tag Force 3 into the Tag Force 1 duel engine. No game data is shipped here: the card data (name, text, stats in
all five languages, thumbnails and large art) is generated from your own copy of both games by `tools/mods/tf3_porte_carta.py`, which writes
the mod's `PSP_GAME/USRDIR/duelsys/cardinfo_*.ehp`, `card/cardm_*.cip`, `card/cardh_*.cip` and `ported.txt`. This plugin holds the behavior.

## How a ported card lives in the game

The game has room for exactly 2480 cards (the save keeps one collection record per card) and wires every id to its art, pack pools and effect,
so a ported card **takes over a Tag Force 1 card** and plays under that card's id (its *slot*): a card that no duelist deck uses, of the same
kind, with the closest name (the trunk list keeps the slot's alphabetical place). The slot card disappears from the game while the mod is on
(the new card is in its trunk entry, in the packs that had the slot card, and in your save's copy count of it). Turning the mod off restores it.

| Card | Slot | Effect |
|---|---|---|
| No Entry!! (TF3 6746) | Non Aggression Area (5549) | Plays like *Earthquake*: the plugin hooks the effect-record lookup (`09eb4574`) and looks the slot's record up as the equivalent card. |
| Gift Card (TF3 7234) | Generation Shift (6621) | Opponent gains 3000 Life Points: lends the record of *Tremendous Fire* (the same plain LP handler TF3 gives it, no extra word) and hooks the LP amounts (`09ee3efc`) to write +3000 for the opponent. |
| Suit of Sword X (TF3 3734) | Super Rejuvenation (5346) | Toss a coin: heads destroys the opponent's monsters, tails yours. Composed from game parts: borrows the *Dark Hole* record (destroy everything), tosses the coin with the game's own random and coin animation, and narrows the target filter to one side. The pattern for cards that combine effects the game already has. |

## Settings (`config.ini` in the mod folder)

- `grant_copies=3`: a ported card you own 0 copies of (the slot card was not in your collection) is added to the trunk as "new" with that many
  copies (0 = off). It only touches cards with 0 copies; save the game to keep them.
- Test aid `force_card` / `force_copies` / `force_deck_size` (env `TF3_FORCE_CARD`, `TF3_FORCE_COPIES`, `TF3_FORCE_DECK`; `force_card` takes the TF3 id
  of a ported card): puts copies of the card in your opening hand. More than 3 copies of one card hang the duel start. Off by default.
- `TF3_FORCE_OPP=1` puts the forced copies into the opponent's deck instead (the CPU does not use ported cards: its AI only knows the cards of the game).
- Research aids: `TF3_DEBUG_STREAM=1` and `TF3_DEBUG_REQ=1` log the card-art stream (BOOT `0882ead0`); `TF3_DEBUG_LP=1` logs the LP amounts the game computes for its own cards (the Gift Card formula was checked against Ookazi and Dian Keto this way).

## Making your own build

```
python -I tools/mods/tf3_porte_carta.py <mod folder> 6746 7234        # slots are chosen for you, or write <TF3 id>:<slot id>
sh tools/build/build_plugins.sh TF3Cards                              # plugin.dll (MODS_OUT=<dir> to build elsewhere)
```
Inputs: your TF1 files in `gen/jogo`, the TF3 files extracted into `ref/tf3` (see the header of `tools/game/tf3_cartas.py`), `gen/decks_db`.
New cards need an entry in `EFEITO` (`tf3_porte_carta.py`) when they play like a card of the game, or code in this plugin when they do not.
Japanese is not ported (that language keeps the original cards).
