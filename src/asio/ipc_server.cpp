#include "ipc_server.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <sstream>

namespace rey::asio {
struct Server::Connection {
  DWORD pid = 0;
  uint64_t serial = 0;
  HANDLE process = nullptr, mapping = nullptr, event = nullptr;
  Shared *shared = nullptr;
  unsigned block = 0, lead = 0, bits = 0, accumulated = 0;
  uint64_t first_frame = 0, first_ns = 0, previous_ns = 0, callbacks = 0;
  uint64_t render_read = UINT64_MAX;
  unsigned render_offset = 0;
  bool running = false, rendered = false;
  std::array<int32_t, max_frames * channels> capture{};
  ~Connection() {
    if (shared) { store(shared->ready, 0); SetEvent(event); UnmapViewOfFile(shared); }
    if (event) CloseHandle(event);
    if (mapping) CloseHandle(mapping);
    if (process) CloseHandle(process);
  }
  void transfer(const rey::engine::AudioBlock &audio, uint64_t now) {
    if (load(shared->ready) != 1) return;
    if (load(shared->running) != 1) { accumulated = 0; running = false; return; }
    if ((++callbacks & 127) == 0 && WaitForSingleObject(process, 0) == WAIT_OBJECT_0) {
      store(shared->ready, 0); return;
    }
    if (!running) { running = true; accumulated = 0; rendered = false; previous_ns = 0; }
    store(shared->clock_position, audio.frame_position + audio.frames);
    if (previous_ns && now - previous_ns > load(shared->capture_gap_max_ns))
      store(shared->capture_gap_max_ns, now - previous_ns);
    previous_ns = now;
    const int64_t full = int64_t(1) << (bits - 1);
    // Playback is scheduled against transport sample indices, never queue age.
    for (unsigned f = 0; f < audio.frames; ++f) {
      const uint64_t wanted = audio.frame_position + f;
      bool supplied = false;
      for (unsigned attempt = 0; attempt < slots; ++attempt) {
        auto read = load(shared->render.read);
        const auto written = load(shared->render.written);
        if (written < read || written - read > slots) { store(shared->ready, 0); return; }
        if (read == written) break;
        if (render_read != read) { render_read = read; render_offset = 0; }
        const auto &item = shared->render.data[read % slots];
        const auto first = item.frame_position;
        if (item.frames != block || first > UINT64_MAX - block) {
          store(shared->ready, 0); return;
        }
        if (first + block <= wanted) {
          add(shared->render_late, block - render_offset); store(shared->render.read, read + 1); continue;
        }
        if (first > wanted) break;
        const auto offset = unsigned(wanted - first);
        if (offset > render_offset) add(shared->render_late, offset - render_offset);
        render_offset = offset + 1;
        for (unsigned ch = 0; ch < channels; ++ch) {
          const int64_t mixed = int64_t(audio.render[size_t(f) * channels + ch]) +
                                item.pcm[size_t(offset) * channels + ch];
          audio.render[size_t(f) * channels + ch] = int32_t(std::clamp(mixed, -full, full - 1));
        }
        rendered = supplied = true;
        if (offset + 1 == block) store(shared->render.read, read + 1);
        break;
      }
      if (rendered && !supplied) add(shared->render_missing);
      if (!accumulated) { first_frame = wanted; first_ns = now; }
      if (wanted != first_frame + accumulated) {
        accumulated = 0; first_frame = wanted; first_ns = now; add(shared->capture_dropped);
      }
      std::copy_n(audio.capture + size_t(f) * channels, channels,
                  capture.data() + size_t(accumulated++) * channels);
      if (accumulated == block) {
        const auto written = load(shared->capture.written), read = load(shared->capture.read);
        if (written < read || written - read > slots) { store(shared->ready, 0); return; }
        if (written - read == slots) add(shared->capture_dropped);
        else {
          auto &item = shared->capture.data[written % slots];
          item.frame_position = first_frame; item.timestamp_ns = first_ns;
          item.frames = block; item.flags = audio.discontinuity ? 1 : 0;
          std::copy_n(capture.data(), size_t(block) * channels, item.pcm);
          store(shared->capture.written, written + 1); SetEvent(event);
        }
        accumulated = 0;
      }
    }
  }
};
Server::Server() = default;
Server::~Server() { offline(); }
void Server::close_locked() {
  active_.store(nullptr, std::memory_order_seq_cst);
  while (readers_.load(std::memory_order_seq_cst)) Sleep(0);
  owned_.reset();
}
void Server::configure(const REY_BRIDGE_PROFILE &profile, unsigned depth, uint64_t generation) {
  std::lock_guard<std::mutex> lock(mutex_);
  close_locked();
  info_ = {std::string(profile.device_id, 32), profile.rate, profile.valid_bits, profile.block, depth, generation};
  available_ = true;
}
void Server::offline() {
  std::lock_guard<std::mutex> lock(mutex_);
  available_ = false; close_locked();
}
bool Server::info(Info &out) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!available_) return false;
  out = info_; return true;
}
std::string Server::status() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::ostringstream out;
  out << "{\"ready\":" << (available_ ? "true" : "false")
      << ",\"connected\":" << (owned_ ? "true" : "false");
  if (owned_ && owned_->shared) {
    auto &s = *owned_->shared;
    out << ",\"connection_id\":" << owned_->serial << ",\"pid\":" << owned_->pid << ",\"running\":" << (load(s.running) == 1 ? "true" : "false")
        << ",\"block\":" << owned_->block << ",\"lead_blocks\":" << owned_->lead
        << ",\"capture_dropped\":" << load(s.capture_dropped) + load(s.client_capture_dropped)
        << ",\"render_late_frames\":" << load(s.render_late)
        << ",\"render_missing_frames\":" << load(s.render_missing)
        << ",\"render_overflow\":" << load(s.render_overflow)
        << ",\"clock_position\":" << load(s.clock_position);
  }
  return out.str() + '}';
}
bool Server::connect(DWORD pid, uint64_t mapping, uint64_t event, unsigned block,
                     unsigned lead, std::string &error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!available_ || !valid_block(block) || lead < 1 || lead > 4 || !pid) {
    error = "USB ASIO profile is unavailable"; return false;
  }
  if (owned_) {
    if (WaitForSingleObject(owned_->process, 0) != WAIT_OBJECT_0) {
      error = "Another ASIO host already owns this card"; return false;
    }
    close_locked();
  }
  auto client = std::make_unique<Connection>();
  client->pid = pid;
  client->process = OpenProcess(PROCESS_DUP_HANDLE | SYNCHRONIZE, FALSE, pid);
  auto duplicate = [&](uint64_t handle, HANDLE &target) {
    return DuplicateHandle(client->process, reinterpret_cast<HANDLE>(uintptr_t(handle)),
        GetCurrentProcess(), &target, 0, FALSE, DUPLICATE_SAME_ACCESS) != FALSE;
  };
  if (!client->process || !duplicate(mapping, client->mapping) || !duplicate(event, client->event)) {
    error = "Cannot acquire authenticated ASIO handles"; return false;
  }
  client->shared = static_cast<Shared *>(MapViewOfFile(client->mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
  auto *s = client->shared;
  if (!s || s->magic_value != magic || s->abi_version != version || s->bytes != sizeof(Shared) ||
      s->block != block || s->lead_blocks != lead || s->rate != info_.rate || s->bits != info_.bits ||
      std::memcmp(s->device_id, info_.id.data(), 32) || load(s->running) || load(s->ready)) {
    error = "Invalid ASIO shared-memory contract"; return false;
  }
  client->block = block; client->lead = lead; client->bits = info_.bits;
  client->serial = ++connection_serial_;
  owned_ = std::move(client);
  active_.store(owned_.get(), std::memory_order_seq_cst);
  store(s->ready, 1); return true;
}
bool Server::disconnect(DWORD pid) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!owned_ || owned_->pid != pid) return false;
  close_locked(); return true;
}
void Server::process(const rey::engine::AudioBlock &audio, uint64_t now) {
  readers_.fetch_add(1, std::memory_order_seq_cst);
  if (auto *client = active_.load(std::memory_order_seq_cst)) client->transfer(audio, now);
  readers_.fetch_sub(1, std::memory_order_seq_cst);
}
} // namespace rey::asio
