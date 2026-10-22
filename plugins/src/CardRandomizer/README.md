# Card Randomizer (behavior mod for Tag Force Recomp)

Every duel is a surprise: when the game reads a deck for the duel, this mod replaces its cards with random ones drawn from **every card in the game**
(the pool is read from the game's own card database at run time, so no card list is shipped here).

- Main and side decks are filled with random non-Fusion, non-Token cards, keeping the original size.
- The Fusion (extra) deck is filled with random Fusion monsters.
- Settings in `config.ini` (mod folder): `randomize_player`, `randomize_opponent`, `keep_balance`, `max_copies`, `seed`.
- `TF_CARDRAND_OFF=1` disables the mod; `TF_CARDRAND_SEED=n` fixes the seed (tests).

Turn it on in the launcher's **Mods** page (applies on restart). It replaces the deck reader of the game, so it also overrides the Better Decks mod.
