#include "settings.hpp"
#include <sstream>
#include <vector>
namespace rey::service {
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
  const auto rate = s.usb.rate;
  if ((rate != 44100 && rate != 48000 && rate != 88200 && rate != 96000 && rate != 176400 && rate != 192000) ||
      (s.usb.bits != 16 && s.usb.bits != 24 && s.usb.bits != 32) ||
      s.usb.block < 16 || s.usb.block > 256 || (s.usb.block & (s.usb.block - 1)) ||
      s.usb.inputs != 8 || s.usb.outputs != 8 || s.usb.safety > 8192 ||
      !s.usb_depth || s.usb_depth > 16 || !audio::valid(s.mix)) {
    error = "Invalid USB or mixer settings"; return false;
  }
  return true;
}
bool valid_device_id(const std::string &id) {
  if (id.size() != 32) return false;
  bool nonzero = false;
  for (const auto c : id) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    nonzero |= c != '0';
  }
  return nonzero;
}
bool load(const std::wstring &path, Settings &out, std::string &error, const std::wstring &key, unsigned *format) {
  if (format) *format = 0;
  // One atomic value commits USB and mixer. Finite tests use an isolated .dat.
  // Profiles is read only to migrate earlier installations.
  std::vector<BYTE> bytes(512);
  DWORD size = DWORD(bytes.size());
  if (path.empty()) {
    auto status = RegGetValueW(HKEY_LOCAL_MACHINE, key.c_str(),
        L"UsbProfile", RRF_RT_REG_BINARY | RRF_SUBKEY_WOW6464KEY, nullptr, bytes.data(), &size);
    if (status == ERROR_FILE_NOT_FOUND) {
      size = DWORD(bytes.size());
      status = RegGetValueW(HKEY_LOCAL_MACHINE, key.c_str(), L"Profiles",
          RRF_RT_REG_BINARY | RRF_SUBKEY_WOW6464KEY, nullptr, bytes.data(), &size);
    }
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
  unsigned magic = 0, version = 0, bits = s.usb.bits, block = s.usb.block, automatic = 1;
  if (!number(magic, UINT32_MAX) || magic != 0x31535952 || !number(version, 3) || !version ||
      !number(s.usb.rate, 192000) || !number(bits, 32) || !number(s.usb_depth, 16) ||
      !number(block, 256) || !number(s.usb.safety, 8192) || !number(automatic, 1)) {
    error = "Malformed saved USB profile"; return false;
  }
  s.usb.bits = uint16_t(bits); s.usb.block = block; s.usb_auto = automatic != 0;
  if (version < 3) {
    // Retired fields are length/bounds checked, then discarded. They cannot
    // influence USB validation, enumeration, stream startup or new saves.
    unsigned ignored = 0, length = 0;
    for (const unsigned maximum : {1u, 65535u, 256u, 8192u, 256u, 1u, 2u})
      if (!number(ignored, maximum)) { error = "Malformed legacy profile"; return false; }
    if (!number(length, 15) || cursor + length > bytes.size()) {
      error = "Malformed legacy profile length"; return false;
    }
    for (unsigned i = 0; i < length; ++i) {
      if (!bytes[cursor] || bytes[cursor] >= 128) { error = "Invalid legacy profile bytes"; return false; }
      ++cursor;
    }
  }
  if (version >= 2) {
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
  if (format) *format = version;
  return true;
}
bool save(const std::wstring &path, const Settings &s, std::string &error, const std::wstring &registry_key) {
  if (!valid(s, error))
    return false;
  std::vector<BYTE> content;
  auto number = [&](uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) content.push_back(BYTE(value >> (8 * i)));
  };
  for (const auto value : {uint32_t(0x31535952), uint32_t(3), s.usb.rate,
      uint32_t(s.usb.bits), s.usb_depth, s.usb.block, s.usb.safety, uint32_t(s.usb_auto)}) number(value);
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
      status = RegSetValueExW(key, L"UsbProfile", 0, REG_BINARY, content.data(), DWORD(content.size()));
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
