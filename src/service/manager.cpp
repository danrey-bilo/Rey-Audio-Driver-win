#include "manager.hpp"
#include "commands.hpp"
#include "../bridge/driver_bridge.hpp"
#include "../platform/device_names.hpp"
#include "pi5ausb/windows.hpp"
namespace rey::service {
Manager::Manager(std::wstring path, bool test, bool asio_only)
    : path_(std::move(path)), digital_test_(test), asio_only_(asio_only) {}
Manager::~Manager() = default;
bool Manager::initialize(std::string &error) {
  Settings settings; unsigned format = 0;
  if (!load(path_, settings, error, L"Software\\ReyAudio", &format)) return false;
  migrate_profile_ = format < 3;
  session_ = std::make_unique<DeviceSession>("", path_, L"Software\\ReyAudio", settings, digital_test_, asio_only_);
  bridge_present_ = !asio_only_ && bridge::DriverBridge::available();
  return true;
}
bool Manager::activate(const std::string &id, std::string &error) {
  if (!valid_device_id(id)) { error = "Invalid USB DeviceId"; return false; }
  auto settings = session_->settings();
  if (migrate_profile_) {
    // Read only the profile belonging to the physically attached USB board.
    // No retired card catalog is enumerated or recreated.
    const auto legacy_path = path_.empty() ? L"" : path_ + L"." + std::wstring(id.begin(), id.end()) + L".dat";
    const auto legacy_key = L"Software\\ReyAudio\\Devices\\" + std::wstring(id.begin(), id.end());
    if (!load(legacy_path, settings, error, legacy_key)) return false;
    if (!save(path_, settings, error)) return false;
    migrate_profile_ = false;
  }
  auto next = std::make_unique<DeviceSession>(id, path_, L"Software\\ReyAudio", settings, digital_test_, asio_only_);
  session_ = std::move(next); // The old session closes its IPC and stream first.
  identity_ = id;
  return true;
}
bool Manager::persist(const Settings &next, std::string &error) {
  if (!session_->update(next, error)) return false;
  migrate_profile_ = false;
  return true;
}
void Manager::scan_usb() {
  std::string error;
  const auto paths = pi5ausb::WindowsDevice::interface_paths(error);
  if (!error.empty()) { device_warning_ = error; return; }
  const auto selection = select_usb(paths, usb_path_);
  device_warning_ = selection.warning;
  if (selection.path != usb_path_) {
    session_->poll(GetTickCount64(), L"", bridge_present_);
    usb_path_.clear();
    if (!selection.path.empty()) {
      pi5ausb::WindowsDevice usb;
      if (!usb.open(selection.path, error)) { device_warning_ = error; return; }
      const std::string id(usb.device().hello.id.data(), 32);
      if (identity_ != id && !activate(id, error)) { device_warning_ = error; return; }
      usb_path_ = selection.path;
      normalize_usb_name(usb_path_);
    }
  }
  bridge_present_ = !asio_only_ && bridge::DriverBridge::available();
}
void Manager::run(HANDLE stop) {
  uint64_t last_scan = 0;
  while (WaitForSingleObject(stop, 200) == WAIT_TIMEOUT) {
    const auto now = GetTickCount64();
    std::lock_guard<std::mutex> lock(mutex_);
    if (now - last_scan >= 1000) { last_scan = now; scan_usb(); }
    session_->poll(now, usb_path_, bridge_present_);
  }
  std::lock_guard<std::mutex> lock(mutex_);
  session_->poll(GetTickCount64(), L"", bridge_present_);
}
std::string Manager::status_locked(const std::string &error) const {
  auto text = session_->status(error); text.pop_back();
  return text + ",\"device_limit\":1,\"device_warning\":" + json_string(device_warning_) + "}";
}
std::string Manager::request(const std::string &command, DWORD sender_pid) {
  if (command.compare(0, 5, "ASIO ") == 0) return asio_request(command, sender_pid);
  std::lock_guard<std::mutex> lock(mutex_);
  if (command == "STATUS") return status_locked();
  Settings next; std::string error;
  if (!command_settings(session_->settings(), command, next, error) || !persist(next, error)) return status_locked(error);
  return status_locked();
}
}
