# Game functions you can hook (European version, ULES-00600)

Addresses are PSP addresses in the game's code. Everything below is **already used by a mod in `plugins/src/`**, so it is known to work; open that mod for a
worked example. Arguments are in `r4..r7` (`A->reg(c, 4)`...), the result in `r2`, see [`MODS.md`](MODS.md) for the API and
[`plugins/examples/HelloMod`](../plugins/examples/HelloMod) for a minimal mod. These addresses are valid for one game version only: a mod declares
the versions it supports with `tfmod_game_ids`.

To find a new one, run the game with `TF_HOOK_TRACE=addr,addr` (logs frame and arguments of each call). Careful: overlapping modules (`libfield` and
`libshop` share the base `0x091C4600`): check `module_loaded("libshop")` inside the hook.

## General (always loaded: `BOOT` and the core libraries)

| Address | What it is | Used by |
|---|---|---|
| `08833fc4` | called once per frame: the place for a per-frame callback | Cheats (built in), Monsters on the field |
| `0884a694` | `libehsys_85A292D7(recipe, out)`: the **deck reader**; fills `out` (0x108 bytes: counts, main/side/extra card ids) for recipe `RCPnnn` | Better Decks, Card / Deck Randomizers |
| `0884a5cc` | `libehsys_E1677D35(recipe)`: pointer to the recipe's name (UTF-16) | Deck Randomizers |
| `0883f4f8` | `libehsys_F11F0D9A`: commits the duel setup (the player's saved deck is copied into the duel here) | Card / Player Deck Randomizer |
| `0884b46c` | card id -> card name | Cheats (built in), Better AI |
| `0884b4b8` | card id -> card description | Better AI |
| `0884b4f8` | card id -> address of the card's 8 bytes in `CARD_Prop` (ATK/DEF/level/attribute, layout in `tools/game/cartas.py`) | Card Randomizer, Cheats (built in) |
| `0884b7ec` / `0884b830` / `0884b77c` | card id -> ATK / DEF / level | Better AI |
| `0884c0d8` | card id -> is it a monster? | Better AI |
| `0883d908` / `0883d938` | read / add the player's DP | Cheats (built in) |
| `0883de3c` | card id -> collection entry (0 for ids that are not collectable cards) | Cheats (built in) |

## Duel (`libduel`, `0x09e.....` / `0x09f.....`)

| Address | What it is | Used by |
|---|---|---|
| `09f626dc` | `libduel_eng_5A41E0A6(player, type)`: the duelist's control type (human or CPU) | CPU vs CPU |
| `09eb14f4` / `09e9ccb8` | duel start (writes the default 8000 LP) / monster selection | CPU vs CPU, Story Duel LP (start only) |
| `09f62d88` | engine -> duel manager callback `(event, player, value, 0)`; event 13 = LP | Story Duel LP |
| `09f56da8` | stats of a monster slot (ATK at `+20`, DEF at `+24` of the buffer) | Better AI |
| `09f5060c` | `libduel_eng_43595669(player, slot, 1)`: can this monster attack now | Better AI |
| `09e74f60` / `09e762e0` / `09e76a14` | AI: attack / hand / spell decision | Better AI |
| `09e75258` / `09e6cff0` / `09e6c4a0` | AI: plan / position decision / set position | Better AI |
| `09e6bad4` | AI: play (summon) a monster | Better AI, CPU vs CPU |
| `09e87760` | AI: execute the chosen action | Better AI |
| `09e8133c` / `09e82bd4` | AI: simulate an action / predict Dark Hole | Better AI |
| `09e75cc0` | AI: score of the hypothetical board (original: total damage of the simulated attack) | Better AI |
| `08f08534` | duel reward routine (DP at the end of the duel) | Cheats (built in) |
| `08f0d7f4` | number drawn on the duel result screen | Cheats (built in) |
| `09f79d1c` | (data) duel field slots, stride `0xfac` | Monsters on the field |

## Card shop (`libshop`, `0x091c....`)

| Address | What it is | Used by |
|---|---|---|
| `091c6550` | shop main loop | Booster box |
| `091c72d4` | pack purchase animation (substates 0-6) | Booster box |
| `091ca5a4` | DP change animation | Booster box |
| `091c5eb8` / `091c8fb8` | prepares a purchase / reaction after buying | Booster box |
| `091d52cc` / `091d5520` | create / run the game's message window | Booster box |
| `088493cc` / `08849ad8` / `08841664` / `08863294` | window update / close, string reader, sound effect | Booster box |

The details of each (arguments, state tables) are in the header comment of the mod that uses it.

## Rules of thumb for coexisting mods

- A hook that returns `0` leaves the original **and the other mods' hooks** running: use it to observe or to change an argument (`set_reg(c, 4, ...)`).
- A hook that returns `1` ends the chain: only one mod can do that on a given address. The deck randomizers and Better Decks all change what the deck
  reader loads, which is why they are documented as mutually exclusive.
- `call_original(c, addr)` runs the original body without firing the hook of the address it is called for.
