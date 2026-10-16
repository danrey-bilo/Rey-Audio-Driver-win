#pragma once
#include <cstdint>
namespace rey::sar {
// SAR exposes a hardware clock at sample_rate / frames_per_tick. AUSB packet
// sizes vary, so preserve partial ticks instead of rounding every packet.
struct Clock {
  static constexpr unsigned frames_per_tick=32;
  std::uint32_t epoch=0;
  unsigned remainder=0;
  unsigned consume(std::uint32_t current,unsigned frames) {
    if(current!=epoch){epoch=current;remainder=0;}
    const auto total=std::uint64_t(remainder)+frames;
    remainder=unsigned(total%frames_per_tick);
    return unsigned(total/frames_per_tick);
  }
};
// In the pinned SAR kernel, pin create and close each increment the generation
// number. Odd numbers therefore mean an open pin, even before KSSTATE_RUN.
// Warm USB during initialization without keeping stopped/cached pins hot forever.
struct Demand {
  std::uint32_t generation=0;
  std::uint64_t warm_until=0;
  bool update(std::uint32_t current,std::uint64_t now) {
    const bool open=((current>>1)&1)!=0;
    if(current!=generation) {
      generation=current;
      warm_until=open?now+300000000ull:0;
    }
    return (current&1)!=0 || (open && now<warm_until);
  }
};
}
