#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "../firmware/factory_badge/main/morse_unlock.h"
struct Input {
    MorseUnlock code;
    uint32_t now = 0;
    unsigned unlocked = 0;
    void quiet(uint32_t duration) { now += duration; assert(!code.update(false,false,now)); }
    bool pulse(uint32_t hold, uint32_t gap=0) {
        assert(!code.update(true,false,now)); now += hold;
        bool result=code.update(false,false,now); unlocked+=result;
        quiet(gap); return result;
    }
    void word(uint32_t gap=200, uint32_t dash=600) {
        for(char symbol : "..-...-") if(symbol) pulse(symbol=='.'?200:dash,gap);
    }
    void expect(const char* value) { assert(!std::strcmp(code.input(),value)); }
};
int main() {
    // All patterns and all groupings: only symbol order matters, including
    // gaps too short, too long, and much longer than the old idle reset.
    for(unsigned pattern=0;pattern<128;++pattern)for(unsigned groups=0;groups<64;++groups){
        Input r;
        for(unsigned i=0;i<7;++i)r.pulse(pattern&(1u<<i)?600:200,groups&(1u<<i)?1800:0);
        assert(r.unlocked==unsigned(pattern==((1u<<2)|(1u<<6))));
    }
    for(uint32_t gap : {0u,1u,49u,399u,400u,600u,1400u,2499u,2500u,10000u,60000u}) {
        Input r;r.word(gap);assert(r.unlocked==1);
    }
    for(uint32_t start : {0u,UINT32_MAX-100}) {
        Input r;r.now=start;r.word(60000,20000);assert(r.unlocked==1);
    }
    // Pauses stay visible even though they no longer affect recognition.
    Input spacing;spacing.pulse(200,600);spacing.expect(". ");
    spacing.quiet(800);spacing.expect(".  ");
    spacing.quiet(60000);spacing.expect(".  ");assert(spacing.code.restartCount()==0);
    spacing.pulse(200,0);spacing.pulse(600,600);spacing.expect(".  .- ");
    spacing.pulse(200,0);spacing.pulse(200,1400);spacing.pulse(200,0);
    assert(spacing.pulse(600));assert(spacing.unlocked==1);
    // Hold length distinguishes dots/dashes, with no upper dash time limit.
    for(uint32_t hold : {0u,49u,399u,400u,1400u,1401u,20000u}) {
        Input r;
        for(char symbol : "..-...")if(symbol)r.pulse(symbol=='.'?200:600,0);
        assert(r.pulse(hold)==(hold>=MorseUnlock::DashThresholdMs));
    }
    // Wrong symbols still cannot unlock a suffix, and idle retry captures the
    // first contact at the reset boundary rather than swallowing it.
    Input wrong;wrong.pulse(600);wrong.word();assert(!wrong.unlocked);
    wrong.quiet(2500);wrong.expect("");assert(wrong.code.restartCount()==1);
    wrong.word();assert(wrong.unlocked==1);
    Input retry;retry.pulse(600);retry.now+=2500;retry.word();assert(retry.unlocked==1);
    Input bounded;for(int i=0;i<100;++i)bounded.pulse(200,0);
    assert(std::strlen(bounded.code.input())==MorseUnlock::InputCapacity);
    bounded.quiet(2500);bounded.word();assert(bounded.unlocked==1);
    // Cancellation and held/mixed-input ownership remain separate from timing.
    Input cancelled;cancelled.pulse(200);cancelled.pulse(200);
    cancelled.code.reset(true);assert(!cancelled.code.update(true,false,cancelled.now+600));
    cancelled.quiet(700);cancelled.expect("");cancelled.word();assert(cancelled.unlocked==1);
    Input disabled;disabled.pulse(200);assert(!disabled.code.update(true,false,disabled.now,false));
    assert(!disabled.code.update(true,false,disabled.now+1000));disabled.quiet(1200);
    disabled.word();assert(disabled.unlocked==1);
    Input both;assert(!both.code.update(true,true,0));both.quiet(200);both.word();assert(!both.unlocked);
    both.quiet(2500);both.word();assert(both.unlocked==1);
    std::puts("PASS: Morse symbol-only recognition, visible spacing, arbitrary pauses, long holds, retries, bounds, cancellation and clock wrap");
}
