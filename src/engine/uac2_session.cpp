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
bool native_asio_container(const GUID &container) {
  constexpr GUID native{0x87835b64,0xf7f6,0x403c,{0xbe,0x2c,0xd6,0xb8,0x09,0x8a,0xc7,0xd5}};
  auto list=SetupDiGetClassDevsW(&native,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
  if(list==INVALID_HANDLE_VALUE)return false;
  bool found=false;
  for(DWORD i=0;;++i){
    SP_DEVINFO_DATA device{};device.cbSize=sizeof(device);
    if(!SetupDiEnumDeviceInfo(list,i,&device))break;
    GUID value{};DEVPROPTYPE type=0;
    if(SetupDiGetDevicePropertyW(list,&device,&DEVPKEY_Device_ContainerId,&type,reinterpret_cast<BYTE *>(&value),sizeof(value),nullptr,0) &&
       type==DEVPROP_TYPE_GUID && IsEqualGUID(value,container)){found=true;break;}
  }
  SetupDiDestroyDeviceInfoList(list);return found;
}
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
    // The two interfaces belong to one board. ASIO uses its native transport;
    // ordinary Windows applications continue to use the UAC2 endpoints.
    if(native_asio_container(board.container))continue;
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
                      void (*ready)(void *, unsigned, unsigned, unsigned), const std::atomic<bool> *requested,
                      bool (*render_ready)(void *, uint64_t), bool (*render_span)(void *, uint64_t &, uint64_t &, unsigned &)) {
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
  stats.capture_buffer = capture.capacity; stats.render_buffer = render.capacity;
  const bool split = render_ready && render_span;
  if (ready) ready(context, capture.capacity, render.capacity, capture.capacity);
  // Probe the actual aligned buffers once, then release both endpoints while
  // ASIO is idle so ordinary Windows audio applications can use the device.
  if (requested && !requested->load(std::memory_order_acquire)) return true;
  Uac2Queue fifo(16384);
  std::array<int32_t, 256 * 8> input{}, output{};
  uint64_t position = 0, render_position = 0;
  bool primed = false;
  rey::RealtimeThread realtime(stats.realtime_failures);
  const auto drain_capture = [&] {
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
          block.capture_only = split;
          block.render_delay_frames = split ? 0 : capture.capacity;
          block.scheduled_ns = rey::now_ns(); position += count;
          stats.capture_position = position;
          if (!process(context, block)) { error = "USB Audio processing callback failed"; ok = false; break; }
          if (!split) stats.overflow_frames.fetch_add(fifo.push(output.data(), count, profile_.bits));
        }
        const auto released = reader->ReleaseBuffer(frames);
        if (!ok || !check(released, "capture release", error)) return false;
        if (!check(reader->GetNextPacketSize(&available), "capture size", error)) return false;
      }
      return true;
  };
  const auto fill_render = [&] {
    BYTE *data = nullptr;
    if (!check(writer->GetBuffer(render.capacity,&data), "render", error)) return false;
    for (unsigned offset = 0; offset < render.capacity; offset += 256) {
      const auto count = std::min(256u,render.capacity-offset);
      output.fill(0);
      AudioBlock block{input.data(),output.data(),count,8,8,profile_.bits};
      block.frame_position = render_position+offset; block.render_only = true;
      block.scheduled_ns = rey::now_ns();
      if (!process(context,block)) {
        writer->ReleaseBuffer(render.capacity,AUDCLNT_BUFFERFLAGS_SILENT);
        error = "USB Audio render callback failed"; return false;
      }
      for (unsigned n = 0; n < count*8; ++n) {
        const auto value = uac2_encode(output[n],profile_.bits);
        std::memcpy(data+(size_t(offset)*8+n)*4,&value,4);
      }
    }
    render_position += render.capacity;
    stats.render_position = render_position;
    return check(writer->ReleaseBuffer(render.capacity,0), "render release", error);
  };
  if (!capture.start(error)) return false;
  if (split) {
    // Establish the playback origin from real ASIO output. Windows may start
    // the two endpoint clocks several buffers apart; counting their events
    // from zero invents late playback on otherwise healthy repeated opens.
    const auto deadline = rey::now_ns()+2000000000;
    for (;;) {
      if (WaitForSingleObject(stop,0) == WAIT_OBJECT_0 ||
          (requested && !requested->load(std::memory_order_acquire))) return true;
      uint64_t first = 0, last = 0;
      unsigned block = 0;
      if (render_span(context,first,last,block) && last-first+block > render.capacity) {
        // A 288-frame Windows buffer is not divisible by a 64-frame ASIO
        // callback. Prefix only its initial remainder with silence instead of
        // waiting another whole Windows period for those few samples.
        const auto prefix = last-first < render.capacity ? render.capacity-(last-first) : 0;
        if (first < prefix) { error = "Invalid initial ASIO playback origin"; return false; }
        render_position = first-prefix;
        stats.startup_silence_frames.fetch_add(prefix);
        break;
      }
      if (rey::now_ns() >= deadline) { error = "ASIO did not provide its initial playback buffers"; return false; }
      if (WaitForSingleObject(capture.event.handle,1) == WAIT_OBJECT_0 && !drain_capture()) return false;
    }
    if (!fill_render()) return false;
  } else {
    BYTE *data = nullptr;
    if (!check(writer->GetBuffer(render.capacity,&data), "prefill", error) ||
        !check(writer->ReleaseBuffer(render.capacity,AUDCLNT_BUFFERFLAGS_SILENT), "prefill release", error)) return false;
  }
  if (!render.start(error)) return false;
  HANDLE events[]{stop, capture.event.handle, render.event.handle};
  while (true) {
    if (requested && !requested->load(std::memory_order_acquire)) return true;
    const auto wait = WaitForMultipleObjects(3, events, FALSE, 2000);
    if (wait == WAIT_OBJECT_0) return true;
    if (wait != WAIT_OBJECT_0+1 && wait != WAIT_OBJECT_0+2) { error = "USB Audio stopped responding or was disconnected"; return false; }
    if (wait == WAIT_OBJECT_0+1) {
      if (!drain_capture()) return false;
    } else {
      if (split) {
        // Hand off fresh capture to the ASIO thread before its playback buffer
        // is due. A bounded wait replaces a whole extra 3 ms capture batch.
        const auto begin = rey::now_ns();
        const auto budget = std::min<uint64_t>(1500000, uint64_t(render.capacity)*1000000000/profile_.rate/2);
        const auto end_frame = render_position+render.capacity;
        while (!render_ready(context,end_frame) && rey::now_ns()-begin < budget) {
          if (WaitForSingleObject(stop,0) == WAIT_OBJECT_0) return true;
          if (WaitForSingleObject(capture.event.handle,0) == WAIT_OBJECT_0) {
            if (!drain_capture()) return false;
          } else SwitchToThread();
        }
        rey::max_counter(stats.render_wait_max_ns,rey::now_ns()-begin);
        if (!render_ready(context,end_frame)) ++stats.render_wait_timeouts;
        if (!fill_render()) return false;
        continue;
      }
      BYTE *data = nullptr;
      if (!check(writer->GetBuffer(render.capacity, &data), "render", error)) return false;
      if (!primed && fifo.available() < render.capacity) {
        stats.startup_silence_frames.fetch_add(render.capacity);
        if (!check(writer->ReleaseBuffer(render.capacity, AUDCLNT_BUFFERFLAGS_SILENT), "startup silence", error)) return false;
        continue;
      } else {
        primed = true;
        const auto count = fifo.pop(data, render.capacity);
        if (count != render.capacity) stats.underrun_frames.fetch_add(render.capacity-count);
      }
      if (!check(writer->ReleaseBuffer(render.capacity, 0), "render release", error)) return false;
    }
  }
}
}
