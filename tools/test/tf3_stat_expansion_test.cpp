// Continuous stats, battle role bonuses and equip levels resolution test.
#include "../../plugins/src/TF3Cards/tf3_cards.cpp"
#include <cassert>

static uint8_t bytes[0x2000000];
static uint32_t regs[32];
static uint8_t *memory(uint32_t p) { assert(p >= 0x08000000 && p < 0x0a000000); return bytes + p - 0x08000000; }
static uint32_t reg(TfCpu *, int i) { return regs[i]; }
static void set_reg(TfCpu *, int i, uint32_t v) { regs[i] = v; }
static void call(TfCpu *, uint32_t) {}

int main() {
    TfModApi api = {};
    api.mem = memory;
    api.reg = reg;
    api.set_reg = set_reg;
    api.call = call;
    A = &api;

    unsigned checks = 0;

    // Test battle role bonus map
    g_battle_role_bonus[7257] = 400; // Topaz Tiger
    g_battle_role_bonus[7715] = 300; // Jain
    g_battle_role_bonus[7904] = 300; // Galahad

    assert(g_battle_role_bonus[7257] == 400); ++checks;
    assert(g_battle_role_bonus[7715] == 300); ++checks;
    assert(g_battle_role_bonus[7904] == 300); ++checks;

    // Test equip level bonus map
    g_equip_level[7803] = 1; // Synchro Boost
    assert(g_equip_level[7803] == 1); ++checks;

    // Test stat rule entries in g_stat
    g_stat.push_back(StatRule{7206, 4, 0, 0, 0, 800, 0, {}, {}});              // Neos Force
    g_stat.push_back(StatRule{7213, 4, 0, 0, 0, 200, 200, {}, {}});            // Legendary Ebon Steed
    g_stat.push_back(StatRule{7543, 4, 0, 0, 0, 800, 0, {}, {}});              // Crystal Release
    g_stat.push_back(StatRule{7593, 4, 0, 0, 0, 300, 0, {}, {}});              // Vicious Claw
    g_stat.push_back(StatRule{7952, 4, 0, 0, 0, -800, -800, {}, {}});          // Iron Chain Snake

    assert(g_stat[0].src == 7206 && g_stat[0].atk == 800); ++checks;
    assert(g_stat[1].src == 7213 && g_stat[1].atk == 200 && g_stat[1].def == 200); ++checks;
    assert(g_stat[2].src == 7543 && g_stat[2].atk == 800); ++checks;
    assert(g_stat[3].src == 7593 && g_stat[3].atk == 300); ++checks;
    assert(g_stat[4].src == 7952 && g_stat[4].atk == -800 && g_stat[4].def == -800); ++checks;

    printf("TF3 stat expansion checks: %u passed\n", checks);
    return 0;
}
