#include "realtime.hpp"
#include <vector>
#include <algorithm>
#include <cstdio>
namespace rey {
namespace {
void prefer_audio_cpu(unsigned role,bool use_pool) {
  ULONG bytes=0; GetSystemCpuSetInformation(nullptr,0,&bytes,GetCurrentProcess(),0);
  if(!bytes) return;
  std::vector<uint8_t> buffer(bytes);
  if(!GetSystemCpuSetInformation(reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(buffer.data()),bytes,&bytes,GetCurrentProcess(),0)) return;
  GROUP_AFFINITY allowed{}; if(!GetThreadGroupAffinity(GetCurrentThread(),&allowed)) return;
  struct Cpu { BYTE processor,core,performance; ULONG id; };
  std::vector<Cpu> cores;
  for(size_t offset=0;offset+sizeof(SYSTEM_CPU_SET_INFORMATION)<=bytes;) {
    const auto* item=reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(buffer.data()+offset);
    if(!item->Size || item->Size>bytes-offset) break;
    const auto& cpu=item->CpuSet;
    if(item->Type==CpuSetInformation && cpu.Group==allowed.Group && cpu.LogicalProcessorIndex<sizeof(ULONG_PTR)*8 &&
      (allowed.Mask & (ULONG_PTR(1)<<cpu.LogicalProcessorIndex)) && (!cpu.Allocated || cpu.AllocatedToTargetProcess)) {
      bool duplicate=false; for(const auto& previous:cores) if(previous.core==cpu.CoreIndex) duplicate=true;
      if(!duplicate) cores.push_back({cpu.LogicalProcessorIndex,cpu.CoreIndex,cpu.EfficiencyClass,cpu.Id});
    }
    offset+=item->Size;
  }
  std::stable_sort(cores.begin(),cores.end(),[](const Cpu& a,const Cpu& b) { return a.performance>b.performance; });
  if(cores.size()<4 || cores[3].performance!=cores.front().performance) return;
  // Keep caller CPU restrictions. An unrestricted machine with enough fast
  // physical cores can give each private audio thread a separate two-core
  // pool, retaining migration without service/host competing on the same core.
  const auto performance_cores=std::count_if(cores.begin(),cores.end(),
      [&](const Cpu& cpu){return cpu.performance==cores.front().performance;});
  // Prefer separate physical cores in the latter half of the fastest group.
  // This avoids concentrating both audio threads next to the boot/IRQ core,
  // without pinning them or overriding the DAW's existing CPU restrictions.
  auto index=1+(performance_cores/2-1+role%3)%(performance_cores-1);
  if(use_pool && performance_cores>=6 && role<2) {
    ULONG selected[64]{},defaults[64]{},selected_count=0,default_count=0;
    if(GetThreadSelectedCpuSets(GetCurrentThread(),selected,64,&selected_count) &&
       GetProcessDefaultCpuSets(GetCurrentProcess(),defaults,64,&default_count) &&
       !selected_count && !default_count) {
      const auto first=performance_cores-4+2*role;
      const ULONG pool[]{cores[size_t(first)].id,cores[size_t(first+1)].id};
      if(SetThreadSelectedCpuSets(GetCurrentThread(),pool,2))index=first;
    }
  }
  PROCESSOR_NUMBER ideal{allowed.Group,cores[size_t(index)].processor,0};
  SetThreadIdealProcessorEx(GetCurrentThread(),&ideal,nullptr);
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
  const bool use_pool=requested_cpu<0 && !GetEnvironmentVariableA("REY_RT_CPUS",nullptr,0);
  prefer_audio_cpu(role,use_pool);
  DWORD index=0; mmcss_=AvSetMmThreadCharacteristicsW(L"Pro Audio",&index);
  if(!mmcss_ || !AvSetMmThreadPriority(mmcss_,AVRT_PRIORITY_CRITICAL)) ++failures;
  if(!mmcss_) SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_HIGHEST);
  // Diagnostic override is per process, reversible, and never changes the host's
  // process affinity or machine policy. Role 0 is service; role 1 is ASIO host.
  char cpus[64]{}; unsigned selected[3]{}; int end=0;
  const DWORD length=GetEnvironmentVariableA("REY_RT_CPUS",cpus,sizeof(cpus));
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
  if(GetEnvironmentVariableA("REY_TIMING_TRACE",trace,sizeof(trace))==1 && trace[0]=='1') {
    ULONG ids[64]{},count=0; GROUP_AFFINITY affinity{};
    GetThreadGroupAffinity(GetCurrentThread(),&affinity);
    const BOOL okay=GetThreadSelectedCpuSets(GetCurrentThread(),ids,64,&count);
    std::fprintf(stderr,"THREAD_PLACEMENT role=%u tid=%lu cpu=%lu group=%u mask=%llx selected_count=%lu selected_first=%lu selected_ok=%u\n",
      role,GetCurrentThreadId(),GetCurrentProcessorNumber(),affinity.Group,
      static_cast<unsigned long long>(affinity.Mask),count,count && okay ? ids[0] : 0,unsigned(okay));
  }
}
RealtimeThread::~RealtimeThread() { if(mmcss_) AvRevertMmThreadCharacteristics(mmcss_); }
}
