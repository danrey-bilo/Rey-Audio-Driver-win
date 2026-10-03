#pragma once
#include <algorithm>
#include <string>
#include <vector>
namespace rey::service {
struct UsbSelection { std::wstring path; std::string warning; };
// Keep the owned interface when another board appears. Never choose arbitrarily
// between two unowned interfaces, and never aggregate them into one clock.
inline UsbSelection select_usb(const std::vector<std::wstring> &paths, const std::wstring &owned) {
  if (!owned.empty() && std::find(paths.begin(), paths.end(), owned) != paths.end())
    return {owned, paths.size() > 1 ? "Only one USB device is supported; the additional device is ignored" : ""};
  if (paths.size() == 1) return {paths.front(), ""};
  return {{}, paths.size() > 1 ? "Connect only one Rey Audio USB device" : ""};
}
}
