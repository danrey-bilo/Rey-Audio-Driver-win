#pragma once
#include "../engine/usb_profile.hpp"
#include "../audio/mixer.hpp"
namespace rey::service {
struct Settings {
  engine::UsbProfile usb;
  unsigned usb_depth = 3;
  bool usb_auto = true;
  audio::Mix mix;
};
bool valid(const Settings &, std::string &error);
// Format 3 contains USB and mixer only. The reader accepts formats 1/2 solely
// for migration; their retired fields never enter Settings or start a transport.
bool load(const std::wstring &path, Settings &, std::string &error,
          const std::wstring &key = L"Software\\ReyAudio", unsigned *format = nullptr);
bool save(const std::wstring &path, const Settings &, std::string &error,
          const std::wstring &key = L"Software\\ReyAudio");
bool valid_device_id(const std::string &);
bool parse_unsigned(const std::string &, unsigned &value, unsigned maximum);
std::string json_string(const std::string &);
}
