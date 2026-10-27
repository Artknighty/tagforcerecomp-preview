# TF3 Cards (data + behavior mod for Tag Force Recomp)

Cards ported from Yu-Gi-Oh! GX Tag Force 3 into the Tag Force 1 duel engine. No game data is shipped here: the card data (name, text, stats in
all five languages, thumbnails and large art) is generated from your own copy of both games by `tools/mods/tf3_porte_carta.py`, which writes
the mod's `PSP_GAME/USRDIR/duelsys/cardinfo_*.ehp`, `card/cardm_*.cip`, `card/cardh_*.cip` and `ported.txt`. This plugin holds the behavior.

## How a ported card lives in the game

Two modes. **Extension mode** (`tools/mods/tf3_porte_ext.py`, the default) adds cards *beyond* the game's 2480-card table and takes nothing from
the game: card n gets the index 2480 + n and the id 6958 + n (the 13-bit id field of the card data allows up to 8191, so up to 1234 cards, more than
the 1114 cards that exist only in Tag Force 3). The game keeps fixed buffers for its card data, so nothing of the new cards goes into the game's files:

- **Tables.** `ext_info_<l>.bin` holds the card data rows, the names, the texts and the sort ranks of the new cards. When the game looks a `CARD_` table up in
  `cardinfo_<l>.ehp` (BOOT `08833540`), the plugin answers with a copy of the table plus the new rows. The mod has no `cardinfo` file.
- **Art on demand.** The art files (`cardm`/`cardh_<l>.cip`) get index entries for the new ids and a small pool of placeholder blocks, not the art. When the
  game asks for the art of a new card (BOOT stream API `08851b7c`/`08851f00`/`08851d24`), the plugin gives its index entry a pool block and, once the game
  has loaded that block, fills it from `ext_art_<m|h>_<l>.bin`.
- **Thumbnails.** The deck editor draws the trunk faces from an atlas (`deck/all_<l>.ehp`); the atlas holds the new cards (3.4 MB with all 1114 fits).
- **Collection.** The save has no room for their records, so this plugin owns them (`collection_ext.bin` in the mod folder: status, copies, copy limit) and
  hands them out when the game asks (BOOT `0883de3c`).
- **Deck editor trunk.** The list builder (`libdeck 08efcb68`) only walks the ids 4007..6957: the plugin runs it, then appends the new cards (with its own,
  larger list buffers).
- New cards are in no booster pool (rarity 0) and the AI decks do not use them.

**Slot mode** (`tf3_porte_carta.py`, older): a ported card takes over an unused Tag Force 1 card (same kind, closest name) and plays under its id; the
slot card disappears while the mod is on. It has no card limit besides the number of free slots.

| Card | Slot (slot mode) | Effect |
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
- `force_cards=a,b,c` (TF3 ids) with `force_copies` copies each puts the cards in the opening hand. With `force_deck_size=0` (default) the whole deck is kept: the forced cards take the first deck positions and the duel start runs without shuffling (the game random returns 0 there, so the first player is fixed too). A small `force_deck_size` (for example 6) is a tiny deck that decks out in two turns. `TF3_FORCE_CARDS` overrides the list.
- Diagnostics: `TF3_DEBUG_TRUNK=1` (where the new cards land in the trunk order), `TF3_TRUNK_MAX=n` (list only n new cards), `TF3_DEBUG_BUMP=<id>:<copies>` (sets a new card's copies once, to test `collection_ext.bin`), `TF3_DEBUG_OBJ=1`, `TF3_DEBUG_LAZY=1` (art pool assignments and fills).
- Research aids: `TF3_DEBUG_STREAM=1` and `TF3_DEBUG_REQ=1` log the card-art stream (BOOT `0882ead0`); `TF3_DEBUG_LP=1` logs the LP amounts the game computes for its own cards (the Gift Card formula was checked against Ookazi and Dian Keto this way).

## Making your own build

```
python tools/mods/tf3_porte_ext.py <mod folder> 6746 7234 3734        # extension mode, up to 1234 ids (no -I: numpy and Pillow live in the user site)
python -I tools/mods/tf3_porte_carta.py <mod folder> 6746 7234        # slots are chosen for you, or write <TF3 id>:<slot id>
sh tools/build/build_plugins.sh TF3Cards                              # plugin.dll (MODS_OUT=<dir> to build elsewhere)
```
Inputs: your TF1 files in `gen/jogo`, the TF3 files extracted into `ref/tf3` (see the header of `tools/game/tf3_cartas.py`), `gen/decks_db`.
New cards need an entry in `EFEITO` (`tf3_porte_carta.py`) when they play like a card of the game, or code in this plugin when they do not.
Japanese is not ported (that language keeps the original cards).

## Diagnostics
`TF3_OFF=<letters>` leaves hook groups out (d deck/duel, r effect record, f frame, c collection, t trunk list, s art stream, e tables) to bisect a problem.

## Data-driven effects
`effects.bin` (written by `tools/mods/tf3_efeitos_param.py` from `tools/mods/tf3_efeitos.json`) gives a new card an effect without new code: the card borrows the effect record of a game card
that plays the same way (`like`, the third column of `ported.txt`) and lists which cards its effect may pick (a sorted set of duel ids, resolved from a filter such as
`{"monster": true, "level_max": 4, "name_has": "Gladiator Beast"}`). The game keeps one small function per card that scans a zone and queues the cards the effect accepts;
for the cards of the file the plugin runs a scan against the id set instead (hooks `09f243c8` / `09f24428`). First zone supported: the Deck ("Add 1 ... from your Deck to your hand").
Checked in a duel: Gladiator Proving Ground opens the card list with only Gladiator Beast monsters of Level 4 or lower and adds the chosen card to the hand.
Zones: the Deck, the Graveyard (add to the hand), Special Summon from the hand and Special Summon from the Graveyard. `tools/mods/tf3_efeitos_texto.py` writes the specs of the cards whose text matches one of these patterns.
Checked in duels: Deck, hand and Graveyard (Special Summon from the Graveyard is built the same way but not yet checked).
