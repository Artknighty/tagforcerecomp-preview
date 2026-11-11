# TF3 Cards (data + behavior mod for Tag Force Recomp)

Cards ported from Yu-Gi-Oh! GX Tag Force 3 into the Tag Force 1 duel engine. The full package imports 1,050 new cards, excluding tokens. Effect support is incomplete: importing a card's data does not implement all its rules. No game data is shipped here: the card data (name, text, stats in
all five languages, thumbnails and large art) is generated from your own copy of both games by `tools/mods/tf3_pacote.py`. The older slot builder, `tf3_porte_carta.py`, writes
the mod's `PSP_GAME/USRDIR/duelsys/cardinfo_*.ehp`, `card/cardm_*.cip`, `card/cardh_*.cip` and `ported.txt`. This plugin holds the behavior.

## How a ported card lives in the game

Two modes. **Extension mode** (`tools/mods/tf3_porte_ext.py`, the default) adds cards *beyond* the game's 2480-card table and takes nothing from
the game: card n gets the index 2480 + n and the id 6958 + n (the 13-bit id field of the card data allows up to 8191, so up to 1234 cards, more than
the 1,050 imported non-token cards). The game keeps fixed buffers for its card data, so nothing of the new cards goes into the game's files:

- **Tables.** `ext_info_<l>.bin` holds the card data rows, the names, the texts and the sort ranks of the new cards. When the game looks a `CARD_` table up in
  `cardinfo_<l>.ehp` (BOOT `08833540`), the plugin answers with a copy of the table plus the new rows. The mod has no `cardinfo` file.
- **Art on demand.** The art files (`cardm`/`cardh_<l>.cip`) get index entries for the new ids and a small pool of placeholder blocks, not the art. When the
  game asks for the art of a new card (BOOT stream API `08851b7c`/`08851f00`/`08851d24`), the plugin gives its index entry a pool block and, once the game
  has loaded that block, fills it from `ext_art_<m|h>_<l>.bin`.
- **Thumbnails.** The deck editor draws the trunk faces from an atlas (`deck/all_<l>.ehp`); the atlas holds the new cards.
- **Collection.** The save has no room for their records, so this plugin owns them (`collection_ext.bin` in the mod folder: status, copies, copy limit) and
  hands them out when the game asks (BOOT `0883de3c`).
- **Deck editor trunk.** The list builder (`libdeck 08efcb68`) only walks the ids 4007..6957: the plugin runs it, then appends the new cards (with its own,
  larger list buffers).
- New cards are in no booster pool (rarity 0) and the AI decks do not use them.

**Slot mode** (`tf3_porte_carta.py`, older): a ported card takes over an unused Tag Force 1 card (same kind, closest name) and plays under its id; the
slot card disappears while the mod is on. It has no card limit besides the number of free slots.

| Card | Slot (slot mode) | Effect |
|---|---|---|
| No Entry!! (TF3 6746) | Non Aggression Area (5549) | Plays like *Earthquake*: the plugin hooks the effect-record lookup (`09eb4574`) and looks the slot's record up as the equivalent card. |
| Gift Card (TF3 7234) | Generation Shift (6621) | Opponent gains 3000 Life Points: lends the record of *Tremendous Fire* (the same plain LP handler TF3 gives it, no extra word) and hooks the LP amounts (`09ee3efc`) to write +3000 for the opponent. |
| Suit of Sword X (TF3 3734) | Super Rejuvenation (5346) | Toss a coin: heads destroys the opponent's monsters, tails yours. Composed from game parts: borrows the *Dark Hole* record (destroy everything), tosses the coin with the game's own random and coin animation, and narrows the target filter to one side. The pattern for cards that combine effects the game already has. |

## Settings (`config.ini` in the mod folder)

- `grant_copies=3`: a ported card you own 0 copies of (the slot card was not in your collection) is added to the trunk as "new" with that many
  copies (0 = off). It only touches cards with 0 copies; save the game to keep them.
- Test aid `force_card` / `force_copies` / `force_deck_size` (env `TF3_FORCE_CARD`, `TF3_FORCE_COPIES`, `TF3_FORCE_DECK`; `force_card` takes the TF3 id
  of a ported card): puts copies of the card in your opening hand. More than 3 copies of one card hang the duel start. Off by default.
- `TF3_FORCE_OPP=1` puts the forced copies into the opponent's deck instead (the CPU does not use ported cards: its AI only knows the cards of the game).
- `force_cards=a,b,c` (TF3 ids) with `force_copies` copies each puts the cards in the opening hand. With `force_deck_size=0` (default) the whole deck is kept: the forced cards take the first deck positions and the duel start runs without shuffling (the game random returns 0 there, so the first player is fixed too). A small `force_deck_size` (for example 6) is a tiny deck that decks out in two turns. `TF3_FORCE_CARDS` overrides the list.
- Diagnostics: `TF3_DEBUG_TRUNK=1` (where the new cards land in the trunk order), `TF3_TRUNK_MAX=n` (list only n new cards), `TF3_DEBUG_BUMP=<id>:<copies>` (sets a new card's copies once, to test `collection_ext.bin`), `TF3_DEBUG_OBJ=1`, `TF3_DEBUG_LAZY=1` (art pool assignments and fills).
- Research aids: `TF3_DEBUG_STREAM=1` and `TF3_DEBUG_REQ=1` log the card-art stream (BOOT `0882ead0`); `TF3_DEBUG_LP=1` logs the LP amounts the game computes for its own cards (the Gift Card formula was checked against Ookazi and Dian Keto this way).

## Making your own build

```
python tools/mods/tf3_porte_ext.py <mod folder> 6746 7234 3734        # extension mode, up to 1234 ids (no -I: numpy and Pillow live in the user site)
python -I tools/mods/tf3_porte_carta.py <mod folder> 6746 7234        # slots are chosen for you, or write <TF3 id>:<slot id>
sh tools/build/build_plugins.sh TF3Cards                              # plugin.dll (MODS_OUT=<dir> to build elsewhere)
```
Inputs: your TF1 files in `gen/jogo`, the TF3 files extracted into `ref/tf3` (see the header of `tools/game/tf3_cartas.py`), `gen/decks_db`.
Effect specs live in `tools/mods/tf3_efeitos*.json` and `tf3_stats*.json`; `tf3_legacy_effects.py` retains older aliases. A shared native record is a starting point: activation gates, costs, fixed card IDs and additional clauses still need validation.
Japanese is not ported (that language keeps the original cards).

## Diagnostics
`TF3_OFF=<letters>` leaves hook groups out (d deck/duel, r effect record, f frame, c collection, t trunk list, s art stream, e tables) to bisect a problem.

## Data-driven effects
`double_tribute_normal: "light" | "dark" | "water" | "fire" | "earth" | "wind"` generates `tribute.bin` (`TF3B`).
Each row holds a source ID, a native double-Tribute donor and eligible Normal target IDs. Raw TF3 categories exclude Normal Tuners despite the older metadata converter.
The plugin delegates the read-only tribute predicate to that donor, retaining native controller, face-down material and negation checks.
Only the predicate temporarily sees the donor ID; material movement and stored field/Graveyard identities remain native and unchanged.

Native Charmer donors (6335–6338) share their target filter and control resolver. Imported sources retain their own Attribute.
Two native ownership-dispatch reads recognize the donor ID; the subsequent Attribute reads keep the actual imported identity.
This distinction is required for Dharc's DARK targets and avoids replacing its card data with the donor's EARTH data.

`summon.bin` contains shared Special Summon restrictions (`TF3S`, u32 count, u16 source ID / u16 flags per row).
Flags are 1 = block both players, 2 = block the opponent, and 4 = the source cannot itself be Special Summoned.
The first two flags require an active face-up source; the inherent restriction also applies while that source's effects are negated.
The plugin extends hand/Deck, Graveyard and banished candidate checks plus the native inherent hand-command builder, retaining native restrictions and non-Special command bits. The native Toon World presence check is unchanged.
Specs use `special_summon_lock: "both" | "opponent"` and `cannot_special_summon: true`. Barrier Statues use standalone `special_summon_except: "light" | "dark" | "water" | "fire" | "earth" | "wind"`, encoded as the allowed Attribute in bits 8..10. Multiple Statues intersect their permitted Attributes. `tf3_verify_parallel.py --barriers` runs one shared Attribute/zone/state/Fusion/Ritual matrix and per-source Normal Summon/removal and native Cyber Dragon scenarios. Token creation and broader chain timing remain separate acceptance work.

`effects.bin` (written by `tools/mods/tf3_efeitos_param.py` from `tools/mods/tf3_efeitos.json`) gives a new card an effect without new code: the card borrows the effect record of a game card
that plays the same way (`like`, the third column of `ported.txt`) and lists which cards its effect may pick (a sorted set of duel ids, resolved from a filter such as
`{"monster": true, "level_max": 4, "name_has": "Gladiator Beast"}`). The game keeps one small function per card that scans a zone and queues the cards the effect accepts;
for the cards of the file the plugin runs a scan against the id set instead (hooks `09f243c8` / `09f24428`). First zone supported: the Deck ("Add 1 ... from your Deck to your hand").
Checked in a duel: Gladiator Proving Ground opens the card list with only Gladiator Beast monsters of Level 4 or lower and adds the chosen card to the hand.
Zones: the Deck, the Graveyard (add to the hand), Special Summon from the hand and Special Summon from the Graveyard. `tools/mods/tf3_efeitos_texto.py` writes the specs of the cards whose text matches one of these patterns.

### Discard a monster to search for one named card

Gravekeeper's Commandant, Warrior of Atlantis, Destroyersaurus and Hecatrice reuse King of the Swamp's native discard and resolution callbacks. Their specs use `like: 6044`, `zone: "deck"` and a single `name_is` filter. The adapter changes Polymerization's fixed lookup only while that imported card resolves. Activation requires the named target in the Deck; the native donor and unrelated searches retain their original behavior.

The text generator recognizes the complete, single-clause form automatically. Cards with additional effects or a Deck-or-Graveyard search require separate work. Test with `tools/test/tf3_verify_parallel.py --suite hand-search --exe <isolated executable> --mods <candidate mods> --tag <unique tag>`. This suite covers the four searches, the native donor and missing-target cases; it does not establish every possible interaction.

### Existing-card verification

Black Veloci uses `battle_role_bonus: 400` and the `BAR1` stat.bin trailer (u32 count, u16 source ID/u16 magnitude). It reuses Steamroid's native +ATK when attacking a monster and -ATK when defending, restricted to the Damage Step. Native direct-attack exclusion, effect negation and kind-4 cleanup remain with Steamroid. The shared stat adapter temporarily uses Riryoku for addition and Miracle Kids for subtraction, restoring the node immediately. `tf3_verify_parallel.py --role-bonus` covers both signs, modifier interactions, real attacking/defending battles, direct attacks and native donor controls.

Garadholg and Rudra share `battle_light_bonus: 200/700`. The `BAP1` stat.bin trailer stores a u32 count and u16 source ID / u16 bonus rows. Native Penumbral Soldier Lady checks LIGHT opponents during the Damage Step and owns modifier lifetime. Two scoped source-dispatch aliases preserve other identity reads. Only the matching source's native kind-4 modifier uses Riryoku's variable additive amount during stat calculation; its original ID/value are restored immediately. `tf3_verify_parallel.py --light-bonus` separates controlled modifier math from actual battle activation. Native opponent deck overrides require the CPUvsCPU test plugin with player automation disabled.

Sonic Chick uses `battle_guard: {"stat": "atk", "min": 1900}` in the stat spec. The `BGP1` stat.bin trailer stores a u32 count followed by u16 source ID / u16 statistic (1 = ATK) / u32 minimum. It creates no stat bonus. The adapter scopes Spirit Reaper's native battle-calculation immunity to eligible battle records and restores their IDs immediately; printed stats, field identity and later events remain unchanged. `tf3_verify_parallel.py --battle-guard` covers real battles, negation and effect removal. Tuner-based Level protection is deferred with Tuner conversion.

The original 101-card review remains open. The explicitly selected simple-effect expansion is tracked separately in `tools/test/tf3_simple_expansion_ids.json`; `python tools/test/tf3_review101_gate.py` checks the exact union without declaring any card complete. Full-card acceptance requires an evidenced per-card checklist; local completion records can be checked with `--require-complete --review <review.json>`.

The simple-effect batch adds Worm Apocalypse's selected Spell/Trap destruction on FLIP, Cup of Ace's coin-selected two-card draw, and Broken Bamboo Sword's zero-bonus equipment behavior. Existing imported IDs are preserved; native TF1 cards are not duplicated. `tf3_verify_parallel.py --suite simple-effects` checks activation, recipients, target cases, negation and equipment cleanup in isolated games. Dark Resonator remains deferred because Tuner property conversion needs separate work.

The Calculator and Dweller in the Depths extend the continuous stat rules. The former sums current native monster levels, including level modifiers; the latter counts face-up Continuous Spells in the controller's Spell/Trap zones. `tf3_verify_parallel.py --suite continuous-final` checks their stat cases, natural summons/updates, Skill Drain and the existing-effect regression suites.

Golden Bamboo Sword reuses Pot of Greed's two-card draw and requires an own face-up Bamboo Sword Equip Spell at activation. The adapter preserves the native two-card Deck minimum and does not re-check the Equip during resolution. `tf3_verify_parallel.py --suite golden-final` covers requirements, normal summon/equip/activation, Imperial Order, native donor isolation and shared-path regression. Only Broken Bamboo Sword is available as a qualifying Equip in the TF3 card database.

Worm Dimikles uses a FLIP-only trigger with Slate Warrior's field resolver and a 300 ATK/DEF rule marked `after_flip`. The resolver writes native field state; no host-side instance cache is added. Real-ID field events honor explicit `trigger_record_like` overrides. Run `tf3_verify_parallel.py --dimikles --suite regression` for native-state comparison, Flip/Normal Summon, negation state, battle cleanup, equipment interactions and the existing regression batch. Natural Trap activation after resolution and wider chain interactions remain outside this acceptance scope.

Gravitic Orb reuses Windstorm of Etaqua's native position-change resolver and filter with Kotetsu's FLIP trigger. `trigger_record_kind: 0` explicitly selects Windstorm's Spell/Trap record for the field event; only the lookup uses that table. The live effect retains its monster identity and native immunity checks. `tf3_verify_parallel.py --orb --suite regression` covers targets, negation, natural battle, native Trap comparison and Wildheart's Trap immunity. Other donors retain their default record kinds.

Twister uses a private copy of Mystical Space Typhoon's record with native face-up Spell/Trap targeting and the native LP-cost callback. Its 500-LP formula retains Spell Economics. The native MST record is unchanged. `tf3_verify_parallel.py --suite twister` checks cost/target predicates and real hand activation scenarios.

Flame Ogre and Genex Power Planner restrict their shared Green Gadget summon adapter to Normal Summon. Flame Ogre rejects the shared Special Summon check and uses Sacred Crane's field draw record. `tf3_audit_native_records.py` checks trigger/resolution record compatibility against locally extracted TF1 data; this static check does not certify timing or complete card behavior. `tf3_verify_parallel.py --suite normal-summon` exercises Normal/Flip Summon and the Special Summon predicates.
The five imported Chrysalis monsters share native Chrysalis Dolphin's Neo Space requirement, self-Tribute and summon sequence. Effect zone `hand_deck_summon` selects one named ordinary Effect Monster from either the hand or Deck and respects the common summon restrictions. The scoped legality adapter prevents paying the Tribute when no legal named target exists; native Dolphin remains unchanged. Neo Space must remain face-up at resolution: its removal retains the paid Tribute but prevents the summon, matching the [official Dolphin FAQ](https://www.db.yugioh-card.com/yugiohdb/faq_search.action?cid=6764&ope=4&request_locale=ja). `tf3_verify_parallel.py --chrysalis` runs the family matrix, hand/Deck play routes and native controls. These checks do not certify broader chains or CPU ignition choices.

Captain Gold, Zeradias and Archfiend General share `deck_field_required` (TF3E zone 6): native discard-to-search plus continuous self-destruction when the named face-up Field Spell is absent from both sides. The common native stabilization scan handles source eligibility and destruction; no source ID alias or Kozaky burn record is used. The encoder requires a single exact Field Spell target and rejects additional options. `tf3_verify_parallel.py --field-search` groups search, survival, negation and removal checks for all three sources.

Checked in duels: Deck, hand and Graveyard (Special Summon from the Graveyard is built the same way but not yet checked).

## Named-target Ritual Spells (ritual.bin)

Synthesis Spell and Emblem of the Awakening reuse Black Luster Ritual's native record (donor 4676). The native Ritual table (09f767ac: one u32 per Ritual, bits 0-12 monster, 13-25 Spell, 26-31 total Levels) is followed directly by the candidate-scan table, so `ritual.bin` (`TF3R`: u32 count, u16 Spell / u16 monster / u8 Levels) rows are written over the terminator only while one of the seven native Ritual functions runs and the original bytes are restored on return. Spec: `"ritual": {"monster": <TF3 id>, "level": N}` with `like: 4676`. `tf3_verify_parallel.py --ritual` runs the legality matrix (82 checks, native controls) and four real hand-menu summons. Monster effects and generic "any Ritual Monster of a kind" Spells are not covered.

Machine Angel Ritual uses `"ritual": {"monsters": [6845, 6846, 6847], "level": "monster", "exact": true}`. Multi-target Spells enter the native monster-choice step before Tribute selection; the chosen row supplies both identity and Level. Exact-Level rules reuse End of the World's native material calculations with a scoped alias, retaining the real Spell for row ownership. TF3R Level byte bit 7 requests exact sums; its low six bits hold the Level. Existing rows keep their original encoding. `--cyber-ritual` adds the 130-check family/native matrix and three real hand-menu summons. These checks cover Ritual Spell behavior; Benten, Idaten and Dakini's own effects remain open, as do wider chains and CPU play decisions.
