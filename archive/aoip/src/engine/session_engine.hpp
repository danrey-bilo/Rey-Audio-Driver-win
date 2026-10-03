#pragma once
#include "peer_session.hpp"
#include "audio_block.hpp"
#include "../transport/iocp_receiver.hpp"
namespace rey::engine {
struct SessionStats {
  aoip::Diagnostics audio;
  transport::ReceiveMetrics receive;
  uint64_t audio_cpu_ns = 0, rx_cpu_ns = 0, tx_cpu_ns = 0, control_failures = 0;
  uint64_t budget_yields = 0, suppressed_frames = 0, max_tx_age_ns = 0;
};
// No ASIO SDK types. A session owns transport and frame scheduling; its client
// owns only PCM exchange. Configuration and memory allocation precede start.
class SessionEngine {
public:
  SessionEngine();
  ~SessionEngine();
  SessionEngine(const SessionEngine &) = delete;
  SessionEngine &operator=(const SessionEngine &) = delete;
  bool open(const Config &, std::string &error);
  bool start(ProcessBlock, void *context, std::string &error);
  void stop();
  bool running() const;
  bool wait_until_stopped(HANDLE external_stop, DWORD timeout_ms = INFINITE) const;
  const Config &config() const;
  const PeerSession &peer() const;
  SessionStats stats() const; // call after stop for final IO/CPU counters
private:
  struct State;
  std::unique_ptr<State> state_;
};
} // namespace rey::engine
