// Verify the temporary native-node adapter and unrelated modifier isolation.
#include "../../plugins/src/TF3Cards/tf3_cards.cpp"
#include <cassert>
static uint8_t ram[0x2000000];
static uint32_t registers[32], node = 0x09f7c460;
static uint16_t expected_id, expected_value;
static uint8_t *memory(uint32_t address) { assert(address >= 0x08000000 && address < 0x0a000000); return ram + address - 0x08000000; }
static uint32_t reg(TfCpu *, int n) { return registers[n]; }
static void set_reg(TfCpu *, int n, uint32_t value) { registers[n] = value; }
static void call(TfCpu *, uint32_t function) {
    assert(function == 0x09f4631c && (registers[5] == 6125 || registers[5] == 6386) && registers[6] == 4);
    registers[2] = *(uint16_t *)memory(node) == registers[5] ? node : 0;
}
static int original(TfCpu *, uint32_t function) {
    assert(function == F_STATS);
    assert(*(uint16_t *)memory(node) == expected_id);
    assert(*(uint16_t *)memory(node+4) == expected_value);
    return 0;
}
int main() {
    TfModApi api = {}; api.reg = reg; api.set_reg = set_reg; api.mem = memory;
    api.call = call; api.call_original = original; A = &api;
    g_battle_light_bonus[7665] = 200; g_battle_light_bonus[7666] = 700;
    for (uint16_t source : {uint16_t(7665),uint16_t(7666),uint16_t(6125),uint16_t(4007)})
    for (uint32_t flags : {0x80u,0xa0u})
    for (uint16_t modifier : {uint16_t(6125),uint16_t(5009)}) {
        const uint32_t entry = DUEL_SIDES + 0x30;
        *(uint16_t *)memory(entry) = source;
        *(uint32_t *)memory(entry+16) = flags;
        *(uint16_t *)memory(entry+10) = 1;
        *(uint16_t *)memory(node) = modifier;
        *(uint16_t *)memory(node+4) = 123;
        const bool adapted = modifier == 6125 && g_battle_light_bonus.count(source);
        expected_id = adapted ? 5009 : modifier;
        expected_value = adapted ? (g_battle_light_bonus[source]/10)*2+1 : 123;
        registers[4] = registers[5] = 0; registers[6] = 0x08810000;
        assert(hk_stats(nullptr) == 1);
        assert(*(uint16_t *)memory(node) == modifier && *(uint16_t *)memory(node+4) == 123);
    }
    g_battle_role_bonus[7581] = 400;
    for (uint32_t flags : {0x80u,0xa0u}) for (uint16_t sign : {uint16_t(0),uint16_t(1)}) {
        *(uint16_t *)memory(DUEL_SIDES+0x30) = 7581;
        *(uint32_t *)memory(DUEL_SIDES+0x40) = flags;
        *(uint16_t *)memory(node) = 6386; *(uint16_t *)memory(node+4) = sign;
        expected_id = sign ? 5009 : 6580; expected_value = sign ? 81 : 400;
        registers[4] = registers[5] = 0; registers[6] = 0x08810000;
        assert(hk_stats(nullptr) == 1);
        assert(*(uint16_t *)memory(node) == 6386 && *(uint16_t *)memory(node+4) == sign);
    }
    puts("LIGHT amount adaptation, restoration, negated instances and native modifier isolation passed");
}
