# Better Decks (behavior mod for Tag Force Recomp)

Replaces the decks of duelists with edited ones, **without touching the game files**: it hooks the game's deck reader. Full documentation: `docs/DECKS.md`.

**Install:** copy the `DecksAprimorados` folder into `mods/` and check it in the **Mods** menu (applies on restart). Turning it off restores the original decks.

**Deck files:** `decks/RCPnnn.txt` (nnn = the game's deck number). Format:
```
# comment
principal: <card ids, one per copy, up to 80>
extra: <fusion monster ids, up to 30>
side: <up to 15>
```
Any deck without a file keeps the original. The deck files are tuned in an arena of CPU-vs-CPU duels against the originals (see `docs/DECKS.md`). They are generated locally from **your own game data**
by `tools/game/decks_final.py`; this repository does not include deck lists.

`TF_DECKS_OFF=1` disables the mod (for comparing in the arena).
