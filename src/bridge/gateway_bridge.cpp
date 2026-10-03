#include "gateway_bridge.hpp"
#include "gateway_abi.hpp"
#include "../audio/pcm_alignment.hpp"
#include <setupapi.h>
#include <algorithm>
#include <array>
#include <iterator>

namespace rey::bridge {
namespace {
std::wstring library_path() {
  wchar_t path[32768]{};
  const auto count = GetModuleFileNameW(nullptr, path, DWORD(std::size(path)));
  if (!count || count == std::size(path)) return {};
  std::wstring result(path, count);
  const auto separator = result.find_last_of(L"\\/");
  if (separator == std::wstring::npos) return {};
  return result.substr(0, separator + 1) + L"tagapi.dll";
}
struct Api {
  HMODULE module = nullptr;
  HRESULT (WINAPI *create_driver)(void **, const GUID *) = nullptr;
  HRESULT (WINAPI *find_driver)(void *) = nullptr;
  HRESULT (WINAPI *open_driver)(void *) = nullptr;
  void (WINAPI *delete_driver)(void *) = nullptr;
  HRESULT (WINAPI *info)(void *, gateway::Info *) = nullptr;
  HRESULT (WINAPI *lines)(void *, gateway::Line *, UINT, UINT *) = nullptr;
  // The SDK's legacy header omits LineId here; the actual v2 wrapper has four arguments.
  HRESULT (WINAPI *create_pipe)(void *, void **, UINT, bool) = nullptr;
  void (WINAPI *delete_pipe)(void *) = nullptr;
  gateway::Common *(WINAPI *common)(void *) = nullptr;
  HRESULT (WINAPI *range)(void *, const gateway::Range *) = nullptr;
  HRESULT (WINAPI *format)(void *, const gateway::Format *) = nullptr;
  HRESULT (WINAPI *minimum)(void *, UINT) = nullptr;
  HRESULT (WINAPI *event)(void *, HANDLE) = nullptr;
  HRESULT (WINAPI *notify)(void *, LONG) = nullptr;
  HRESULT (WINAPI *advance)(void *, UINT) = nullptr;
  HRESULT (WINAPI *reset)(void *) = nullptr;
  ~Api() { if (module) FreeLibrary(module); }
  template<class T> bool symbol(T &target, const char *name) {
    target = reinterpret_cast<T>(GetProcAddress(module, name));
    return target != nullptr;
  }
  bool load() {
    const auto path = library_path();
    if (path.empty()) return false;
    module = LoadLibraryExW(path.c_str(), nullptr,
                           LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    return module && symbol(create_driver, "Driver_Create") && symbol(find_driver, "Driver_Find") &&
        symbol(open_driver, "Driver_Open") && symbol(delete_driver, "Driver_Delete") &&
        symbol(info, "Driver_GetInfo") && symbol(lines, "Driver_GetLineList") &&
        symbol(create_pipe, "Driver_CreatePipe") && symbol(delete_pipe, "Pipe_Delete") &&
        symbol(common, "Pipe_GetCommonData") && symbol(range, "Pipe_SetFormatRange") &&
        symbol(format, "Pipe_SetDefaultFormat") && symbol(minimum, "Pipe_SetMinimumBufferLength") &&
        symbol(event, "Pipe_SetNotificationEvent") && symbol(notify, "Pipe_Notify") &&
        symbol(advance, "Pipe_AdvanceDevicePosition") && symbol(reset, "Pipe_ResetDevicePosition");
  }
};
struct Pipe {
  void *object = nullptr;
  HANDLE event = nullptr;
  gateway::Common *common = nullptr;
  unsigned channels = 0;
};
} // namespace

struct GatewayBridge::State {
  Api api;
  void *driver = nullptr;
  Pipe capture, render;
  REY_BRIDGE_PROFILE profile{};
  REY_BRIDGE_STATS stats{};
  DWORD error = ERROR_SUCCESS;
  bool checked(HRESULT result, const char *operation, std::string &message) {
    if (SUCCEEDED(result)) return true;
    error = DWORD(result);
    message = std::string("TAG ") + operation + " failed: " + std::to_string(error);
    return false;
  }
  bool open_pipe(Pipe &pipe, const gateway::Line &line, unsigned channels, std::string &message) {
    pipe.channels = channels;
    if (!checked(api.create_pipe(driver, &pipe.object, line.id, line.capture), "pipe", message)) return false;
    pipe.common = api.common(pipe.object);
    if (!pipe.common) { error = ERROR_INVALID_DATA; message = "TAG has no shared buffer descriptor"; return false; }
    const unsigned container = profile.valid_bits / 8;
    const gateway::Range range{profile.rate, profile.rate, profile.valid_bits, profile.valid_bits,
                               channels, channels, container, container};
    const gateway::Format format{profile.rate, profile.valid_bits, channels, container,
                                 channels == 8 ? 0x63fu : channels == 2 ? 3u : 0u};
    if (!checked(api.range(pipe.object, &range), "format range", message) ||
        !checked(api.format(pipe.object, &format), "default format", message)) return false;
    // This buffer belongs only to normal Windows applications. The ASIO path
    // does not pass through TAG, its Windows engine, or this minimum size.
    const auto minimum_us = UINT((uint64_t(profile.block) * 2000000 + profile.rate - 1) / profile.rate);
    if (!checked(api.minimum(pipe.object, minimum_us), "minimum buffer", message)) return false;
    pipe.event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!pipe.event) { error = GetLastError(); message = "Cannot allocate TAG notification event"; return false; }
    return checked(api.event(pipe.object, pipe.event), "notification event", message);
  }
  void close(Pipe &pipe) {
    if (pipe.object) api.delete_pipe(pipe.object);
    if (pipe.event) CloseHandle(pipe.event);
    pipe = {};
  }
  bool stream_matches(const Pipe &pipe) const {
    const auto &f = pipe.common->driver.stream.format;
    return f.Format.wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
        f.Format.cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX) &&
        f.Format.nSamplesPerSec == profile.rate && f.Format.nChannels == pipe.channels &&
        f.Format.wBitsPerSample == profile.valid_bits &&
        f.Samples.wValidBitsPerSample == profile.valid_bits &&
        f.Format.nBlockAlign == pipe.channels * (profile.valid_bits / 8) &&
        IsEqualGUID(f.SubFormat, gateway::pcm);
  }
  bool update(Pipe &pipe) {
    if (!pipe.object) return true;
    // All descriptor access happens on the transport callback thread. The
    // driver waits for acknowledgement before completing stream transitions.
    (void)WaitForSingleObject(pipe.event, 0);
    if (!(pipe.common->driver.filter.flags & gateway::waiting_for_host)) return true;
    const auto &stream = pipe.common->driver.stream;
    LONG result = 0;
    if (pipe.common->driver.filter.has_stream && !stream_matches(pipe)) {
      result = gateway::format_not_supported;
    }
    if (!pipe.common->driver.filter.has_stream || stream.state != gateway::stream_running) {
      const auto reset = api.reset(pipe.object);
      if (FAILED(reset)) { error = DWORD(reset); return false; }
    }
    const auto hr = api.notify(pipe.object, result);
    if (FAILED(hr)) { error = DWORD(hr); return false; }
    return true;
  }
  bool transfer(Pipe &pipe, const engine::AudioBlock &block, bool capture) {
    if (!pipe.object) return true;
    const auto &stream = pipe.common->driver.stream;
    const auto &filter = pipe.common->driver.filter;
    if (!filter.has_stream || stream.state != gateway::stream_running || !stream.buffer_frames) return true;
    const auto capacity = stream.buffer_frames;
    const auto position = pipe.common->host.position;
    const unsigned width = profile.valid_bits / 8;
    if (!pipe.common->host.buffer || position >= capacity || block.frames > capacity / 2 ||
        uint64_t(capacity) * pipe.channels * width > filter.reserved_bytes ||
        !stream_matches(pipe)) {
      error = ERROR_INVALID_DATA;
      return false;
    }
    auto *buffer = static_cast<uint8_t *>(pipe.common->host.buffer);
    for (unsigned frame = 0; frame < block.frames; ++frame) {
      auto *audio = buffer + size_t((position + frame) % capacity) * pipe.channels * width;
      for (unsigned channel = 0; channel < pipe.channels; ++channel) {
        const auto sample = size_t(frame) * pipe.channels + channel;
        if (capture) {
          const auto value = rey::audio::wave_sample(block.capture[sample], profile.valid_bits);
          for (unsigned byte = 0; byte < width; ++byte)
            audio[channel * width + byte] = uint8_t(value >> (32 - profile.valid_bits + byte * 8));
        } else {
          uint32_t value = 0;
          for (unsigned byte = 0; byte < width; ++byte)
            value |= uint32_t(audio[channel * width + byte]) << (32 - profile.valid_bits + byte * 8);
          block.render[sample] = rey::audio::wire_sample(value, profile.valid_bits);
        }
      }
    }
    const auto result = api.advance(pipe.object, block.frames);
    if (FAILED(result)) { error = DWORD(result); return false; }
    if (capture) stats.capture_frames += block.frames;
    else stats.render_frames += block.frames;
    return true;
  }
};

GatewayBridge::GatewayBridge() : state_(std::make_unique<State>()) {}
GatewayBridge::~GatewayBridge() { detach(); }
bool GatewayBridge::available() {
  const auto path = library_path();
  if (path.empty() || GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
  const auto devices = SetupDiGetClassDevsW(&gateway::product, nullptr, nullptr,
                                           DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
  if (devices == INVALID_HANDLE_VALUE) return false;
  SP_DEVICE_INTERFACE_DATA item{};
  item.cbSize = sizeof(item);
  const bool found = SetupDiEnumDeviceInterfaces(devices, nullptr, &gateway::product, 0, &item) != FALSE;
  SetupDiDestroyDeviceInfoList(devices);
  return found;
}
bool GatewayBridge::attach(const REY_BRIDGE_PROFILE &profile, std::string &error) {
  detach();
  error.clear();
  if (!rey_bridge_valid_profile(&profile)) {
    state_->error = ERROR_INVALID_PARAMETER; error = "Invalid TAG audio profile"; return false;
  }
  state_ = std::make_unique<State>();
  auto &s = *state_;
  s.profile = profile;
  if (!s.api.load()) {
    s.error = GetLastError();
    if (!s.error) s.error = ERROR_PROC_NOT_FOUND;
    error = "Cannot load the local TAG SDK (tagapi.dll)"; return false;
  }
  if (!s.checked(s.api.create_driver(&s.driver, &gateway::product), "driver object", error) ||
      !s.checked(s.api.find_driver(s.driver), "driver discovery", error) ||
      !s.checked(s.api.open_driver(s.driver), "driver ownership", error)) { detach(); return false; }
  gateway::Info info{};
  std::array<gateway::Line, 128> lines{};
  UINT count = 0;
  if (!s.checked(s.api.info(s.driver, &info), "driver version", error) ||
      info.major != 2 || info.minor != 0 ||
      !s.checked(s.api.lines(s.driver, lines.data(), UINT(sizeof(lines)), &count), "line list", error) ||
      count > lines.size()) {
    if (error.empty()) { s.error = ERROR_NOT_SUPPORTED; error = "Unsupported TAG driver API"; }
    detach(); return false;
  }
  const gateway::Line *capture = nullptr, *render = nullptr;
  for (UINT n = 0; n < count; ++n) {
    if (lines[n].capture && !capture) capture = &lines[n];
    if (!lines[n].capture && !render) render = &lines[n];
  }
  if ((profile.inputs && !capture) || (profile.outputs && !render)) {
    s.error = ERROR_NOT_FOUND;
    error = "The local TAG package has no complete capture/render pair"; detach(); return false;
  }
  if ((profile.inputs && !s.open_pipe(s.capture, *capture, profile.inputs, error)) ||
      (profile.outputs && !s.open_pipe(s.render, *render, profile.outputs, error))) { detach(); return false; }
  s.stats.size = sizeof(s.stats); s.stats.version = REY_BRIDGE_VERSION; s.stats.attached = 1;
  return true;
}
bool GatewayBridge::process(const engine::AudioBlock &block) {
  auto &s = *state_;
  if (!s.stats.attached || !block.frames || block.frames > REY_BRIDGE_FRAMES ||
      block.inputs != s.profile.inputs || block.outputs != s.profile.outputs ||
      block.bits != s.profile.valid_bits || (block.inputs && !block.capture) ||
      (block.outputs && !block.render)) { s.error = ERROR_INVALID_PARAMETER; return false; }
  if (block.outputs) std::fill_n(block.render, size_t(block.frames) * block.outputs, 0);
  if (!s.update(s.capture) || !s.update(s.render) ||
      !s.transfer(s.capture, block, true) || !s.transfer(s.render, block, false)) return false;
  ++s.stats.exchanges;
  if (block.discontinuity) ++s.stats.discontinuities;
  return true;
}
void GatewayBridge::detach() {
  if (!state_) return;
  state_->close(state_->capture); state_->close(state_->render);
  if (state_->driver) state_->api.delete_driver(state_->driver);
  state_->driver = nullptr; state_->stats.attached = 0;
}
DWORD GatewayBridge::last_error() const { return state_->error; }
bool GatewayBridge::stats(REY_BRIDGE_STATS &out) {
  if (!state_->stats.attached) return false;
  out = state_->stats;
  for (const auto *pipe : {&state_->capture, &state_->render}) {
    if (!pipe->common) continue;
    const auto &stream = pipe->common->driver.stream;
    if (pipe == &state_->capture) {
      out.capture_running = pipe->common->driver.filter.has_stream && stream.state == gateway::stream_running;
      out.capture_overruns = stream.overruns;
    } else {
      out.render_running = pipe->common->driver.filter.has_stream && stream.state == gateway::stream_running;
      out.render_underruns = stream.underruns;
    }
  }
  return true;
}
} // namespace rey::bridge
