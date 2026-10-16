#include "../src/asio/ipc_server.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <limits>
#include <cstdlib>

// Deterministic scheduling of a host publication inside the render fence.
// The static clock dependency is replaced only in this contract executable.
static uint64_t mock_ns=0;
static rey::asio::Shared* publish_to=nullptr;
static bool publish_late=false, observed_consumed_clock=false;
namespace rey {
uint64_t now_ns() {
  mock_ns+=1000;
  if(mock_ns>10000000)throw std::runtime_error("render wait was not bounded");
  if(publish_to && mock_ns>=10000) {
    if(load(publish_to->clock_position)!=0)throw std::runtime_error("prospective playback clock discarded a usable host block");
    observed_consumed_clock=true;
    if(publish_late) {
      auto& item=publish_to->render.data[1];item.frames=16;item.frame_position=16;
      for(auto& sample:item.pcm)sample=222;
      asio::store(publish_to->render.written,2);
      publish_late=false;publish_to=nullptr;
    }
  }
  return mock_ns;
}
}

namespace {
void require(bool okay, const char *message) { if (!okay) throw std::runtime_error(message); }
struct Fixture {
  rey::asio::Server server;
  HANDLE mapping = nullptr, event = nullptr;
  rey::asio::Shared *shared = nullptr;
  Fixture() {
    mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(rey::asio::Shared), nullptr);
    event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    shared = static_cast<rey::asio::Shared *>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(rey::asio::Shared)));
    require(shared && event, "mapping");
    std::memset(shared, 0, sizeof(*shared));
    shared->magic_value = rey::asio::magic; shared->abi_version = rey::asio::version; shared->bytes = sizeof(*shared);
    shared->block = 16; shared->lead_blocks = 2; shared->rate = 192000; shared->bits = 32;
    std::memset(shared->device_id, '1', 32);
    REY_BRIDGE_PROFILE profile{sizeof(profile), 1, 192000, 32, 8, 8, 64, 0, {}};
    std::memset(profile.device_id, '1', 32); server.configure(profile, 3, 7);
  }
  ~Fixture() { server.offline(); if(shared)UnmapViewOfFile(shared); if(event)CloseHandle(event); if(mapping)CloseHandle(mapping); }
  bool connect() { std::string error; return server.connect(GetCurrentProcessId(), uintptr_t(mapping), uintptr_t(event), 16, 2, error); }
};
}
int main(int argc, char **argv) try {
  if (argc == 3 && std::string(argv[1]) == "child_wait") {
    const auto handle = reinterpret_cast<HANDLE>(uintptr_t(std::strtoull(argv[2], nullptr, 10)));
    return WaitForSingleObject(handle, 5000) == WAIT_OBJECT_0 ? 0 : 3;
  }
  if (argc != 2) return 1;
  Fixture f;
  const std::string test = argv[1];
  if (test == "invalid") {
    f.shared->bytes = 4; require(!f.connect(), "small ABI accepted");
  } else if (test == "ownership") {
    require(f.connect(), "connect"); require(!f.connect(), "two owners");
    require(!f.server.disconnect(GetCurrentProcessId() + 1), "other owner disconnected");
    require(f.server.disconnect(GetCurrentProcessId()), "owner close");
    require(rey::asio::load(f.shared->ready) == 0, "disconnect flag");
  } else if (test == "dead_owner") {
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
    HANDLE quit = CreateEventW(&attributes, TRUE, FALSE, nullptr);
    require(quit && SetHandleInformation(f.mapping, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT) &&
        SetHandleInformation(f.event, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT), "child handles");
    wchar_t path[32768]{};
    require(GetModuleFileNameW(nullptr, path, DWORD(std::size(path))) != 0, "test path");
    std::wstring command = L"\"" + std::wstring(path) + L"\" child_wait " + std::to_wstring(uintptr_t(quit));
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION child{};
    require(CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
        nullptr, nullptr, &startup, &child) != FALSE, "child process");
    struct ChildLifetime {
      PROCESS_INFORMATION value; HANDLE quit;
      ~ChildLifetime() { SetEvent(quit); WaitForSingleObject(value.hProcess, 6000);
        CloseHandle(value.hThread); CloseHandle(value.hProcess); CloseHandle(quit); }
    } lifetime{child, quit};
    std::string error;
    require(f.server.connect(child.dwProcessId, uintptr_t(f.mapping), uintptr_t(f.event), 16, 2, error), "child connect");
    rey::asio::store(f.shared->running, 1);
    f.server.poll();
    require(f.server.running(), "maintenance disconnected a live owner");
    SetEvent(quit);
    require(WaitForSingleObject(child.hProcess, 5000) == WAIT_OBJECT_0, "child exit");
    require(!f.server.running(), "dead owner still running");
    f.server.poll();
    require(f.server.status().find("\"connected\":false") != std::string::npos, "dead owner retained without USB callbacks");
    require(rey::asio::load(f.shared->ready) == 0, "dead mapping still ready");
    require(f.server.block_size() == 64, "dead owner's buffer retained");
    rey::asio::store(f.shared->running, 0);
    require(f.connect(), "new owner could not connect after process exit");
    f.server.poll();
    require(f.server.status().find("\"connected\":true") != std::string::npos, "new live owner was lost");
  } else {
    require(f.connect(), "connect"); rey::asio::store(f.shared->running, 1);
    if(test=="render_wait" || test=="render_timeout") {
      std::array<int32_t,32*8> captured{},played{};
      auto& first=f.shared->render.data[0];first.frames=16;first.frame_position=0;
      for(auto& sample:first.pcm)sample=111;
      rey::asio::store(f.shared->render.written,1);
      publish_to=f.shared;publish_late=test=="render_wait";
      rey::engine::AudioBlock audio{captured.data(),played.data(),32,8,8,32};
      audio.render_wait_ns=std::numeric_limits<unsigned>::max();
      f.server.process(audio,1000000);
      require(observed_consumed_clock,"render clock was not observed during the fence");
      require(mock_ns<=150000,"render fence exceeded its budget plus per-frame clock checks");
      require(played[0]==111 && played[15*8+7]==111,"existing render was changed");
      require(played[16*8]==(test=="render_wait"?222:0),"late host publication was not handled");
      require(rey::asio::load(f.shared->render_missing)==(test=="render_wait"?0u:16u),"missed render was hidden");
      require(rey::asio::load(f.shared->clock_position)==32,"consumed clock did not commit");
      return 0;
    }
    if (test == "burst") {
      std::array<int32_t, 256 * 8> capture{}, render{};
      for (unsigned offset = 0; offset < 576; offset += 256) {
        const auto frames = std::min(256u, 576-offset);
        for (unsigned n = 0; n < frames*8; ++n) capture[n] = int(offset*8+n);
        rey::engine::AudioBlock burst{capture.data(), render.data(), frames, 8, 8, 32};
        burst.frame_position = offset; burst.render_delay_frames = 576;
        f.server.process(burst, 1000000);
      }
      require(rey::asio::load(f.shared->capture.written) == 36, "USB Audio burst lost ASIO blocks");
      require(!rey::asio::load(f.shared->capture_dropped), "USB Audio burst overflowed the queue");
      require(f.shared->capture.data[35].pcm[127] == 4607, "burst channel mapping");
      return 0;
    }
    std::array<int32_t, 24 * 8> capture{}, render{};
    for(unsigned frame=0;frame<24;++frame) for(unsigned ch=0;ch<8;++ch) capture[frame*8+ch]=int(frame*8+ch)-50;
    rey::engine::AudioBlock block{capture.data(), render.data(), 24, 8, 8, 32};
    if (test == "batch") block.render_delay_frames = 24;
    if (test == "split") block.capture_only = true;
    f.server.process(block, 1000000);
    require(rey::asio::load(f.shared->capture.written) == 1, "capture assembly");
    auto &first = f.shared->capture.data[0];
    require(first.frames == 16 && first.frame_position == 0 && first.pcm[0] == -50 && first.pcm[127] == 77, "capture layout");
    if (test == "split") {
      require(rey::asio::load(f.shared->clock_position) == 0, "capture advanced the playback deadline");
      require(!f.server.render_ready(16), "empty render queue reported ready");
      auto &out = f.shared->render.data[0]; out.frames = 16; out.frame_position = 0;
      for (auto &sample : out.pcm) sample = 1234567;
      rey::asio::store(f.shared->render.written, 1);
      require(f.server.render_ready(16) && !f.server.render_ready(17), "render readiness ignored the frame deadline");
      block.capture_only = false; block.render_only = true;
      f.server.process(block,1125000);
      require(render[0] == 1234567 && render[15*8+7] == 1234567 && render[16*8] == 0, "separate render mapping");
      require(rey::asio::load(f.shared->capture.written) == 1, "render created duplicate capture");
      require(rey::asio::load(f.shared->clock_position) == 24, "render deadline did not advance");
    } else if (test == "batch") {
      auto &out = f.shared->render.data[0]; out.frames = 16; out.frame_position = 0;
      for (auto &sample : out.pcm) sample = 1234567;
      rey::asio::store(f.shared->render.written, 1);
      block.frame_position = 24; render.fill(0); f.server.process(block, 1125000);
      require(render[0] == 1234567 && render[15*8+7] == 1234567 && render[16*8] == 0, "batch render delay");
      require(rey::asio::load(f.shared->render_late) == 0, "delayed render was discarded");
      require(rey::asio::load(f.shared->clock_position) == 24, "batch playback deadline");
      require(f.shared->capture.data[1].frame_position == 16, "capture timeline was delayed");
      f.server.transport_buffers(240, 241, 240);
      rey::asio::Info info; require(f.server.info(info) && info.capture_buffer == 240 && info.render_buffer == 241 && info.render_delay == 240, "buffer metadata");
    } else if (test == "render") {
      auto &out = f.shared->render.data[0]; out.frames = 16; out.frame_position = 32;
      for(auto &sample:out.pcm)sample=-987654;
      rey::asio::store(f.shared->render.written, 1);
      block.frame_position=24; render.fill(0); f.server.process(block, 1125000);
      require(render[0] == 0 && render[7*8] == 0 && render[8*8] == -987654 && render[23*8+7] == -987654, "scheduled render");
      require(rey::asio::load(f.shared->render.read)==1, "render release");
    } else if(test == "overflow") {
      for(unsigned i=1;i<rey::asio::slots*2;++i){block.frame_position=i*24; f.server.process(block, 1000000+i*125000);}
      require(rey::asio::load(f.shared->capture.written)==rey::asio::slots, "bounded capture ring");
      require(rey::asio::load(f.shared->capture_dropped)>0, "capture overflow counter");
    } else if(test != "capture") return 1;
  }
  return 0;
} catch (const std::exception &e) { std::fprintf(stderr,"ASIO_IPC_CONTRACT %s\n", e.what()); return 2; }
