#pragma once
#include <string>
#include "ipc_protocol.hpp"
#include "../engine/audio_block.hpp"
#include "../../include/rey/bridge.h"
#include <atomic>
#include <memory>
#include <mutex>

namespace rey::asio {
class Server {
public:
  Server();
  ~Server();
  void configure(const REY_BRIDGE_PROFILE &, unsigned depth, uint64_t generation);
  void transport_buffers(unsigned capture, unsigned render, unsigned delay);
  void offline();
  bool info(Info &);
  std::string status() const;
  bool running() const;
  unsigned block_size() const;
  bool render_ready(uint64_t end_frame);
  bool render_span(uint64_t &first_frame, uint64_t &end_frame, unsigned &block_frames);
  bool connect(DWORD pid, uint64_t mapping, uint64_t event, unsigned block,
               unsigned lead_blocks, std::string &error);
  bool disconnect(DWORD pid);
  // Only the transport callback calls process. No waits, mutexes or allocations.
  void process(const rey::engine::AudioBlock &, uint64_t now_ns);
private:
  struct Connection;
  void close_locked();
  mutable std::mutex mutex_;
  Info info_;
  bool available_ = false;
  uint64_t connection_serial_ = 0;
  std::unique_ptr<Connection> owned_;
  std::atomic<Connection *> active_{nullptr};
  std::atomic<unsigned> readers_{0};
};
} // namespace rey::asio
