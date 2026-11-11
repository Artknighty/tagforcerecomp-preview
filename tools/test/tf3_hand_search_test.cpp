// Keep fixed-name searches scoped to one imported source and native call site.
#include "../../plugins/src/TF3Cards/tf3_cards.cpp"
#include <cassert>

static uint8_t bytes[0x2000000];
static uint32_t registers_[32], queried_side, queried_id;
static int32_t target_index;
static bool nested;
static uint8_t *memory(uint32_t p) { assert(p >= 0x08000000 && p < 0x0a000000); return bytes + p - 0x08000000; }
static uint32_t reg(TfCpu *, int i) { return registers_[i]; }
static void set_reg(TfCpu *, int i, uint32_t v) { registers_[i] = v; }
static void call(TfCpu *, uint32_t fn) {
    assert(fn == F_DECK_FIND_ID);
    queried_side = registers_[4]; queried_id = registers_[5]; registers_[2] = target_index;
}
static int original(TfCpu *c, uint32_t fn) {
    if (fn == F_HAND_SEARCH_COST) registers_[2] = 1;
    else if (fn == F_DECK_FIND_ID) queried_id = registers_[5];
    else {
        assert(fn == F_HAND_SEARCH);
        if (nested) { assert(!g_hand_search_target); return 0; }
        assert(g_hand_search_target == 5533);
        registers_[5] = 4837; registers_[31] = 0x09eeca88;
        assert(hk_hand_search_find(c) == 1 && queried_id == 5533);
        registers_[5] = 4007; assert(!hk_hand_search_find(c));
        registers_[5] = 4837; registers_[31] = 0x09f14d50;
        assert(!hk_hand_search_find(c));
        // A nested original donor must not inherit the imported target.
        nested = true; registers_[4] = 0x08000200;
        assert(hk_hand_search(c) == 1);
        nested = false; assert(g_hand_search_target == 5533);
    }
    return 0;
}
int main() {
    TfModApi api = {}; api.mem = memory; api.reg = reg; api.set_reg = set_reg;
    api.call = call; api.call_original = original; A = &api;
    PORTED.push_back({7282, 7106, 6044, "search"});
    g_fx[7282] = {1, 0, 0, 0, {5533}};
    *(uint16_t *)memory(0x08000100) = 7282;
    *(uint16_t *)memory(0x08000200) = 6044;
    for (uint32_t side = 0; side < 2; ++side) {
        memory(0x08000100)[2] = (uint8_t)side;
        registers_[4] = 0x08000100; target_index = -1;
        assert(hk_hand_search_cost(nullptr) == 1 && registers_[2] == 0);
        assert(queried_side == side && queried_id == 5533);
        assert(registers_[4] == 0x08000100);
        target_index = 0;
        assert(hk_hand_search_cost(nullptr) == 1 && registers_[2] == 1);
        assert(hk_hand_search(nullptr) == 1 && !g_hand_search_target);
    }
    registers_[4] = 0x08000200;
    assert(!hk_hand_search_cost(nullptr) && !hk_hand_search(nullptr));
    registers_[5] = 4837; registers_[31] = 0x09eeca88;
    assert(!hk_hand_search_find(nullptr));
    g_fx[7282].ids.push_back(4837);
    assert(!hand_search_target(0x08000100));
    assert(!hand_search_target(0));
    puts("TF3 hand-search scope and cost tests passed");
}
