#include "../src/asio/ipc_protocol.hpp"
#include "../src/platform/realtime.hpp"
#include <objbase.h>
#include "iasiodrv.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>

namespace {
struct Pulse { uint64_t sent = 0, rtt = 0, position = 0, rtt_frames = 0; bool received = false; };
std::vector<Pulse> pulses;
std::array<ASIOBufferInfo, 16> buffers{};
IASIO *driver = nullptr;
unsigned block = 64, rate = 192000, bits = 32, pulse_id = 0;
uint64_t callbacks = 0, frames = 0, next_pulse = 0, invalid = 0, duplicates = 0, position_backwards = 0;
uint64_t first_ns = 0, last_ns = 0, previous_position = 0;
bool dense_markers = false, stream_only = false;
bool channel_tones = false;
bool pulse_loop = false;
uint64_t rejected_peaks = 0;
uint64_t detection_after = 0;
std::atomic<uint64_t> emit_until_ns{UINT64_MAX};
std::atomic<bool> marker_gate{true};
std::vector<int32_t> recorded;
constexpr double pi = 3.14159265358979323846;
double tone_frequency(unsigned channel) { return 401.0 + channel * 113.0; }
std::vector<uint64_t> callback_gaps;
size_t gap_count = 0;
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
  if (last_ns && gap_count < callback_gaps.size()) callback_gaps[gap_count++] = now - last_ns;
  last_ns = now;
  ASIOSamples position{}; ASIOTimeStamp stamp{};
  if (driver->getSamplePosition(&position, &stamp) == ASE_OK) {
    const uint64_t p = (uint64_t(position.hi) << 32) | position.lo;
    if (callbacks && p < previous_position) ++position_backwards;
    previous_position = p;
  }
  const auto width = bits / 8;
  if (pulse_loop) for (unsigned f = 0; f < block; ++f) {
    const double value = double(sample(static_cast<uint8_t *>(buffers[0].buffers[half])+size_t(f)*width)) /
        double(uint64_t(1) << (bits-1));
    if (value < .002 || previous_position+f < detection_after || !pulse_id) continue;
    unsigned code = 0;
    bool correct = true;
    for (unsigned ch = 1; ch < 8; ++ch) {
      const double other = double(sample(static_cast<uint8_t *>(buffers[ch].buffers[half])+size_t(f)*width)) /
          double(uint64_t(1) << (bits-1));
      correct &= std::abs(std::abs(other/value) - double(8-ch)/8) < .04;
      if(other<0)code|=1u<<(ch-1);
    }
    if (!correct) { ++rejected_peaks; continue; }
    // The seven channel signs identify a marker independently of earlier
    // markers. A missing pulse must not turn every later RTT into a growing
    // multiple of the marker interval.
    unsigned id = 0;
    for(unsigned candidate=pulse_id;candidate && candidate+64>pulse_id;--candidate)
      if((candidate&127u)==code && pulses[candidate].sent<=now &&
         now-pulses[candidate].sent<1000000000ull){id=candidate;break;}
    if(!id){++rejected_peaks;continue;}
    auto &pulse=pulses[id];
    if(pulse.received){++duplicates;continue;}
    pulse.received = true; pulse.rtt = now-pulse.sent;
    pulse.rtt_frames = previous_position+f-pulse.position;
    detection_after = previous_position+f+rate/1000;
  }
  for (unsigned f = 0; !stream_only && !pulse_loop && f < block; ++f) {
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
  if (channel_tones) {
    const double amplitude = double(uint64_t(1) << (bits-1)) * .001;
    for (unsigned f = 0; f < block; ++f) for (unsigned ch = 0; ch < 8; ++ch) {
      recorded[size_t((frames+f) % (rate*2))*8+ch] = sample(static_cast<uint8_t *>(buffers[ch].buffers[half]) + size_t(f)*width);
      store(static_cast<uint8_t *>(buffers[ch+8].buffers[half]) + size_t(f)*width,
            int32_t(amplitude * std::sin(2*pi*tone_frequency(ch)*double((frames+f) % rate)/rate)));
    }
  }
  if ((!stream_only || pulse_loop) && marker_gate.load(std::memory_order_relaxed) && now < emit_until_ns.load(std::memory_order_relaxed) && now - first_ns >= (pulse_loop ? 5000000000ull : 50000000ull) && frames >= next_pulse && pulse_id + 1 < pulses.size()) {
    ++pulse_id; pulses[pulse_id].sent = now;
    pulses[pulse_id].position = previous_position;
    for (unsigned ch = 0; ch < 8; ++ch) {
      if (pulse_loop) {
        const auto value = int32_t(double(uint64_t(1) << (bits-1))*.005*double(8-ch)/8) *
            (ch && (pulse_id&(1u<<(ch-1))) ? -1 : 1);
        for (unsigned f = 0; f < 4; ++f) store(static_cast<uint8_t *>(buffers[ch+8].buffers[half])+size_t(f)*width,value);
      } else store(static_cast<uint8_t *>(buffers[ch + 8].buffers[half]), marker(pulse_id, ch));
    }
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
  std::wstring ready_file;
  unsigned seconds = 5, requested_rate = 0;
  bool registered = false;
  for (int i = 1; i < argc; ++i) {
    const std::wstring arg = argv[i];
    if (arg == L"--registered") { registered = true; continue; }
    if (arg == L"--dense-markers") { dense_markers = true; continue; }
    if (arg == L"--stream-only") { stream_only = true; continue; }
    if (arg == L"--channel-tones") { channel_tones = stream_only = true; continue; }
    if (arg == L"--pulse-loop") { pulse_loop = true; continue; }
    if (i + 1 >= argc) return 1;
    if (arg == L"--dll") dll = argv[++i];
    else if (arg == L"--pulse-ready-file") {ready_file=argv[++i];marker_gate=false;}
    else if (arg == L"--seconds") seconds = unsigned(_wtoi(argv[++i]));
    else if (arg == L"--block") block = unsigned(_wtoi(argv[++i]));
    else if (arg == L"--rate") requested_rate = unsigned(_wtoi(argv[++i]));
    else return 1;
  }
  if ((dll.empty() == !registered) || !seconds || seconds > 175 || !rey::asio::valid_block(block) ||
      (pulse_loop && (seconds < 10 || dense_markers || stream_only))) return 1;
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
    callback_gaps.resize(size_t(uint64_t(rate) * (seconds + 2) / block + 1024));
    if (channel_tones) recorded.resize(size_t(rate)*2*8);
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
    emit_until_ns = begin+uint64_t(seconds)*1000000000ull-200000000ull;
    while(rey::now_ns()<begin+uint64_t(seconds)*1000000000ull) {
      if(!marker_gate.load(std::memory_order_relaxed) && !ready_file.empty()) {
        std::ifstream input{std::filesystem::path(ready_file)};
        std::array<char,16384> bytes{};input.read(bytes.data(),bytes.size());
        std::string text(bytes.data(),size_t(input.gcount()));
        text.erase(std::remove(text.begin(),text.end(),'\0'),text.end());
        if(text.find("JACK_USB_CHECK ready;")!=std::string::npos) {
          marker_gate=true;
          std::printf("ASIO_FIXTURE_READY seconds_since_start=%.3f\n",(rey::now_ns()-begin)/1e9);
        }
      }
      Sleep(20);
    }
    rey::asio::Diagnostics live;
    if (!check(driver->future(rey::asio::diagnostics_selector, &live), "live_diagnostics")) return 14;
    const bool stream_completed = live.running != 0;
    if (!check(driver->stop(), "stop")) return 13;
    const auto ended = rey::now_ns();
    rey::asio::Diagnostics stats;
    if (!check(driver->future(rey::asio::diagnostics_selector, &stats), "diagnostics")) return 14;
    std::vector<uint64_t> rtts;
    std::vector<uint64_t> frame_rtts;
    unsigned missing = 0;
    for (unsigned id = 1; id <= pulse_id; ++id) {
      if (pulses[id].received) { rtts.push_back(pulses[id].rtt); frame_rtts.push_back(pulses[id].rtt_frames); }
      else if (pulses[id].sent + (pulse_loop ? 150000000ull : 10000000ull) < ended) ++missing;
    }
    std::sort(rtts.begin(), rtts.end());
    std::sort(frame_rtts.begin(), frame_rtts.end());
    auto percentile = [&](double p) { return rtts.empty() ? 0.0 : rtts[size_t(p * (rtts.size() - 1))] / 1000.0; };
    auto sample_percentile = [&](double p) { return frame_rtts.empty() ? 0.0 : frame_rtts[size_t(p*(frame_rtts.size()-1))]*1000000.0/rate; };
    callback_gaps.resize(gap_count); std::sort(callback_gaps.begin(), callback_gaps.end());
    auto gap_percentile = [&](double p) { return callback_gaps.empty() ? 0.0 : callback_gaps[size_t(p * (callback_gaps.size()-1))] / 1000.0; };
    bool channels_okay = true;
    if (channel_tones) {
      const auto count = size_t(rate)*2;
      for (unsigned ch = 0; ch < 8; ++ch) {
        double own = 0, other = 0;
        for (unsigned tone = 0; tone < 8; ++tone) {
          const double coefficient = 2*std::cos(2*pi*tone_frequency(tone)/rate);
          double previous = 0, older = 0;
          for (size_t n = 0; n < count; ++n) {
            const auto next = double(recorded[n*8+ch]) + coefficient*previous-older;
            older = previous; previous = next;
          }
          const double amplitude = 2*std::sqrt(std::max(0.0, previous*previous + older*older - coefficient*previous*older)) /
              count / double(uint64_t(1) << (bits-1));
          if (ch == tone) own = amplitude; else other = std::max(other, amplitude);
        }
        const bool okay = frames >= count && own > .0005 && own < .0015 && other < own*.1;
        channels_okay &= okay;
        std::printf("ASIO_CHANNEL input=%u expected_hz=%.0f amplitude=%.8f other_channel_tone=%.8f okay=%u\n",
                    ch+1, tone_frequency(ch), own, other, unsigned(okay));
      }
    }
    const bool okay = channels_okay && stream_completed && callbacks > 100 && (stream_only || rtts.size() > 20) && !invalid && !duplicates && !missing &&
      !position_backwards && !stats.capture_dropped && !stats.render_late_frames && !stats.render_missing_frames &&
      !stats.render_overflow && !stats.mmcss_failures;
    if (pulse_loop) std::printf("ASIO_SAMPLE_RTT p50_us=%.3f p95_us=%.3f p99_us=%.3f max_us=%.3f rejected_peak_candidates=%llu digital_jack_loop=1 physical_audio=0\n",
                               sample_percentile(.5),sample_percentile(.95),sample_percentile(.99),sample_percentile(1),(unsigned long long)rejected_peaks);
    std::printf("ASIO_RESULT seconds=%.3f callbacks=%llu frames=%llu pulses=%u received=%zu missing=%u invalid=%llu duplicates=%llu "
      "rtt_p50_us=%.3f rtt_p95_us=%.3f rtt_p99_us=%.3f rtt_max_us=%.3f capture_dropped=%llu render_late_frames=%llu "
      "render_missing_frames=%llu render_overflow=%llu callback_gap_max_us=%.3f processing_max_us=%.3f "
      "capture_age_max_us=%.3f service_gap_max_us=%.3f positions_backwards=%llu mmcss_failures=%llu "
      "cadence_seconds=%.9f cadence_frames=%llu dense_markers=%u stream_only=%u gap_p50_us=%.3f gap_p95_us=%.3f gap_p99_us=%.3f gap_max_us=%.3f "
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
      unsigned(dense_markers), unsigned(stream_only), gap_percentile(.5), gap_percentile(.95), gap_percentile(.99), gap_percentile(1), unsigned(okay));
    driver->disposeBuffers(); return okay ? 0 : 15;
  }();
  driver->Release(); driver = nullptr; FreeLibrary(library); CoUninitialize(); return result;
}
