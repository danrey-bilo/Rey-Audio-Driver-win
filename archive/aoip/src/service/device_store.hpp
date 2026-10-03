#pragma once
#include "settings.hpp"
#include <vector>
namespace rey::service {
bool valid_device_id(const std::string &);
class DeviceStore {
public:
  explicit DeviceStore(std::wstring path) : base_(std::move(path)) {}
  std::wstring path(const std::string &id) const;
  std::wstring key(const std::string &id) const;
  std::vector<std::string> devices(std::string &error) const;
  bool load(const std::string &id, Settings &, std::string &error) const;
  bool save(const std::string &id, const Settings &, std::string &error) const;
  std::string selection() const;
  bool select(const std::string &id, std::string &error) const;
  bool forget(const std::string &id, std::string &error) const;
private:
  std::wstring base_;
};
}
