# Story Duel LP (behavior mod for Tag Force Recomp)

Story duels start with 4000 Life Points for both duelists instead of the game's 8000 (faster, tenser duels).

**Install:** copy the `StoryLP` folder (`plugin.dll`, `mod.txt`) into `mods/` and check it in the **Mods** menu.

**How it works.** The duel engine's start function (`09eb14f4`) writes a fixed 8000 into both duelists' LP and tells the duel manager through
callback `09f62d88(13, player, LP, 0)`. The mod wraps the start function: while it runs, the LP in that callback and in the duel state is
replaced. Free Duel sets its own LP from the rules screen after this point, so it is not affected.

`config.ini` in the mod folder: `lp=4000` (the value for both duelists, 100..99999).
