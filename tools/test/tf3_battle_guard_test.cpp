// Conditional battle protection preserves damage and unrelated field state.
#include "../../plugins/src/TF3Cards/tf3_cards.cpp"
#include <cassert>
static uint8_t bytes[0x2000000];
static uint32_t regs[32];
static unsigned source_index, calls;
static uint8_t *memory(uint32_t p) { assert(p >= 0x08000000 && p < 0x0a000000); return bytes + p - 0x08000000; }
static uint32_t reg(TfCpu *, int i) { return regs[i]; }
static void set_reg(TfCpu *, int i, uint32_t v) { regs[i] = v; }
static int original(TfCpu *, uint32_t fn) {
    assert(fn == 0x09eae5fc); ++calls;
    const uint32_t source=0x09f77dcc+0x38*source_index;
    assert(*(uint32_t *)memory(source+16)==5526);
    assert(*(uint32_t *)memory(source+28)==300);
    assert(*(uint32_t *)memory(0x09f77dcc+0x38*(1-source_index)+16)==4007);
    return 0;
}
int main() {
    TfModApi api = {}; api.mem=memory; api.reg=reg; api.set_reg=set_reg; api.call_original=original; A=&api;
    unsigned count=0;
    for (unsigned side=0; side<2; ++side)
    for (unsigned slot=0; slot<5; ++slot) for (unsigned participant=0; participant<2; ++participant)
    for (int delta=-1; delta<=1; ++delta) for (unsigned negated=0; negated<2; ++negated) {
        memset(bytes,0,sizeof bytes);
        g_battle_guard[7798] = BattleGuard{1,1900};
        const uint32_t field=DUEL_SIDES+side*DUEL_SIDE_SIZE+0x30+20*slot;
        const uint32_t other=DUEL_SIDES+(1-side)*DUEL_SIDE_SIZE+0x30;
        *(uint32_t *)memory(field)=7798; *(uint32_t *)memory(field+8)=1;
        *(uint32_t *)memory(field+16)=negated ? 0xa0 : 0x80;
        *(uint32_t *)memory(other)=4007; *(uint32_t *)memory(other+8)=1; *(uint32_t *)memory(other+16)=0x80;
        *(uint32_t *)memory(DUEL_SIDES+side*DUEL_SIDE_SIZE)=8000;
        const uint32_t index=participant;
        source_index=index; calls=0;
        const uint32_t battle=0x09f77dcc+0x38*index, opponent=0x09f77dcc+0x38*(1-index);
        *(uint32_t *)memory(battle)=side; *(uint32_t *)memory(battle+4)=slot;
        *(uint32_t *)memory(battle+16)=7798; *(uint32_t *)memory(battle+28)=300;
        *(uint32_t *)memory(opponent)=1-side; *(uint32_t *)memory(opponent+4)=0;
        *(uint32_t *)memory(opponent+16)=4007; *(int32_t *)memory(opponent+28)=1900+delta;
        *(uint32_t *)memory(battle+44)=1;
        const bool expected=!negated && delta>=0;
        assert(hk_battle_guard_calculate(nullptr)==expected);
        assert(calls==expected);
        assert(*(uint32_t *)memory(battle+16)==7798);
        assert(*(uint32_t *)memory(battle+44)==1);
        assert(*(uint32_t *)memory(field)==7798);
        assert(*(uint32_t *)memory(DUEL_SIDES+side*DUEL_SIDE_SIZE)==8000);
        ++count;
    }
    const uint32_t stale=DUEL_SIDES+DUEL_SIDE_SIZE+0x30+20*4;
    *(uint32_t *)memory(stale+16)=0x80;
    *(uint32_t *)memory(0x09f77da8)=1;
    assert(hk_battle_guard_calculate(nullptr)==0);
    *(uint32_t *)memory(0x09f77da8)=0;
    *(uint32_t *)memory(stale)=4009;
    assert(hk_battle_guard_calculate(nullptr)==0);
    printf("Battle guard threshold/state/slot checks: %u passed\n",count);
}
