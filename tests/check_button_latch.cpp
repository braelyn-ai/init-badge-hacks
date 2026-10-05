#include <cassert>
#include <cstdio>
#include <initializer_list>
#include "../firmware/factory_badge/main/board_buttons.h"
#include "../firmware/devices_badge/button_gesture.h"
using board::ButtonLatch;
using Action=BadgeButtonAction;

// Simulates the 5 ms sampling timer and a main loop that reads every loopMs.
// press(t) gives the raw yellow/blue levels at time t.
template<typename Press>
static int actions(uint32_t loopMs,uint32_t untilMs,Press press,Action *last) {
  ButtonLatch latch; BadgeButtonGesture gesture; int count=0;
  for(uint32_t t=0;t<=untilMs;t+=5) {
    bool y,b; press(t,y,b); latch.sample(y,b,t);
    if(t%loopMs==0) {
      const uint8_t held=latch.take();
      Action a=gesture.update(held&ButtonLatch::Yellow,held&ButtonLatch::Blue,t);
      if(a!=Action::NONE) {++count;*last=a;}
    }
  }
  return count;
}

int main() {
  Action last=Action::NONE;
  // Measured October 5, 2026: ~172 ms per loop on init(), ~6 ms elsewhere.
  for(uint32_t loop:{5u,170u,175u,345u}) {
    for(uint32_t start:{1u,40u,99u,168u}) {
      // A 40 ms tap that starts and ends between two slow reads still pages.
      last=Action::NONE;
      assert(actions(loop,1500,[&](uint32_t t,bool &y,bool &b){y=t>=start&&t<start+40;b=false;},&last)==1);
      assert(last==Action::YELLOW);
      last=Action::NONE;
      assert(actions(loop,1500,[&](uint32_t t,bool &y,bool &b){y=false;b=t>=start&&t<start+40;},&last)==1);
      assert(last==Action::BLUE);
      // Bounce shorter than the 10 ms debounce is never latched.
      assert(actions(loop,1500,[&](uint32_t t,bool &y,bool &b){y=t>=start&&t<start+5;b=false;},&last)==0);
      // A brief two-pusher tap is a cancelled chord: no page change, no Settings.
      assert(actions(loop,1500,[&](uint32_t t,bool &y,bool &b){y=b=t>=start&&t<start+60;},&last)==0);
    }
    // Holding both for a second still opens Settings exactly once.
    last=Action::NONE;
    assert(actions(loop,2500,[&](uint32_t t,bool &y,bool &b){y=b=t>=100&&t<1100;},&last)==1);
    assert(last==Action::SETTINGS);
    // A held single pusher pages once, not repeatedly.
    last=Action::NONE;
    assert(actions(loop,3000,[&](uint32_t t,bool &y,bool &b){y=false;b=t>=100&&t<2100;},&last)==1);
    assert(last==Action::BLUE);
  }
  // A latched press is reported once, then released.
  ButtonLatch latch;
  latch.sample(true,false,0); latch.sample(true,false,10); latch.sample(false,false,20); latch.sample(false,false,30);
  assert(latch.take()==ButtonLatch::Yellow);
  assert(latch.take()==0);
  std::puts("Button latch: short taps survive slow UI loops, bounce rejected, chord hold/cancel and single holds unchanged");
}
