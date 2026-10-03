#pragma once
#include "device_session.hpp"
#include "device_store.hpp"
#include <map>
#include <memory>
namespace rey::service {
// Control-thread catalog. PCM callbacks only touch their own DeviceSession.
class Manager {
public:
  Manager(std::wstring configuration, bool digital_test);
  ~Manager();
  bool initialize(std::string &error);
  void run(HANDLE stop);
  std::string request(const std::string &command);
private:
  DeviceSession *add(const std::string &id, std::string &error);
  std::string status_locked(const std::string &error = "") const;
  std::string connect_lan(const Settings &profile);
  bool persist(DeviceSession &, const std::string &id, const Settings &, std::string &error);
  std::vector<uint16_t> ports(const std::string &override_id = "", const Settings *replacement = nullptr) const;
  void scan_usb();
  std::wstring path_;
  DeviceStore store_;
  bool digital_test_, winsock_ = false, bridge_present_ = false;
  mutable std::mutex mutex_;
  std::map<std::string, std::unique_ptr<DeviceSession>> sessions_;
  std::map<std::wstring, std::string> usb_paths_;
  std::unique_ptr<DeviceSession> draft_;
  std::string selected_, catalog_error_;
};
}
