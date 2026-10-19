# Monsters on the field (behavior mod for Tag Force Recomp) — EXPERIMENTAL PROTOTYPE

During duels, shows the art of your **face-up monsters** at a larger size, standing over their zone on the field (a small
"hologram" effect). It is off by default and only covers a few monsters for now.

**Requirements:** the GPU renderer (Direct3D 11 or Vulkan) and mod API version 5 or newer.

**Install:** copy the `MonstrosNoCampo` folder (`plugin.dll`, `mod.txt` and a `sprites/` folder) into `mods/` and check it in the
**Mods** menu (applies on restart). Then use **Mods → "Monsters on the field"** to switch it on and the slider to change the size.

**Sprites.** Nothing from the game is in this repository. The sprites (`sprites/<card id>.rgba`) are generated on your machine by
`tools/mods/mod_monstros.py` from the card art of a texture pack you own (the pack is only read, never modified).
`python tools/mods/mod_monstros.py [texture_pack] [mod_folder] [card ids...]` builds the sprites (with no ids, those of the starting Water deck).

**How it works.** It reads the duel state (zones of the player's monster row), asks the runtime whether the game really drew a card
quad at that position this frame (`quad_near`), and draws the sprite over it with `sprite`. When a cut-in or the result screen hides the
field, the sprites disappear. The measurements and addresses are documented at the top of `monstros.cpp`.

`config.ini` in the mod folder: `enabled=0/1`, `scale=50..200` (percent).
