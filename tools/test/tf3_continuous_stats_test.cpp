// Level sums and Spell/Trap-zone counts retain source-relative scope.
#include "../../plugins/src/TF3Cards/tf3_cards.cpp"
#include <cassert>
static uint8_t bytes[0x2000000];
static uint32_t regs[32], levels[2][5], queries;
static uint8_t *memory(uint32_t p) { assert(p >= 0x08000000 && p < 0x0a000000); return bytes + p - 0x08000000; }
static uint32_t reg(TfCpu *, int i) { return regs[i]; }
static void set_reg(TfCpu *, int i, uint32_t v) { regs[i] = v; }
static void call(TfCpu *, uint32_t fn) {
    assert(fn == F_LEVEL && regs[4] < 2 && regs[5] < 5); ++queries;
    regs[2] = levels[regs[4]][regs[5]];
}
static void put(unsigned side, unsigned slot, uint16_t id, bool down = false) {
    const uint32_t p = DUEL_SIDES + side * DUEL_SIDE_SIZE + 0x30 + slot * 20;
    *(uint16_t *)memory(p) = id;
    *(uint32_t *)memory(p + 8) = down ? 0 : 1;
    *(uint32_t *)memory(p + 16) = down ? (slot >= 5 ? 0x100000 : 0x100002) : 0x80;
}
int main() {
    TfModApi api = {}; api.mem = memory; api.reg = reg; api.set_reg = set_reg; api.call = call; A = &api;
    StatRule sum = {}; sum.cond_side = 1; sum.per = 9; sum.cond = {4001, 4002};
    for (unsigned side = 0; side < 2; ++side) {
        memset(bytes, 0, sizeof bytes); put(side, 0, 4001); put(side, 1, 4002); put(1-side, 0, 4002);
        levels[side][0] = 2; levels[side][1] = 8; levels[1-side][0] = 12; queries = 0;
        assert(stat_cond_count(nullptr, sum, side, -1) == 10 && queries == 2);
        levels[side][1] = 9; assert(stat_cond_count(nullptr, sum, side, -1) == 11);
        put(side, 1, 4002, true); assert(stat_cond_count(nullptr, sum, side, -1) == 2);
        StatRule spells = {}; spells.cond_side = 17; spells.per = 1; spells.cond = {5121};
        put(side, 5, 5121); put(side, 6, 5121); put(side, 10, 5121); put(1-side, 5, 5121);
        assert(stat_cond_count(nullptr, spells, side, -1) == 2);
        put(side, 6, 5121, true); assert(stat_cond_count(nullptr, spells, side, -1) == 1);
        *(uint32_t *)memory(DUEL_SIDES + side * DUEL_SIDE_SIZE + 0x30 + 5 * 20 + 16) = 0xa0;
        assert(stat_cond_count(nullptr, spells, side, -1) == 1);
    }
    puts("Continuous-stat scope, current levels, face-down and field-zone exclusions passed");
}
