#include "records.h"

int32_t FindEntryIndex(uint16_t key, const uint8_t *table, int32_t count, int32_t stride) {
    /* the original steps by (stride >> 1) halfwords, rounding a negative stride up */
    const int64_t step = 2 * (int64_t)(stride < 0 ? (stride + 1) >> 1 : stride >> 1);
    int32_t lo = 0, hi = count - 1;
    while (lo <= hi) {
        const int32_t mid = lo + (hi - lo) / 2;
        const uint16_t k = *(const uint16_t *)(table + (int64_t)mid * step);
        if (k == key) return mid;
        if (k < key) lo = mid + 1; else hi = mid - 1;
    }
    return -1;
}

int32_t MaxScrollPosition(const RankingRow *rows, uint32_t count) {
    if (count == 0) return 0;
    int32_t lines = 0;
    uint32_t fitted = 0;
    for (;;) {
        lines += rows[count - 1 - fitted].heightBits & 0x3f;
        if (lines > 9) break;
        if (++fitted >= count) break;
    }
    return fitted == count ? 0 : (int32_t)(count - fitted);
}

uint16_t LabelIndex(const uint16_t *table, uint32_t count, uint16_t item) {
    return item < count ? (uint16_t)(table[item] >> 1) : 0;
}
