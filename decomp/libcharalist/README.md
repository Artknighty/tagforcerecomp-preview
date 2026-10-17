# libcharalist: the character list library

Used by the story code (libstory) to show a random character: it builds three lists of characters from its own table (41 entries, `CharacterEntry`) and the
save data (progress records, card ownership, three save flags that reveal special characters), picks one entry at random and keeps the choice
(`ChosenCharacter`, `ChosenPicture`) until `Unload`. The module image loads at `0x08EEB400` (the same base as libduelrecord, at other times); the swap
table name is `libcharalist_08eeb400`. It has 57 functions; the 13 the story runs are replaced here (`charalist.cpp`, typed views in `charalist.h`).

| function | what it does |
|---|---|
| `Start` (E11636EC) | clear the state, build the lists, count the entries of the first two lists (the third only when those are empty), pick one at random, choose its picture, free the lists |
| `BuildLists` (`FUN_00003f68`) | walk the ids 2..151; the ones with a category 1..6 take the next roster position and go into the list of their kind when they qualify in the current mode |
| `BuildDetailList` (`FUN_0000519c`) | lists 0 and 1: a 0x28-byte `DetailRecord` per character (pictures, names, special cases for three characters until their save flag is set) |
| `BuildSimpleList` (`FUN_0000581c`) | list 2: character and category only |
| `FreeLists` (`FUN_00000bec`) | frees the lists and their records |
| `LoadDatabase`, `DatabaseResource` | the `charalist.ehp` archive, loaded on first use, and its nine named pictures/tables (the file name has the language letter) |
| `FindCharacterEntry`, `FindEntryIndex` | binary searches (the second is the Duel Ranking's) |
| `Unload`, `SeedGenerator`, `ChosenCharacter`, `ChosenPicture` | the exports |

## How it was checked

Replay of `sessao_20261012_190727` (the story start, frame 1700): with all these swaps on and with `TF_DECOMP_SKIP=libcharalist.*` (the original code) the story picks the
same character and picture (8 / 0) from lists of the same sizes (2, 0, 10), and the capture is identical. `FindCharacterEntry`, `FindEntryIndex`, `ChosenCharacter` and
`ChosenPicture` are also `pure` (compared with `TF_DECOMP_VERIFY`). The others take heap blocks or random numbers and cannot be run twice.
