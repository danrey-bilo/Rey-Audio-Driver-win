#include "device_store.hpp"
#include <filesystem>
#include <algorithm>
namespace rey::service {
bool valid_device_id(const std::string &id) {
  if (id.size() != 32) return false;
  bool nonzero = false;
  for (const auto c : id) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    nonzero |= c != '0';
  }
  return nonzero;
}
std::wstring DeviceStore::path(const std::string &id) const {
  return base_.empty() ? L"" : base_ + L"." + std::wstring(id.begin(), id.end()) + L".dat";
}
std::wstring DeviceStore::key(const std::string &id) const {
  return L"Software\\ReyAudio\\Devices\\" + std::wstring(id.begin(), id.end());
}
bool DeviceStore::load(const std::string &id, Settings &s, std::string &error) const {
  if (!valid_device_id(id)) { error = "Invalid DeviceId"; return false; }
  return rey::service::load(path(id), s, error, key(id));
}
bool DeviceStore::save(const std::string &id, const Settings &s, std::string &error) const {
  if (!valid_device_id(id)) { error = "Invalid DeviceId"; return false; }
  return rey::service::save(path(id), s, error, key(id));
}
std::vector<std::string> DeviceStore::devices(std::string &error) const {
  std::vector<std::string> ids;
  if (base_.empty()) {
    HKEY registry = nullptr;
    auto status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\ReyAudio\\Devices", 0,
                               KEY_READ | KEY_WOW64_64KEY, &registry);
    if (status == ERROR_FILE_NOT_FOUND) return ids;
    if (status != ERROR_SUCCESS) { error = "Cannot enumerate card profiles"; return ids; }
    for (DWORD i = 0; i < 64; ++i) {
      WCHAR name[128]{}; DWORD size = 128;
      status = RegEnumKeyExW(registry, i, name, &size, nullptr, nullptr, nullptr, nullptr);
      if (status == ERROR_NO_MORE_ITEMS) break;
      if (status != ERROR_SUCCESS) { error = "Cannot enumerate card profile"; break; }
      std::wstring wide(name, size); std::string id(wide.begin(), wide.end());
      if (valid_device_id(id)) ids.push_back(id);
    }
    RegCloseKey(registry);
  } else {
    WIN32_FIND_DATAW info{};
    const auto search = FindFirstFileW((base_ + L".*.dat").c_str(), &info);
    if (search == INVALID_HANDLE_VALUE) return ids;
    const auto prefix = std::filesystem::path(base_).filename().wstring() + L".";
    do {
      const std::wstring name(info.cFileName);
      if ((info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
          name.size() != prefix.size() + 36 || name.compare(0, prefix.size(), prefix)) continue;
      const auto wide = name.substr(prefix.size(), 32); const std::string id(wide.begin(), wide.end());
      if (valid_device_id(id)) ids.push_back(id);
    } while (ids.size() < 64 && FindNextFileW(search, &info));
    FindClose(search);
  }
  std::sort(ids.begin(), ids.end());
  return ids;
}
std::string DeviceStore::selection() const {
  char id[33]{};
  if (base_.empty()) {
    WCHAR wide[33]{}; DWORD size = sizeof(wide);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"Software\\ReyAudio", L"SelectedDevice",
                    RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, wide, &size) != ERROR_SUCCESS) return "";
    for (unsigned i = 0; i < 32; ++i) id[i] = char(wide[i]);
  } else {
    const auto file = CreateFileW((base_ + L".selected").c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) return "";
    DWORD bytes = 0;
    const bool ok = GetFileSize(file, nullptr) == 32 && ReadFile(file, id, 32, &bytes, nullptr) && bytes == 32;
    CloseHandle(file); if (!ok) return "";
  }
  return valid_device_id(id) ? std::string(id) : "";
}
bool DeviceStore::select(const std::string &id, std::string &error) const {
  if (!id.empty() && !valid_device_id(id)) { error = "Invalid DeviceId"; return false; }
  bool ok;
  if (base_.empty()) {
    HKEY registry = nullptr;
    auto status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"Software\\ReyAudio", 0, nullptr, 0,
        KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &registry, nullptr);
    if (status == ERROR_SUCCESS) {
      const std::wstring wide(id.begin(), id.end());
      status = RegSetValueExW(registry, L"SelectedDevice", 0, REG_SZ,
          reinterpret_cast<const BYTE *>(wide.c_str()), DWORD((wide.size() + 1) * 2));
      RegCloseKey(registry);
    }
    ok = status == ERROR_SUCCESS;
  } else {
    const auto temporary = base_ + L".selected.new";
    const auto attributes = GetFileAttributesW(temporary.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))) {
      error = "Unsafe selection temporary path"; return false;
    }
    const auto file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    DWORD bytes = 0;
    ok = file != INVALID_HANDLE_VALUE && WriteFile(file, id.data(), DWORD(id.size()), &bytes, nullptr) &&
         bytes == id.size() && FlushFileBuffers(file);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (ok) ok = MoveFileExW(temporary.c_str(), (base_ + L".selected").c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
  }
  if (!ok) error = "Cannot save selected card";
  return ok;
}
bool DeviceStore::forget(const std::string &id, std::string &error) const {
  if (!valid_device_id(id)) { error = "Invalid DeviceId"; return false; }
  const auto status = base_.empty() ? RegDeleteTreeW(HKEY_LOCAL_MACHINE, key(id).c_str()) :
      (DeleteFileW(path(id).c_str()) ? ERROR_SUCCESS : GetLastError());
  if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) { error = "Cannot forget card profile"; return false; }
  return true;
}
}
