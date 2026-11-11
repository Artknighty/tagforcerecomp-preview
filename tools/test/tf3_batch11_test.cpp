#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

int main() {
    // 1. Verify destroy.bin
    FILE *f = fopen("mods/TF3Cards/destroy.bin", "rb");
    assert(f != nullptr);
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> dest_data(sz);
    assert(fread(dest_data.data(), 1, sz, f) == (size_t)sz);
    fclose(f);

    assert(dest_data.size() >= 8);
    assert(dest_data[0] == 'T' && dest_data[1] == 'F' && dest_data[2] == '3' && dest_data[3] == 'D');
    uint32_t dest_count = *(uint32_t *)&dest_data[4];
    assert(dest_count >= 120); // 89 prior + 40 new
    printf("PASS: destroy.bin TF3D header found with %u destroy rules (>= 120)\n", dest_count);

    // 2. Verify summon.bin
    f = fopen("mods/TF3Cards/summon.bin", "rb");
    assert(f != nullptr);
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> sum_data(sz);
    assert(fread(sum_data.data(), 1, sz, f) == (size_t)sz);
    fclose(f);

    assert(sum_data.size() >= 8);
    assert(sum_data[0] == 'T' && sum_data[1] == 'F' && sum_data[2] == '3' && sum_data[3] == 'S');
    uint32_t sum_count = *(uint32_t *)&sum_data[4];
    assert(sum_count >= 30); // 24 prior + 6 new
    printf("PASS: summon.bin TF3S header found with %u summon rules (>= 30)\n", sum_count);

    // 3. Verify amount.bin
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
    assert(amt_count >= 155); // 151 prior + 6 new
    printf("PASS: amount.bin TF3V header found with %u amount rules (>= 155)\n", amt_count);

    printf("ALL BATCH 11 C++ CHECKS PASSED!\n");
    return 0;
}
