// The native draw path gives exactly two cards to the selected coin recipient.
#include "../../plugins/src/TF3Cards/tf3_cards.cpp"
#include <cassert>
static uint8_t bytes[0x2000000];
static uint32_t regs[32], coin, drawn[2], animations;
static uint8_t *memory(uint32_t p) { assert(p >= 0x08000000 && p < 0x0a000000); return bytes + p - 0x08000000; }
static uint32_t reg(TfCpu *, int i) { return regs[i]; }
static void set_reg(TfCpu *, int i, uint32_t v) { regs[i] = v; }
static void call(TfCpu *, uint32_t fn) {
    if (fn == F_RANDOM) { assert(regs[4] == 2); regs[2] = coin; }
    else if (fn == F_COIN_ANIM) { ++animations; assert(regs[7] == coin); }
    else { assert(fn == F_DRAW_CARDS && regs[4] < 2); drawn[regs[4]] += regs[5]; }
}
int main() {
    _putenv_s("TF3_FORCE_COIN", "");
    TfModApi api = {}; api.mem = memory; api.reg = reg; api.set_reg = set_reg; api.call = call; A = &api;
    PORTED.push_back({7800, 7623, 4844, "Cup of Ace"}); g_amt[{7800, 1}] = {2, 0};
    *(uint16_t *)memory(0x08000100) = 7800;
    for (unsigned side = 0; side < 2; ++side) for (coin = 0; coin < 2; ++coin) {
        memory(0x08000102)[0] = side; regs[4] = 0x08000100;
        drawn[0] = drawn[1] = animations = 0;
        assert(hk_draw(nullptr) == 1 && animations == 1);
        assert(drawn[side] == (coin ? 2 : 0) && drawn[1 - side] == (coin ? 0 : 2));
        assert(*(uint16_t *)memory(0x08000100) == 7800);
    }
    *(uint16_t *)memory(0x08000100) = 4844;
    assert(!hk_draw(nullptr));
    puts("Cup of Ace recipient, amount and native isolation passed");
}
