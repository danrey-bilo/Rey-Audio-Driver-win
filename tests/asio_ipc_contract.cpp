#include "../src/asio/ipc_server.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>

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
  } else {
    require(f.connect(), "connect"); rey::asio::store(f.shared->running, 1);
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
    f.server.process(block, 1000000);
    require(rey::asio::load(f.shared->capture.written) == 1, "capture assembly");
    auto &first = f.shared->capture.data[0];
    require(first.frames == 16 && first.frame_position == 0 && first.pcm[0] == -50 && first.pcm[127] == 77, "capture layout");
    if (test == "batch") {
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
