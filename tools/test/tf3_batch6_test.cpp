#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <map>

// Lightweight verification harness for Batch 6 binary representations

int main() {
    // 1. Verify BAR1 in stat.bin
    FILE *f = fopen("mods/TF3Cards/stat.bin", "rb");
    assert(f != nullptr);
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> stat_data(sz);
    assert(fread(stat_data.data(), 1, sz, f) == (size_t)sz);
    fclose(f);

    bool found_bar1 = false;
    for (size_t i = 0; i + 8 <= stat_data.size(); ++i) {
        if (stat_data[i] == 'B' && stat_data[i+1] == 'A' && stat_data[i+2] == 'R' && stat_data[i+3] == '1') {
            found_bar1 = true;
            uint32_t count = *(uint32_t *)&stat_data[i+4];
            assert(count >= 4); // Topaz Tiger, Jain, Galahad, Rampaging Rhynos
            printf("PASS: BAR1 found with %u entries\n", count);
            break;
        }
    }
    assert(found_bar1);

    // 2. Verify amount.bin
    f = fopen("mods/TF3Cards/amount.bin", "rb");
    assert(f != nullptr);
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> amt_data(sz);
    assert(fread(amt_data.data(), 1, sz, f) == (size_t)sz);
    fclose(f);

    assert(amt_data.size() >= 8);
    assert(amt_data[0] == 'T' && amt_data[1] == 'F' && amt_data[2] == '3' && amt_data[3] == 'V');
    uint32_t amt_count = *(uint32_t *)&amt_data[4];
    assert(amt_count > 0);
    printf("PASS: amount.bin TF3V header found with %u amount entries\n", amt_count);

    // 3. Verify effects.bin
    f = fopen("mods/TF3Cards/effects.bin", "rb");
    assert(f != nullptr);
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> fx_data(sz);
    assert(fread(fx_data.data(), 1, sz, f) == (size_t)sz);
    fclose(f);

    assert(fx_data.size() >= 8);
    assert(fx_data[0] == 'T' && fx_data[1] == 'F' && fx_data[2] == '3' && fx_data[3] == 'E');
    uint32_t fx_count = *(uint32_t *)&fx_data[4];
    assert(fx_count > 0);
    printf("PASS: effects.bin TF3E header found with %u effect records\n", fx_count);

    printf("ALL BATCH 6 C++ CHECKS PASSED!\n");
    return 0;
}
