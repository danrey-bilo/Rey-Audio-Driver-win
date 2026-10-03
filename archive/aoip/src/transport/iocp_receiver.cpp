#include "iocp_receiver.hpp"
#include "aoip/spsc_queue.hpp"
#include <array>
#include <cstddef>

namespace piaoip::transport {
struct IocpReceiver::State {
  static constexpr unsigned pending_limit = 8, ready_limit = 1024,
                            pool_size = ready_limit + pending_limit;
  struct Slot {
    OVERLAPPED operation{};
    aoip::Packet packet;
  };
  std::array<Slot, pool_size> slots;
  std::array<Slot *, pool_size> free{};
  aoip::SpscQueue<Slot *, ready_limit> ready;
  aoip::SpscQueue<Slot *, 2048> returned;
  SOCKET socket = INVALID_SOCKET;
  HANDLE port = nullptr;
  unsigned free_count = 0;
  std::atomic<unsigned> pending{0};
  std::atomic<uint64_t> calls{0}, completions{0}, waits{0}, max_batch{0}, overflows{0}, errors{0};
  ~State() {
    if (port)
      CloseHandle(port);
  }

  void reset() {
    ready.reset();
    returned.reset();
    free_count = 0;
    pending = 0;
    calls = 0;
    completions = 0;
    waits = 0;
    max_batch = 0;
    overflows = 0;
    errors = 0;
    for (auto &slot : slots)
      free[free_count++] = &slot;
  }
  void collect() {
    Slot *const *entry;
    while ((entry = returned.acquire_read())) {
      free[free_count++] = *entry;
      returned.release_read();
    }
  }
  bool post() {
    auto *slot = free[--free_count];
    slot->operation = {};
    WSABUF buffer{sizeof(slot->packet.data), reinterpret_cast<char *>(slot->packet.data)};
    DWORD flags = 0, received = 0;
    ++pending;
    ++calls;
    const int result = WSARecv(socket, &buffer, 1, &received, &flags, &slot->operation, nullptr);
    if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
      --pending;
      ++errors;
      free[free_count++] = slot;
      return false;
    }
    // Synchronous success still queues a completion: this socket does not opt
    // in to FILE_SKIP_COMPLETION_PORT_ON_SUCCESS.
    return true;
  }
};

IocpReceiver::IocpReceiver() : state_(std::make_unique<State>()) {}
IocpReceiver::~IocpReceiver() = default;
bool IocpReceiver::attach(SOCKET socket) {
  auto &s = *state_;
  if (s.port || socket == INVALID_SOCKET)
    return false;
  s.port = CreateIoCompletionPort(reinterpret_cast<HANDLE>(socket), nullptr, 1, 1);
  if (!s.port)
    return false;
  s.socket = socket;
  s.reset();
  return true;
}
bool IocpReceiver::reset() {
  if (state_->pending)
    return false;
  state_->reset();
  return true;
}
void IocpReceiver::wake() {
  if (state_->port)
    PostQueuedCompletionStatus(state_->port, 0, 2, nullptr);
}
void IocpReceiver::run(const std::atomic<bool> &running, HANDLE event, Accept accept,
                       void *context) {
  auto &s = *state_;
  bool stopping = false;
  std::array<OVERLAPPED_ENTRY, State::pending_limit> entries{};
  for (;;) {
    if (!running.load(std::memory_order_relaxed) && !stopping) {
      stopping = true;
      CancelIoEx(reinterpret_cast<HANDLE>(s.socket), nullptr);
    }
    s.collect();
    while (!stopping && s.free_count && s.pending < State::pending_limit) {
      if (!s.post()) {
        stopping = true;
        CancelIoEx(reinterpret_cast<HANDLE>(s.socket), nullptr);
        break;
      }
    }
    if (stopping && !s.pending)
      break;
    ULONG count = 0;
    ++s.waits;
    if (!GetQueuedCompletionStatusEx(s.port, entries.data(), ULONG(entries.size()), &count, 100,
                                     FALSE)) {
      if (GetLastError() != WAIT_TIMEOUT) {
        ++s.errors;
        stopping = true;
        CancelIoEx(reinterpret_cast<HANDLE>(s.socket), nullptr);
      }
      continue;
    }
    unsigned completed = 0;
    bool published = false;
    for (ULONG i = 0; i < count; ++i) {
      auto &entry = entries[i];
      if (!entry.lpOverlapped)
        continue; // stop / returned-pool wake
      auto *slot = reinterpret_cast<State::Slot *>(entry.lpOverlapped);
      --s.pending;
      ++s.completions;
      ++completed;
      DWORD bytes = 0, flags = 0;
      // GQCSEx reports NTSTATUS in Internal. WSAGetOverlappedResult gives the
      // documented Winsock status and catches truncated datagrams as errors.
      const bool success =
          WSAGetOverlappedResult(s.socket, &slot->operation, &bytes, FALSE, &flags) != FALSE;
      if (!success) {
        const int error = WSAGetLastError();
        if (!stopping && error != WSA_OPERATION_ABORTED && error != WSAECONNRESET)
          ++s.errors;
        s.free[s.free_count++] = slot;
        continue;
      }
      slot->packet.size = bytes;
      if (stopping || !running.load(std::memory_order_relaxed) || !accept(context, slot->packet)) {
        s.free[s.free_count++] = slot;
        continue;
      }
      auto *target = s.ready.reserve_write();
      if (!target) {
        ++s.overflows;
        s.free[s.free_count++] = slot;
      } else {
        *target = slot;
        s.ready.commit_write();
        published = true;
      }
    }
    if (completed > s.max_batch.load(std::memory_order_relaxed))
      s.max_batch = completed;
    if (published)
      SetEvent(event);
  }
  // Cancelled operations have all completed before returning. Pool destruction
  // and socket close are safe only after the caller joins this dispatcher.
}
const aoip::Packet *IocpReceiver::acquire() const {
  const auto *entry = state_->ready.acquire_read();
  return entry ? &(*entry)->packet : nullptr;
}
void IocpReceiver::release() {
  auto &s = *state_;
  const auto *entry = s.ready.acquire_read();
  if (!entry)
    return;
  // returned capacity exceeds the entire pool, so one correct consumer cannot
  // fill it. Release the ready slot only after publishing the returned owner.
  auto *target = s.returned.reserve_write();
  if (!target)
    return;
  *target = *entry;
  s.returned.commit_write();
  s.ready.release_read();
  // Normal reception keeps eight operations pending. Wake only near pool
  // starvation, avoiding one user/kernel transition for every audio frame.
  if (s.pending.load(std::memory_order_acquire) <= 2)
    wake();
}
uint64_t IocpReceiver::size() const {
  return state_->ready.size();
}
ReceiveMetrics IocpReceiver::metrics() const {
  auto &s = *state_;
  return {s.calls.load(),     s.completions.load(), s.waits.load(),
          s.max_batch.load(), s.overflows.load(),   s.errors.load()};
}
} // namespace piaoip::transport
