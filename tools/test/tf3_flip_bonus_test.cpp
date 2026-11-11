// Resolved-FLIP bonuses depend on the native flag and stay isolated to the source instance.
#include "../../plugins/src/TF3Cards/tf3_cards.cpp"
#include <cassert>
static uint8_t bytes[0x2000000];
static uint32_t regs[32];
static uint16_t expected_record;
static uint8_t *memory(uint32_t p) { assert(p >= 0x08000000 && p < 0x0a000000); return bytes + p - 0x08000000; }
static uint32_t reg(TfCpu *, int i) { return regs[i]; }
static void set_reg(TfCpu *, int i, uint32_t v) { regs[i] = v; }
static int original(TfCpu *, uint32_t fn) {
    if (fn == F_EFFECT_RECORD) {
        assert(*(uint16_t *)memory(regs[4]) == expected_record);
        regs[2] = 0x08000800; return 0;
    }
    assert(fn == F_STATS);
    *(int32_t *)memory(regs[6] + 16) = 4;
    *(int32_t *)memory(regs[6] + 20) = 1700;
    *(int32_t *)memory(regs[6] + 24) = 1400;
    return 0;
}
int main() {
    TfModApi api = {}; api.mem = memory; api.reg = reg; api.set_reg = set_reg; api.call_original = original; A = &api;
    StatRule rule = {}; rule.src = 8000; rule.per = 16; rule.atk = rule.def = 300; g_stat.push_back(rule);
    PORTED.push_back({8000, 7546, 5769, "Worm Dimikles"});
    g_trigger_record_donors[8000] = 5037;
    for (unsigned kind = 0; kind < 4; ++kind) {
        regs[4] = 0x08000100; *(uint16_t *)memory(regs[4]) = 8000;
        memory(regs[4] + 3)[0] = kind << 4; expected_record = kind == 1 ? 5037 : 5769;
        assert(hk_effect_record(nullptr) == 1);
        assert(*(uint16_t *)memory(0x08000100) == 8000);
    }
    for (uint32_t side = 0; side < 2; ++side) for (uint32_t slot = 0; slot < 5; ++slot)
    for (uint32_t enabled : {0u, 1u}) for (uint32_t flags : {0x80u, 0x90u, 0x100002u}) for (uint32_t id : {8000u, 8001u}) {
        memset(bytes, 0, sizeof bytes);
        const uint32_t entry = DUEL_SIDES + side * DUEL_SIDE_SIZE + 0x30 + slot * 20;
        *(uint32_t *)memory(entry) = id; *(uint32_t *)memory(entry + 8) = enabled; *(uint32_t *)memory(entry + 16) = flags;
        regs[4] = side; regs[5] = slot; regs[6] = 0x08000100;
        assert(hk_stats(nullptr) == 1);
        const bool boosted = id == 8000 && enabled && flags == 0x90;
        assert(*(int32_t *)memory(0x08000114) == (boosted ? 2000 : 1700));
        assert(*(int32_t *)memory(0x08000118) == (boosted ? 1700 : 1400));
    }
    puts("Resolved-FLIP flag, negation, source identity and both-controller scope passed");
}
