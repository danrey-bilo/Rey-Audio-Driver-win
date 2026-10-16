#define NOMINMAX
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <avrt.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>
template<class T> struct Com {T *p=nullptr;~Com(){if(p)p->Release();}T **out(){return &p;}};
struct Stream {
  Com<IMMDevice> device;Com<IAudioClient> audio;WAVEFORMATEX *format=nullptr;
  HANDLE event=nullptr;UINT32 capacity=0;
  ~Stream(){if(audio.p)audio.p->Stop();if(event)CloseHandle(event);CoTaskMemFree(format);}
};
bool check(HRESULT hr,const char *operation) {if(SUCCEEDED(hr))return true;std::printf("SHARED_ERROR operation=%s hr=0x%08lx\n",operation,ULONG(hr));return false;}
bool open(IMMDeviceEnumerator *e,const wchar_t *id,Stream &s,bool client3=false) {
  if(!check(e->GetDevice(id,s.device.out()),"device") ||
      !check(s.device.p->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void **>(s.audio.out())),"client"))return false;
  Com<IAudioClient3> modern;
  if(client3) {
    AudioClientProperties properties{};properties.cbSize=sizeof(properties);properties.eCategory=AudioCategory_Media;
    if(!check(s.audio.p->QueryInterface(__uuidof(IAudioClient3),reinterpret_cast<void **>(modern.out())),"client3") ||
        !check(modern.p->SetClientProperties(&properties),"client3_properties"))return false;
  }
  if(!check(s.audio.p->GetMixFormat(&s.format),"format"))return false;
  if(s.format->nChannels!=2 || s.format->wBitsPerSample!=32 ||
      (s.format->wFormatTag!=WAVE_FORMAT_IEEE_FLOAT &&
       (s.format->wFormatTag!=WAVE_FORMAT_EXTENSIBLE || reinterpret_cast<WAVEFORMATEXTENSIBLE *>(s.format)->SubFormat.Data1!=3)))return false;
  if(client3) {
    UINT32 normal=0,fundamental=0,minimum=0,maximum=0;
    if(!check(modern.p->GetSharedModeEnginePeriod(s.format,&normal,&fundamental,&minimum,&maximum),"client3_period") ||
        !check(modern.p->InitializeSharedAudioStream(AUDCLNT_STREAMFLAGS_EVENTCALLBACK,minimum,s.format,nullptr),"client3_initialize"))return false;
  } else if(!check(s.audio.p->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_EVENTCALLBACK,200000,0,s.format,nullptr),"initialize"))return false;
  s.event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
  return s.event && check(s.audio.p->SetEventHandle(s.event),"event") && check(s.audio.p->GetBufferSize(&s.capacity),"buffer_size");
}
int wmain(int argc,wchar_t **argv) {
  if(argc<4 || argc>6)return 1;
  const unsigned pair=unsigned(_wtoi(argv[3]));if(pair>3)return 1;
  const unsigned seconds=argc>=5?unsigned(_wtoi(argv[4])):3;if(!seconds || seconds>60)return 1;
  const bool client3=argc==6 && std::wcscmp(argv[5],L"--client3-capture")==0;
  if(argc==6 && !client3)return 1;
  if(FAILED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)))return 2;
  Com<IMMDeviceEnumerator> devices;
  if(!check(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),reinterpret_cast<void **>(devices.out())),"enumerator"))return 3;
  Stream render,capture;if(!open(devices.p,argv[1],render) || !open(devices.p,argv[2],capture,client3))return 4;
  Com<IAudioRenderClient> output;Com<IAudioCaptureClient> input;
  if(!check(render.audio.p->GetService(__uuidof(IAudioRenderClient),reinterpret_cast<void **>(output.out())),"render_service") ||
      !check(capture.audio.p->GetService(__uuidof(IAudioCaptureClient),reinterpret_cast<void **>(input.out())),"capture_service"))return 5;
  uint64_t sent=0,received=0,matched=0,wrong=0,duplicates=0,skipped=0,discontinuities=0,startup_wrong=0,steady_discontinuities=0;
  bool previous_valid=false;unsigned previous=0;
  auto submit=[&](UINT32 count) {
    if(!count)return true;BYTE *bytes=nullptr;if(!check(output.p->GetBuffer(count,&bytes),"render_buffer"))return false;
    auto *samples=reinterpret_cast<float *>(bytes);
    for(UINT32 f=0;f<count;++f) {const float v=float(((pair+1)<<16)|unsigned(sent++&65535))/4194304.f;samples[f*2]=v;samples[f*2+1]=-v/2;}
    return check(output.p->ReleaseBuffer(count,0),"render_release");
  };
  if(!submit(render.capacity))return 6;
  DWORD task=0;HANDLE rt=AvSetMmThreadCharacteristicsW(L"Pro Audio",&task);
  if(!check(capture.audio.p->Start(),"capture_start") || !check(render.audio.p->Start(),"render_start"))return 7;
  LARGE_INTEGER frequency{},started{},finished{};QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&started);
  const auto started_ms=GetTickCount64(),end=started_ms+seconds*1000;HANDLE events[]{render.event,capture.event};
  while(GetTickCount64()<end) {
    const auto wait=WaitForMultipleObjects(2,events,FALSE,1000);if(wait>=WAIT_OBJECT_0+2)return 8;
    UINT32 padding=0;if(!check(render.audio.p->GetCurrentPadding(&padding),"padding") || padding>render.capacity || !submit(render.capacity-padding))return 9;
    UINT32 available=0;if(!check(input.p->GetNextPacketSize(&available),"packet_size"))return 10;
    while(available) {
      BYTE *bytes=nullptr;UINT32 count=0;DWORD flags=0;UINT64 position=0,qpc=0;
      if(!check(input.p->GetBuffer(&bytes,&count,&flags,&position,&qpc),"capture_buffer"))return 11;
      const bool steady=GetTickCount64()-started_ms>=100;
      if(flags&AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY){++discontinuities;if(steady)++steady_discontinuities;}
      received+=count;
      if(!(flags&AUDCLNT_BUFFERFLAGS_SILENT)) {
        auto *samples=reinterpret_cast<const float *>(bytes);
        for(UINT32 f=0;f<count;++f) {
          const auto left=samples[f*2],right=samples[f*2+1];if(left==0 && right==0)continue;
          const auto word=unsigned(std::lround(left*4194304.f));
          if((word>>16)!=pair+1 || std::fabs(left+right*2)>1.f/4194304.f) {if(steady)++wrong;else ++startup_wrong;previous_valid=false;continue;}
          if(!steady){previous_valid=false;continue;}
          ++matched;const auto serial=word&65535;
          if(previous_valid) {const auto delta=(serial-previous)&65535;if(!delta)++duplicates;else if(delta!=1)skipped+=delta-1;}
          previous=serial;previous_valid=true;
        }
      }
      if(!check(input.p->ReleaseBuffer(count),"capture_release") || !check(input.p->GetNextPacketSize(&available),"next_packet"))return 12;
    }
  }
  QueryPerformanceCounter(&finished);if(rt)AvRevertMmThreadCharacteristics(rt);
  const bool okay=matched>capture.format->nSamplesPerSec && wrong==0 && duplicates==0 && skipped==0 && steady_discontinuities==0;
  std::printf("SHARED_RESULT pair=%u rate=%lu wall_ms=%.3f sent=%llu received=%llu matched=%llu wrong=%llu duplicates=%llu skipped=%llu discontinuities=%llu startup_wrong=%llu steady_discontinuities=%llu warmup_ms=100 okay=%u physical_audio=0 client3_capture=%u\n",
      pair,capture.format->nSamplesPerSec,double(finished.QuadPart-started.QuadPart)*1000/frequency.QuadPart,
      (unsigned long long)sent,(unsigned long long)received,(unsigned long long)matched,(unsigned long long)wrong,
      (unsigned long long)duplicates,(unsigned long long)skipped,(unsigned long long)discontinuities,
      (unsigned long long)startup_wrong,(unsigned long long)steady_discontinuities,unsigned(okay),unsigned(client3));
  return okay?0:13;
}
