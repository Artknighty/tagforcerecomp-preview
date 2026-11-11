// Temporary native donor identity must not leak into field instances.
#include "../../plugins/src/TF3Cards/tf3_cards.cpp"
#include <cassert>
static uint8_t bytes[0x2000000];
static uint32_t regs[32], entry, expected_donor, calls;
static uint8_t *memory(uint32_t p) { assert(p >= 0x08000000 && p < 0x0a000000); return bytes+p-0x08000000; }
static uint32_t reg(TfCpu *, int n) { return regs[n]; }
static void set_reg(TfCpu *, int n, uint32_t value) { regs[n] = value; }
static int original(TfCpu *, uint32_t function) {
    assert(function == F_DOUBLE_TRIBUTE);
    assert((*(uint16_t *)memory(entry) & 0x1fff) == expected_donor);
    ++calls; regs[2] = 1; return 0;
}
int main() {
    TfModApi api = {}; api.mem = memory; api.reg = reg; api.set_reg = set_reg; api.call_original = original; A = &api;
    const uint16_t donors[] = {5409, 6040, 6121, 6259};
    for (uint16_t i=0; i<4; ++i) g_double_tribute[8000+i] = {donors[i], {4007}};
    for (uint32_t side=0; side<2; ++side) for (uint32_t slot=0; slot<5; ++slot)
    for (uint16_t i=0; i<4; ++i) for (uint16_t tags : {0u, 0x2000u, 0xc000u}) for (uint32_t target : {4007u, 4041u}) {
        entry = DUEL_SIDES + side*DUEL_SIDE_SIZE + 0x30 + slot*20;
        const uint16_t source = (8000+i) | tags;
        *(uint16_t *)memory(entry) = source;
        regs[4] = side; regs[5] = target; regs[6] = side; regs[7] = slot;
        expected_donor = donors[i]; calls = 0;
        assert(hk_double_tribute(nullptr) == 1);
        assert(regs[2] == (uint32_t)(target == 4007));
        assert(calls == (uint32_t)(target == 4007));
        assert(*(uint16_t *)memory(entry) == source);
    }
    regs[6] = 2; assert(hk_double_tribute(nullptr) == 0);
    regs[6] = 0; regs[7] = 5; assert(hk_double_tribute(nullptr) == 0);
    puts("Double-Tribute target gating and 240 field-identity checks passed");
}
