#pragma once
#include "../config/settings.hpp"
#include "aoip/channels.hpp"
#include "aoip/transport_budget.hpp"
namespace piaoip::engine {
// Control-plane state is prepared before audio threads start. The session is
// single-owner; stop/retry uses the same token and cannot evict another owner.
class PeerSession {
public:
  bool prepare(Config &config, std::string &error);
  bool subscribe(const Config &config, std::string &error);
  bool keepalive(const Config &config);
  void unsubscribe(const Config &config);
  const char *identity() const {
    return identity_;
  }
  const char *backend() const {
    return backend_;
  }
  unsigned capture_frames() const {
    return capture_frames_;
  }
  uint16_t epoch() const {
    return epoch_;
  }
  const aoip::ChannelMap &inputs() const {
    return inputs_;
  }
  const aoip::ChannelMap &outputs() const {
    return outputs_;
  }
  const aoip::TransportBudget &budget() const {
    return budget_;
  }

private:
  char identity_[33]{}, backend_[32]{};
  aoip::ChannelMap inputs_, outputs_;
  aoip::TransportBudget budget_{};
  unsigned capture_frames_ = 0;
  uint32_t token_ = 0;
  uint16_t epoch_ = 0;
  bool subscribed_ = false;
};
} // namespace piaoip::engine
