#include "driver_bridge.hpp"
#include "../engine/session_engine.hpp"
#include "aoip/pcm_codec.hpp"
#include <setupapi.h>
namespace piaoip::bridge {
namespace {
constexpr GUID control_guid = {
    0x8a72d7d1, 0x0a12, 0x4b61, {0x92, 0x57, 0x81, 0x16, 0x35, 0xef, 0x11, 0x35}};
}
DriverBridge::~DriverBridge() {
  detach();
}
bool DriverBridge::available() {
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
bool DriverBridge::attach(const engine::SessionEngine &engine, std::string &error) {
  const auto &cfg = engine.config();
  const auto &peer = engine.peer();
  PIAOIP_BRIDGE_PROFILE profile = {
      sizeof(profile), PIAOIP_BRIDGE_VERSION, cfg.rate, cfg.bits, peer.inputs().count,
      peer.outputs().count, unsigned(cfg.block), cfg.safety, {}};
  std::memcpy(profile.device_id, peer.identity(), 32);
  return attach(profile, error);
}
bool DriverBridge::attach(const PIAOIP_BRIDGE_PROFILE &profile, std::string &error) {
  detach();
  if (!piaoip_bridge_valid_profile(&profile)) {
    error = "Invalid audio bridge profile";
    error_ = ERROR_INVALID_PARAMETER;
    return false;
  }
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
  if (!piaoip_bridge_valid_profile(&profile_) ||
      !DeviceIoControl(device_, IOCTL_PIAOIP_ATTACH, &profile_, sizeof(profile_), nullptr, 0,
                       &bytes, nullptr)) {
    error_ = GetLastError();
    error = "ACX endpoint attachment failed: " + std::to_string(error_);
    detach();
    return false;
  }
  return true;
}
bool DriverBridge::process(const engine::AudioBlock &block) {
  if (device_ == INVALID_HANDLE_VALUE || block.frames > PIAOIP_BRIDGE_FRAMES ||
      block.inputs != profile_.inputs || block.outputs != profile_.outputs)
    return false;
  exchange_.size = sizeof(exchange_);
  exchange_.version = PIAOIP_BRIDGE_VERSION;
  exchange_.frames = block.frames;
  exchange_.capture_channels = block.inputs;
  exchange_.render_channels = block.outputs;
  exchange_.flags = (block.missing ? PIAOIP_BRIDGE_MISSING : 0) |
                    (block.discontinuity ? PIAOIP_BRIDGE_DISCONTINUITY : 0);
  exchange_.epoch = block.epoch;
  exchange_.reserved = 0;
  exchange_.frame_position = block.frame_position;
  exchange_.qpc = 0;
  for (size_t n = 0; n < size_t(block.frames) * block.inputs; ++n)
    exchange_.samples[n] = int32_t(aoip::wave_sample(block.capture[n], block.bits));
  DWORD bytes = 0;
  // Development bridge: METHOD_BUFFERED exchanges are measured explicitly.
  // The driver's PCM work has a fixed frame bound and no network operations.
  // Scheduling and lock wait time still need measurement on the kernel stand.
  // Kernel cancellation/Verifier and real WASAPI streaming are release gates.
  if (!DeviceIoControl(device_, IOCTL_PIAOIP_EXCHANGE, &exchange_, sizeof(exchange_), &exchange_,
                       sizeof(exchange_), &bytes, nullptr)) {
    error_ = GetLastError();
    return false;
  }
  if (bytes != sizeof(exchange_) || exchange_.size != sizeof(exchange_) ||
      exchange_.version != PIAOIP_BRIDGE_VERSION || exchange_.frames != block.frames ||
      exchange_.epoch != block.epoch || exchange_.capture_channels != block.inputs ||
      exchange_.render_channels != block.outputs ||
      exchange_.flags & ~(PIAOIP_BRIDGE_CAPTURE_ACTIVE | PIAOIP_BRIDGE_RENDER_ACTIVE)) {
    error_ = ERROR_INVALID_DATA;
    return false;
  }
  for (size_t n = 0; n < size_t(block.frames) * block.outputs; ++n)
    block.render[n] = aoip::wire_sample(uint32_t(exchange_.samples[n]), block.bits);
  return true;
}
void DriverBridge::detach() {
  if (device_ != INVALID_HANDLE_VALUE) {
    DWORD bytes = 0;
    DeviceIoControl(device_, IOCTL_PIAOIP_DETACH, nullptr, 0, nullptr, 0, &bytes, nullptr);
    CloseHandle(device_);
    device_ = INVALID_HANDLE_VALUE;
  }
}
bool DriverBridge::stats(PIAOIP_BRIDGE_STATS &result) {
  DWORD bytes = 0;
  return device_ != INVALID_HANDLE_VALUE &&
         DeviceIoControl(device_, IOCTL_PIAOIP_STATS, nullptr, 0, &result, sizeof(result), &bytes,
                         nullptr) &&
         bytes == sizeof(result) && result.size == sizeof(result) &&
         result.version == PIAOIP_BRIDGE_VERSION;
}
} // namespace piaoip::bridge
