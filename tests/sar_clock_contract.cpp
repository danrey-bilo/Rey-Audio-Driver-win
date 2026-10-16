#include "../include/rey/sar_clock.hpp"
#include <array>
#include <cstdio>
int main() {
  rey::sar::Clock clock;
  unsigned ticks=0,total=0;
  // Ten milliseconds of 44.1-kHz USB cadence: 441 frames, not 80 ticks.
  for(unsigned packet=0;packet<80;++packet) {
    const unsigned frames=((packet+1)*441/80)-(packet*441/80);
    ticks+=clock.consume(3,frames);total+=frames;
  }
  if(total!=441 || ticks!=13 || clock.remainder!=25)return 1;
  if(clock.consume(3,7)!=1 || clock.remainder!=0)return 2;
  clock.consume(3,31);
  if(clock.consume(5,1)!=0 || clock.remainder!=1)return 3;
  for(const unsigned rate:std::array<unsigned,6>{44100,48000,88200,96000,176400,192000}) {
    rey::sar::Clock hardware;unsigned observed=0;
    for(unsigned packet=0;packet<8000;++packet)
      observed+=hardware.consume(1,((packet+1)*rate/8000)-(packet*rate/8000));
    if(observed!=rate/32 || hardware.remainder!=rate%32)return 4;
  }
  rey::sar::Demand demand;
  if(demand.update(0,0) || !demand.update(2,0) || !demand.update(2,299999999) || demand.update(2,300000000))return 5;
  if(!demand.update(3,400000000) || !demand.update(3,900000000))return 6;
  if(!demand.update(2,1000000000) || demand.update(2,1300000000))return 7;
  if(!demand.update(6,1400000000) || demand.update(8,1400000001))return 8;
  std::puts("SAR clock/demand: six rates, fractions, epoch reset, initialization and bounded idle warmup passed");
  return 0;
}
