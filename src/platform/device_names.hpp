#pragma once
#include <string>
namespace rey {
// Control-plane metadata only, after WinUSB HELLO/descriptor validation. Windows
// may retain the old product name for an existing PnP instance. Requires SYSTEM
// or an administrator; failure does not affect streaming or device detection.
bool normalize_usb_name(const std::wstring &validated_path);
}
