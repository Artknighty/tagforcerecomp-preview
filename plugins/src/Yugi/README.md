# Yugi (behavior mod for Tag Force Recomp)

Adds **Yugi Muto** as a Free Duel opponent, **without touching the game files**. The game has three empty character slots (ids 47, 48, 49: no name, hidden
from the duelist lists, placeholder deck). The mod gives one of them (default 47) a name, a class and a deck, all in memory:

- **Name:** the character name tables of every language are pointed at "Yugi Muto" / "Yugi".
- **Class and visibility:** the slot gets a class, so the Free Duel opponent list and the Database list show it (Duelist 3 tab, between Lioside and Gillian).
- **Deck:** the Free Duel hands the opponent deck 246 (the slot's deck number), which the mod answers with `deck.txt`.
- **Save:** the slot's progress record is kept in RAM at load; the mod hides that write from the game's "Save all contents?" prompt. Without the mod the
  slot is hidden again, so a save that went through the mod plays like an untouched one.

**Install:** copy the `Yugi` folder into `mods/` and check it in the **Mods** menu (applies on restart).

**Files:** `deck.txt` (card ids, one per copy: `principal:`, `extra:`, `side:`, same format as BetterDecks) and `config.ini` (`slot=47|48|49`, `class=1..6`,
`name=`, `short_name=`).

**Portrait:** the list face is a cell of the EnhancedTextures head sheet and the Free Duel versus screen gets a picture drawn over the frame by the plugin (`portrait.rgba`); both are made from your own art with `tools/mods/mod_yugi_portrait.py` (not in the repository).

**Theme music:** put a WAV (PCM 16-bit, stereo, 44100 Hz) in the mod folder and name it in `config.ini` (`bgm=theme.wav`). While you duel Yugi, and only then, the game's duel music is replaced by that file (looped). Needs plugin API 7. Convert an MP3 with `ffmpeg -i in.mp3 -t 180 -ar 44100 -ac 2 -c:a pcm_s16le theme.wav`. The WAV is not shipped (bring your own).

**Known limits (v0.1):** the deck has no name in the duel setup screen, and Yugi exists only in Free Duel (no story scenes, dialogue or voice).
