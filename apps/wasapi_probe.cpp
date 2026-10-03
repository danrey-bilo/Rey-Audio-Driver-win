#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <audioclient.h>
#include <mmreg.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <avrt.h>
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>

namespace {
template <class T> class ComPtr {
  T *value_ = nullptr;

public:
  ~ComPtr() {
    if (value_)
      value_->Release();
  }
  T *operator->() const {
    return value_;
  }
  T **put() {
    return &value_;
  }
  T *get() const {
    return value_;
  }
};
struct Handle {
  HANDLE value = nullptr;
  ~Handle() {
    if (value)
      CloseHandle(value);
  }
};
struct Format {
  WAVEFORMATEX *value = nullptr;
  ~Format() {
    CoTaskMemFree(value);
  }
};
struct Realtime {
  HANDLE value = nullptr;
  Realtime() {
    DWORD index = 0;
    value = AvSetMmThreadCharacteristicsW(L"Pro Audio", &index);
    if (value)
      AvSetMmThreadPriority(value, AVRT_PRIORITY_CRITICAL);
  }
  ~Realtime() {
    if (value)
      AvRevertMmThreadCharacteristics(value);
  }
};
struct Options {
  bool all = false, raw = false;
  unsigned seconds = 0, period = 0;
  EDataFlow flow = eAll;
  std::wstring id;
};
uint64_t ticks() {
  LARGE_INTEGER value{};
  QueryPerformanceCounter(&value);
  return uint64_t(value.QuadPart);
}
uint64_t frequency() {
  LARGE_INTEGER value{};
  QueryPerformanceFrequency(&value);
  return uint64_t(value.QuadPart);
}
bool check(HRESULT result, const char *operation) {
  if (SUCCEEDED(result))
    return true;
  std::printf("WASAPI_ERROR operation=%s hresult=0x%08lx\n", operation, ULONG(result));
  return false;
}
std::string utf8(const wchar_t *value) {
  const int length = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
  if (!length)
    return {};
  std::string result(size_t(length), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), length, nullptr, nullptr);
  result.pop_back();
  return result;
}
bool matches(const wchar_t *name) {
  std::wstring lower(name);
  lower.erase(
      std::remove_if(lower.begin(), lower.end(),
                     [](wchar_t ch) { return std::iswspace(ch) || ch == L'-' || ch == L'_'; }),
      lower.end());
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](wchar_t ch) { return std::towlower(ch); });
  return lower.find(L"piaoip") != std::wstring::npos;
}
bool number(const wchar_t *value, unsigned &result, unsigned maximum) {
  if (*value < L'0' || *value > L'9')
    return false;
  wchar_t *end = nullptr;
  const auto parsed = std::wcstoul(value, &end, 10);
  if (*end || !parsed || parsed > maximum)
    return false;
  result = unsigned(parsed);
  return true;
}
bool stream(IAudioClient3 *client, const WAVEFORMATEX *format, EDataFlow flow, unsigned period,
            unsigned seconds) {
  if (!check(client->InitializeSharedAudioStream(AUDCLNT_STREAMFLAGS_EVENTCALLBACK, period, format,
                                                 nullptr),
             "initialize_shared"))
    return false;
  Handle event{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
  if (!event.value || !check(client->SetEventHandle(event.value), "set_event"))
    return false;
  UINT32 capacity = 0;
  if (!check(client->GetBufferSize(&capacity), "buffer_size"))
    return false;
  ComPtr<IAudioRenderClient> render;
  ComPtr<IAudioCaptureClient> capture;
  if (flow == eRender) {
    if (!check(client->GetService(__uuidof(IAudioRenderClient),
                                  reinterpret_cast<void **>(render.put())),
               "render_service"))
      return false;
    BYTE *buffer = nullptr;
    if (!check(render->GetBuffer(capacity, &buffer), "render_prefill") ||
        !check(render->ReleaseBuffer(capacity, AUDCLNT_BUFFERFLAGS_SILENT),
               "render_prefill_release"))
      return false;
  } else if (!check(client->GetService(__uuidof(IAudioCaptureClient),
                                       reinterpret_cast<void **>(capture.put())),
                    "capture_service"))
    return false;

  // Fixed capacity: recording timing never grows storage in the audio loop.
  std::vector<uint64_t> gaps(400000);
  size_t gap_count = 0;
  uint64_t frames = 0, events = 0, discontinuities = 0, timestamp_errors = 0, silent = 0;
  uint64_t lost_timing = 0, last = 0;
  bool okay = true;
  const auto hz = frequency();
  const auto start = ticks();
  const auto end = start + uint64_t(seconds) * hz;
  Realtime realtime;
  if (!check(client->Start(), "start"))
    return false;
  while (ticks() < end) {
    const auto wait = WaitForSingleObject(event.value, 500);
    if (wait != WAIT_OBJECT_0) {
      std::printf("WASAPI_WAIT result=%lu\n", wait);
      okay = false;
      break;
    }
    const auto now = ticks();
    if (last) {
      if (gap_count < gaps.size())
        gaps[gap_count++] = now - last;
      else
        ++lost_timing;
    }
    last = now;
    ++events;
    if (flow == eRender) {
      UINT32 padding = 0;
      if (!check(client->GetCurrentPadding(&padding), "padding") || padding > capacity) {
        okay = false;
        break;
      }
      const UINT32 available = capacity - padding;
      if (available) {
        BYTE *buffer = nullptr;
        if (!check(render->GetBuffer(available, &buffer), "render_buffer") ||
            !check(render->ReleaseBuffer(available, AUDCLNT_BUFFERFLAGS_SILENT),
                   "render_release")) {
          okay = false;
          break;
        }
        frames += available;
      }
    } else {
      UINT32 available = 0;
      if (!check(capture->GetNextPacketSize(&available), "capture_size")) {
        okay = false;
        break;
      }
      while (available) {
        BYTE *buffer = nullptr;
        UINT32 count = 0;
        DWORD flags = 0;
        UINT64 device_position = 0, qpc_position = 0;
        if (!check(capture->GetBuffer(&buffer, &count, &flags, &device_position, &qpc_position),
                   "capture_buffer")) {
          okay = false;
          break;
        }
        frames += count;
        discontinuities += (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0;
        timestamp_errors += (flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) != 0;
        silent += (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
        if (!check(capture->ReleaseBuffer(count), "capture_release") ||
            !check(capture->GetNextPacketSize(&available), "capture_size")) {
          okay = false;
          break;
        }
      }
      if (!okay)
        break;
    }
  }
  okay = check(client->Stop(), "stop") && okay;
  gaps.resize(gap_count);
  std::sort(gaps.begin(), gaps.end());
  const auto percentile = [&](double fraction) {
    if (gaps.empty())
      return 0.0;
    return double(gaps[std::min(gaps.size() - 1, size_t(fraction * double(gaps.size() - 1)))]) *
           1e6 / hz;
  };
  std::printf("WASAPI_STREAM flow=%s seconds=%.3f period=%u capacity=%u frames=%llu events=%llu "
              "discontinuities=%llu timestamp_errors=%llu silent_packets=%llu "
              "event_gap_p50_us=%.1f event_gap_p95_us=%.1f event_gap_p99_us=%.1f "
              "event_gap_max_us=%.1f timing_overflow=%llu mmcss=%u okay=%u\n",
              flow == eRender ? "render" : "capture", double(ticks() - start) / hz, period,
              capacity, frames, events, discontinuities, timestamp_errors, silent, percentile(0.50),
              percentile(0.95), percentile(0.99), percentile(1), lost_timing,
              unsigned(realtime.value != nullptr), unsigned(okay));
  return okay;
}
bool inspect(IMMDevice *device, EDataFlow flow, const Options &options, const wchar_t *name) {
  ComPtr<IAudioClient3> client;
  if (!check(device->Activate(__uuidof(IAudioClient3), CLSCTX_ALL, nullptr,
                              reinterpret_cast<void **>(client.put())),
             "activate_client3"))
    return false;
  AudioClientProperties properties{};
  properties.cbSize = sizeof(properties);
  properties.eCategory = AudioCategory_Media;
  properties.Options = options.raw ? AUDCLNT_STREAMOPTIONS_RAW : AUDCLNT_STREAMOPTIONS_NONE;
  if (!check(client->SetClientProperties(&properties), "client_properties"))
    return false;
  Format format;
  if (!check(client->GetMixFormat(&format.value), "mix_format"))
    return false;
  UINT32 normal = 0, fundamental = 0, minimum = 0, maximum = 0;
  if (!check(client->GetSharedModeEnginePeriod(format.value, &normal, &fundamental, &minimum,
                                               &maximum),
             "engine_period"))
    return false;
  REFERENCE_TIME device_default = 0, device_minimum = 0, latency = 0;
  client->GetDevicePeriod(&device_default, &device_minimum);
  const auto latency_result = client->GetStreamLatency(&latency);
  unsigned valid_bits = format.value->wBitsPerSample;
  if (format.value->wFormatTag == WAVE_FORMAT_EXTENSIBLE && format.value->cbSize >= 22)
    valid_bits =
        reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(format.value)->Samples.wValidBitsPerSample;
  std::printf(
      "WASAPI_PERIOD name=\"%s\" flow=%s rate=%lu channels=%u container_bits=%u "
      "valid_bits=%u default=%u fundamental=%u min=%u max=%u min_us=%.1f "
      "device_default_hns=%lld device_min_hns=%lld latency_hns=%lld latency_hr=0x%08lx raw=%u\n",
      utf8(name).c_str(), flow == eRender ? "render" : "capture", format.value->nSamplesPerSec,
      format.value->nChannels, format.value->wBitsPerSample, valid_bits, normal, fundamental,
      minimum, maximum, double(minimum) * 1e6 / format.value->nSamplesPerSec, device_default,
      device_minimum, latency, ULONG(latency_result), unsigned(options.raw));
  if (!options.seconds)
    return true;
  const unsigned period = options.period ? options.period : minimum;
  if (!fundamental || period < minimum || period > maximum || period % fundamental) {
    std::printf("WASAPI_ERROR operation=period_bounds\n");
    return false;
  }
  return stream(client.get(), format.value, flow, period, options.seconds);
}
} // namespace
int wmain(int argc, wchar_t **argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::wstring arg = argv[i];
    if (arg == L"--all")
      options.all = true;
    else if (arg == L"--raw")
      options.raw = true;
    else if (arg == L"--id" && i + 1 < argc)
      options.id = argv[++i];
    else if (arg == L"--flow" && i + 1 < argc) {
      const std::wstring value = argv[++i];
      if (value == L"capture")
        options.flow = eCapture;
      else if (value == L"render")
        options.flow = eRender;
      else
        return 1;
    } else if (arg == L"--seconds" && i + 1 < argc) {
      if (!number(argv[++i], options.seconds, 30))
        return 1;
    } else if (arg == L"--period" && i + 1 < argc) {
      if (!number(argv[++i], options.period, 65536))
        return 1;
    } else {
      std::fprintf(stderr,
                   "Usage: PiAoipWasapiProbe [--all | --id endpoint-id] [--flow capture|render] "
                   "[--raw] [--seconds 1..30 --period frames]\nDefault: PiAoIP endpoints only. "
                   "--all is read-only. Streams render silence or discard capture.\n");
      return 1;
    }
  }
  if ((options.all && options.seconds) || (options.period && !options.seconds))
    return 1;
  const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (!check(initialized, "com_initialize"))
    return 2;
  int result = 0;
  {
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDeviceCollection> devices;
    if (!check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void **>(enumerator.put())),
               "enumerator") ||
        !check(enumerator->EnumAudioEndpoints(options.flow, DEVICE_STATE_ACTIVE, devices.put()),
               "enumerate"))
      result = 2;
    else {
      UINT count = 0, selected = 0;
      devices->GetCount(&count);
      for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device;
        ComPtr<IMMEndpoint> endpoint;
        ComPtr<IPropertyStore> properties;
        if (FAILED(devices->Item(i, device.put())) ||
            FAILED(device->QueryInterface(__uuidof(IMMEndpoint),
                                          reinterpret_cast<void **>(endpoint.put()))) ||
            FAILED(device->OpenPropertyStore(STGM_READ, properties.put()))) {
          result = 2;
          continue;
        }
        EDataFlow flow = eAll;
        endpoint->GetDataFlow(&flow);
        PROPVARIANT name{};
        properties->GetValue(PKEY_Device_FriendlyName, &name);
        LPWSTR id = nullptr;
        device->GetId(&id);
        const auto *friendly = name.vt == VT_LPWSTR ? name.pwszVal : L"";
        const bool chosen =
            id && (options.all || (!options.id.empty() ? options.id == id : matches(friendly)));
        if (chosen) {
          ++selected;
          std::printf("WASAPI_ENDPOINT flow=%s name=\"%s\" id=\"%s\"\n",
                      flow == eRender ? "render" : "capture", utf8(friendly).c_str(),
                      utf8(id).c_str());
          if (!inspect(device.get(), flow, options, friendly))
            result = 3;
        }
        CoTaskMemFree(id);
        PropVariantClear(&name);
      }
      std::printf("WASAPI_ENUMERATED active=%u selected=%u\n", count, selected);
      if (!selected)
        result = 4;
    }
  }
  CoUninitialize();
  return result;
}
