#include "settings.hpp"
#include <sstream>
namespace rey::service {
Settings::Settings() {
  usb.block = 64;
  usb.safety = 0;
  usb.inputs = usb.outputs = 8;
  lan.block = 256;
  lan.safety = 1536;
  lan.capture_frames = 32;
  lan.energy_saving = false;
}
bool parse_unsigned(const std::string &text, unsigned &value,
                    unsigned maximum) {
  if (text.empty() || text.size() > 10)
    return false;
  uint64_t number = 0;
  for (const auto c : text) {
    if (c < '0' || c > '9')
      return false;
    number = number * 10 + unsigned(c - '0');
    if (number > maximum)
      return false;
  }
  value = unsigned(number);
  return true;
}
bool valid(const Settings &s, std::string &error) {
  in_addr ip{};
  if (!aoip::valid_rate(s.usb.rate) || !aoip::valid_bits(s.usb.bits) ||
      !aoip::valid_buffer(unsigned(s.usb.block)) || s.usb.block > 256 ||
      s.usb.safety > 8192 || !s.usb_depth || s.usb_depth > 16 ||
      !aoip::valid_buffer(unsigned(s.lan.block)) || s.lan.block > 256 ||
      s.lan.safety > 8192 || s.lan.capture_frames > 256 || !s.lan.port ||
      (s.preferred != "auto" && s.preferred != "usb" && s.preferred != "lan") ||
      (s.lan.peer[0] && inet_pton(AF_INET, s.lan.peer, &ip) != 1) ||
      (s.lan_enabled && !s.lan.peer[0])) {
    error = "Invalid transport settings";
    return false;
  }
  return true;
}
std::string select_route(const Settings &s, bool present) {
  if (s.preferred == "usb")
    return s.usb_auto && present ? "usb" : "none";
  if (s.lan_enabled)
    return "lan";
  return s.preferred != "lan" && s.usb_auto && present ? "usb" : "none";
}
bool load(const std::wstring &path, Settings &out, std::string &error) {
  const auto attributes = GetFileAttributesW(path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES &&
      GetLastError() == ERROR_FILE_NOT_FOUND)
    return true;
  if (attributes == INVALID_FILE_ATTRIBUTES ||
      (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
    error = "Cannot read service configuration";
    return false;
  }
  Settings s;
  auto number = [&](const wchar_t *section, const wchar_t *key,
                    unsigned initial, unsigned &target, unsigned maximum) {
    wchar_t text[32]{}, fallback[32]{};
    std::swprintf(fallback, 32, L"%u", initial);
    GetPrivateProfileStringW(section, key, fallback, text, 32, path.c_str());
    std::string ascii;
    for (const auto c : std::wstring(text))
      ascii.push_back(c <= 127 ? char(c) : '?');
    return parse_unsigned(ascii, target, maximum);
  };
  unsigned bits = s.usb.bits, usb_block = unsigned(s.usb.block),
           lan_block = unsigned(s.lan.block), port = s.lan.port, automatic = 1,
           enabled = 0, energy = 0;
  if (!number(L"USB", L"Rate", s.usb.rate, s.usb.rate, 192000) ||
      !number(L"USB", L"Bits", bits, bits, 32) ||
      !number(L"USB", L"QueueDepth", s.usb_depth, s.usb_depth, 16) ||
      !number(L"USB", L"EndpointFrames", usb_block, usb_block, 256) ||
      !number(L"USB", L"GuardFrames", s.usb.safety, s.usb.safety, 8192) ||
      !number(L"USB", L"AutoConnect", 1, automatic, 1) ||
      !number(L"AoIP", L"Enabled", 0, enabled, 1) ||
      !number(L"AoIP", L"Port", port, port, 65535) ||
      !number(L"AoIP", L"BlockFrames", lan_block, lan_block, 256) ||
      !number(L"AoIP", L"GuardFrames", s.lan.safety, s.lan.safety, 8192) ||
      !number(L"AoIP", L"PacketFrames", s.lan.capture_frames,
              s.lan.capture_frames, 256) ||
      !number(L"AoIP", L"EnergySaving", 0, energy, 1)) {
    error = "Malformed service configuration";
    return false;
  }
  s.usb.bits = uint16_t(bits);
  s.usb.block = usb_block;
  s.lan.block = lan_block;
  s.lan.port = uint16_t(port);
  s.usb_auto = automatic != 0;
  s.lan_enabled = enabled != 0;
  s.lan.energy_saving = energy != 0;
  wchar_t text[64]{};
  GetPrivateProfileStringW(L"AoIP", L"Peer", L"", text, 64, path.c_str());
  if (!WideCharToMultiByte(CP_UTF8, 0, text, -1, s.lan.peer, sizeof(s.lan.peer),
                           nullptr, nullptr)) {
    error = "Invalid peer address";
    return false;
  }
  GetPrivateProfileStringW(L"Routing", L"Preferred", L"auto", text, 64,
                           path.c_str());
  s.preferred.clear();
  for (const auto c : std::wstring(text))
    s.preferred.push_back(c <= 127 ? char(c) : '?');
  if (!valid(s, error))
    return false;
  out = s;
  return true;
}
bool save(const std::wstring &path, const Settings &s, std::string &error) {
  if (!valid(s, error))
    return false;
  std::ostringstream text;
  text << "; Rey Audio Driver - independent manual transport "
          "settings\r\n[Routing]\r\nPreferred="
       << s.preferred << "\r\n\r\n[USB]\r\nRate=" << s.usb.rate
       << "\r\nBits=" << s.usb.bits << "\r\nQueueDepth=" << s.usb_depth
       << "\r\nEndpointFrames=" << s.usb.block
       << "\r\nGuardFrames=" << s.usb.safety << "\r\nAutoConnect=" << s.usb_auto
       << "\r\n\r\n[AoIP]\r\nPeer=" << s.lan.peer
       << "\r\nEnabled=" << s.lan_enabled << "\r\nPort=" << s.lan.port
       << "\r\nBlockFrames=" << s.lan.block
       << "\r\nGuardFrames=" << s.lan.safety
       << "\r\nPacketFrames=" << s.lan.capture_frames
       << "\r\nEnergySaving=" << s.lan.energy_saving << "\r\n";
  const auto content = text.str();
  const auto temporary = path + L".new";
  const auto attributes = GetFileAttributesW(temporary.c_str());
  if (attributes != INVALID_FILE_ATTRIBUTES &&
      (attributes &
       (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))) {
    error = "Configuration temporary path is unsafe";
    return false;
  }
  const auto file = CreateFileW(
      temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    error = "Cannot save configuration: " + std::to_string(GetLastError());
    return false;
  }
  DWORD written = 0;
  const bool ok = WriteFile(file, content.data(), DWORD(content.size()),
                            &written, nullptr) &&
                  written == content.size() && FlushFileBuffers(file);
  CloseHandle(file);
  if (!ok || !MoveFileExW(temporary.c_str(), path.c_str(),
                          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    error = "Configuration commit failed: " + std::to_string(GetLastError());
    return false;
  }
  return true;
}
std::string json_string(const std::string &s) {
  std::string out = "\"";
  for (const auto c : s) {
    if (c == '\\' || c == '"') {
      out += '\\';
      out += c;
    } else if (static_cast<unsigned char>(c) < 32 ||
               static_cast<unsigned char>(c) >= 127) {
      char escaped[7]{};
      std::snprintf(escaped, sizeof(escaped), "\\u%04x",
                    static_cast<unsigned char>(c));
      out += escaped;
    } else {
      out += c;
    }
  }
  return out + '"';
}
} // namespace rey::service
