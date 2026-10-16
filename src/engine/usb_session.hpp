#pragma once
#include "usb_profile.hpp"
#include "audio_block.hpp"
#include "pi5ausb/windows.hpp"
#include <atomic>
namespace rey::engine {
// USB PCM backend: exact device identity and bounded overlapped queues.
// Backend 3 follows JACK's physical sample clock through packet frame counts.
// Converter latency is qualified separately from this digital transport.
class UsbSession {
public:
  bool open(const UsbProfile &, const std::string &expected_id, std::string &error);
  bool open(const UsbProfile &, const std::string &expected_id, const std::wstring &path, std::string &error);
  bool run(ProcessBlock, void *, HANDLE stop, unsigned seconds, unsigned depth,
           std::string &error,const std::atomic<bool> *requested=nullptr,unsigned jack_block=128);
  bool jack_audio() const { return usb_.device().hello.backend != pi5ausb::Backend::digital_loopback; }
  const std::string &identity() const { return id_; }

private:
  pi5ausb::WindowsDevice usb_;
  pi5ausb::Profile profile_;
  std::string id_;
};
} // namespace rey::engine
