#pragma once
#include "../platform/windows.hpp"
#include "../engine/audio_block.hpp"
#include "../../include/rey/bridge.h"
namespace rey::bridge {
#ifdef REY_ENABLE_TAG_BRIDGE
class GatewayBridge;
#endif
// One device session owns its endpoint backend. All exchange storage is prepared
// before the engine starts; the optional TAG adapter owns the existing local pair.
class DriverBridge {
public:
  DriverBridge();
  ~DriverBridge();
  DriverBridge(const DriverBridge &) = delete;
  DriverBridge &operator=(const DriverBridge &) = delete;
  static bool available(); // read-only interface enumeration; never takes ownership
  bool attach(const REY_BRIDGE_PROFILE &, std::string &error);
  bool process(const engine::AudioBlock &);
  void detach();
  DWORD last_error() const {
#ifdef REY_ENABLE_TAG_BRIDGE
    if (gateway_) return gateway_error();
#endif
    return error_;
  }
  bool stats(REY_BRIDGE_STATS &);

private:
#ifdef REY_ENABLE_TAG_BRIDGE
  std::unique_ptr<GatewayBridge> gateway_;
  DWORD gateway_error() const;
#endif
  HANDLE device_ = INVALID_HANDLE_VALUE;
  REY_BRIDGE_PROFILE profile_{};
  REY_BRIDGE_EXCHANGE exchange_{};
  DWORD error_ = 0;
};
} // namespace rey::bridge
