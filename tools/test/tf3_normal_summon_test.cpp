// Keep Normal-Summon-only adapters separate from other summon action kinds.
#include "../../plugins/src/TF3Cards/tf3_cards.cpp"
#include <cassert>
static uint8_t bytes[0x2000000];
static uint32_t regs[32], calls;
static uint8_t *memory(uint32_t p) { assert(p >= 0x08000000 && p < 0x0a000000); return bytes + p - 0x08000000; }
static uint32_t reg(TfCpu *, int i) { return regs[i]; }
static void set_reg(TfCpu *, int i, uint32_t v) { regs[i] = v; }
static int original(TfCpu *, uint32_t fn) {
    assert(fn == F_POST_ACTION); ++calls;
    assert(((*(uint32_t *)memory(ACTION_STATE) >> 8) & 0x1fff) == 6151);
    return 0;
}
int main() {
    TfModApi api = {}; api.mem = memory; api.reg = reg; api.set_reg = set_reg;
    api.call_original = original; A = &api;
    PORTED.push_back({7155, 6938, 6502, "Flame Ogre"});
    PORTED.push_back({7891, 7809, 6151, "Genex Power Planner"});
    for (uint32_t side = 0; side < 2; ++side) for (uint32_t bypass = 0; bypass < 2; ++bypass) {
        regs[4] = side; regs[5] = 7155; regs[6] = bypass;
        assert(hk_special_summon_check(nullptr) == 1 && regs[2] == 0);
        assert(regs[4] == side && regs[5] == 7155 && regs[6] == bypass);
        regs[5] = 7891; assert(!hk_special_summon_check(nullptr));
        regs[5] = 4007; assert(!hk_special_summon_check(nullptr));
    }
    for (uint32_t id : {7155u, 7891u}) {
        g_trig[id] = 6151; g_trig_summon[id] = true;
        for (uint32_t side = 0; side < 2; ++side) {
            for (uint32_t kind = 0; kind < 4; ++kind) {
                const uint32_t action = (id << 8) | (kind << 6) | 32 | side;
                *(uint32_t *)memory(ACTION_STATE) = action; calls = 0;
                const int handled = post_action_inner(nullptr);
                assert(handled == (kind == 0));
                assert(calls == (kind == 0));
                assert(*(uint32_t *)memory(ACTION_STATE) == action);
                assert(g_trigger_scope.empty());
            }
        }
    }
    puts("Normal Summon trigger isolation passed");
}
