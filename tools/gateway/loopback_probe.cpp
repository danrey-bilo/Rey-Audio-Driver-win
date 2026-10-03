#define NOMINMAX
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <avrt.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

template<class T> struct Com {
  T *p=nullptr; ~Com(){if(p)p->Release();} T **out(){return &p;}
};
struct Endpoint {
  Com<IMMDevice> device;
  Com<IAudioClient> audio;
  WAVEFORMATEX *format=nullptr;
  HANDLE event=nullptr;
  UINT32 capacity=0;
  ~Endpoint(){if(audio.p)audio.p->Stop();if(event)CloseHandle(event);CoTaskMemFree(format);}
};
bool check(HRESULT result,const char *action){
  if(SUCCEEDED(result))return true;
  std::printf("LOOPBACK_ERROR action=%s hr=0x%08lx\n",action,ULONG(result));return false;
}
unsigned bits=32, requested_rate=0;
uint32_t marker(unsigned channel){return uint32_t(channel+1)<<(bits-8);}
uint32_t sample_at(const BYTE *samples,size_t index){
  uint32_t value=0;
  for(unsigned b=0;b<bits/8;++b)value|=uint32_t(samples[index*(bits/8)+b])<<(b*8);
  return value;
}
bool open(IMMDeviceEnumerator *e,const wchar_t *id,Endpoint &ep){
  if(!check(e->GetDevice(id,ep.device.out()),"device") ||
     !check(ep.device.p->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,
                                 reinterpret_cast<void**>(ep.audio.out())),"client") ||
     !check(ep.audio.p->GetMixFormat(&ep.format),"mix_format"))return false;
  if(ep.format->nChannels!=8 || ep.format->wBitsPerSample!=32 ||
     ep.format->wFormatTag!=WAVE_FORMAT_EXTENSIBLE ||
     reinterpret_cast<WAVEFORMATEXTENSIBLE*>(ep.format)->SubFormat.Data1!=3)return false;
  auto *native=reinterpret_cast<WAVEFORMATEXTENSIBLE*>(ep.format);
  native->SubFormat.Data1=1;
  native->Samples.wValidBitsPerSample=WORD(bits);
  ep.format->wBitsPerSample=WORD(bits);
  ep.format->nBlockAlign=WORD(8*(bits/8));
  ep.format->nAvgBytesPerSec=ep.format->nSamplesPerSec*ep.format->nBlockAlign;
  if(requested_rate && ep.format->nSamplesPerSec!=requested_rate){
    std::printf("LOOPBACK_ERROR action=rate requested=%u actual=%lu\n",requested_rate,ep.format->nSamplesPerSec);return false;
  }
  std::printf("LOOPBACK_FORMAT rate=%lu channels=8 app_format=pcm%u exclusive=1\n",ep.format->nSamplesPerSec,bits);
  const REFERENCE_TIME period=requested_rate?100000:30000;
  if(!check(ep.audio.p->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE,AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                   period,period,ep.format,nullptr),"initialize"))return false;
  ep.event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
  return ep.event && check(ep.audio.p->SetEventHandle(ep.event),"event") &&
         check(ep.audio.p->GetBufferSize(&ep.capacity),"capacity");
}
void fill(BYTE *buffer,UINT32 frames){
  for(UINT32 f=0;f<frames;++f)for(unsigned ch=0;ch<8;++ch)
    for(unsigned b=0;b<bits/8;++b)buffer[(size_t(f)*8+ch)*(bits/8)+b]=BYTE(marker(ch)>>(b*8));
}
int wmain(int argc,wchar_t **argv){
  if(argc!=3 && argc!=5)return 1;
  if(argc==5){requested_rate=unsigned(_wtoi(argv[3]));bits=unsigned(_wtoi(argv[4]));}
  if(bits!=16 && bits!=24 && bits!=32)return 1;
  if(FAILED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)))return 2;
  Com<IMMDeviceEnumerator> e;
  if(!check(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,
                            __uuidof(IMMDeviceEnumerator),reinterpret_cast<void**>(e.out())),"enumerator"))return 3;
  Endpoint render,capture;
  if(!open(e.p,argv[1],render) || !open(e.p,argv[2],capture))return 4;
  Com<IAudioRenderClient> output; Com<IAudioCaptureClient> input;
  if(!check(render.audio.p->GetService(__uuidof(IAudioRenderClient),reinterpret_cast<void**>(output.out())),"render_service") ||
     !check(capture.audio.p->GetService(__uuidof(IAudioCaptureClient),reinterpret_cast<void**>(input.out())),"capture_service"))return 5;
  BYTE *buffer=nullptr;
  if(!check(output.p->GetBuffer(render.capacity,&buffer),"prefill"))return 6;
  fill(buffer,render.capacity);
  if(!check(output.p->ReleaseBuffer(render.capacity,0),"prefill_release"))return 7;
  DWORD index=0;HANDLE rt=AvSetMmThreadCharacteristicsW(L"Pro Audio",&index);
  if(rt)AvSetMmThreadPriority(rt,AVRT_PRIORITY_CRITICAL);
  if(!check(capture.audio.p->Start(),"start_capture") || !check(render.audio.p->Start(),"start_render"))return 8;
  const HANDLE events[]={render.event,capture.event};
  const auto end=GetTickCount64()+2000;
  std::array<uint64_t,8> matched{}, wrong{};
  uint64_t frames=0,discontinuities=0;
  while(GetTickCount64()<end){
    const DWORD wait=WaitForMultipleObjects(2,events,FALSE,1000);
    if(wait!=WAIT_OBJECT_0 && wait!=WAIT_OBJECT_0+1)return 9;
    if(wait==WAIT_OBJECT_0){
      // Event-driven exclusive streams submit a complete buffer per notification.
      const UINT32 count=render.capacity;
      if(!check(output.p->GetBuffer(count,&buffer),"output_buffer"))return 12;
      fill(buffer,count);
      if(!check(output.p->ReleaseBuffer(count,0),"output_release"))return 13;
    }else{
      UINT32 next=0;if(!check(input.p->GetNextPacketSize(&next),"next_packet"))return 14;
      while(next){
        DWORD flags=0;UINT32 count=0;
        if(!check(input.p->GetBuffer(&buffer,&count,&flags,nullptr,nullptr),"input_buffer"))return 15;
        if(flags&AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY)++discontinuities;
        if(!(flags&AUDCLNT_BUFFERFLAGS_SILENT)){
          for(UINT32 f=0;f<count;++f){
            bool nonzero=false;for(unsigned ch=0;ch<8;++ch)nonzero|=sample_at(buffer,size_t(f)*8+ch)!=0;
            if(!nonzero)continue;
            for(unsigned ch=0;ch<8;++ch){
              const auto value=sample_at(buffer,size_t(f)*8+ch);
              if(value==marker(ch))++matched[ch];else {
                if(wrong[ch]<3)std::printf("LOOPBACK_MISMATCH channel=%u actual=%lu expected=%lu\n",
                  ch+1,ULONG(value),ULONG(marker(ch)));
                ++wrong[ch];
              }
            }
          }
        }
        frames+=count;
        if(!check(input.p->ReleaseBuffer(count),"input_release") ||
           !check(input.p->GetNextPacketSize(&next),"next_packet"))return 16;
      }
    }
  }
  if(rt)AvRevertMmThreadCharacteristics(rt);
  bool good=true;
  for(unsigned ch=0;ch<8;++ch){
    std::printf("LOOPBACK_CHANNEL channel=%u matched=%llu wrong=%llu\n",ch+1,
                (unsigned long long)matched[ch],(unsigned long long)wrong[ch]);
    good&=matched[ch]>capture.format->nSamplesPerSec && wrong[ch]<capture.format->nSamplesPerSec/10;
  }
  std::printf("LOOPBACK_RESULT frames=%llu discontinuities=%llu okay=%u physical_audio=0 asio_latency_measured=0\n",
              (unsigned long long)frames,(unsigned long long)discontinuities,unsigned(good));
  return good?0:17;
}
