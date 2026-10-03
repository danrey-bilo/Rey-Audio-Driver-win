#include "driver_bridge.hpp"
#include "../audio/pcm_alignment.hpp"
#include <setupapi.h>
#ifdef REY_ENABLE_TAG_BRIDGE
#include "gateway_bridge.hpp"
#endif
namespace rey::bridge {
namespace {
constexpr GUID control_guid = {
    0x8a72d7d1, 0x0a12, 0x4b61, {0x92, 0x57, 0x81, 0x16, 0x35, 0xef, 0x11, 0x35}};
}
DriverBridge::DriverBridge() = default;
DriverBridge::~DriverBridge() {
  detach();
}
bool DriverBridge::available() {
#ifdef REY_ENABLE_TAG_BRIDGE
  if (GatewayBridge::available()) return true;
#endif
  const auto devices = SetupDiGetClassDevsW(&control_guid, nullptr, nullptr,
                                          DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
  if (devices == INVALID_HANDLE_VALUE)
    return false;
  SP_DEVICE_INTERFACE_DATA item{};
  item.cbSize = sizeof(item);
  const bool present = SetupDiEnumDeviceInterfaces(devices, nullptr, &control_guid, 0, &item) != FALSE;
  SetupDiDestroyDeviceInfoList(devices);
  return present;
}
bool DriverBridge::attach(const REY_BRIDGE_PROFILE &profile, std::string &error) {
  detach();
  if (!rey_bridge_valid_profile(&profile)) {
    error = "Invalid audio bridge profile";
    error_ = ERROR_INVALID_PARAMETER;
    return false;
  }
#ifdef REY_ENABLE_TAG_BRIDGE
  if (GatewayBridge::available()) {
    gateway_ = std::make_unique<GatewayBridge>();
    if (gateway_->attach(profile, error)) return true;
    error_ = gateway_->last_error();
    gateway_.reset();
    return false;
  }
#endif
  HDEVINFO devices =
      SetupDiGetClassDevsW(&control_guid, nullptr, nullptr, DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
  if (devices == INVALID_HANDLE_VALUE) {
    error = "ACX bridge interface is unavailable";
    return false;
  }
  SP_DEVICE_INTERFACE_DATA item{};
  item.cbSize = sizeof(item);
  DWORD required = 0;
  if (SetupDiEnumDeviceInterfaces(devices, nullptr, &control_guid, 0, &item)) {
    SetupDiGetDeviceInterfaceDetailW(devices, &item, nullptr, 0, &required, nullptr);
    std::vector<uint8_t> storage(required);
    auto *detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(storage.data());
    if (required >= sizeof(*detail)) {
      detail->cbSize = sizeof(*detail);
      if (SetupDiGetDeviceInterfaceDetailW(devices, &item, detail, required, nullptr, nullptr))
        device_ = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
  }
  error_ = GetLastError();
  SetupDiDestroyDeviceInfoList(devices);
  if (device_ == INVALID_HANDLE_VALUE) {
    error = "Cannot open the ACX bridge: " + std::to_string(error_);
    return false;
  }
  profile_ = profile;
  DWORD bytes = 0;
  if (!rey_bridge_valid_profile(&profile_) ||
      !DeviceIoControl(device_, IOCTL_REY_ATTACH, &profile_, sizeof(profile_), nullptr, 0,
                       &bytes, nullptr)) {
    error_ = GetLastError();
    error = "ACX endpoint attachment failed: " + std::to_string(error_);
    detach();
    return false;
  }
  return true;
}
bool DriverBridge::process(const engine::AudioBlock &block) {
#ifdef REY_ENABLE_TAG_BRIDGE
  if (gateway_) return gateway_->process(block);
#endif
  if (device_ == INVALID_HANDLE_VALUE || block.frames > REY_BRIDGE_FRAMES ||
      block.inputs != profile_.inputs || block.outputs != profile_.outputs)
    return false;
  exchange_.size = sizeof(exchange_);
  exchange_.version = REY_BRIDGE_VERSION;
  exchange_.frames = block.frames;
  exchange_.capture_channels = block.inputs;
  exchange_.render_channels = block.outputs;
  exchange_.flags = (block.missing ? REY_BRIDGE_MISSING : 0) |
                    (block.discontinuity ? REY_BRIDGE_DISCONTINUITY : 0);
  exchange_.epoch = block.epoch;
  exchange_.reserved = 0;
  exchange_.frame_position = block.frame_position;
  exchange_.qpc = 0;
  for (size_t n = 0; n < size_t(block.frames) * block.inputs; ++n)
    exchange_.samples[n] = int32_t(rey::audio::wave_sample(block.capture[n], block.bits));
  DWORD bytes = 0;
  // Development bridge: METHOD_BUFFERED exchanges are measured explicitly.
  // The driver's PCM work has a fixed frame bound and no network operations.
  // Scheduling and lock wait time still need measurement on the kernel stand.
  // Kernel cancellation/Verifier and real WASAPI streaming are release gates.
  if (!DeviceIoControl(device_, IOCTL_REY_EXCHANGE, &exchange_, sizeof(exchange_), &exchange_,
                       sizeof(exchange_), &bytes, nullptr)) {
    error_ = GetLastError();
    return false;
  }
  if (bytes != sizeof(exchange_) || exchange_.size != sizeof(exchange_) ||
      exchange_.version != REY_BRIDGE_VERSION || exchange_.frames != block.frames ||
      exchange_.epoch != block.epoch || exchange_.capture_channels != block.inputs ||
      exchange_.render_channels != block.outputs ||
      exchange_.flags & ~(REY_BRIDGE_CAPTURE_ACTIVE | REY_BRIDGE_RENDER_ACTIVE)) {
    error_ = ERROR_INVALID_DATA;
    return false;
  }
  for (size_t n = 0; n < size_t(block.frames) * block.outputs; ++n)
    block.render[n] = rey::audio::wire_sample(uint32_t(exchange_.samples[n]), block.bits);
  return true;
}
void DriverBridge::detach() {
#ifdef REY_ENABLE_TAG_BRIDGE
  gateway_.reset();
#endif
  if (device_ != INVALID_HANDLE_VALUE) {
    DWORD bytes = 0;
    DeviceIoControl(device_, IOCTL_REY_DETACH, nullptr, 0, nullptr, 0, &bytes, nullptr);
    CloseHandle(device_);
    device_ = INVALID_HANDLE_VALUE;
  }
}
bool DriverBridge::stats(REY_BRIDGE_STATS &result) {
#ifdef REY_ENABLE_TAG_BRIDGE
  if (gateway_) return gateway_->stats(result);
#endif
  DWORD bytes = 0;
  return device_ != INVALID_HANDLE_VALUE &&
         DeviceIoControl(device_, IOCTL_REY_STATS, nullptr, 0, &result, sizeof(result), &bytes,
                         nullptr) &&
         bytes == sizeof(result) && result.size == sizeof(result) &&
         result.version == REY_BRIDGE_VERSION;
}
#ifdef REY_ENABLE_TAG_BRIDGE
DWORD DriverBridge::gateway_error() const { return gateway_->last_error(); }
#endif
} // namespace rey::bridge
