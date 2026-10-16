#pragma once
#include "usb_profile.hpp"
#include "audio_block.hpp"
#include "pi5ausb/windows.hpp"
#include <atomic>
namespace rey::engine {
// Laboratory USB backend: exact device identity, bounded overlapped queues,
// packed PCM adapter, and the Windows endpoint bridge contract.
// Device-clocked ADC/DAC and direct KMDF USB streaming remain qualification
// work.
class UsbSession {
public:
  bool open(const UsbProfile &, const std::string &expected_id, std::string &error);
  bool open(const UsbProfile &, const std::string &expected_id, const std::wstring &path, std::string &error);
  bool run(ProcessBlock, void *, HANDLE stop, unsigned seconds, unsigned depth,
           std::string &error,const std::atomic<bool> *requested=nullptr,unsigned jack_block=128);
  bool jack_audio() const { return usb_.device().hello.backend == pi5ausb::Backend::jack_audio; }
  const std::string &identity() const { return id_; }

private:
  pi5ausb::WindowsDevice usb_;
  pi5ausb::Profile profile_;
  std::string id_;
};
} // namespace rey::engine
