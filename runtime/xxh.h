/* XXH64 (algoritmo publico de Yann Collet), implementacao propria. Usado no hash dos dados das texturas (texrepl.cpp). */
#pragma once
#include <stdint.h>
#include <string.h>
static inline uint64_t xxh_rotl64(uint64_t x, int r) { return (x << r) | (x >> (64 - r)); }
static inline uint64_t xxh64(const void *data, size_t len, uint64_t seed) {
    static const uint64_t P1 = 11400714785074694791ull, P2 = 14029467366897019727ull, P3 = 1609587929392839161ull, P4 = 9650029242287828579ull, P5 = 2870177450012600261ull;
    const uint8_t *p = (const uint8_t *)data, *end = p + len; uint64_t h;
    auto rd64 = [](const uint8_t *q) { uint64_t v; memcpy(&v, q, 8); return v; };
    auto rd32 = [](const uint8_t *q) { uint32_t v; memcpy(&v, q, 4); return v; };
    auto round = [&](uint64_t a, uint64_t in) { a += in * P2; a = xxh_rotl64(a, 31); return a * P1; };
    auto merge = [&](uint64_t a, uint64_t v) { v = round(0, v); a ^= v; return a * P1 + P4; };
    if (len >= 32) {
        uint64_t v1 = seed + P1 + P2, v2 = seed + P2, v3 = seed, v4 = seed - P1;
        do { v1 = round(v1, rd64(p)); v2 = round(v2, rd64(p + 8)); v3 = round(v3, rd64(p + 16)); v4 = round(v4, rd64(p + 24)); p += 32; } while (p <= end - 32);
        h = xxh_rotl64(v1, 1) + xxh_rotl64(v2, 7) + xxh_rotl64(v3, 12) + xxh_rotl64(v4, 18);
        h = merge(h, v1); h = merge(h, v2); h = merge(h, v3); h = merge(h, v4);
    } else h = seed + P5;
    h += (uint64_t)len;
    while (p + 8 <= end) { h ^= round(0, rd64(p)); h = xxh_rotl64(h, 27) * P1 + P4; p += 8; }
    if (p + 4 <= end) { h ^= (uint64_t)rd32(p) * P1; h = xxh_rotl64(h, 23) * P2 + P3; p += 4; }
    while (p < end) { h ^= (*p++) * P5; h = xxh_rotl64(h, 11) * P1; }
    h ^= h >> 33; h *= P2; h ^= h >> 29; h *= P3; h ^= h >> 32;
    return h;
}
