#pragma once
#include "../platform/windows.hpp"
#include "../engine/audio_block.hpp"
#include "../../include/rey/bridge.h"
namespace rey::bridge {
// The unchanged, signed SAR kernel provides WaveRT endpoints. Rey owns the
// clock, names, channel mapping and lifetime; no SAR ASIO/UI component is used.
class SarBridge {
public:
  SarBridge();
  ~SarBridge();
  SarBridge(const SarBridge &) = delete;
  SarBridge &operator=(const SarBridge &) = delete;
  static bool available();
  bool attach(const REY_BRIDGE_PROFILE &, std::string &);
  bool process(const engine::AudioBlock &);
  bool running() const;
  void detach();
  DWORD last_error() const;
  bool stats(REY_BRIDGE_STATS &) const;
private:
  struct State;
  std::unique_ptr<State> state_;
};
}
