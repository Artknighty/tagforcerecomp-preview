// Native dispatch aliases must not replace the Attribute query's real identity.
#include "../../plugins/src/TF3Cards/tf3_cards.cpp"
#include <cassert>
static uint32_t registers[32], returned;
static uint32_t reg(TfCpu *, int n) { return registers[n]; }
static void set_reg(TfCpu *, int n, uint32_t value) { registers[n] = value; }
static int original(TfCpu *, uint32_t function) {
    assert(function == 0x09f4abf0); registers[2] = returned; return 0;
}
int main() {
    TfModApi api = {}; api.reg = reg; api.set_reg = set_reg; api.call_original = original; A = &api;
    PORTED.push_back({7782, 7664, 6335, "Dharc the Dark Charmer"});
    for (uint32_t caller : {0x09f2d534u, 0x09f53168u, 0x09f2d5ccu, 0x09f531b8u, 0u}) {
        for (uint32_t id : {7782u, 6335u, 4007u}) {
            returned = id; registers[31] = caller; registers[2] = 123;
            const bool dispatch = caller == 0x09f2d534 || caller == 0x09f53168;
            assert(hk_charmer_source_id(nullptr) == (int)dispatch);
            assert(registers[2] == (dispatch ? (id == 7782 ? 6335u : id) : 123u));
        }
    }
    g_battle_light_bonus[7665] = 200;
    g_battle_light_bonus[7666] = 700;
    g_battle_role_bonus[7581] = 400;
    for (uint32_t caller : {0x09ea0930u, 0x09ea0bf4u, 0x09f2d5ccu, 0x09f531b8u, 0u}) {
        for (uint32_t id : {7665u, 7666u, 7581u, 6386u, 6125u, 7782u, 4007u}) {
            returned = id; registers[31] = caller; registers[2] = 123;
            const bool dispatch = caller == 0x09ea0930 || caller == 0x09ea0bf4;
            assert(hk_charmer_source_id(nullptr) == (int)dispatch);
            assert(registers[2] == (dispatch ? (g_battle_light_bonus.count(id) ? 6125u : g_battle_role_bonus.count(id) ? 6386u : id) : 123u));
        }
    }
    puts("Charmer dispatch aliases and real Attribute identity passed");
}
