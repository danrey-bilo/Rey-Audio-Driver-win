#pragma once
#include "device_session.hpp"
#include "usb_selection.hpp"
namespace rey::service {
// One control plane, one owned USB interface, one audio session. Enumeration and
// persistence never run on the PCM callback thread.
class Manager {
public:
  Manager(std::wstring configuration, bool digital_test, bool asio_only = false);
  ~Manager();
  bool initialize(std::string &error);
  void run(HANDLE stop);
  std::string request(const std::string &command, DWORD sender_pid = 0);
private:
  bool activate(const std::string &id, std::string &error);
  bool persist(const Settings &, std::string &error);
  std::string status_locked(const std::string &error = "") const;
  void scan_usb();
  std::string asio_request(const std::string &, DWORD sender_pid);
  std::wstring path_, usb_path_;
  std::string identity_, device_warning_;
  bool digital_test_, asio_only_, bridge_present_ = false, migrate_profile_ = true;
  mutable std::mutex mutex_;
  std::unique_ptr<DeviceSession> session_;
};
}
