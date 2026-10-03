#include "ipc_client.hpp"
#include <algorithm>
#include <cstring>
#include <sstream>

namespace rey::asio {
namespace {
struct Handle { HANDLE value = INVALID_HANDLE_VALUE; ~Handle() { if(value != INVALID_HANDLE_VALUE && value) CloseHandle(value); } };
bool io(HANDLE pipe, HANDLE event, void *data, DWORD length, DWORD &bytes, bool write) {
  OVERLAPPED op{}; op.hEvent = event; ResetEvent(event);
  if ((write ? WriteFile(pipe, data, length, &bytes, &op) : ReadFile(pipe, data, length, &bytes, &op))) return true;
  if (GetLastError() != ERROR_IO_PENDING) return false;
  if (WaitForSingleObject(event, 2000) != WAIT_OBJECT_0) {
    CancelIoEx(pipe, &op); GetOverlappedResult(pipe, &op, &bytes, TRUE); return false;
  }
  return GetOverlappedResult(pipe, &op, &bytes, FALSE) != FALSE;
}
} // namespace
bool command(const std::string &text, std::string &reply, std::string &error) {
  constexpr auto name = L"\\\\.\\pipe\\ReyAudio.Control.v1";
  Handle pipe, event;
  for (unsigned attempt = 0; attempt < 3; ++attempt) {
    pipe.value = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                            FILE_FLAG_OVERLAPPED, nullptr);
    if (pipe.value != INVALID_HANDLE_VALUE) break;
    if (GetLastError() != ERROR_PIPE_BUSY || !WaitNamedPipeW(name, 1000)) break;
  }
  if (pipe.value == INVALID_HANDLE_VALUE) { error = "Rey Audio service is unavailable"; return false; }
  DWORD mode = PIPE_READMODE_MESSAGE;
  event.value = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!event.value || !SetNamedPipeHandleState(pipe.value, &mode, nullptr, nullptr)) {
    error = "Cannot configure Rey control pipe"; return false;
  }
  DWORD bytes = 0;
  char response[32768]{};
  if (!io(pipe.value, event.value, const_cast<char *>(text.data()), DWORD(text.size()), bytes, true) ||
      !io(pipe.value, event.value, response, sizeof(response) - 1, bytes, false)) {
    error = "Rey service control timeout"; return false;
  }
  reply.assign(response, bytes);
  char ack[] = "ACK";
  (void)io(pipe.value, event.value, ack, 3, bytes, true);
  return true;
}
bool query_info(const std::string &id, Info &info, std::string &error) {
  std::string reply, prefix, extra;
  if (!command("ASIO INFO " + id, reply, error)) return false;
  std::istringstream in(reply);
  if (!(in >> prefix >> info.id >> info.rate >> info.bits >> info.block >> info.depth >> info.generation) ||
      (in >> extra) || prefix != "REY_ASIO_INFO" || info.id.size() != 32 || !valid_rate(info.rate) ||
      (info.bits != 16 && info.bits != 24 && info.bits != 32) || !valid_block(info.block) ||
      !info.depth || info.depth > 16) { error = reply; return false; }
  for (const auto c : info.id) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
    error = "Invalid service DeviceId"; return false;
  }
  return true;
}
Client::~Client() { close(); }
bool Client::connect(const Info &info, unsigned block, unsigned lead, std::string &error) {
  close(); info_ = info;
  mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Shared), nullptr);
  event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (mapping_) shared_ = static_cast<Shared *>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
  if (!shared_ || !event_) { error = "Cannot allocate ASIO shared buffers"; close(); return false; }
  std::memset(shared_, 0, sizeof(Shared));
  shared_->magic_value = magic; shared_->abi_version = version; shared_->bytes = sizeof(Shared);
  shared_->block = block; shared_->lead_blocks = lead; shared_->rate = info.rate; shared_->bits = info.bits; shared_->depth = info.depth;
  std::memcpy(shared_->device_id, info.id.data(), 32);
  std::ostringstream text;
  text << "ASIO OPEN " << info.id << ' ' << GetCurrentProcessId() << ' ' << uintptr_t(mapping_) << ' '
       << uintptr_t(event_) << ' ' << block << ' ' << lead;
  std::string reply;
  const auto acknowledged = command(text.str(), reply, error);
  connected_ = acknowledged && reply == "REY_ASIO_OK";
  if (!connected_ || load(shared_->ready) != 1) {
    if (error.empty()) error = reply;
    close(); return false;
  }
  return true;
}
void Client::close() {
  if (shared_) store(shared_->running, 0);
  if (connected_) { std::string reply, ignored; command("ASIO CLOSE " + info_.id, reply, ignored); }
  connected_ = false;
  if (shared_) UnmapViewOfFile(shared_);
  if (event_) CloseHandle(event_);
  if (mapping_) CloseHandle(mapping_);
  shared_ = nullptr; event_ = mapping_ = nullptr;
}
bool Client::capture(Slot &out, uint64_t &dropped) {
  auto read = load(shared_->capture.read);
  const auto written = load(shared_->capture.written);
  if (written < read || written - read > slots || written == read) return false;
  // Discard only blocks whose first playback sample is already past the
  // transport deadline. The user's lead stays fixed; indices remain absolute.
  const auto clock = load(shared_->clock_position);
  const auto initial_read = read;
  while (read < written && shared_->capture.data[read % slots].frame_position +
          uint64_t(shared_->lead_blocks) * shared_->block < clock) { ++dropped; ++read; }
  if (read != initial_read) add(shared_->client_capture_dropped, read - initial_read);
  if (read == written) { store(shared_->capture.read, read); return false; }
  const auto &item = shared_->capture.data[read % slots];
  if (item.frames != shared_->block || item.frames > max_frames) return false;
  out.frame_position = item.frame_position; out.timestamp_ns = item.timestamp_ns;
  out.frames = item.frames; out.flags = item.flags;
  std::copy_n(item.pcm, size_t(out.frames) * channels, out.pcm);
  store(shared_->capture.read, read + 1); return true;
}
bool Client::render(const Slot &item) {
  const auto written = load(shared_->render.written), read = load(shared_->render.read);
  if (written < read || written - read >= slots) { add(shared_->render_overflow); return false; }
  auto &out = shared_->render.data[written % slots];
  out.frame_position = item.frame_position; out.timestamp_ns = item.timestamp_ns;
  out.frames = item.frames; out.flags = item.flags;
  std::copy_n(item.pcm, size_t(item.frames) * channels, out.pcm);
  store(shared_->render.written, written + 1); return true;
}
} // namespace rey::asio
