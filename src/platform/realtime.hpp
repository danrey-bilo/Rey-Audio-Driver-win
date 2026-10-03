#pragma once
#include <windows.h>
#include <avrt.h>
#include <atomic>
#include <cstdint>
namespace rey {
uint64_t now_ns();
uint64_t thread_cpu_ns();
void max_counter(std::atomic<uint64_t>& value, uint64_t candidate);
class RealtimeThread {
  HANDLE mmcss_=nullptr;
public:
  explicit RealtimeThread(std::atomic<uint64_t>& failures,unsigned role=0,int requested_cpu=-1);
  ~RealtimeThread();
};
}
