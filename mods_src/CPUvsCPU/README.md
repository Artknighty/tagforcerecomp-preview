# CPU vs CPU (behavior mod for Tag Force Recomp)

Makes the CPU also control **your** side in duels (Free Duel, story…): both players play with the game's AI (and with the Better AI mod,
if enabled, both sides get its improvements). Useful to watch the AI play your own deck and to study or compare AIs.

**Use:** copy the `CPUvsCPU` folder into `mods/`. Menu **Mods → "CPU vs CPU (0 = off, 1 = on)"**. It applies from the next duel.
`TF_CPUCPU=0/1` overrides for tests.

**Test arena.** This mod is also the engine of the arena described in `docs/TESTING.md`: it logs every move (`[mon]` lines), detects the end
of the duel (`[arena] FIM ...`), and reads these variables: `TF_RNG_SEED` (shuffle seed), `TF_PRIMEIRO` (who starts), `TF_ESPELHO`
(copy one side's deck to both), `TF_DECK_J0` / `TF_DECK_J1` (force the deck of each side), `TF_ARENA_SAIR=1` (exit after the duel).

**Technical:** the game keeps each player's control type at `0x09f77888 + 4*player` (0 = human, 1 = CPU); the plugin changes the 0 of player 0 into
1 in the call at `09f626dc`. Decks are forced in a hook on `0883f4f8` (duel setup).
