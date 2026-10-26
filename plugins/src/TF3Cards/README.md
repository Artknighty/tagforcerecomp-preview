# TF3 Cards (behavior mod for Tag Force Recomp)

Cards ported from Yu-Gi-Oh! GX Tag Force 3 into the Tag Force 1 duel engine. The card data (name, text, stats, art) comes from the mod's own
`PSP_GAME/USRDIR/duelsys/cardinfo_eng.ehp` and `card/cardm_e.cip` / `cardh_e.cip`, generated from your own copy of the game with
`tools/mods/tf3_porte_carta.py` and `tools/mods/tf3_porte_arte.py`; no game data is shipped here. This plugin holds the behavior.

- **No Entry!!** (6746): "Change all Attack Position monsters to Defense Position." It plays like *Earthquake* (5983): the plugin hooks the
  effect-record lookup (`09eb4574`) and, for the ported card's id, looks the record up as the equivalent game card.
- Test aid (`config.ini` or env): `force_card` / `force_copies` / `force_deck_size` (`TF3_FORCE_CARD`, `TF3_FORCE_COPIES`, `TF3_FORCE_DECK`) put
  copies of a card in your opening hand. More than 3 copies of one card hang the duel start. Off by default (`force_copies=0`).
