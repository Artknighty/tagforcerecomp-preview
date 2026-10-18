# Cheats (behavior mod for Tag Force Recomp)

Optional cheats, useful for testing the game (for example, watching what the CPU holds while it plays) and for players who want them.
Every cheat starts **off** (the DP multiplier starts at 1x) and is turned on in the game window's **Cheats** menu (separate from the Mods menu);
the choices are saved in `config.ini`.

**Install:** copy the `Cheats` folder (`plugin.dll` + `mod.txt`) into the game's `mods/` folder and check it in the **Mods** menu (applies on restart).
Requires Tag Force Recomp with mod API version 4 or newer.

## Cheats

| Cheat | How to use | What it does |
|---|---|---|
| **See the opponent's cards** | Cheats menu → "See the opponent's cards", or **F6** in game | A panel in a corner lists the CPU's hand and its face-down cards on the field (with their zone, e.g. M4 / S3): one row per card in the color of its frame (Normal, Effect, Fusion, Ritual, Spell, Trap), with the attribute, level and ATK/DEF of monsters or the Spell/Trap type (Quick-Play, Continuous, Counter...). In CPU vs CPU duels it shows both sides. It only reads the duel state: the duel itself does not change. The corner is chosen in Cheats → "Card panel position" (0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right). |
| **DP multiplier** (was the "DP multiplier" mod) | Cheats → "DP multiplier in duels", or in game hold **L + R** (keyboard Q + Tab, or Q + W in the classic layout) and press **Up/Down** | Multiplies the DP earned at the end of each duel, 1x to 10x. The result screens show the multiplied values. The game's cap (999,999,999 DP) still applies. The value saved by the old mod is imported automatically. |
| **Unlock everything** | Cheats → "Unlock everything (cards, packs, duelists)", with a save loaded | One-shot, also on key **F2**, or automatically once per start with `unlock_on_start=1` (launcher: Cheats page; if you then press Continue the game reloads the save and undoes it: press F2 again). Adds every card to the collection (3 copies each, marked as seen), makes every card pack available in the shop and puts every duelist that has real decks in the Free Duel opponent lists (all three tabs) with all of his decks. It edits the save in memory: **save the game afterwards** to keep it. Story progress, DP, level, the save flags and the duel statistics are not touched. Use it after the game has loaded your save (the game reloads the save file when you press Continue, which would undo it). |

`config.ini`: `show_opponent_cards=0/1`, `panel_corner=0..3`, `dp_multiplier=1..10`, `unlock_on_start=0/1`. For tests: `TF_CHEAT_VER=1`, `TF_DPMUL=N`, `TF_CHEAT_UNLOCK=<frame>` (applies the unlock once at that frame, if a save is loaded).

## Adding a cheat (for modders)

`cheats.cpp` has one section per cheat (state + config + hook/per-frame code) and registers them in `tfmod_init` with the mod API
(`mods_sdk/tfmod.h`): `use_menu(TFMOD_MENU_CHEATS)` (items go to the Cheats menu), `add_toggle` (on/off item, optional in-game key), `add_slider` (value), `panel` (info panel on screen),
`message_box`, `hook`/`call` (game functions). The duel memory map used here is documented at the top of `cheats.cpp`.
