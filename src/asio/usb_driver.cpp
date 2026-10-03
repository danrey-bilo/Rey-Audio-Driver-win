#include "ipc_client.hpp"
#include "../platform/realtime.hpp"
#include <objbase.h>
#include <shellapi.h>
#include "iasiodrv.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

namespace rey::asio {
HMODULE module = nullptr;
std::atomic<long> objects{0};
namespace {
int32_t unpack(const uint8_t *p, unsigned bits) {
  uint32_t value = 0;
  for (unsigned n = 0; n < bits / 8; ++n) value |= uint32_t(p[n]) << (n * 8);
  if (bits < 32 && (value & (uint32_t(1) << (bits - 1)))) value |= ~((uint32_t(1) << bits) - 1);
  int32_t signed_value; std::memcpy(&signed_value, &value, sizeof(value)); return signed_value;
}
void pack(uint8_t *p, int32_t value, unsigned bits) {
  for (unsigned n = 0; n < bits / 8; ++n) p[n] = uint8_t(uint32_t(value) >> (n * 8));
}
struct Channel {
  unsigned number = 0; bool input = false;
  std::unique_ptr<uint8_t[]> buffers[2];
};
} // namespace
class Driver final : public IASIO {
public:
  Driver() { ++objects; }
  ~Driver() { disposeBuffers(); if (stop_) CloseHandle(stop_); --objects; }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
    if (!out) return E_POINTER; *out = nullptr;
    if (id != IID_IUnknown && id != clsid) return E_NOINTERFACE;
    *out = static_cast<IASIO *>(this); AddRef(); return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
  ULONG STDMETHODCALLTYPE Release() override { const auto n = --references_; if (!n) delete this; return n; }
  ASIOBool init(void *) override {
    if (running_) return ASIOFalse;
    disposeBuffers();
    char lead[16]{};
    const std::string choice = "auto";
    lead_ = 3;
    unsigned preferred = 0;
    HKEY preferences = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\ReyAudio\\ASIO", 0, KEY_READ, &preferences) == ERROR_SUCCESS) {
      DWORD value = 0, bytes = sizeof(value), type = 0;
      if (RegQueryValueExW(preferences, L"RenderLeadBlocks", nullptr, &type, reinterpret_cast<BYTE *>(&value), &bytes) == ERROR_SUCCESS &&
          type == REG_DWORD && value >= 1 && value <= 4) lead_ = value;
      bytes = sizeof(value);
      if (RegQueryValueExW(preferences, L"BufferSize", nullptr, &type, reinterpret_cast<BYTE *>(&value), &bytes) == ERROR_SUCCESS &&
          type == REG_DWORD && valid_block(value)) preferred = value;
      RegCloseKey(preferences);
    }
    if (GetEnvironmentVariableA("REY_ASIO_LEAD_BLOCKS", lead, sizeof(lead)) == 1 && lead[0] >= '1' && lead[0] <= '4')
      lead_ = unsigned(lead[0] - '0');
    std::string error;
    for (unsigned attempt = 0; attempt < 15; ++attempt) {
      if (query_info(choice, info_, error)) {
        block_ = preferred ? preferred : info_.block; initialized_ = true;
        if (!stop_) stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!stop_) { initialized_ = false; set_error("Cannot allocate ASIO stop event"); }
        return initialized_ ? ASIOTrue : ASIOFalse;
      }
      Sleep(100);
    }
    set_error(error); initialized_ = false; return ASIOFalse;
  }
  void getDriverName(char *name) override { if (name) std::strcpy(name, "Rey Audio USB ASIO"); }
  long getDriverVersion() override { return 281; }
  void getErrorMessage(char *text) override {
    if (text) std::strcpy(text, stream_lost_.load() ? "USB stream ended; reopen Rey USB ASIO" : error_.data());
  }
  ASIOError start() override {
    if (!initialized_ || !callbacks_ || running_) return ASE_InvalidMode;
    stop();
    Info current; std::string error;
    if (!query_info(info_.id, current, error) || current.rate != info_.rate || current.bits != info_.bits) {
      set_error(error.empty() ? "USB format changed; reopen ASIO" : error); return ASE_NoClock;
    }
    if (!client_.connect(current, block_, lead_, error)) { set_error(error); return ASE_HWMalfunction; }
    callbacks_count_ = dropped_ = gap_max_ = processing_max_ = age_max_ = 0;
    mmcss_failures_ = 0; half_ = 0; position_ = timestamp_ = 0; stream_lost_ = false;
    ResetEvent(stop_); running_ = true;
    try { worker_ = std::thread([this] { loop(); }); }
    catch (...) { running_ = false; client_.close(); return ASE_NoMemory; }
    return ASE_OK;
  }
  ASIOError stop() override {
    if (client_.shared()) store(client_.shared()->running, 0);
    if (stop_) SetEvent(stop_);
    running_ = false;
    // Hosts normally stop from their control thread. A callback may request a
    // stop; its storage remains owned until a subsequent control-thread join.
    if (worker_.joinable() && worker_.get_id() == std::this_thread::get_id()) return ASE_OK;
    if (worker_.joinable()) worker_.join();
    snapshot_service(); client_.close(); return ASE_OK;
  }
  ASIOError getChannels(long *input, long *output) override {
    if (!input || !output) return ASE_InvalidParameter;
    *input = *output = channels; return initialized_ ? ASE_OK : ASE_NotPresent;
  }
  ASIOError getLatencies(long *input, long *output) override {
    if (!input || !output || !initialized_) return ASE_InvalidParameter;
    const auto packet_frames = (info_.rate + 7999) / 8000;
    *input = long(block_ + info_.depth * packet_frames);
    *output = long((lead_ - 1) * block_ + 1); // USB layout's rational-cadence phase frame
    return ASE_OK;
  }
  ASIOError getBufferSize(long *minimum, long *maximum, long *preferred, long *granularity) override {
    if (!minimum || !maximum || !preferred || !granularity) return ASE_InvalidParameter;
    *minimum = 16; *maximum = max_frames; *preferred = block_; *granularity = -1; return ASE_OK;
  }
  ASIOError canSampleRate(ASIOSampleRate rate) override {
    if (!std::isfinite(rate) || rate < 44100 || rate > 192000) return ASE_NoClock;
    return valid_rate(unsigned(rate)) && rate == unsigned(rate) ? ASE_OK : ASE_NoClock;
  }
  ASIOError getSampleRate(ASIOSampleRate *rate) override {
    if (!rate || !initialized_) return ASE_InvalidParameter;
    *rate = info_.rate; return ASE_OK;
  }
  ASIOError setSampleRate(ASIOSampleRate rate) override {
    if (!initialized_ || running_ || canSampleRate(rate) != ASE_OK) return ASE_NoClock;
    if (rate == info_.rate) return ASE_OK;
    std::string reply, error;
    if (!command("ASIO RATE " + info_.id + " " + std::to_string(unsigned(rate)), reply, error) || reply != "REY_ASIO_OK") {
      set_error(error.empty() ? reply : error); return ASE_NoClock;
    }
    for (unsigned attempt = 0; attempt < 30; ++attempt) {
      Info next;
      if (query_info(info_.id, next, error) && next.rate == rate) {
        info_ = next;
        if (callbacks_ && callbacks_->sampleRateDidChange) callbacks_->sampleRateDidChange(rate);
        return ASE_OK;
      }
      Sleep(100);
    }
    set_error("USB sample rate did not become ready"); return ASE_NoClock;
  }
  ASIOError getClockSources(ASIOClockSource *clocks, long *count) override {
    if (!clocks || !count || *count < 1) return ASE_InvalidParameter;
    *count = 1; clocks[0] = {}; clocks[0].index = 0;
    clocks[0].associatedChannel = clocks[0].associatedGroup = -1; clocks[0].isCurrentSource = ASIOTrue;
    std::strcpy(clocks[0].name, "Rey USB PCM clock"); return ASE_OK;
  }
  ASIOError setClockSource(long index) override { return index == 0 ? ASE_OK : ASE_InvalidParameter; }
  ASIOError getSamplePosition(ASIOSamples *pos, ASIOTimeStamp *stamp) override {
    if (!pos || !stamp) return ASE_InvalidParameter;
    const auto p = position_.load(), t = timestamp_.load();
    pos->lo = ULONG(p); pos->hi = ULONG(p >> 32); stamp->lo = ULONG(t); stamp->hi = ULONG(t >> 32); return ASE_OK;
  }
  ASIOError getChannelInfo(ASIOChannelInfo *info) override {
    if (!info || info->channel < 0 || info->channel >= long(channels)) return ASE_InvalidParameter;
    info->isActive = ASIOFalse; info->channelGroup = 0;
    for (const auto &c : buffers_) if (c.number == unsigned(info->channel) && c.input == (info->isInput != 0)) info->isActive = ASIOTrue;
    info->type = info_.bits == 16 ? ASIOSTInt16LSB : info_.bits == 24 ? ASIOSTInt24LSB : ASIOSTInt32LSB;
    std::snprintf(info->name, sizeof(info->name), "Rey USB %s %ld", info->isInput ? "input" : "output", info->channel + 1);
    return ASE_OK;
  }
  ASIOError createBuffers(ASIOBufferInfo *info, long count, long frames, ASIOCallbacks *callbacks) override {
    if (!initialized_ || running_ || !info || !callbacks || count < 1 || count > 16 ||
        !valid_block(unsigned(frames)) || !buffers_.empty() ||
        (!callbacks->bufferSwitch && !callbacks->bufferSwitchTimeInfo)) return ASE_InvalidParameter;
    for (long i = 0; i < count; ++i) {
      if (info[i].channelNum < 0 || info[i].channelNum >= long(channels)) return ASE_InvalidParameter;
      for (long j = 0; j < i; ++j) if (info[j].channelNum == info[i].channelNum && (info[j].isInput != 0) == (info[i].isInput != 0))
        return ASE_InvalidParameter;
    }
    try {
      buffers_.reserve(size_t(count)); block_ = unsigned(frames);
      for (long i = 0; i < count; ++i) {
        Channel c; c.number = unsigned(info[i].channelNum); c.input = info[i].isInput != 0;
        for (auto &half : c.buffers) { half = std::make_unique<uint8_t[]>(size_t(frames) * (info_.bits / 8)); }
        buffers_.push_back(std::move(c));
      }
    } catch (...) { buffers_.clear(); return ASE_NoMemory; }
    for (long i = 0; i < count; ++i) for (unsigned half = 0; half < 2; ++half)
      info[i].buffers[half] = buffers_[size_t(i)].buffers[half].get();
    callbacks_ = callbacks;
    time_info_ = callbacks->bufferSwitchTimeInfo && callbacks->asioMessage &&
                 callbacks->asioMessage(kAsioSupportsTimeInfo, 0, nullptr, nullptr) == 1;
    if (!time_info_ && !callbacks->bufferSwitch) { disposeBuffers(); return ASE_InvalidParameter; }
    return ASE_OK;
  }
  ASIOError disposeBuffers() override { stop(); buffers_.clear(); callbacks_ = nullptr; return ASE_OK; }
  ASIOError controlPanel() override {
    wchar_t path[32768]{}; const auto n = GetModuleFileNameW(module, path, DWORD(std::size(path)));
    if (!n || n == std::size(path)) return ASE_NotPresent;
    std::wstring file(path, n); file = file.substr(0, file.find_last_of(L"\\/") + 1) + L"ReyAudioControl.exe";
    return uintptr_t(ShellExecuteW(nullptr, L"open", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32 ? ASE_OK : ASE_NotPresent;
  }
  ASIOError future(long selector, void *data) override {
    if (selector == kAsioCanTimeInfo) return ASE_SUCCESS;
    if (selector != diagnostics_selector || !data) return ASE_NotPresent;
    auto &out = *static_cast<Diagnostics *>(data);
    if (out.size != sizeof(out) || out.abi_version != version) return ASE_InvalidParameter;
    out.running = running_; out.block = block_; out.callbacks = callbacks_count_; out.capture_dropped = dropped_;
    out.callback_gap_max_ns = gap_max_; out.callback_processing_max_ns = processing_max_;
    out.capture_to_callback_max_ns = age_max_; out.mmcss_failures = mmcss_failures_;
    // Service words are sampled only after stop/join, avoiding mapping teardown races.
    out.render_late_frames = saved_late_; out.render_missing_frames = saved_missing_;
    out.render_overflow = saved_overflow_; out.service_gap_max_ns = saved_gap_; out.capture_dropped += saved_dropped_;
    return ASE_SUCCESS;
  }
  // This frontend converts output after the direct host callback returns. It
  // does not implement an asynchronous outputReady conversion/notification.
  ASIOError outputReady() override { return ASE_NotPresent; }
private:
  void set_error(const std::string &message) { std::snprintf(error_.data(), error_.size(), "%s", message.c_str()); }
  void snapshot_service() {
    if (auto *s = client_.shared()) {
      saved_late_ = load(s->render_late); saved_missing_ = load(s->render_missing);
      saved_overflow_ = load(s->render_overflow); saved_dropped_ = load(s->capture_dropped); saved_gap_ = load(s->capture_gap_max_ns);
    }
  }
  void loop() {
    rey::RealtimeThread priority(mmcss_failures_, 1);
    SetThreadDescription(GetCurrentThread(), L"Rey USB ASIO callbacks");
    // Start the sample exchange only after the callback thread is ready. MMCSS
    // setup must not consume the first playback deadline or queue stale input.
    store(client_.shared()->running, 1);
    uint64_t base = UINT64_MAX, previous = 0;
    HANDLE events[]{stop_, client_.event()};
    while (running_ && WaitForSingleObject(stop_, 0) != WAIT_OBJECT_0) {
      if (WaitForMultipleObjects(2, events, FALSE, 1000) != WAIT_OBJECT_0 + 1) break;
      if (load(client_.shared()->ready) != 1) { stream_lost_ = true; break; }
      uint64_t dropped = 0;
      while (running_ && client_.capture(capture_, dropped)) {
        if (dropped) { dropped_.fetch_add(dropped); dropped = 0; }
        const auto now = rey::now_ns();
        if (previous) rey::max_counter(gap_max_, now - previous);
        previous = now; rey::max_counter(age_max_, now - capture_.timestamp_ns);
        if (base == UINT64_MAX) base = capture_.frame_position;
        position_ = capture_.frame_position - base; timestamp_ = capture_.timestamp_ns;
        const auto width = info_.bits / 8;
        for (auto &c : buffers_) {
          auto *data = c.buffers[half_].get();
          if (!c.input) std::memset(data, 0, size_t(block_) * width);
          else for (unsigned f = 0; f < block_; ++f)
            pack(data + size_t(f) * width, capture_.pcm[size_t(f) * channels + c.number], info_.bits);
        }
        if (time_info_) {
          ASIOTime time{};
          time.timeInfo.sampleRate = info_.rate;
          time.timeInfo.flags = kSystemTimeValid | kSamplePositionValid | kSampleRateValid;
          time.timeInfo.samplePosition.lo = ULONG(position_.load()); time.timeInfo.samplePosition.hi = ULONG(position_.load() >> 32);
          time.timeInfo.systemTime.lo = ULONG(capture_.timestamp_ns); time.timeInfo.systemTime.hi = ULONG(capture_.timestamp_ns >> 32);
          callbacks_->bufferSwitchTimeInfo(&time, half_, ASIOTrue);
        } else callbacks_->bufferSwitch(half_, ASIOTrue);
        render_.frame_position = capture_.frame_position + uint64_t(lead_) * block_;
        render_.timestamp_ns = now; render_.frames = block_; render_.flags = 0;
        std::fill_n(render_.pcm, size_t(block_) * channels, 0);
        for (const auto &c : buffers_) if (!c.input) for (unsigned f = 0; f < block_; ++f)
          render_.pcm[size_t(f) * channels + c.number] = unpack(c.buffers[half_].get() + size_t(f) * width, info_.bits);
        client_.render(render_); half_ ^= 1; ++callbacks_count_;
        rey::max_counter(processing_max_, rey::now_ns() - now);
      }
      if (dropped) dropped_.fetch_add(dropped);
    }
    if (running_ && WaitForSingleObject(stop_, 0) != WAIT_OBJECT_0) stream_lost_ = true;
    running_ = false;
  }
  std::atomic<ULONG> references_{1};
  Info info_;
  Client client_;
  std::vector<Channel> buffers_;
  ASIOCallbacks *callbacks_ = nullptr;
  HANDLE stop_ = nullptr;
  std::thread worker_;
  std::atomic<bool> running_{false};
  bool initialized_ = false, time_info_ = false;
  std::atomic<bool> stream_lost_{false};
  unsigned block_ = 64, lead_ = 3, half_ = 0;
  std::array<char, 124> error_{"No error"};
  Slot capture_{}, render_{};
  std::atomic<uint64_t> position_{0}, timestamp_{0}, callbacks_count_{0}, dropped_{0};
  std::atomic<uint64_t> gap_max_{0}, processing_max_{0}, age_max_{0}, mmcss_failures_{0};
  uint64_t saved_late_ = 0, saved_missing_ = 0, saved_overflow_ = 0, saved_dropped_ = 0, saved_gap_ = 0;
};

class Factory final : public IClassFactory {
public:
  Factory() { ++objects; }
  ~Factory() { --objects; }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
    if (!out) return E_POINTER; *out = nullptr;
    if (id != IID_IUnknown && id != IID_IClassFactory) return E_NOINTERFACE;
    *out = this; AddRef(); return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
  ULONG STDMETHODCALLTYPE Release() override { auto n = --refs_; if (!n) delete this; return n; }
  HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown *outer, REFIID id, void **out) override {
    if (outer) return CLASS_E_NOAGGREGATION;
    try { auto *driver = new Driver(); const auto hr = driver->QueryInterface(id, out); driver->Release(); return hr; }
    catch (...) { return E_OUTOFMEMORY; }
  }
  HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override { objects += lock ? 1 : -1; return S_OK; }
private: std::atomic<ULONG> refs_{1};
};
} // namespace rey::asio

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) { rey::asio::module = instance; DisableThreadLibraryCalls(instance); }
  return TRUE;
}
extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID id, REFIID iid, void **out) {
  if (id != rey::asio::clsid) return CLASS_E_CLASSNOTAVAILABLE;
  try { auto *factory = new rey::asio::Factory(); const auto hr = factory->QueryInterface(iid, out); factory->Release(); return hr; }
  catch (...) { return E_OUTOFMEMORY; }
}
extern "C" HRESULT __stdcall DllCanUnloadNow() { return rey::asio::objects == 0 ? S_OK : S_FALSE; }

namespace {
constexpr auto class_path = L"Software\\Classes\\CLSID\\{91BA2C4A-56BC-4C49-A499-231979EC5F60}";
constexpr auto asio_path = L"Software\\ASIO\\Rey Audio USB ASIO";
HRESULT registry(const std::wstring &path, const wchar_t *name, const std::wstring &value) {
  HKEY key = nullptr;
  auto error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &key, nullptr);
  if (!error) { error = RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE *>(value.c_str()), DWORD((value.size() + 1) * sizeof(wchar_t))); RegCloseKey(key); }
  return HRESULT_FROM_WIN32(error);
}
}
extern "C" HRESULT __stdcall DllRegisterServer() {
  wchar_t dll[32768]{}; const auto length = GetModuleFileNameW(rey::asio::module, dll, DWORD(std::size(dll)));
  if (!length || length == std::size(dll)) return E_FAIL;
  HRESULT result = registry(class_path, nullptr, L"Rey Audio USB ASIO");
  if (SUCCEEDED(result)) result = registry(std::wstring(class_path) + L"\\InprocServer32", nullptr, dll);
  if (SUCCEEDED(result)) result = registry(std::wstring(class_path) + L"\\InprocServer32", L"ThreadingModel", L"Both");
  if (SUCCEEDED(result)) result = registry(asio_path, L"CLSID", L"{91BA2C4A-56BC-4C49-A499-231979EC5F60}");
  if (SUCCEEDED(result)) result = registry(asio_path, L"Description", L"Rey Audio USB ASIO");
  return result;
}
extern "C" HRESULT __stdcall DllUnregisterServer() {
  RegDeleteTreeW(HKEY_LOCAL_MACHINE, asio_path); RegDeleteTreeW(HKEY_LOCAL_MACHINE, class_path); return S_OK;
}
