# Mouse Menus (experimental)

Native mouse for the game's own menu lists (title menu, Main Menu and every other `MenuList`). It does not read the picture or press the d-pad:
the plugin hooks the list controller (`libtitle` `0x08f019ac`, one call per game frame) and writes the list's own `selected` byte when the
pointer is over a row, so the cursor moves in the same frame, with the game's own cursor sound (25106).

- Hover a row: the cursor goes to it (disabled rows are skipped, like the d-pad).
- Click a row: the cursor goes to it and the PC scheme's left click = X confirms it (a click on a disabled row selects it so the game plays its "disabled" sound).
- Only lists. Option screens, dialogs, Deck Edit, duels and the map are not handled yet.

Known limits of the prototype: a click outside any row still confirms the current row (the left click is also X); the picture area is assumed to be the
16:9 / 4:3 / stretched rectangle of the Graphics > Aspect ratio setting without integer scaling; the title list assumes the PC-style 22 px row pitch.
