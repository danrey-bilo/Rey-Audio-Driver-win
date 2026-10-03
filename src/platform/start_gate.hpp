#pragma once
#include <windows.h>
#include <atomic>

namespace piaoip {
// One-shot session startup. Workers finish setup before the peer starts PCM;
// release publishes the acknowledged epoch. Stop also wakes unreleased workers.
class StartGate {
  HANDLE ready_ = nullptr, release_ = nullptr;
  unsigned participants_ = 0;
  std::atomic<unsigned> arrived_{0};
  std::atomic<bool> published_{false};

public:
  StartGate() = default;
  StartGate(const StartGate &) = delete;
  StartGate &operator=(const StartGate &) = delete;
  ~StartGate() {
    if (ready_)
      CloseHandle(ready_);
    if (release_)
      CloseHandle(release_);
  }
  bool prepare(unsigned participants) {
    if (!participants || ready_ || release_)
      return false;
    participants_ = participants;
    ready_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    release_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    return ready_ && release_;
  }
  void arrive() {
    if (arrived_.fetch_add(1, std::memory_order_acq_rel) + 1 == participants_)
      SetEvent(ready_);
  }
  bool wait_ready(HANDLE stop, DWORD timeout_ms) const {
    HANDLE events[] = {stop, ready_};
    return WaitForMultipleObjects(2, events, FALSE, timeout_ms) == WAIT_OBJECT_0 + 1;
  }
  bool wait_release(HANDLE stop) const {
    HANDLE events[] = {stop, release_};
    return WaitForMultipleObjects(2, events, FALSE, INFINITE) == WAIT_OBJECT_0 + 1 &&
           published_.load(std::memory_order_acquire);
  }
  void release() {
    published_.store(true, std::memory_order_release);
    SetEvent(release_);
  }
};
} // namespace piaoip
