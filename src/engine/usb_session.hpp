#pragma once
#include "../config/settings.hpp"
#include "audio_block.hpp"
#include "pi5ausb/windows.hpp"
namespace piaoip::engine {
// Laboratory USB backend: exact device identity, bounded overlapped queues,
// packed PCM adapter, and the same ACX bridge contract as the LAN engine.
// Device-clocked ADC/DAC and direct KMDF USB streaming remain qualification
// work.
class UsbSession {
public:
  bool open(const Config &, const std::string &expected_id, std::string &error);
  bool run(ProcessBlock, void *, HANDLE stop, unsigned seconds, unsigned depth,
           std::string &error);
  const std::string &identity() const { return id_; }

private:
  pi5ausb::WindowsDevice usb_;
  pi5ausb::Profile profile_;
  std::string id_;
};
} // namespace piaoip::engine
