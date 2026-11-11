// Attribute intersections and inherent-command masking share one mechanism.
#include "../../plugins/src/TF3Cards/tf3_cards.cpp"
#include <cassert>
static uint8_t ram[0x2000000];
static uint32_t registers[32];
static uint8_t *memory(uint32_t address) { assert(address>=0x08000000 && address<0x0a000000); return ram+address-0x08000000; }
static uint32_t reg(TfCpu *,int n) { return registers[n]; }
static void set_reg(TfCpu *,int n,uint32_t value) { registers[n]=value; }
static void call(TfCpu *,uint32_t address) {
    assert(address==0x09f678f8 && registers[4]>=4001 && registers[4]<=4006);
    registers[2]=registers[4]-4000;
}
static int original(TfCpu *,uint32_t address) { assert(address==0x09e8f790);registers[2]=7;return 0; }
int main() {
    TfModApi api={};api.mem=memory;api.reg=reg;api.set_reg=set_reg;api.call=call;api.call_original=original;A=&api;
    TfCpu *cpu=reinterpret_cast<TfCpu *>(1);
    unsigned checks=0;
    for (uint32_t allowed=1;allowed<=6;++allowed) {
        g_summon_rules.clear();g_summon_rules[7100+allowed]=allowed<<8;
        for (uint32_t source=0;source<2;++source) for (uint32_t side=0;side<2;++side)
        for (uint32_t slot=0;slot<5;++slot) for (uint32_t state=0;state<4;++state)
        for (uint32_t attribute=1;attribute<=6;++attribute) {
            for (uint32_t s=0;s<2;++s) memset(memory(DUEL_SIDES+s*DUEL_SIDE_SIZE+0x30),0,100);
            const uint32_t entry=DUEL_SIDES+source*DUEL_SIDE_SIZE+0x30+20*slot;
            *(uint16_t *)memory(entry)=state==3 ? 0 : 7100+allowed;
            *(uint16_t *)memory(entry+8)=state==0 ? 1 : 0;
            *(uint32_t *)memory(entry+16)=state==2 ? 0x100002 : 0x80;
            const bool blocked=state==0 && attribute!=allowed;
            assert(summon_blocked(side,4000+attribute,cpu)==blocked);
            assert(!summon_blocked(side));
            *(uint16_t *)memory(DUEL_SIDES+8044)=4000+attribute;
            registers[4]=side;registers[5]=0;registers[2]=123;
            assert(hk_special_summon_command(cpu)==int(blocked));
            assert(registers[2]==(blocked ? 3u : 123u));
            ++checks;
        }
    }
    printf("%u Attribute/side/slot/state cases preserve non-Special command bits\n",checks);
}
