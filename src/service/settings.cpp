#include "settings.hpp"
#include <sstream>
#include <vector>
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
      (s.lan_enabled && !s.lan.peer[0]) || !rey::audio::valid(s.mix)) {
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
bool load(const std::wstring &path, Settings &out, std::string &error, const std::wstring &key) {
  // One registry value commits both profiles atomically. The finite test
  // harness uses the same binary representation in an isolated .dat file.
  std::vector<BYTE> bytes(512);
  DWORD size = DWORD(bytes.size());
  if (path.empty()) {
    const auto status = RegGetValueW(HKEY_LOCAL_MACHINE, key.c_str(),
        L"Profiles", RRF_RT_REG_BINARY, nullptr,
        bytes.data(), &size);
    if (status == ERROR_FILE_NOT_FOUND) return true;
    if (status != ERROR_SUCCESS) {
      error = "Cannot read saved profiles: " + std::to_string(status);
      return false;
    }
  } else {
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND)
      return true;
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))) {
      error = "Cannot read test settings"; return false;
    }
    const auto file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) { error = "Cannot open test settings"; return false; }
    const auto length = GetFileSize(file, nullptr);
    const bool ok = length <= bytes.size() && ReadFile(file, bytes.data(), size, &size, nullptr);
    CloseHandle(file);
    if (!ok || size != length) { error = "Invalid saved profile length"; return false; }
  }
  bytes.resize(size);
  size_t cursor = 0;
  auto number = [&](unsigned &value, unsigned maximum) {
    if (cursor + 4 > bytes.size()) return false;
    uint32_t n = 0;
    for (unsigned i = 0; i < 4; ++i) n |= uint32_t(bytes[cursor++]) << (8 * i);
    value = n; return n <= maximum;
  };
  Settings s;
  unsigned magic = 0, version = 0, bits = s.usb.bits, usb_block = unsigned(s.usb.block),
           lan_block = unsigned(s.lan.block), port = s.lan.port, automatic = 1,
           enabled = 0, energy = 0, route = 0, peer_length = 0;
  if (!number(magic, UINT32_MAX) || magic != 0x31535952 ||
      !number(version, 2) || !version ||
      !number(s.usb.rate, 192000) || !number(bits, 32) ||
      !number(s.usb_depth, 16) || !number(usb_block, 256) ||
      !number(s.usb.safety, 8192) || !number(automatic, 1) ||
      !number(enabled, 1) || !number(port, 65535) ||
      !number(lan_block, 256) || !number(s.lan.safety, 8192) ||
      !number(s.lan.capture_frames, 256) || !number(energy, 1) ||
      !number(route, 2) || !number(peer_length, 15) ||
      cursor + peer_length > bytes.size()) {
    error = "Malformed saved profiles";
    return false;
  }
  s.usb.bits = uint16_t(bits);
  s.usb.block = usb_block;
  s.lan.block = lan_block;
  s.lan.port = uint16_t(port);
  s.usb_auto = automatic != 0;
  s.lan_enabled = enabled != 0;
  s.lan.energy_saving = energy != 0;
  for (unsigned i = 0; i < peer_length; ++i) {
    if (bytes[cursor] == 0 || bytes[cursor] >= 128) { error = "Invalid saved peer"; return false; }
    s.lan.peer[i] = char(bytes[cursor++]);
  }
  s.lan.peer[peer_length] = 0;
  s.preferred = route == 0 ? "auto" : route == 1 ? "usb" : "lan";
  if (version == 2) {
    for (auto *group : {&s.mix.inputs, &s.mix.outputs}) for (auto &c : *group) {
      unsigned gain = 0, flags = 0;
      if (!number(gain, 7200) || !number(flags, 7)) { error = "Malformed mixer settings"; return false; }
      c.gain_cdb = int(gain) - 6000; c.mute = (flags & 1) != 0;
      c.solo = (flags & 2) != 0; c.invert = (flags & 4) != 0;
    }
    unsigned gain = 0, mute = 0;
    if (!number(gain, 6600) || !number(mute, 1)) { error = "Malformed master settings"; return false; }
    s.mix.master_cdb = int(gain) - 6000; s.mix.master_mute = mute != 0;
  }
  if (cursor != bytes.size()) { error = "Unexpected saved profile data"; return false; }
  if (!valid(s, error))
    return false;
  out = s;
  return true;
}
bool save(const std::wstring &path, const Settings &s, std::string &error, const std::wstring &registry_key) {
  if (!valid(s, error))
    return false;
  std::vector<BYTE> content;
  auto number = [&](uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) content.push_back(BYTE(value >> (8 * i)));
  };
  for (const auto value : {uint32_t(0x31535952), uint32_t(2), s.usb.rate,
      uint32_t(s.usb.bits), s.usb_depth, uint32_t(s.usb.block), s.usb.safety,
      uint32_t(s.usb_auto), uint32_t(s.lan_enabled), uint32_t(s.lan.port),
      uint32_t(s.lan.block), s.lan.safety, s.lan.capture_frames,
      uint32_t(s.lan.energy_saving), uint32_t(s.preferred == "auto" ? 0 : s.preferred == "usb" ? 1 : 2),
      uint32_t(std::strlen(s.lan.peer))}) number(value);
  content.insert(content.end(), s.lan.peer, s.lan.peer + std::strlen(s.lan.peer));
  for (const auto *group : {&s.mix.inputs, &s.mix.outputs}) for (const auto &c : *group) {
    number(uint32_t(c.gain_cdb + 6000)); number(unsigned(c.mute) | (unsigned(c.solo) << 1) | (unsigned(c.invert) << 2));
  }
  number(uint32_t(s.mix.master_cdb + 6000)); number(unsigned(s.mix.master_mute));
  if (path.empty()) {
    HKEY key = nullptr;
    auto status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, registry_key.c_str(), 0,
        nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE | KEY_WOW64_64KEY,
        nullptr, &key, nullptr);
    if (status == ERROR_SUCCESS) {
      status = RegSetValueExW(key, L"Profiles", 0, REG_BINARY, content.data(), DWORD(content.size()));
      RegCloseKey(key);
    }
    if (status != ERROR_SUCCESS) error = "Cannot save profiles: " + std::to_string(status);
    return status == ERROR_SUCCESS;
  }
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
