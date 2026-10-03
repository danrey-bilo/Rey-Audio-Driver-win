#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#include <atomic>
#include <memory>
#include "aoip/protocol.hpp"

namespace piaoip::transport {
struct ReceiveMetrics {
  uint64_t calls = 0, completions = 0, waits = 0, max_batch = 0, overflows = 0, errors = 0;
};

// The socket stays owned by the caller. One dispatcher produces immutable
// packets; one audio consumer releases them. An OVERLAPPED and its packet stay
// in the same pool slot through completion AND consumption. No Packet copies.
class IocpReceiver {
public:
  using Accept = bool (*)(void *, aoip::Packet &);
  IocpReceiver();
  ~IocpReceiver();
  IocpReceiver(const IocpReceiver &) = delete;
  IocpReceiver &operator=(const IocpReceiver &) = delete;
  bool attach(SOCKET socket);
  bool reset(); // only while dispatcher AND consumer are joined
  void run(const std::atomic<bool> &running, HANDLE receive_event, Accept accept, void *context);
  void wake(); // wake the dispatcher after running becomes false
  const aoip::Packet *acquire() const;
  void release();
  uint64_t size() const;
  ReceiveMetrics metrics() const;

private:
  struct State;
  std::unique_ptr<State> state_;
};
} // namespace piaoip::transport
