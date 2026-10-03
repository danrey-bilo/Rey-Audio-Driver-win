#include "../src/asio/ipc_protocol.hpp"
#include "../src/platform/realtime.hpp"
#include <objbase.h>
#include "iasiodrv.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
struct Pulse { uint64_t sent = 0, rtt = 0; bool received = false; };
std::vector<Pulse> pulses;
std::array<ASIOBufferInfo, 16> buffers{};
IASIO *driver = nullptr;
unsigned block = 64, rate = 192000, bits = 32, pulse_id = 0;
uint64_t callbacks = 0, frames = 0, next_pulse = 0, invalid = 0, duplicates = 0, position_backwards = 0;
uint64_t first_ns = 0, last_ns = 0, previous_position = 0;
bool dense_markers = false;
int32_t sample(const uint8_t *data) {
  uint32_t value = 0; for (unsigned byte = 0; byte < bits / 8; ++byte) value |= uint32_t(data[byte]) << (byte * 8);
  if (bits < 32 && (value & (uint32_t(1) << (bits - 1)))) value |= ~((uint32_t(1) << bits) - 1);
  int32_t signed_value; std::memcpy(&signed_value, &value, sizeof(value)); return signed_value;
}
void store(uint8_t *data, int32_t value) {
  for (unsigned byte = 0; byte < bits / 8; ++byte) data[byte] = uint8_t(uint32_t(value) >> (byte * 8));
}
int32_t marker(unsigned id, unsigned ch) { return int32_t(id + ch * 997) * (ch & 1 ? -1 : 1); }
void process(long half, ASIOBool) {
  const auto now = rey::now_ns();
  if (!first_ns) first_ns = now;
  last_ns = now;
  ASIOSamples position{}; ASIOTimeStamp stamp{};
  if (driver->getSamplePosition(&position, &stamp) == ASE_OK) {
    const uint64_t p = (uint64_t(position.hi) << 32) | position.lo;
    if (callbacks && p < previous_position) ++position_backwards;
    previous_position = p;
  }
  const auto width = bits / 8;
  for (unsigned f = 0; f < block; ++f) {
    const auto id = sample(static_cast<uint8_t *>(buffers[0].buffers[half]) + size_t(f) * width);
    if (!id) continue;
    if (id < 1 || unsigned(id) >= pulses.size() || !pulses[unsigned(id)].sent) { ++invalid; continue; }
    auto &pulse = pulses[unsigned(id)];
    bool correct = true;
    for (unsigned ch = 0; ch < 8; ++ch)
      correct &= sample(static_cast<uint8_t *>(buffers[ch].buffers[half]) + size_t(f) * width) == marker(unsigned(id), ch);
    if (!correct) ++invalid;
    else if (pulse.received) ++duplicates;
    else { pulse.received = true; pulse.rtt = now - pulse.sent; }
  }
  for (unsigned ch = 0; ch < 8; ++ch)
    std::memset(buffers[ch + 8].buffers[half], 0, size_t(block) * width);
  if (now - first_ns >= 50000000 && frames >= next_pulse && pulse_id + 1 < pulses.size()) {
    ++pulse_id; pulses[pulse_id].sent = now;
    for (unsigned ch = 0; ch < 8; ++ch)
      store(static_cast<uint8_t *>(buffers[ch + 8].buffers[half]), marker(pulse_id, ch));
    next_pulse = frames + (dense_markers ? block : rate / 50);
  }
  frames += block; ++callbacks;
}
ASIOTime *time_process(ASIOTime *time, long half, ASIOBool direct) { process(half, direct); return time; }
void changed(ASIOSampleRate) {}
long message(long selector, long value, void *, double *) {
  if (selector == kAsioSelectorSupported) return value == kAsioSupportsTimeInfo || value == kAsioEngineVersion;
  if (selector == kAsioSupportsTimeInfo) return 1;
  if (selector == kAsioEngineVersion) return 2;
  return 0;
}
bool check(ASIOError result, const char *action) {
  if (result == ASE_OK || result == ASE_SUCCESS) return true;
  char error[124]{}; if (driver) driver->getErrorMessage(error);
  std::printf("ASIO_ERROR action=%s code=%ld message=%s\n", action, long(result), error); return false;
}
}
int wmain(int argc, wchar_t **argv) {
  std::wstring dll;
  unsigned seconds = 5, requested_rate = 0;
  bool registered = false;
  for (int i = 1; i < argc; ++i) {
    const std::wstring arg = argv[i];
    if (arg == L"--registered") { registered = true; continue; }
    if (arg == L"--dense-markers") { dense_markers = true; continue; }
    if (i + 1 >= argc) return 1;
    if (arg == L"--dll") dll = argv[++i];
    else if (arg == L"--seconds") seconds = unsigned(_wtoi(argv[++i]));
    else if (arg == L"--block") block = unsigned(_wtoi(argv[++i]));
    else if (arg == L"--rate") requested_rate = unsigned(_wtoi(argv[++i]));
    else return 1;
  }
  if ((dll.empty() == !registered) || !seconds || seconds > 175 || !rey::asio::valid_block(block)) return 1;
  if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 2;
  HMODULE library = nullptr;
  HRESULT created = E_FAIL;
  if (registered) {
    created = CoCreateInstance(rey::asio::clsid, nullptr, CLSCTX_INPROC_SERVER,
                               rey::asio::clsid, reinterpret_cast<void **>(&driver));
  } else {
    library = LoadLibraryExW(dll.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!library) { std::printf("ASIO_LOAD_ERROR %lu\n", GetLastError()); return 3; }
    const auto factory_fn = reinterpret_cast<HRESULT (WINAPI *)(REFCLSID, REFIID, void **)>(GetProcAddress(library, "DllGetClassObject"));
    IClassFactory *factory = nullptr;
    if (!factory_fn || FAILED(factory_fn(rey::asio::clsid, IID_IClassFactory, reinterpret_cast<void **>(&factory)))) return 4;
    created = factory->CreateInstance(nullptr, rey::asio::clsid, reinterpret_cast<void **>(&driver));
    factory->Release();
  }
  if (FAILED(created) || !driver) { std::printf("ASIO_CREATE_ERROR registered=%u hr=%08lx\n", unsigned(registered), ULONG(created)); return 5; }
  int result = [&] {
    if (!driver->init(nullptr)) { char error[124]{}; driver->getErrorMessage(error); std::printf("ASIO_INIT_ERROR %s\n", error); return 6; }
    if (requested_rate && !check(driver->setSampleRate(requested_rate), "rate")) return 7;
    ASIOSampleRate active_rate = 0;
    long inputs = 0, outputs = 0;
    if (!check(driver->getChannels(&inputs, &outputs), "channels") || inputs != 8 || outputs != 8 ||
        !check(driver->getSampleRate(&active_rate), "get_rate")) return 8;
    rate = unsigned(active_rate);
    ASIOChannelInfo channel{}; channel.isInput = ASIOTrue; channel.channel = 0;
    if (!check(driver->getChannelInfo(&channel), "channel")) return 9;
    if (channel.type == ASIOSTInt16LSB) bits = 16;
    else if (channel.type == ASIOSTInt24LSB) bits = 24;
    else if (channel.type == ASIOSTInt32LSB) bits = 32;
    else return 10;
    const auto pulse_capacity = dense_markers ?
        (uint64_t(rate) * (seconds + 1) + block - 1) / block + 32 : 16384;
    if (pulse_capacity + 7 * 997 >= (uint64_t(1) << (bits - 1))) {
      std::printf("ASIO_MARKER_ERROR dense markers exceed the PCM word range\n");
      return 10;
    }
    // Allocate and touch every marker record before streaming. The callback
    // never allocates; dense mode measures a marker in every ASIO block.
    pulses.resize(size_t(pulse_capacity));
    for (unsigned i = 0; i < buffers.size(); ++i) {
      buffers[i].isInput = i < 8 ? ASIOTrue : ASIOFalse; buffers[i].channelNum = long(i % 8);
    }
    ASIOCallbacks callbacks_api{process, changed, message, time_process};
    if (!check(driver->createBuffers(buffers.data(), long(buffers.size()), block, &callbacks_api), "buffers")) return 11;
    long input_latency = 0, output_latency = 0;
    driver->getLatencies(&input_latency, &output_latency);
    std::printf("ASIO_PROFILE rate=%u bits=%u channels=8x8 block=%u input_latency_frames=%ld output_latency_frames=%ld\n",
                rate, bits, block, input_latency, output_latency);
    if (!check(driver->start(), "start")) return 12;
    const auto begin = rey::now_ns();
    Sleep(seconds * 1000);
    rey::asio::Diagnostics live;
    if (!check(driver->future(rey::asio::diagnostics_selector, &live), "live_diagnostics")) return 14;
    const bool stream_completed = live.running != 0;
    if (!check(driver->stop(), "stop")) return 13;
    const auto ended = rey::now_ns();
    rey::asio::Diagnostics stats;
    if (!check(driver->future(rey::asio::diagnostics_selector, &stats), "diagnostics")) return 14;
    std::vector<uint64_t> rtts;
    unsigned missing = 0;
    for (unsigned id = 1; id <= pulse_id; ++id) {
      if (pulses[id].received) rtts.push_back(pulses[id].rtt);
      else if (pulses[id].sent + 10000000 < ended) ++missing;
    }
    std::sort(rtts.begin(), rtts.end());
    auto percentile = [&](double p) { return rtts.empty() ? 0.0 : rtts[size_t(p * (rtts.size() - 1))] / 1000.0; };
    const bool okay = stream_completed && callbacks > 100 && rtts.size() > 20 && !invalid && !duplicates && !missing &&
      !position_backwards && !stats.capture_dropped && !stats.render_late_frames && !stats.render_missing_frames &&
      !stats.render_overflow && !stats.mmcss_failures;
    std::printf("ASIO_RESULT seconds=%.3f callbacks=%llu frames=%llu pulses=%u received=%zu missing=%u invalid=%llu duplicates=%llu "
      "rtt_p50_us=%.3f rtt_p95_us=%.3f rtt_p99_us=%.3f rtt_max_us=%.3f capture_dropped=%llu render_late_frames=%llu "
      "render_missing_frames=%llu render_overflow=%llu callback_gap_max_us=%.3f processing_max_us=%.3f "
      "capture_age_max_us=%.3f service_gap_max_us=%.3f positions_backwards=%llu mmcss_failures=%llu "
      "cadence_seconds=%.9f cadence_frames=%llu dense_markers=%u "
      "okay=%u physical_audio=0 ableton_host=0\n", (ended - begin) / 1e9,
      (unsigned long long)callbacks, (unsigned long long)frames, pulse_id, rtts.size(), missing,
      (unsigned long long)invalid, (unsigned long long)duplicates,
      percentile(.5), percentile(.95), percentile(.99), percentile(1),
      (unsigned long long)stats.capture_dropped, (unsigned long long)stats.render_late_frames,
      (unsigned long long)stats.render_missing_frames, (unsigned long long)stats.render_overflow,
      stats.callback_gap_max_ns / 1000.0, stats.callback_processing_max_ns / 1000.0,
      stats.capture_to_callback_max_ns / 1000.0, stats.service_gap_max_ns / 1000.0,
      (unsigned long long)position_backwards, (unsigned long long)stats.mmcss_failures,
      (last_ns - first_ns) / 1e9, (unsigned long long)((callbacks ? callbacks - 1 : 0) * block),
      unsigned(dense_markers), unsigned(okay));
    driver->disposeBuffers(); return okay ? 0 : 15;
  }();
  driver->Release(); driver = nullptr; FreeLibrary(library); CoUninitialize(); return result;
}
