#pragma once
#include "../config/settings.hpp"
#include "../engine/audio_block.hpp"
#include "../../include/piaoip/bridge.h"
namespace piaoip::engine { class SessionEngine; }
namespace piaoip::bridge {
// One exclusive service handle owns one dynamic ACX child. All exchange storage
// is prepared before the engine starts; PCM never contains kernel pointers.
class DriverBridge {
public:
  DriverBridge() = default;
  ~DriverBridge();
  DriverBridge(const DriverBridge &) = delete;
  DriverBridge &operator=(const DriverBridge &) = delete;
  static bool available(); // read-only interface enumeration; never takes ownership
  bool attach(const engine::SessionEngine &, std::string &error);
  bool attach(const PIAOIP_BRIDGE_PROFILE &, std::string &error);
  bool process(const engine::AudioBlock &);
  void detach();
  DWORD last_error() const {
    return error_;
  }
  bool stats(PIAOIP_BRIDGE_STATS &);

private:
  HANDLE device_ = INVALID_HANDLE_VALUE;
  PIAOIP_BRIDGE_PROFILE profile_{};
  PIAOIP_BRIDGE_EXCHANGE exchange_{};
  DWORD error_ = 0;
};
} // namespace piaoip::bridge
