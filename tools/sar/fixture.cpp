#include "../../src/bridge/sar_bridge.hpp"
#include <avrt.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdio>
#include <cwchar>

int wmain(int argc,wchar_t **argv) {
  const unsigned seconds=argc>1?unsigned(_wtoi(argv[1])):30;
  const unsigned rate=argc>2?unsigned(_wtoi(argv[2])):96000;
  const unsigned frames=32;
  if(!seconds || seconds>60) return 1;
  REY_BRIDGE_PROFILE profile{sizeof(profile),REY_BRIDGE_VERSION,rate,32,8,8,frames,0,{}};
  std::memset(profile.device_id,'1',32);
  rey::bridge::SarBridge bridge;std::string error;
  if(!bridge.attach(profile,error)) {std::printf("SAR_FIXTURE_ATTACH_ERROR %s\n",error.c_str());return 2;}
  std::array<int32_t,frames*8> capture{},render{};
  HANDLE timer=CreateWaitableTimerExW(nullptr,nullptr,2,TIMER_ALL_ACCESS);
  if(!timer) return 3;
  DWORD task=0;HANDLE rt=AvSetMmThreadCharacteristicsW(L"Pro Audio",&task);
  if(rt) AvSetMmThreadPriority(rt,AVRT_PRIORITY_HIGH);
  LARGE_INTEGER frequency{},started{};QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&started);
  const uint64_t end=uint64_t(rate)*seconds;
  uint64_t steps=0;bool okay=true;
  std::printf("SAR_FIXTURE_READY endpoints=8 channels_each=2 rate=%u seconds=%u physical_audio=0\n",rate,seconds);std::fflush(stdout);
  for(uint64_t position=0;position<end;position+=frames) {
    rey::engine::AudioBlock block;
    block.frames=frames;block.inputs=block.outputs=8;block.bits=32;block.epoch=1;
    block.frame_position=position;block.capture=capture.data();block.render=render.data();
    block.capture_only=true;
    if(!bridge.process(block)) {okay=false;break;}
    block.capture_only=false;block.render_only=true;
    if(!bridge.process(block)) {okay=false;break;}
    capture=render;++steps;
    LARGE_INTEGER now{};QueryPerformanceCounter(&now);
    const auto deadline=started.QuadPart+int64_t((position+frames)*uint64_t(frequency.QuadPart)/rate);
    const auto remaining=deadline-now.QuadPart;
    if(remaining>0) {
      LARGE_INTEGER due{};due.QuadPart=-std::max<int64_t>(1,remaining*10000000/frequency.QuadPart);
      SetWaitableTimer(timer,&due,0,nullptr,nullptr,FALSE);
      WaitForSingleObject(timer,1000);
    }
  }
  REY_BRIDGE_STATS stats{};bridge.stats(stats);
  std::printf("SAR_FIXTURE_RESULT okay=%u error=%lu steps=%llu capture_frames=%llu render_frames=%llu capture_running=%u render_running=%u\n",
      unsigned(okay),bridge.last_error(),(unsigned long long)steps,(unsigned long long)stats.capture_frames,
      (unsigned long long)stats.render_frames,stats.capture_running,stats.render_running);std::fflush(stdout);
  bridge.detach();
  if(rt) AvRevertMmThreadCharacteristics(rt);
  CloseHandle(timer);
  return okay?0:4;
}
