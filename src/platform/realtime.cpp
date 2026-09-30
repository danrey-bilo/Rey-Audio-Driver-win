#include "realtime.hpp"
#include <vector>
#include <algorithm>
#include <cstdio>
namespace piaoip {
namespace {
void prefer_audio_cpu(unsigned role) {
  ULONG bytes=0; GetSystemCpuSetInformation(nullptr,0,&bytes,GetCurrentProcess(),0);
  if(!bytes) return;
  std::vector<uint8_t> buffer(bytes);
  if(!GetSystemCpuSetInformation(reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buffer.data()),bytes,&bytes,GetCurrentProcess(),0)) return;
  GROUP_AFFINITY allowed{}; if(!GetThreadGroupAffinity(GetCurrentThread(),&allowed)) return;
  struct Cpu { ULONG id; BYTE core,performance; };
  std::vector<Cpu> cores;
  for(size_t offset=0;offset+sizeof(SYSTEM_CPU_SET_INFORMATION)<=bytes;) {
    const auto* item=reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(buffer.data()+offset);
    if(!item->Size || item->Size>bytes-offset) break;
    const auto& cpu=item->CpuSet;
    if(item->Type==CpuSetInformation && cpu.Group==allowed.Group && cpu.LogicalProcessorIndex<sizeof(ULONG_PTR)*8 &&
      (allowed.Mask & (ULONG_PTR(1)<<cpu.LogicalProcessorIndex)) && (!cpu.Allocated || cpu.AllocatedToTargetProcess)) {
      bool duplicate=false; for(const auto& previous:cores) if(previous.core==cpu.CoreIndex) duplicate=true;
      if(!duplicate) cores.push_back({cpu.Id,cpu.CoreIndex,cpu.EfficiencyClass});
    }
    offset+=item->Size;
  }
  std::stable_sort(cores.begin(),cores.end(),[](const Cpu& a,const Cpu& b) { return a.performance>b.performance; });
  if(cores.size()<4 || cores[3].performance!=cores.front().performance) return;
  const ULONG chosen=cores[1+role%3].id; // separate physical cores; respect caller affinity/reservations
  SetThreadSelectedCpuSets(GetCurrentThread(),&chosen,1);
}
}
uint64_t now_ns() {
  static const uint64_t hz=[] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return uint64_t(f.QuadPart); }();
  LARGE_INTEGER t; QueryPerformanceCounter(&t); auto v=uint64_t(t.QuadPart);
  return v/hz*1000000000ull+v%hz*1000000000ull/hz;
}
uint64_t thread_cpu_ns() {
  FILETIME created{},exited{},kernel{},user{};
  if(!GetThreadTimes(GetCurrentThread(),&created,&exited,&kernel,&user)) return 0;
  const uint64_t ticks=((uint64_t(kernel.dwHighDateTime)<<32)|kernel.dwLowDateTime)+
    ((uint64_t(user.dwHighDateTime)<<32)|user.dwLowDateTime);
  return ticks*100;
}
void max_counter(std::atomic<uint64_t>& v,uint64_t x) {
  auto old=v.load(std::memory_order_relaxed);
  while(x>old && !v.compare_exchange_weak(old,x,std::memory_order_relaxed)) {}
}
RealtimeThread::RealtimeThread(std::atomic<uint64_t>& failures,unsigned role,int requested_cpu) {
  prefer_audio_cpu(role);
  DWORD index=0; mmcss_=AvSetMmThreadCharacteristicsW(L"Pro Audio",&index);
  if(!mmcss_ || !AvSetMmThreadPriority(mmcss_,AVRT_PRIORITY_CRITICAL)) ++failures;
  if(!mmcss_) SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_HIGHEST);
  // Diagnostic override is per process, reversible, and never changes the host's
  // process affinity or machine policy. Order is audio, receive, transmit.
  char cpus[64]{}; unsigned selected[3]{}; int end=0;
  const DWORD length=GetEnvironmentVariableA("PIAOIP_RT_CPUS",cpus,sizeof(cpus));
  if(length && length<sizeof(cpus) && std::sscanf(cpus,"%u,%u,%u%n",&selected[0],&selected[1],&selected[2],&end)==3 &&
      cpus[end]=='\0' && role<3 && selected[role]<sizeof(ULONG_PTR)*8) {
    requested_cpu=int(selected[role]);
  }
  if(requested_cpu>=0 && requested_cpu<int(sizeof(ULONG_PTR)*8)) {
    GROUP_AFFINITY affinity{};
    if(GetThreadGroupAffinity(GetCurrentThread(),&affinity) && (affinity.Mask&(ULONG_PTR(1)<<requested_cpu))) {
      affinity.Mask=ULONG_PTR(1)<<requested_cpu;
      if(!SetThreadGroupAffinity(GetCurrentThread(),&affinity,nullptr)) ++failures;
    } else ++failures;
  }
  char trace[4]{};
  if(GetEnvironmentVariableA("PIAOIP_TIMING_TRACE",trace,sizeof(trace))==1 && trace[0]=='1') {
    ULONG ids[64]{},count=0; GROUP_AFFINITY affinity{};
    GetThreadGroupAffinity(GetCurrentThread(),&affinity);
    const BOOL okay=GetThreadSelectedCpuSets(GetCurrentThread(),ids,64,&count);
    std::fprintf(stderr,"THREAD_PLACEMENT role=%u tid=%lu cpu=%lu group=%u mask=%llx selected_count=%lu selected_first=%lu selected_ok=%u\n",
      role,GetCurrentThreadId(),GetCurrentProcessorNumber(),affinity.Group,
      static_cast<unsigned long long>(affinity.Mask),count,count && okay ? ids[0] : 0,unsigned(okay));
  }
}
RealtimeThread::~RealtimeThread() { if(mmcss_) AvRevertMmThreadCharacteristics(mmcss_); }
DeadlineWaiter::DeadlineWaiter() {
  timer_=CreateWaitableTimerExW(nullptr,nullptr,0x00000002,TIMER_ALL_ACCESS);
  if(!timer_) timer_=CreateWaitableTimerW(nullptr,FALSE,nullptr);
}
DeadlineWaiter::~DeadlineWaiter() { if(timer_) CloseHandle(timer_); }
void DeadlineWaiter::wait(uint64_t deadline,HANDLE stop,HANDLE wake,uint64_t spin_ns) {
  // Even an 83 us block must sleep rather than consume a full CPU and exhaust
  // its MMCSS quota. Reserve only a bounded tail for active audio deadlines.
  auto now=now_ns();
  if(timer_ && deadline>now+spin_ns+10000) {
    LARGE_INTEGER due; due.QuadPart=-static_cast<LONGLONG>((deadline-now-spin_ns)/100);
    if(SetWaitableTimer(timer_,&due,0,nullptr,nullptr,FALSE)) {
      HANDLE handles[]={stop,timer_,wake};
      auto result=WaitForMultipleObjects(wake ? 3 : 2,handles,FALSE,100);
      if(result==WAIT_OBJECT_0 || result==WAIT_OBJECT_0+2) return;
    }
  }
  while(now_ns()<deadline) { if(WaitForSingleObject(stop,0)==WAIT_OBJECT_0) return; YieldProcessor(); }
}
}
