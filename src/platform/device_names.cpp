#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <setupapi.h>
#include <vector>
#include "device_names.hpp"
namespace rey {
bool normalize_usb_name(const std::wstring &path) {
  constexpr GUID guid{0x7743f19e, 0x2a5d, 0x4f47, {0x93, 0x0d, 0x41, 0x75, 0x85, 0xaa, 0x87, 0x11}};
  constexpr wchar_t name[] = L"Rey Audio USB";
  const auto list = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
  if (list == INVALID_HANDLE_VALUE) return false;
  bool result = false;
  for (DWORD n = 0;; ++n) {
    SP_DEVICE_INTERFACE_DATA item{};
    item.cbSize = sizeof(item);
    if (!SetupDiEnumDeviceInterfaces(list, nullptr, &guid, n, &item)) break;
    DWORD bytes = 0;
    SetupDiGetDeviceInterfaceDetailW(list, &item, nullptr, 0, &bytes, nullptr);
    if (bytes < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) || bytes > 65536) continue;
    std::vector<unsigned char> storage(bytes);
    auto *detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(storage.data());
    detail->cbSize = sizeof(*detail);
    SP_DEVINFO_DATA device{};
    device.cbSize = sizeof(device);
    if (!SetupDiGetDeviceInterfaceDetailW(list, &item, detail, bytes, nullptr, &device) ||
        _wcsicmp(detail->DevicePath, path.c_str())) continue;
    wchar_t old[128]{};
    if (SetupDiGetDeviceRegistryPropertyW(list, &device, SPDRP_FRIENDLYNAME, nullptr,
        reinterpret_cast<PBYTE>(old), sizeof(old), nullptr) && !std::wcscmp(old, name)) result = true;
    else result = SetupDiSetDeviceRegistryPropertyW(list, &device, SPDRP_FRIENDLYNAME,
        reinterpret_cast<const BYTE *>(name), sizeof(name)) != FALSE;
    break;
  }
  SetupDiDestroyDeviceInfoList(list);
  return result;
}
}
