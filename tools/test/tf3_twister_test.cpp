// Twister uses a private record and the native Spell LP-cost formula.
#include "../../plugins/src/TF3Cards/tf3_cards.cpp"
#include <cassert>
static uint8_t bytes[0x2000000];
static uint32_t regs[32];
static uint8_t *memory(uint32_t p) { assert(p >= 0x08000000 && p < 0x0a000000); return bytes + p - 0x08000000; }
static uint32_t reg(TfCpu *, int i) { return regs[i]; }
static void set_reg(TfCpu *, int i, uint32_t v) { regs[i] = v; }
static uint32_t allocate(uint32_t n) { assert(n == 24); return 0x08000600; }
static int original(TfCpu *, uint32_t f) {
    if (f == F_EFFECT_RECORD) {
        assert(*(uint16_t *)memory(regs[4]) == 4909);
        regs[2] = 0x08000500;
    } else {
        assert(f == F_LP_COST && *(uint16_t *)memory(regs[4]) == 6348);
        regs[2] = 500;
    }
    return 0;
}
int main() {
    TfModApi api = {}; api.mem = memory; api.reg = reg; api.set_reg = set_reg;
    api.guest_alloc = allocate; api.call_original = original; A = &api;
    PORTED.push_back({7200, 6999, 4909, "Twister"});
    const uint32_t native[6] = {4909, 0x09ee3b5c, 0x09ebe3a8, 0, 0, 0x09f0fb30};
    memcpy(memory(0x08000500), native, sizeof native);
    *(uint16_t *)memory(0x08000100) = 7200;
    regs[4] = 0x08000100;
    assert(hk_effect_record(nullptr) == 1 && regs[2] == 0x08000600);
    assert(!memcmp(memory(0x08000500), native, sizeof native));
    assert(*(uint16_t *)memory(regs[4]) == 7200);
    const uint32_t *record = (uint32_t *)memory(regs[2]);
    assert(record[1] == native[1] && record[2] == 0x09ebc8ec && record[3] == 0 && record[4] == 0x09ec7bf8 && record[5] == native[5]);
    for (unsigned side = 0; side < 2; ++side) {
        memory(regs[4])[2] = (uint8_t)side;
        assert(hk_twister_cost(nullptr) == 1 && regs[2] == 500);
        assert(*(uint16_t *)memory(regs[4]) == 7200 && memory(regs[4])[2] == side);
    }
    *(uint16_t *)memory(regs[4]) = 4909;
    assert(!hk_twister_cost(nullptr) && !hk_effect_record(nullptr));
    puts("Twister record isolation and native cost alias passed");
}
