#pragma once
#include "../config/settings.hpp"
#include "../audio/mixer.hpp"
namespace rey::service {
struct Settings {
  piaoip::Config usb, lan;
  unsigned usb_depth = 3;
  bool usb_auto = true, lan_enabled = false;
  std::string preferred = "auto";
  rey::audio::Mix mix;
  Settings();
};
bool valid(const Settings &, std::string &error);
bool load(const std::wstring &path, Settings &, std::string &error,
          const std::wstring &key = L"Software\\ReyAudio");
bool save(const std::wstring &path, const Settings &, std::string &error,
          const std::wstring &key = L"Software\\ReyAudio");
bool parse_unsigned(const std::string &, unsigned &value, unsigned maximum);
std::string json_string(const std::string &);
// Applied independently to each physical board. The mixer selection does not
// participate in transport selection or a session's restart key.
std::string select_route(const Settings &, bool usb_present);
} // namespace rey::service
