// Golden Bamboo checks control/face-up state only at activation and preserves the native Deck requirement.
#include "../../plugins/src/TF3Cards/tf3_cards.cpp"
#include <cassert>
static uint8_t bytes[0x2000000];
static uint32_t regs[32], calls, drawn[2];
static uint8_t *memory(uint32_t p) { assert(p >= 0x08000000 && p < 0x0a000000); return bytes + p - 0x08000000; }
static uint32_t reg(TfCpu *, int i) { return regs[i]; }
static void set_reg(TfCpu *, int i, uint32_t v) { regs[i] = v; }
static void call(TfCpu *, uint32_t fn) {
    assert(fn == F_DRAW_CARDS && regs[4] < 2); drawn[regs[4]] += regs[5];
}
static int original(TfCpu *, uint32_t fn) {
    assert(fn == F_DRAW_CHECK && *(uint16_t *)memory(regs[4]) == 4844); ++calls;
    const uint8_t *o = memory(regs[4]);
    const uint32_t side = (o[2] & 1) ^ ((o[3] >> 6) & 1);
    regs[2] = *(uint32_t *)memory(DUEL_SIDES + side * DUEL_SIDE_SIZE + 16) >= 2;
    return 0;
}
int main() {
    TfModApi api = {}; api.mem = memory; api.reg = reg; api.set_reg = set_reg; api.call_original = original; api.call = call; A = &api;
    PORTED.push_back({8000, 7635, 4844, "Golden Bamboo Sword"});
    PORTED.push_back({8001, 7225, 4310, "Broken Bamboo Sword"});
    for (unsigned side = 0; side < 2; ++side) for (unsigned owner_flip = 0; owner_flip < 2; ++owner_flip)
    for (unsigned target_side = 0; target_side < 2; ++target_side) for (unsigned slot : {0u, 5u, 9u, 10u})
    for (unsigned enabled : {0u, 1u, 33u}) for (unsigned count : {0u, 1u, 2u, 3u}) {
        memset(bytes, 0, sizeof bytes); calls = 0; regs[4] = 0x08000100;
        *(uint16_t *)memory(regs[4]) = 8000; memory(regs[4] + 2)[0] = side ^ owner_flip;
        memory(regs[4] + 3)[0] = owner_flip << 6;
        const uint32_t entry = DUEL_SIDES + target_side * DUEL_SIDE_SIZE + 0x30 + slot * 20;
        *(uint16_t *)memory(entry) = 8001; *(uint32_t *)memory(entry + 8) = enabled;
        *(uint32_t *)memory(DUEL_SIDES + side * DUEL_SIDE_SIZE + 16) = count;
        const bool present = side == target_side && slot >= 5 && slot < 10 && (enabled & 1);
        assert(hk_golden_draw_check(nullptr) == 1 && regs[2] == (present && count >= 2));
        assert(calls == unsigned(present) && *(uint16_t *)memory(0x08000100) == 8000);
    }
    *(uint16_t *)memory(0x08000100) = 4844;
    assert(!hk_golden_draw_check(nullptr));
    // The activation requirement is deliberately absent from draw resolution.
    g_amt[{8000, 1}] = {2, 0};
    for (unsigned side = 0; side < 2; ++side) {
        memset(bytes, 0, sizeof bytes); drawn[0] = drawn[1] = 0;
        regs[4] = 0x08000100; *(uint16_t *)memory(regs[4]) = 8000;
        memory(regs[4] + 2)[0] = side;
        assert(hk_draw(nullptr) == 1 && drawn[side] == 2 && drawn[1 - side] == 0);
    }
    puts("Golden Bamboo activation scope, face-up state, Deck count and donor isolation passed");
}
