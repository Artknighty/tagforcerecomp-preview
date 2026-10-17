# `libduel_eng`: duel rules and state

`libduel_eng` is loaded at `0x09E6B800` alongside `libduel_mgr` and `libduel_draw`. This module owns the duel's rules and mutable state; its field-card records live at `0x09F79D1C`, with one `0x14`-byte slot per zone and player records `0xFAC` bytes apart.

## Current C++ swaps

| C++ name | Original | Purpose |
|---|---:|---|
| `ReadCardSlotFlag9` | `FUN_09f49ca0` | Reads bit 9 from the selected field-card slot's state word. The meaning of this bit is still unknown. |
| `FindCardEffectEntry` | `FUN_09f462c8` | Walks an indexed effect list and returns the matching entry when its kind is 0–5. |
| `FindCardEffectEntryOfKind` | `FUN_09f4631c` | The same search, restricted to one kind. |
| `FindCardEffectEntryByValue` | `FUN_09f46378` | Finds kind 1/2 entries with no extra flags, optionally matching the 16-bit value. |
| `CountCardEffectEntries` | `FUN_09f4640c` | Counts entries of kind 0–5 with a given effect code. |
| `CountCardEffectEntriesByValue` | `FUN_09f464b4` | Counts entries by effect code and 16-bit value. |
| `HasFieldCardEffect` | `FUN_09f465c0` | Tests whether a field-card slot has an effect code. |
| `HasFieldCardEffectWithValue` | `FUN_09f4670c` | Tests for a kind 0–5 entry with the effect code and value. |
| `HasFieldCardEffectOfKind` | `FUN_09f4679c` | Tests for an effect code in one exact kind. |
| `HasFieldCardEffectKind1Or2` | `FUN_09f467f0` | Tests for a kind 1/2 entry with no extra flags, regardless of value. |
| `HasFieldCardEffectKind1Or2WithValue` | `FUN_09f46844` | Tests for a kind 1/2 entry with no extra flags and a matching value. |
| `GetFieldCardEffectValue` | `FUN_09f46898` | Looks up an effect code on a player's field-card slot and returns its 16-bit value, or `-1` when absent. |
| `IsCardNameRestricted` | `FUN_09f4faec` | Checks active field slots for card `0x1368`, compares card ids through `CardIdsEquivalent`, and tests the restriction's effect value. |
| `GetEffectiveFieldCardId` | `libduel_eng_EA6BA0BA` | Resolves the card id after the duel's identity-changing field effects and restriction checks. |
| `CardHasAttribute` | `FUN_09f49e18` | Compares a card's attribute, read through the `CardAttribute` ehsys import, with a requested code; preserves the original a1 register value across the import call. |
| `IsActiveFieldCardWithId` | `FUN_09f4c238` | Matches an effective card id, applies the field-slot eligibility mask, then checks the per-slot suppression bit. |
| `IsFieldCardTargetSelectable` | `FUN_09f4c338` | Checks the slot's +0x08 value and requires bit 1 in its state word to be clear; target-selection callers use it as an eligibility test. |
| `IsFieldCardCategoryInEffectSet` | `FUN_09f49cd8` | Uses flag 9 for three special card ids; otherwise asks the engine whether the card category belongs to its effect-capable set. |
| `CountActiveFieldCardsWithId` | `FUN_09f4c704` | Counts active matching card ids in the appropriate monster, spell/trap, or field-spell zones. |
| `CountOtherActiveFieldCardsWithId` | `FUN_09f4c490` | Counts active matches for one player while excluding one zone; field spells retain the original special case. |
| `CountActiveCardIdOnAllowedSides` | `FUN_09f4c984` | Counts across both sides or applies the two exclusion bits to count only allowed sides. |
| `IsDuelTargetAllowed` | `libduel_eng_331C6B94` | Checks continuous-effect and target-restriction records before allowing a monster or spell/trap zone as a target. |
| `CanSelectFieldZone` | `FUN_09f4e2c4` | Requires an empty zone, no occupied target override there, and approval from `IsDuelTargetAllowed`. |
| `FirstSelectableMonsterZone` | `FUN_09f4e500` | Returns the first monster zone from the game's priority table that can be selected, or `-1`. |
| `CountAvailableMonsterZones` | `FUN_09f4e3b0` | Counts selectable monster zones, capped by card `0x16DF`'s total-field-card limit when active. |

`duel_data.h` names the known parts of a field-card slot and leaves the rest as `unknownNN`. Effect entries are 8-byte linked-list nodes at `0x09F7C458`, indexed by 16-bit ids; 0 terminates the list. Extend the structures only when the generated code and observed duel state support the field's meaning.

## Coverage

The full recorded Free Duel against Jaden is `sessao_20261012_213918` (50,807 frames; the duel ends at frame 49,909). It exercises the manager, drawing and engine modules together. Function tracing of `libduel_eng` was split to fit the Windows environment-variable limit; the first half completed, while tracing the hottest second-half addresses timed out at frame 19,445. Use the recorded input session as the Oracle, and avoid assuming that partial traces cover the whole duel.

All 25 registered swaps were exercised by this duel. The field/effect queries and restriction check passed `TF_DECOMP_VERIFY=1` through frame 12,000; `GetEffectiveFieldCardId` through frame 5,000; `CardHasAttribute` through frame 12,000; and field-card eligibility/category/count, target, zone and monster-availability queries through frames 6,000 or 12,000. No verify run reported a mismatch. Incremental full-session comparisons kept all 13 captures identical: first with the then-current `libduel_eng` batch skipped, then with each later swap isolated against its generated body. `CountCardEffectEntriesOfKind` exists in the effect-list format but was not called by this session and has not been swapped.

## Builds

The swaps compile into every build (`decomp/libduel_eng/*.cpp`, glob in `tools/build_run.sh`), including the single player build `gen/run.exe`; `sh tools/build_run.sh` (with the game closed) also refreshes `gen/run_bg.exe`, the executable replays use.

The module is not complete: all functions without a C++ swap still run as generated code.
