// Unit tests for TF3 LP effect resolution (burn, heal, battle triggers, dynamic ATK damage).
#include "../../plugins/src/TF3Cards/tf3_cards.cpp"
#include <cassert>

static uint8_t bytes[0x2000000];
static uint32_t regs[32];
static uint8_t *memory(uint32_t p) { assert(p >= 0x08000000 && p < 0x0a000000); return bytes + p - 0x08000000; }
static uint32_t reg(TfCpu *, int i) { return regs[i]; }
static void set_reg(TfCpu *, int i, uint32_t v) { regs[i] = v; }
static int original(TfCpu *, uint32_t) { return 0; }

int main() {
    TfModApi api = {};
    api.mem = memory;
    api.reg = reg;
    api.set_reg = set_reg;
    api.call_original = original;
    A = &api;

    // Load amount map directly
    g_amt[{7583, 2}] = Amount{0, -1500};               // Cannon Soldier MK-2
    g_amt[{7574, 2}] = Amount{0, -800};                // Shadowpriestess of Ohm
    g_amt[{7141, 2}] = Amount{0, -500};                // Chain Detonation
    g_amt[{7153, 2}] = Amount{800, 800};               // Stray Asmodian
    g_amt[{6960, 2}] = Amount{0, -700};                // Load of Darkness
    g_amt[{7951, 2}] = Amount{0, -300};                // Iron Chain Repairman
    g_amt[{7412, 2}] = Amount{0, (int32_t)(INT32_MIN + 2)}; // Darkblaze Dragon (destroyed_atk)

    unsigned checks = 0;

    // Test static burn & heal for both controllers
    struct TestCase {
        uint16_t id;
        int32_t expected_self;
        int32_t expected_opp;
    } cases[] = {
        {7583, 0, -1500},
        {7574, 0, -800},
        {7141, 0, -500},
        {7153, 800, 800},
        {6960, 0, -700},
        {7951, 0, -300},
    };

    const uint32_t obj_addr = 0x09000000;
    const uint32_t out_addr = 0x09000100;
    regs[4] = obj_addr;
    regs[5] = out_addr;

    for (const auto &tc : cases) {
        for (uint8_t controller = 0; controller < 2; ++controller) {
            memset(bytes, 0, sizeof(bytes));
            uint8_t *obj = memory(obj_addr);
            *(uint16_t *)obj = tc.id;
            obj[2] = controller;
            obj[3] = 0;

            int res = hk_lp_amounts(nullptr);
            assert(res == 1);
            int32_t *d = (int32_t *)memory(out_addr);
            assert(d[controller] == tc.expected_self);
            assert(d[1 - controller] == tc.expected_opp);
            ++checks;
        }
    }

    // Test dynamic battle damage (Darkblaze Dragon, id 7412):
    // Battle records at 0x09f77dcc: 2 x 14 words
    // side at +0, gid at +4, ATK at +7
    for (uint8_t controller = 0; controller < 2; ++controller) {
        memset(bytes, 0, sizeof(bytes));
        uint8_t *obj = memory(obj_addr);
        *(uint16_t *)obj = 7412;
        obj[2] = controller;
        obj[3] = 0;

        // Opponent's battle record
        uint32_t *w = (uint32_t *)memory(0x09f77dcc);
        uint32_t opp_record_index = 14;
        w[opp_record_index + 0] = 1 - controller; // Opponent side
        w[opp_record_index + 4] = 4007;           // Sparkman
        w[opp_record_index + 7] = 1600;           // ATK = 1600

        int res = hk_lp_amounts(nullptr);
        assert(res == 1);
        int32_t *d = (int32_t *)memory(out_addr);
        assert(d[controller] == 0);
        assert(d[1 - controller] == -1600); // Inflicts opponent's ATK as damage!
        ++checks;
    }

    // A/B test: unknown card returns 0 (fallthrough to native game logic)
    {
        memset(bytes, 0, sizeof(bytes));
        uint8_t *obj = memory(obj_addr);
        *(uint16_t *)obj = 9999; // Not in amount map
        obj[2] = 0;
        obj[3] = 0;

        int res = hk_lp_amounts(nullptr);
        assert(res == 0);
        ++checks;
    }

    printf("TF3 LP effects resolution checks: %u passed\n", checks);
    return 0;
}
