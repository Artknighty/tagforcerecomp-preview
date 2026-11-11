// Named hand/Deck selection must preserve candidate counts, sides and cost scope.
#include "../../plugins/src/TF3Cards/tf3_cards.cpp"
#include <cassert>
static uint8_t bytes[0x2000000];
static uint32_t regs[32], original_calls;
static bool allowed;
static std::vector<std::pair<uint32_t,uint32_t>> picks;
static uint8_t *memory(uint32_t p) { assert(p >= 0x08000000 && p < 0x0a000000); return bytes+p-0x08000000; }
static uint32_t reg(TfCpu *, int n) { return regs[n]; }
static void set_reg(TfCpu *, int n, uint32_t v) { regs[n] = v; }
static void call(TfCpu *c, uint32_t fn) {
    if (fn == F_SUMMON_CHECK_HAND) { assert(regs[5] == 6768); regs[2] = allowed; }
    else if (fn == F_CAND_ADD) {
        picks.push_back({regs[5],regs[6]}); ++*(uint32_t *)memory(CAND_COUNTER);
    } else { assert(fn == F_CAND_COUNT); assert(hk_cand_count(c)); }
}
static int original(TfCpu *, uint32_t fn) { assert(fn == 0x09ee3038); ++original_calls; regs[2] = 17; return 0; }
int main() {
    TfModApi api = {}; api.mem=memory; api.reg=reg; api.set_reg=set_reg; api.call=call; api.call_original=original; A=&api;
    g_fx[7300]={5,0,0,0,{6768}};
    const uint32_t obj=0x08000100;
    *(uint16_t *)memory(obj)=7300;
    unsigned cases=0;
    for (uint32_t side=0;side<2;++side) for (uint32_t hands=0;hands<=3;++hands)
    for (uint32_t decks=0;decks<=3;++decks) for (bool legal : {false,true}) {
        allowed=legal;
        const uint32_t base=DUEL_SIDES+side*DUEL_SIDE_SIZE;
        *(uint32_t *)memory(base+12)=4; *(uint32_t *)memory(base+16)=4;
        for (uint32_t i=0;i<4;++i) {
            *(uint32_t *)memory(base+288+4*i)=(i<hands?6768:4007)|0x40800000;
            *(uint32_t *)memory(base+928+4*i)=(i<decks?6768:4007)|0x40800000;
        }
        picks.clear(); *(uint32_t *)memory(CAND_COUNTER)=23;
        regs[4]=side;regs[5]=7300;regs[6]=0;
        assert(hk_cand_count(nullptr));
        const uint32_t expected=legal?hands+decks:0;
        assert(regs[2]==expected && *(uint32_t *)memory(CAND_COUNTER)==23);
        assert(picks.size()==expected);
        for (size_t i=0;i<picks.size();++i) assert(picks[i]==std::make_pair(i<hands?11u:13u,(uint32_t)(i<hands?i:i-hands)));
        picks.clear();regs[4]=side;regs[5]=7300;
        assert(hk_cand_run(nullptr) && regs[2]==expected && !g_pick_extra);
        memory(obj)[2]=(uint8_t)(side|4);regs[4]=obj;original_calls=0;
        assert(hk_chrysalis_cost(nullptr) && regs[2]==(expected?17u:0u));
        assert(original_calls==(uint32_t)(expected!=0) && regs[4]==obj);
        ++cases;
    }
    *(uint16_t *)memory(obj)=6764;regs[4]=obj;assert(!hk_chrysalis_cost(nullptr));
    regs[4]=0;assert(!hk_chrysalis_cost(nullptr));
    puts("Chrysalis: 64 hand/Deck/count/cost cases and native donor scope passed");
    assert(cases==64);
}
