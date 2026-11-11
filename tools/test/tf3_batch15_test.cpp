#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

int main() {
    // 1. Verify summon.bin
    FILE *f = fopen("mods/TF3Cards/summon.bin", "rb");
    assert(f != nullptr);
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> sum_data(sz);
    assert(fread(sum_data.data(), 1, sz, f) == (size_t)sz);
    fclose(f);

    assert(sum_data.size() >= 8);
    assert(sum_data[0] == 'T' && sum_data[1] == 'F' && sum_data[2] == '3' && sum_data[3] == 'S');
    uint32_t sum_count = *(uint32_t *)&sum_data[4];
    assert(sum_count >= 60);
    printf("PASS: summon.bin TF3S header found with %u summon rules (>= 60)\n", sum_count);

    // 2. Verify stat.bin
    f = fopen("mods/TF3Cards/stat.bin", "rb");
    assert(f != nullptr);
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> stat_data(sz);
    assert(fread(stat_data.data(), 1, sz, f) == (size_t)sz);
    fclose(f);

    assert(stat_data.size() >= 8);
    assert(stat_data[0] == 'T' && stat_data[1] == 'F' && stat_data[2] == '3' && stat_data[3] == 'S');
    uint32_t stat_count = *(uint32_t *)&stat_data[4];
    assert(stat_count >= 79);
    printf("PASS: stat.bin TF3S header found with %u stat rules (>= 79)\n", stat_count);

    // 3. Verify destroy.bin
    f = fopen("mods/TF3Cards/destroy.bin", "rb");
    assert(f != nullptr);
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> dest_data(sz);
    assert(fread(dest_data.data(), 1, sz, f) == (size_t)sz);
    fclose(f);

    assert(dest_data.size() >= 8);
    assert(dest_data[0] == 'T' && dest_data[1] == 'F' && dest_data[2] == '3' && dest_data[3] == 'D');
    uint32_t dest_count = *(uint32_t *)&dest_data[4];
    assert(dest_count >= 169);
    printf("PASS: destroy.bin TF3D header found with %u destroy rules (>= 169)\n", dest_count);

    // 4. Verify amount.bin
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
    assert(amt_count >= 170);
    printf("PASS: amount.bin TF3V header found with %u amount rules (>= 170)\n", amt_count);

    // 5. Verify effects.bin
    f = fopen("mods/TF3Cards/effects.bin", "rb");
    assert(f != nullptr);
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> eff_data(sz);
    assert(fread(eff_data.data(), 1, sz, f) == (size_t)sz);
    fclose(f);

    assert(eff_data.size() >= 8);
    assert(eff_data[0] == 'T' && eff_data[1] == 'F' && eff_data[2] == '3' && eff_data[3] == 'E');
    uint32_t eff_count = *(uint32_t *)&eff_data[4];
    assert(eff_count >= 99);
    printf("PASS: effects.bin TF3E header found with %u effect rules (>= 99)\n", eff_count);

    printf("ALL BATCH 15 C++ CHECKS PASSED!\n");
    return 0;
}
