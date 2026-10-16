#include "sar_bridge.hpp"
#include "../audio/pcm_alignment.hpp"
#include "../../include/rey/sar_clock.hpp"
#include <winioctl.h>
#include "../../external/sar-0.13.1/sar.h"
#include <setupapi.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <cwchar>
#include <vector>

namespace rey::bridge {
namespace {
constexpr GUID sar_guid = {0xc16e8d6c,0xc4cc,0x4c76,{0xb1,0x1c,0x79,0xb8,0x41,0x4e,0xa9,0x68}};
constexpr unsigned endpoint_count = 8, allocation_bytes = 4 * 1024 * 1024;
static_assert(sizeof(SarCreateEndpointRequest) == 268 && sizeof(SarEndpointRegisters) == 24);
static_assert(sizeof(SarSetBufferLayoutResponse) == 16 && sizeof(SarHandleQueueResponse) == 16);
std::wstring interface_path() {
  const auto list = SetupDiGetClassDevsW(&sar_guid,nullptr,nullptr,DIGCF_DEVICEINTERFACE|DIGCF_PRESENT);
  if(list == INVALID_HANDLE_VALUE) return {};
  SP_DEVICE_INTERFACE_DATA item{}; item.cbSize = sizeof(item);
  std::wstring path;
  for(DWORD index=0;SetupDiEnumDeviceInterfaces(list,nullptr,&sar_guid,index,&item);++index) {
    DWORD bytes=0;
    SetupDiGetDeviceInterfaceDetailW(list,&item,nullptr,0,&bytes,nullptr);
    if(bytes >= sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) && bytes <= 65536) {
      std::vector<uint8_t> storage(bytes);
      auto *detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(storage.data());
      detail->cbSize=sizeof(*detail);
      SP_DEVINFO_DATA device{};device.cbSize=sizeof(device);
      wchar_t instance[256]{};
      if(SetupDiGetDeviceInterfaceDetailW(list,&item,detail,bytes,nullptr,&device) &&
          SetupDiGetDeviceInstanceIdW(list,&device,instance,256,nullptr) &&
          _wcsicmp(instance,L"ROOT\\REYAUDIO_SAR\\0000")==0) {path=detail->DevicePath;break;}
    }
  }
  SetupDiDestroyDeviceInfoList(list);
  return path;
}
uint32_t load(const volatile DWORD &value) {
  return uint32_t(InterlockedCompareExchange(reinterpret_cast<volatile LONG *>(const_cast<volatile DWORD *>(&value)),0,0));
}
void store(volatile DWORD &target,DWORD value) {
  InterlockedExchange(reinterpret_cast<volatile LONG *>(&target),LONG(value));
}
}
struct SarBridge::State {
  HANDLE device = INVALID_HANDLE_VALUE, notification = nullptr;
  void *mapping = nullptr;
  unsigned data_bytes = 0;
  volatile SarEndpointRegisters *registers = nullptr;
  REY_BRIDGE_PROFILE profile{};
  REY_BRIDGE_STATS counters{};
  DWORD error = 0;
  OVERLAPPED queue{};
  bool queue_pending = false;
  std::array<SarHandleQueueResponse,32> responses{};
  struct Lease { HANDLE handle = nullptr; uint32_t generation = 0; };
  std::array<Lease,endpoint_count> leases{};
  sar::Clock clock;
  uint32_t clock_ticks=0;
  std::array<sar::Demand,endpoint_count> demand{};
  bool fail(DWORD code) { error=code; return false; }
  bool request(DWORD code,void *input,DWORD input_bytes,void *output,DWORD output_bytes,DWORD &bytes) {
    OVERLAPPED operation{};
    operation.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!operation.hEvent) return fail(GetLastError());
    bool success=DeviceIoControl(device,code,input,input_bytes,output,output_bytes,&bytes,&operation)!=FALSE;
    if(!success && GetLastError()==ERROR_IO_PENDING) {
      const auto wait=WaitForSingleObject(operation.hEvent,5000);
      if(wait==WAIT_OBJECT_0) success=GetOverlappedResult(device,&operation,&bytes,FALSE)!=FALSE;
      else {
        CancelIoEx(device,&operation);
        // Keep the stack OVERLAPPED alive until its cancellation is acknowledged.
        (void)GetOverlappedResult(device,&operation,&bytes,TRUE);
        error=wait==WAIT_TIMEOUT?ERROR_TIMEOUT:GetLastError();
      }
    }
    if(!success && !error) error=GetLastError();
    CloseHandle(operation.hEvent);
    return success;
  }
  bool consume(unsigned bytes) {
    if(bytes>sizeof(responses) || bytes%sizeof(responses[0])) return fail(ERROR_INVALID_DATA);
    for(unsigned n=0;n<bytes/sizeof(responses[0]);++n) {
      const auto &reply=responses[n];
      const auto index=unsigned(reply.associatedData>>32);
      const auto handle=reinterpret_cast<HANDLE>(reply.handle);
      if(index>=endpoint_count) { if(handle) CloseHandle(handle); return fail(ERROR_INVALID_DATA); }
      auto &target=leases[index];
      if(target.handle) CloseHandle(target.handle);
      target.handle=handle; target.generation=uint32_t(reply.associatedData);
    }
    return true;
  }
  bool refresh_notifications() {
    DWORD bytes=0;
    if(queue_pending) {
      if(!GetOverlappedResult(device,&queue,&bytes,FALSE)) {
        const auto code=GetLastError();
        return code==ERROR_IO_INCOMPLETE || fail(code);
      }
      queue_pending=false;
      if(!consume(bytes)) return false;
    }
    bool needed=false;
    for(unsigned n=0;n<endpoint_count;++n) {
      const auto generation=load(registers[n].generation);
      if(load(registers[n].notificationCount) && GENERATION_IS_ACTIVE(generation) &&
          (!leases[n].handle || GENERATION_NUMBER(generation)!=GENERATION_NUMBER(leases[n].generation))) needed=true;
    }
    if(!needed) return true;
    ResetEvent(notification); queue={};queue.hEvent=notification;
    if(DeviceIoControl(device,SAR_WAIT_HANDLE_QUEUE,nullptr,0,responses.data(),sizeof(responses),&bytes,&queue))
      return consume(bytes);
    const auto code=GetLastError();
    if(code==ERROR_IO_PENDING) {queue_pending=true;return true;}
    return fail(code);
  }
  bool transfer(unsigned slot,const engine::AudioBlock &block) {
    auto &reg=registers[slot];
    const auto generation=load(reg.generation);
    if(!GENERATION_IS_ACTIVE(generation)) return true;
    const auto offset=load(reg.bufferOffset),capacity=load(reg.bufferSize),position=load(reg.positionRegister);
    const auto notifications=load(reg.notificationCount);
    const unsigned stride=2*sizeof(int32_t),amount=block.frames*stride;
    if(!capacity) return true;
    if(capacity%stride || position>=capacity || position%stride || offset%sizeof(int32_t) ||
        offset>data_bytes || capacity>data_bytes-offset || amount>capacity/2 || notifications>2)
      return fail(ERROR_INVALID_DATA);
    auto &lease=leases[slot];
    if(notifications && (!lease.handle || GENERATION_NUMBER(lease.generation)!=GENERATION_NUMBER(generation))) return true;
    const bool capture=(slot%2)==0;
    const unsigned first=(slot/2)*2;
    auto *base=static_cast<uint8_t *>(mapping)+offset;
    for(unsigned frame=0;frame<block.frames;++frame) {
      auto *samples=reinterpret_cast<uint32_t *>(base+(position+frame*stride)%capacity);
      for(unsigned ch=0;ch<2;++ch) {
        const auto sample=size_t(frame)*8+first+ch;
        if(capture) samples[ch]=audio::wave_sample(block.capture[sample],block.bits);
        else block.render[sample]=audio::wire_sample(samples[ch],block.bits);
      }
    }
    if(load(reg.generation)!=generation) {
      if(!capture) for(unsigned frame=0;frame<block.frames;++frame)
        block.render[size_t(frame)*8+first]=block.render[size_t(frame)*8+first+1]=0;
      return true;
    }
    const unsigned next=(position+amount)%capacity;
    const bool notify=(notifications>=1 && position>=capacity/2 && next<capacity/2) ||
                      (notifications>=2 && position<capacity/2 && next>=capacity/2);
    store(reg.positionRegister,next);
    if(notify && !SetEvent(lease.handle)) return fail(GetLastError());
    return true;
  }
};
SarBridge::SarBridge():state_(std::make_unique<State>()) {}
SarBridge::~SarBridge() { detach(); }
bool SarBridge::available() { return !interface_path().empty(); }
bool SarBridge::attach(const REY_BRIDGE_PROFILE &profile,std::string &message) {
  detach(); state_=std::make_unique<State>(); auto &s=*state_;
  if(!rey_bridge_valid_profile(&profile) || profile.inputs!=8 || profile.outputs!=8) {
    s.error=ERROR_INVALID_PARAMETER;message="SAR requires the Rey eight-channel profile";return false;
  }
  const auto path=interface_path();
  if(path.empty()) {s.error=ERROR_NOT_FOUND;message="The signed Windows endpoint driver is unavailable";return false;}
  s.device=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
  if(s.device==INVALID_HANDLE_VALUE) {s.error=GetLastError();message="Cannot own the signed endpoint driver: "+std::to_string(s.error);return false;}
  s.profile=profile;
  SarSetBufferLayoutRequest request{};
  request.bufferSize=allocation_bytes;
  request.frameSize=sar::Clock::frames_per_tick*sizeof(int32_t);
  request.sampleRate=profile.rate;request.sampleSize=sizeof(int32_t);request.minimumFrameCount=2;
  SarSetBufferLayoutResponse response{};DWORD bytes=0;
  const bool mapped=s.request(SAR_SET_BUFFER_LAYOUT,&request,sizeof(request),&response,sizeof(response),bytes);
  if(mapped && bytes==sizeof(response)) s.mapping=response.virtualAddress;
  if(!mapped ||
      bytes!=sizeof(response) || !response.virtualAddress ||
      response.actualSize!=allocation_bytes+SAR_BUFFER_CELL_SIZE || response.registerBase!=allocation_bytes) {
    if(!s.error) s.error=ERROR_INVALID_DATA;
    message="Cannot map the Windows audio buffers: "+std::to_string(s.error);detach();return false;
  }
  s.data_bytes=response.registerBase;
  s.registers=reinterpret_cast<volatile SarEndpointRegisters *>(static_cast<uint8_t *>(s.mapping)+response.registerBase);
  s.notification=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  if(!s.notification) {s.error=GetLastError();message="Cannot create the Windows audio event";detach();return false;}
  for(unsigned slot=0;slot<endpoint_count;++slot) {
    SarCreateEndpointRequest endpoint{};
    const bool capture=(slot%2)==0;const auto first=(slot/2)*2+1;
    endpoint.type=capture?SAR_ENDPOINT_TYPE_RECORDING:SAR_ENDPOINT_TYPE_PLAYBACK;
    endpoint.index=slot;endpoint.channelCount=2;
    std::swprintf(endpoint.id,64,L"rey-%ls-%u-%u",capture?L"input":L"output",first,first+1);
    std::swprintf(endpoint.name,64,L"%ls %u-%u",capture?L"Input":L"Output",first,first+1);
    if(!s.request(SAR_CREATE_ENDPOINT,&endpoint,sizeof(endpoint),nullptr,0,bytes)) {
      message="Cannot create Rey Windows pair "+std::to_string(first)+": "+std::to_string(s.error);detach();return false;
    }
  }
  s.counters.size=sizeof(s.counters);s.counters.version=REY_BRIDGE_VERSION;s.counters.attached=1;
  return true;
}
bool SarBridge::process(const engine::AudioBlock &block) {
  auto &s=*state_;
  if(!s.counters.attached || !block.frames || block.frames>REY_BRIDGE_FRAMES || block.inputs!=8 || block.outputs!=8 ||
      block.bits!=s.profile.valid_bits || (block.capture_only && block.render_only) ||
      (!block.render_only && !block.capture) || (!block.capture_only && !block.render)) return s.fail(ERROR_INVALID_PARAMETER);
  if(!s.refresh_notifications()) return false;
  // All pairs belong to one hardware clock. Advance it once per received
  // packet, including inactive pairs, never again in the render-only phase.
  if(!block.render_only) {
    s.clock_ticks+=s.clock.consume(block.epoch,block.frames);
    for(unsigned slot=0;slot<endpoint_count;++slot)store(s.registers[slot].clockRegister,s.clock_ticks);
  }
  if(!block.capture_only) std::fill_n(block.render,size_t(block.frames)*8,0);
  for(unsigned slot=0;slot<endpoint_count;++slot) {
    if((slot%2)==0?block.render_only:block.capture_only) continue;
    if(!s.transfer(slot,block)) return false;
  }
  ++s.counters.exchanges;
  if(!block.render_only) s.counters.capture_frames+=block.frames;
  if(!block.capture_only) s.counters.render_frames+=block.frames;
  if(block.discontinuity) ++s.counters.discontinuities;
  return true;
}
bool SarBridge::running() const {
  auto &s=*state_;if(!s.registers)return false;
  const auto now=rey::now_ns();bool requested=false;
  for(unsigned slot=0;slot<endpoint_count;++slot)
    requested=s.demand[slot].update(load(s.registers[slot].generation),now)||requested;
  return requested;
}
void SarBridge::detach() {
  if(!state_) return;auto &s=*state_;
  if(s.device!=INVALID_HANDLE_VALUE) {
    if(s.queue_pending) {
      CancelIoEx(s.device,&s.queue);DWORD bytes=0;
      if(GetOverlappedResult(s.device,&s.queue,&bytes,TRUE)) s.consume(bytes);
      s.queue_pending=false;
    }
    CloseHandle(s.device);s.device=INVALID_HANDLE_VALUE;
  }
  for(auto &lease:s.leases) {if(lease.handle) CloseHandle(lease.handle);lease={};}
  if(s.notification) {CloseHandle(s.notification);s.notification=nullptr;}
  if(s.mapping) {UnmapViewOfFile(s.mapping);s.mapping=nullptr;}
  s.registers=nullptr;s.counters.attached=0;
}
DWORD SarBridge::last_error() const {return state_->error;}
bool SarBridge::stats(REY_BRIDGE_STATS &result) const {
  const auto &s=*state_;if(!s.counters.attached) return false;
  result=s.counters;result.capture_running=result.render_running=0;
  for(unsigned slot=0;slot<endpoint_count;++slot) if(GENERATION_IS_ACTIVE(load(s.registers[slot].generation)))
    ((slot%2)==0?result.capture_running:result.render_running)=1;
  return true;
}
}
