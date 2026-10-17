/* libduelrecord: the Duel Ranking screen (Main Menu > Duel Ranking). Functions are named after what they do. */
#pragma once
#include <stdint.h>

/* Binary search over a table of fixed-size entries whose first field is a sorted u16 key.
 * Returns the index of the entry with that key, or -1. Original: FUN_00000614 (module offset 0x614). */
int32_t FindEntryIndex(uint16_t key, const uint8_t *table, int32_t count, int32_t stride);

#include "ranking_state.h"

/* How many rows can scroll off the top: the number of rows minus the rows (counted from the bottom) that fit in 9 text lines.
 * 0 when everything fits. Original: FUN_0000299c. */
int32_t MaxScrollPosition(const RankingRow *rows, uint32_t count);

/* Text table index of a ranking item's label (the table entry shifted right by one), 0 when the item is out of range.
 * `table` holds `count` u16 entries. Original: FUN_000028e0 (which picks table and count by tab). */
uint16_t LabelIndex(const uint16_t *table, uint32_t count, uint16_t item);
