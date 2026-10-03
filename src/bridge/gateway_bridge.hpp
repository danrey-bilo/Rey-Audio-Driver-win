#pragma once
#include <windows.h>
#include <memory>
#include <string>
#include "../engine/audio_block.hpp"
#include "../../include/rey/bridge.h"

namespace rey::bridge {
// Local evaluation adapter for the original, Microsoft-signed TAG package.
// Its existing capture/render pair serves one Pi. Transport owns the clock.
class GatewayBridge {
public:
  GatewayBridge();
  ~GatewayBridge();
  GatewayBridge(const GatewayBridge &) = delete;
  GatewayBridge &operator=(const GatewayBridge &) = delete;
  static bool available();
  bool attach(const REY_BRIDGE_PROFILE &, std::string &error);
  bool process(const engine::AudioBlock &);
  void detach();
  DWORD last_error() const;
  bool stats(REY_BRIDGE_STATS &);
private:
  struct State;
  std::unique_ptr<State> state_;
};
} // namespace rey::bridge
