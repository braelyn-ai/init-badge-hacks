#include <cassert>
#include <cstdio>
#include <initializer_list>
#include "../firmware/devices_badge/button_gesture.h"
using Action=BadgeButtonAction;
struct Sample {uint32_t at;bool yellow,blue;Action expected;};
static unsigned cases=0;
void run(const char *name,std::initializer_list<Sample> samples) {
  BadgeButtonGesture buttons;
  for(const auto &s:samples) {
    Action actual=buttons.update(s.yellow,s.blue,s.at);
    if(actual!=s.expected) {std::printf("FAIL %s at %u\n",name,s.at);assert(false);}
  }
  ++cases;
}
int main() {
  constexpr auto N=Action::NONE,Y=Action::YELLOW,B=Action::BLUE,S=Action::SETTINGS;
  // Chords report SETTINGS only after both pushers are held for 500 ms.
  run("simultaneous hold",{{0,0,0,N},{1,1,1,N},{126,1,1,N},{500,1,1,N},{501,1,1,S},{600,1,1,N},{700,0,0,N}});
  run("yellow then blue hold",{{100,1,0,N},{200,1,1,N},{699,1,1,N},{700,1,1,S},{1000,1,1,N},{1001,0,0,N}});
  run("blue then yellow hold",{{100,0,1,N},{224,1,1,N},{724,1,1,S},{800,0,0,N}});
  run("chord at grace boundary",{{100,0,1,N},{225,1,1,N},{725,1,1,S},{900,0,0,N}});
  run("short chord cancels",{{0,1,1,N},{300,1,1,N},{310,0,1,N},{900,0,1,N},{950,0,0,N},{1000,0,1,N},{1010,0,0,B}});
  run("yellow quick tap",{{10,1,0,N},{40,0,0,Y},{200,0,0,N}});
  run("blue quick tap",{{10,0,1,N},{40,0,0,B},{200,0,0,N}});
  run("yellow hold",{{10,1,0,N},{134,1,0,N},{135,1,0,Y},{5000,1,0,N},{5010,0,0,N}});
  run("blue hold",{{10,0,1,N},{135,0,1,B},{5000,0,1,N},{5010,0,0,N}});
  run("late second suppressed",{{0,0,1,N},{125,0,1,B},{130,1,1,N},{700,1,1,N},{710,1,0,N},{720,1,1,N},{1300,1,1,N},{1310,0,0,N}});
  run("chord partial release and repress",{{0,1,1,N},{500,1,1,S},{540,0,1,N},{590,1,1,N},{1200,1,1,N},{1250,0,0,N},{1300,0,1,N},{1310,0,0,B}});
  run("separate quick gestures",{{0,1,0,N},{10,0,0,Y},{20,0,1,N},{30,0,0,B},{40,1,1,N},{540,1,1,S},{550,0,0,N}});
  run("rollover hold",{{UINT32_MAX-60,0,1,N},{40,0,1,N},{64,0,1,B},{100,0,0,N}});
  run("rollover chord",{{UINT32_MAX-60,1,0,N},{UINT32_MAX-50,1,1,N},{440,1,1,N},{450,1,1,S},{600,0,0,N}});
  run("no sampled overlap",{{0,1,0,N},{10,0,1,Y},{200,0,1,N},{300,0,0,N}});
  std::printf("PASS %u raw button gesture cases (held chords, cancelled chords, singles, holds, rearm, rollover)\n",cases);
}
