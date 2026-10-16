#pragma once
#include "audio_block.hpp"
#include "usb_profile.hpp"
#include <atomic>
#include <string>
#include <vector>
namespace rey::engine {
struct Uac2Device {
  std::string identity;
  std::wstring capture, render;
  std::wstring path() const { return L"uac2:" + std::wstring(identity.begin(), identity.end()); }
};
struct Uac2Stats {
  std::atomic<uint64_t> discontinuities{0}, underrun_frames{0}, overflow_frames{0};
  std::atomic<uint64_t> startup_silence_frames{0};
  std::atomic<uint64_t> realtime_failures{0};
  std::atomic<unsigned> capture_buffer{0}, render_buffer{0};
  void reset() { discontinuities = underrun_frames = overflow_frames = realtime_failures = startup_silence_frames = 0; capture_buffer = render_buffer = 0; }
};
// Device identity is verified against the physical USB container, not its editable name.
std::vector<Uac2Device> uac2_devices(std::string &error);
inline bool uac2_path(const std::wstring &path) { return path.compare(0, 5, L"uac2:") == 0; }
class Uac2Session {
public:
  bool open(const UsbProfile &, const std::wstring &path, const std::string &expected, std::string &error);
  bool run(ProcessBlock, void *, HANDLE stop, Uac2Stats &, std::string &error,
           void (*ready)(void *, unsigned, unsigned) = nullptr,
           const std::atomic<bool> *requested = nullptr);
  const std::string &identity() const { return device_.identity; }
private:
  Uac2Device device_;
  UsbProfile profile_;
};
}
