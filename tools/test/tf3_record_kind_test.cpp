// Cross-kind lookup changes only the requested table bits and restores the complete object.
#include "../../plugins/src/TF3Cards/tf3_cards.cpp"
#include <cassert>
static uint8_t bytes[0x2000000];
static uint32_t regs[32];
static uint8_t expected_flags;
static uint8_t *memory(uint32_t p) { assert(p >= 0x08000000 && p < 0x0a000000); return bytes + p - 0x08000000; }
static uint32_t reg(TfCpu *, int i) { return regs[i]; }
static void set_reg(TfCpu *, int i, uint32_t v) { regs[i] = v; }
static int original(TfCpu *, uint32_t fn) {
    assert(fn == F_EFFECT_RECORD && *(uint16_t *)memory(regs[4]) == 5001);
    assert(memory(regs[4]+3)[0] == expected_flags); regs[2] = 0x08000800; return 0;
}
int main() {
    TfModApi api = {}; api.mem = memory; api.reg = reg; api.set_reg = set_reg; api.call_original = original; A = &api;
    PORTED.push_back({8000, 7206, 5769, "Gravitic Orb"});
    g_trig[8000] = 5769; g_trigger_record_donors[8000] = 5001;
    for (unsigned configured = 0; configured < 2; ++configured) for (unsigned flags = 0; flags < 256; ++flags) {
        if (configured) g_trigger_record_kinds[8000] = 0; else g_trigger_record_kinds.clear();
        regs[4] = 0x08000100; *(uint16_t *)memory(regs[4]) = 8000; memory(regs[4]+3)[0] = flags;
        expected_flags = configured && ((flags >> 4) & 3) == 1 ? flags & ~0x30 : flags;
        trigger_record_lookup(nullptr, regs[4], 8000, 5001);
        assert(*(uint16_t *)memory(0x08000100) == 8000 && memory(0x08000103)[0] == flags);
    }
    g_trigger_record_kinds[8000] = 0;
    regs[4] = 0x08000100; memory(regs[4]+3)[0] = 0x91; expected_flags = 0x81;
    assert(hk_effect_record(nullptr) == 1 && *(uint16_t *)memory(0x08000100) == 8000 && memory(0x08000103)[0] == 0x91);
    puts("Explicit record-kind lookup, unrelated flag preservation and real-ID field lookup passed");
}
