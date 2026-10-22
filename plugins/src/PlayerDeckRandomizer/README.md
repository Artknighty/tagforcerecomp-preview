# Player Deck Randomizer (behavior mod for Tag Force Recomp)

Every duel, **your** deck is swapped for a whole random deck (recipe) of the game, and that deck's own name is shown at the start of the duel
(the pool is read from the game's own data at run time, so no deck list is shipped here). Your saved deck is not touched.

- Placeholder and empty recipes are skipped; every real recipe (story characters' ladders, unique duelists, special decks) can be drawn.
- Turn on **Opponent Deck Randomizer** as well to randomize both sides; each works alone or together.
- `config.ini`: `seed` (0 = new random deck every duel). `TF_DECKRAND_OFF=1` disables the mod, `TF_DECKRAND_SEED=n` fixes the seed (tests).

Turn it on in the launcher's **Mods** page (applies on restart). It hooks the same deck reader as Card Randomizer and Better Decks: do not combine it with them.
