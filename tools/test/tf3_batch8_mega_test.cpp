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
    assert(dest_count >= 35);
    printf("PASS: destroy.bin TF3D header found with %u destroy rules\n", dest_count);

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
    assert(amt_count >= 80);
    printf("PASS: amount.bin TF3V header found with %u amount rules\n", amt_count);

    // 3. Verify stat.bin BGP1 trailer
    f = fopen("mods/TF3Cards/stat.bin", "rb");
    assert(f != nullptr);
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> stat_data(sz);
    assert(fread(stat_data.data(), 1, sz, f) == (size_t)sz);
    fclose(f);

    bool found_bgp1 = false;
    for (size_t i = 0; i + 8 <= stat_data.size(); ++i) {
        if (stat_data[i] == 'B' && stat_data[i+1] == 'G' && stat_data[i+2] == 'P' && stat_data[i+3] == '1') {
            found_bgp1 = true;
            uint32_t count = *(uint32_t *)&stat_data[i+4];
            assert(count >= 17); // 15 prior + 2 new
            printf("PASS: BGP1 trailer found with %u battle guard entries\n", count);
            break;
        }
    }
    assert(found_bgp1);

    printf("ALL MEGA BATCH C++ CHECKS PASSED!\n");
    return 0;
}
