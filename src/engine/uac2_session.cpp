#include "uac2_session.hpp"
#include "uac2_pcm.hpp"
#include "../platform/realtime.hpp"
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <setupapi.h>
#include <initguid.h>
#include <devpkey.h>
#include <ks.h>
#include <ksmedia.h>
#include <array>
#include <algorithm>
#include <cstring>
#include <cstdio>

namespace rey::engine {
namespace {
template<class T> struct Com {
  T *p = nullptr;
  ~Com() { if (p) p->Release(); }
  T *operator->() const { return p; }
  T **put() { return &p; }
};
struct Apartment {
  HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
};
struct Event {
  HANDLE handle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  ~Event() { if (handle) CloseHandle(handle); }
};
bool check(HRESULT hr, const char *where, std::string &error) {
  if (SUCCEEDED(hr)) return true;
  char text[160];
  std::snprintf(text, sizeof(text), "USB Audio %s failed (0x%08lx)", where, static_cast<unsigned long>(hr));
  error = text; return false;
}
std::string serial_id(const wchar_t *instance) {
  const std::wstring value(instance);
  constexpr wchar_t prefix[] = L"USB\\VID_1D6B&PID_0104\\";
  if (_wcsnicmp(value.c_str(), prefix, std::size(prefix)-1)) return {};
  auto id = value.substr(std::size(prefix)-1);
  if (id.size() != 32) return {};
  std::string result;
  for (auto c : id) {
    if (c >= L'A' && c <= L'F') c += L'a'-L'A';
    if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'))) return {};
    result.push_back(char(c));
  }
  return result == std::string(32, '0') ? std::string{} : result;
}
struct Board { GUID container; std::string id; };
std::vector<Board> boards() {
  std::vector<Board> result;
  auto list = SetupDiGetClassDevsW(nullptr, L"USB", nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);
  if (list == INVALID_HANDLE_VALUE) return result;
  for (DWORD n = 0;; ++n) {
    SP_DEVINFO_DATA device{}; device.cbSize = sizeof(device);
    if (!SetupDiEnumDeviceInfo(list, n, &device)) break;
    wchar_t instance[512]{};
    if (!SetupDiGetDeviceInstanceIdW(list, &device, instance, 512, nullptr)) continue;
    auto id = serial_id(instance);
    if (id.empty()) continue;
    wchar_t product[256]{}; DEVPROPTYPE product_type = 0;
    if (!SetupDiGetDevicePropertyW(list, &device, &DEVPKEY_Device_BusReportedDeviceDesc, &product_type,
          reinterpret_cast<BYTE *>(product), sizeof(product), nullptr, 0) || product_type != DEVPROP_TYPE_STRING ||
        std::wcscmp(product, L"Rey Audio USB")) continue;
    GUID container{}; DEVPROPTYPE type = 0;
    if (SetupDiGetDevicePropertyW(list, &device, &DEVPKEY_Device_ContainerId, &type,
          reinterpret_cast<BYTE *>(&container), sizeof(container), nullptr, 0) && type == DEVPROP_TYPE_GUID)
      result.push_back({container, id});
  }
  SetupDiDestroyDeviceInfoList(list); return result;
}
struct Endpoint {
  Com<IMMDevice> device;
  Com<IAudioClient> client;
  Event event;
  UINT32 capacity = 0;
  bool started = false;
  ~Endpoint() { if (started) client->Stop(); }
  bool initialize(IMMDeviceEnumerator *enumerator, const std::wstring &id, const UsbProfile &profile, std::string &error) {
    if (!check(enumerator->GetDevice(id.c_str(), device.put()), "endpoint", error) ||
        !check(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void **>(client.put())), "activate", error)) return false;
    WAVEFORMATEXTENSIBLE format{};
    format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    format.Format.nChannels = 8; format.Format.nSamplesPerSec = profile.rate;
    format.Format.wBitsPerSample = 32; format.Format.nBlockAlign = 32;
    format.Format.nAvgBytesPerSec = profile.rate * 32;
    format.Format.cbSize = sizeof(format) - sizeof(WAVEFORMATEX);
    format.Samples.wValidBitsPerSample = 32;
    constexpr GUID pcm{0x00000001,0x0000,0x0010,{0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71}};
    format.dwChannelMask = 0xff; format.SubFormat = pcm;
    WAVEFORMATEX *mix = nullptr;
    if (SUCCEEDED(client->GetMixFormat(&mix)) && mix) {
      if (mix->nChannels == 8 && mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE && mix->cbSize >= 22)
        format.dwChannelMask = reinterpret_cast<WAVEFORMATEXTENSIBLE *>(mix)->dwChannelMask;
      CoTaskMemFree(mix);
    }
    if (!check(client->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, &format.Format, nullptr), "8-channel PCM32 format", error)) return false;
    REFERENCE_TIME minimum = 0, normal = 0;
    if (!check(client->GetDevicePeriod(&normal, &minimum), "period", error)) return false;
    // An event represents a whole exclusive buffer. Respect the USB driver's minimum;
    // split its frames into <=256-frame callbacks for the existing ASIO/mixer contract.
    const auto requested = std::max<REFERENCE_TIME>(minimum, (REFERENCE_TIME(profile.block) * 10000000 + profile.rate - 1) / profile.rate);
    auto hr = client->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                requested, requested, &format.Format, nullptr);
    if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) {
      UINT32 aligned = 0;
      if (!check(client->GetBufferSize(&aligned), "aligned size", error)) return false;
      client.p->Release(); client.p = nullptr;
      if (!check(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void **>(client.put())), "realign", error)) return false;
      const auto period = (REFERENCE_TIME(aligned) * 10000000 + profile.rate/2) / profile.rate;
      hr = client->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, period, period, &format.Format, nullptr);
    }
    if (!check(hr, "exclusive stream (close other applications using Rey Audio)", error) ||
        !check(client->GetBufferSize(&capacity), "buffer size", error)) return false;
    if (!event.handle || !capacity || capacity > 8192) { error = "USB Audio buffer exceeds the bounded stream capacity"; return false; }
    return check(client->SetEventHandle(event.handle), "event", error);
  }
  bool start(std::string &error) { started = check(client->Start(), "start", error); return started; }
};
}

std::vector<Uac2Device> uac2_devices(std::string &error) {
  Apartment apartment;
  std::vector<Uac2Device> result;
  if (!check(apartment.result, "COM", error)) return result;
  const auto physical = boards();
  if (physical.empty()) return result;
  Com<IMMDeviceEnumerator> enumerator;
  if (!check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                             reinterpret_cast<void **>(enumerator.put())), "enumerator", error)) return result;
  for (const auto &board : physical) {
    Uac2Device pair; pair.identity = board.id;
    bool ambiguous = false;
    for (const auto flow : {eCapture, eRender}) {
      Com<IMMDeviceCollection> collection;
      if (!check(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, collection.put()), "enumeration", error)) return {};
      UINT count = 0; collection->GetCount(&count);
      for (UINT i = 0; i < count; ++i) {
        Com<IMMDevice> device; Com<IPropertyStore> properties;
        if (FAILED(collection->Item(i, device.put())) || FAILED(device->OpenPropertyStore(STGM_READ, properties.put()))) continue;
        PROPVARIANT value{};
        const auto hr = properties->GetValue(PKEY_Device_ContainerId, &value);
        const bool match = SUCCEEDED(hr) && value.vt == VT_CLSID && value.puuid && IsEqualGUID(*value.puuid, board.container);
        PropVariantClear(&value);
        if (!match) continue;
        LPWSTR id = nullptr;
        if (FAILED(device->GetId(&id))) continue;
        auto &target = flow == eCapture ? pair.capture : pair.render;
        if (!target.empty()) ambiguous = true;
        target = id; CoTaskMemFree(id);
      }
    }
    if (!ambiguous && !pair.capture.empty() && !pair.render.empty()) result.push_back(std::move(pair));
  }
  return result;
}

bool Uac2Session::open(const UsbProfile &profile, const std::wstring &path, const std::string &expected, std::string &error) {
  for (auto device : uac2_devices(error)) {
    if (device.path() == path && device.identity == expected) { device_ = std::move(device); profile_ = profile; return true; }
  }
  if (error.empty()) error = "Rey USB Audio input/output pair is unavailable";
  return false;
}

bool Uac2Session::run(ProcessBlock process, void *context, HANDLE stop, Uac2Stats &stats, std::string &error,
                      void (*ready)(void *, unsigned, unsigned), const std::atomic<bool> *requested) {
  Apartment apartment;
  if (!check(apartment.result, "COM", error)) return false;
  Com<IMMDeviceEnumerator> enumerator;
  if (!check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                             reinterpret_cast<void **>(enumerator.put())), "enumerator", error)) return false;
  Endpoint capture, render;
  if (!capture.initialize(enumerator.p, device_.capture, profile_, error) || !render.initialize(enumerator.p, device_.render, profile_, error)) return false;
  Com<IAudioCaptureClient> reader; Com<IAudioRenderClient> writer;
  if (!check(capture.client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void **>(reader.put())), "capture service", error) ||
      !check(render.client->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void **>(writer.put())), "render service", error)) return false;
  BYTE *initial = nullptr;
  if (!check(writer->GetBuffer(render.capacity, &initial), "prefill", error) ||
      !check(writer->ReleaseBuffer(render.capacity, AUDCLNT_BUFFERFLAGS_SILENT), "prefill release", error)) return false;
  stats.capture_buffer = capture.capacity; stats.render_buffer = render.capacity;
  if (ready) ready(context, capture.capacity, render.capacity);
  // Probe the actual aligned buffers once, then release both endpoints while
  // ASIO is idle so ordinary Windows audio applications can use the device.
  if (requested && !requested->load(std::memory_order_acquire)) return true;
  Uac2Queue fifo(16384);
  std::array<int32_t, 256 * 8> input{}, output{};
  uint64_t position = 0;
  bool primed = false;
  rey::RealtimeThread realtime(stats.realtime_failures);
  if (!capture.start(error) || !render.start(error)) return false;
  HANDLE events[]{stop, capture.event.handle, render.event.handle};
  while (true) {
    if (requested && !requested->load(std::memory_order_acquire)) return true;
    const auto wait = WaitForMultipleObjects(3, events, FALSE, 2000);
    if (wait == WAIT_OBJECT_0) return true;
    if (wait != WAIT_OBJECT_0+1 && wait != WAIT_OBJECT_0+2) { error = "USB Audio stopped responding or was disconnected"; return false; }
    if (wait == WAIT_OBJECT_0+1) {
      UINT32 available = 0;
      if (!check(reader->GetNextPacketSize(&available), "capture size", error)) return false;
      while (available) {
        BYTE *data = nullptr; UINT32 frames = 0; DWORD flags = 0;
        if (!check(reader->GetBuffer(&data, &frames, &flags, nullptr, nullptr), "capture", error)) return false;
        if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) ++stats.discontinuities;
        bool ok = true;
        for (unsigned offset = 0; offset < frames; offset += 256) {
          const auto count = std::min(256u, frames-offset);
          for (unsigned n = 0; n < count*8; ++n) {
            int32_t value = 0;
            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT)) std::memcpy(&value, data + (size_t(offset)*8+n)*4, 4);
            input[n] = uac2_decode(value, profile_.bits);
          }
          output.fill(0);
          AudioBlock block{input.data(), output.data(), count, 8, 8, profile_.bits};
          block.frame_position = position; block.discontinuity = (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0;
          block.render_delay_frames = capture.capacity;
          block.scheduled_ns = rey::now_ns(); position += count;
          if (!process(context, block)) { error = "USB Audio processing callback failed"; ok = false; break; }
          stats.overflow_frames.fetch_add(fifo.push(output.data(), count, profile_.bits));
        }
        const auto released = reader->ReleaseBuffer(frames);
        if (!ok || !check(released, "capture release", error)) return false;
        if (!check(reader->GetNextPacketSize(&available), "capture size", error)) return false;
      }
    } else {
      BYTE *data = nullptr;
      if (!check(writer->GetBuffer(render.capacity, &data), "render", error)) return false;
      if (!primed && fifo.available() < render.capacity) {
        stats.startup_silence_frames.fetch_add(render.capacity);
        if (!check(writer->ReleaseBuffer(render.capacity, AUDCLNT_BUFFERFLAGS_SILENT), "startup silence", error)) return false;
        continue;
      }
      primed = true;
      const auto count = fifo.pop(data, render.capacity);
      if (count != render.capacity) stats.underrun_frames.fetch_add(render.capacity-count);
      if (!check(writer->ReleaseBuffer(render.capacity, 0), "render release", error)) return false;
    }
  }
}
}
