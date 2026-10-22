# Cheats (part of the recomp)

Optional cheats, useful for testing the game (for example, watching what the CPU holds) and for players who want them.
Cheats are a feature of the recomp itself (`runtime/mods/cheats.cpp`, compiled into the exe), not a mod: there is nothing to install and nothing in `mods/`.
Every cheat starts **off** (the DP multiplier starts at 1x) and is turned on in the launcher's **Cheats** page (the game window has no menu bar);
the choices are saved in `cheats.ini` next to `tagforce_recomp.ini`, where the page's master switch is the `enabled` key. Settings of the old `mods/Cheats`
folder are imported on the first start. Works with the European version (the hooks use its addresses).

## Cheats

| Cheat | How to use | What it does |
|---|---|---|
| **See the opponent's cards** | Launcher → Cheats, or **F6** in game | A panel in a corner lists the CPU's hand and its face-down cards on the field (with their zone, e.g. M4 / S3): one row per card in the color of its frame (Normal, Effect, Fusion, Ritual, Spell, Trap), with the attribute, level and ATK/DEF of monsters or the Spell/Trap type (Quick-Play, Continuous, Counter...). In CPU vs CPU duels it shows both sides. It only reads the duel state: the duel itself does not change. The corner is chosen in Cheats → "Card panel position" (0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right). |
| **DP multiplier** (was the "DP multiplier" mod) | Cheats → "DP multiplier in duels", or in game hold **L + R** (keyboard Q + Tab, or Q + W in the classic layout) and press **Up/Down** | Multiplies the DP earned at the end of each duel, 1x to 10x. The result screens show the multiplied values. The game's cap (999,999,999 DP) still applies. The value saved by the old mod is imported automatically. |
| **Unlock everything** | Cheats → "Unlock everything (cards, packs, duelists)", with a save loaded | One-shot, also on key **F2**, or automatically once per start with `unlock_on_start=1` (launcher: Cheats page; if you then press Continue the game reloads the save and undoes it: press F2 again). Adds every card to the collection (3 copies each, marked as seen), makes every card pack available in the shop and puts every duelist that has real decks in the Free Duel opponent lists (all three tabs) with all of his decks. It edits the save in memory: **save the game afterwards** to keep it. Story progress, DP, level, the save flags and the duel statistics are not touched. Use it after the game has loaded your save (the game reloads the save file when you press Continue, which would undo it). |

| **Fast-forward** | Cheats -> "Fast-forward (game speed)" (1x to 8x), or in game hold **L + R** and press **Left / Right**. The game runs faster and the music is time-stretched (same pitch). Always starts at 1x (not saved). |

`cheats.ini`: `enabled=0/1`, `show_opponent_cards=0/1`, `panel_corner=0..3`, `dp_multiplier=1..10`, `unlock_on_start=0/1`. For tests: `TF_CHEAT_VER=1`, `TF_DPMUL=N`, `TF_CHEAT_UNLOCK=<frame>` (applies the unlock once at that frame, if a save is loaded).

## Adding a cheat

`cheats.cpp` has one section per cheat (state + config + hook/per-frame code) and registers them in `cheats_init` with the plugin API
(`plugins/sdk/tfmod.h`; `plugins.cpp` hands it over exactly as it does to a mod): `use_menu(TFMOD_MENU_CHEATS)`, `add_toggle` (on/off item, optional in-game key), `add_slider` (value), `panel` (info panel on screen),
`message_box`, `hook`/`call` (game functions). The duel memory map used here is documented at the top of `cheats.cpp`.
