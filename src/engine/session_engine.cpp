#include "session_engine.hpp"
#include "aoip/timeline.hpp"
#include "aoip/spsc_queue.hpp"
#include "aoip/receive_budget.hpp"
#include "aoip/pcm_codec.hpp"
#include "../platform/start_gate.hpp"
namespace piaoip::engine {
namespace {
struct Counters {
// clang-format off
#define PIAOIP_COUNTERS(X) \
  X(rx_packets) X(invalid_packets) X(rx_queue_overflows) \
  X(late_frames) X(missing_frames) X(resyncs) \
  X(callbacks) X(deadline_misses) X(skipped_frames) \
  X(max_callback_ns) X(max_wake_late_ns) X(max_rx_gap_ns) \
  X(tx_packets) X(tx_queue_overflows) X(tx_expired_packets) X(tx_errors) \
  X(mmcss_failures) X(last_tx_error) X(host_overruns) X(expired_output_frames) X(tx_retries)
// clang-format on
#define FIELD(name) std::atomic<uint64_t> name{0};
  PIAOIP_COUNTERS(FIELD)
#undef FIELD
  aoip::Diagnostics snapshot() const {
    aoip::Diagnostics result;
#define COPY(name) result.name = name.load(std::memory_order_relaxed);
    PIAOIP_COUNTERS(COPY)
#undef COPY
    return result;
  }
#undef PIAOIP_COUNTERS
};
} // namespace
struct SessionEngine::State {
  Config cfg;
  PeerSession peer;
  bool wsa = false, opened = false, started = false;
  SOCKET socket = INVALID_SOCKET;
  HANDLE stop_event = nullptr, rx_event = nullptr, tx_event = nullptr;
  StartGate start_gate;
  std::atomic<bool> active{false};
  std::atomic<uint64_t> audio_cpu{0}, rx_cpu{0}, tx_cpu{0}, control_failures{0}, budget_yields{0},
      suppressed{0}, max_tx_age{0};
  std::thread audio, rx, tx, control;
  transport::IocpReceiver receiver;
  std::unique_ptr<aoip::SpscQueue<aoip::Packet, 1024>> outgoing;
  aoip::Timeline timeline;
  std::vector<int32_t> capture, render, packed_render;
  ProcessBlock process = nullptr;
  void *context = nullptr;
  Counters counters;
  uint64_t previous_rx = 0, tx_frame = 0, position = 0;
  uint32_t sequence = 0;
  bool output_silent = false;

  ~State() {
    stop();
    if (socket != INVALID_SOCKET)
      closesocket(socket);
    if (stop_event)
      CloseHandle(stop_event);
    if (rx_event)
      CloseHandle(rx_event);
    if (tx_event)
      CloseHandle(tx_event);
    if (wsa)
      WSACleanup();
  }
  void halt() {
    active = false;
    if (stop_event)
      SetEvent(stop_event);
    receiver.wake();
  }
  void stop() {
    halt();
    if (audio.joinable())
      audio.join();
    if (rx.joinable())
      rx.join();
    if (tx.joinable())
      tx.join();
    if (control.joinable())
      control.join();
    if (opened)
      peer.unsubscribe(cfg);
  }
  uint64_t lateness_limit() const {
    return std::max<uint64_t>(500000, uint64_t(std::max(2u * unsigned(cfg.block), cfg.safety)) *
                                          1000000000 / cfg.rate);
  }
  bool accept(aoip::Packet &packet) {
    packet.time_ns = now_ns();
    if (!aoip::validate_packet(packet.data, packet.size, 1, cfg.channels, cfg.rate, cfg.bits) ||
        aoip::get16(packet.data + 4) != 3 ||
        (aoip::get64(packet.data + 40) & ~peer.inputs().mask)) {
      ++counters.invalid_packets;
      return false;
    }
    if (aoip::get16(packet.data + 34) != peer.epoch())
      return false;
    const bool silent = aoip::get64(packet.data + 40) == 0;
    if (previous_rx && !silent)
      max_counter(counters.max_rx_gap_ns, packet.time_ns - previous_rx);
    previous_rx = silent ? 0 : packet.time_ns;
    ++counters.rx_packets;
    return true;
  }
  void receive_loop() {
    SetThreadDescription(GetCurrentThread(), L"PiAoIP service receive");
    RealtimeThread realtime(counters.mmcss_failures, 1, cfg.realtime_cpus[1]);
    start_gate.arrive();
    if (!start_gate.wait_release(stop_event))
      return;
    receiver.run(
        active, rx_event,
        [](void *state, aoip::Packet &packet) {
          return static_cast<State *>(state)->accept(packet);
        },
        this);
    const auto io = receiver.metrics();
    counters.rx_queue_overflows += io.overflows;
    counters.invalid_packets += io.errors;
    rx_cpu = thread_cpu_ns();
  }
  void control_loop() {
    if (!start_gate.wait_release(stop_event))
      return;
    unsigned failures = 0;
    while (active && WaitForSingleObject(stop_event, 1000) == WAIT_TIMEOUT) {
      if (peer.keepalive(cfg))
        failures = 0;
      else {
        ++control_failures;
        if (++failures >= 2)
          halt();
      }
    }
  }
  void send_loop() {
    SetThreadDescription(GetCurrentThread(), L"PiAoIP service transmit");
    RealtimeThread realtime(counters.mmcss_failures, 2, cfg.realtime_cpus[2]);
    DeadlineWaiter waiter;
    start_gate.arrive();
    if (!start_gate.wait_release(stop_event))
      return;
    HANDLE events[] = {stop_event, tx_event};
    const auto expiry =
        std::max<uint64_t>(3000000, uint64_t(cfg.block + cfg.safety) * 1000000000 / cfg.rate);
    while (active) {
      const auto *packet = outgoing->acquire_read();
      if (!packet) {
        WaitForMultipleObjects(2, events, FALSE, 100);
        continue;
      }
      auto now = now_ns();
      if (now > packet->time_ns)
        max_counter(max_tx_age, now - packet->time_ns);
      while (active) {
        if (now_ns() > packet->time_ns + expiry) {
          ++counters.tx_expired_packets;
          break;
        }
        const int n =
            send(socket, reinterpret_cast<const char *>(packet->data), int(packet->size), 0);
        if (n == int(packet->size)) {
          ++counters.tx_packets;
          break;
        }
        const int error = WSAGetLastError();
        if (n == SOCKET_ERROR && error == WSAEWOULDBLOCK) {
          ++counters.tx_retries;
          waiter.wait(now_ns() + 25000, stop_event);
          continue;
        }
        ++counters.tx_errors;
        counters.last_tx_error = error;
        break;
      }
      outgoing->release_read();
    }
    tx_cpu = thread_cpu_ns();
  }
  void enqueue(uint64_t ready) {
    const auto &outputs = peer.outputs();
    if (!outputs.count) {
      tx_frame += cfg.block;
      return;
    }
    uint64_t signal = cfg.energy_saving ? 0 : outputs.mask;
    if (cfg.energy_saving)
      for (unsigned ch = 0; ch < outputs.count; ++ch) {
        for (unsigned f = 0; f < unsigned(cfg.block); ++f)
          if (render[size_t(f) * outputs.count + ch]) {
            signal |= uint64_t(1) << outputs.physical[ch];
            break;
          }
      }
    if (!signal) {
      suppressed += cfg.block;
      if (output_silent) {
        tx_frame += cfg.block;
        return;
      }
    }
    aoip::ChannelMap wire;
    wire.assign(signal);
    // Compact only selected nonzero channels. Full masks keep the SIMD path.
    const int32_t *source = render.data();
    if (wire.mask != outputs.mask) {
      for (unsigned f = 0; f < unsigned(cfg.block); ++f)
        for (unsigned ch = 0; ch < wire.count; ++ch)
          packed_render[size_t(f) * wire.count + ch] =
              render[size_t(f) * outputs.count + unsigned(outputs.packed[wire.physical[ch]])];
      source = packed_render.data();
    }
    const unsigned capacity =
        signal ? aoip::masked_packet_frames(wire.count, cfg.bits) : unsigned(cfg.block);
    unsigned offset = 0;
    do {
      const auto frames = std::min(capacity, unsigned(cfg.block) - offset);
      for (unsigned copy = 0; copy < (signal ? 1u : 3u); ++copy) {
        const auto seq = sequence++;
        auto *packet = outgoing->reserve_write();
        if (!packet) {
          ++counters.tx_queue_overflows;
          continue;
        }
        packet->time_ns = ready;
        aoip::write_masked_header(packet->data, 2, seq, tx_frame + offset, signal, cfg.rate,
                                  cfg.bits, frames, peer.epoch());
        aoip::pack_pcm(packet->data + aoip::masked_header_bytes,
                       source + size_t(offset) * wire.count, size_t(frames) * wire.count, cfg.bits);
        packet->size = aoip::masked_header_bytes + frames * wire.count * (cfg.bits / 8);
        aoip::put32(packet->data + 36, aoip::packet_crc(packet->data, packet->size));
        outgoing->commit_write();
      }
      offset += frames;
    } while (offset < unsigned(cfg.block));
    SetEvent(tx_event);
    tx_frame += cfg.block;
    output_silent = !signal;
  }
  void audio_loop() {
    SetThreadDescription(GetCurrentThread(), L"PiAoIP service audio");
    RealtimeThread realtime(counters.mmcss_failures, 0, cfg.realtime_cpus[0]);
    DeadlineWaiter waiter;
    start_gate.arrive();
    if (!start_gate.wait_release(stop_event))
      return;
    const double nominal = double(cfg.block) * 1000000000.0 / cfg.rate;
    double deadline = double(now_ns()) + nominal;
    const uint64_t initial_timeout = now_ns() + 2000000000;
    bool locked = !peer.inputs().count, remote_silent = false, discontinuity = false;
    uint64_t last_packet = 0, silence_first = UINT64_MAX, last_callback = 0;
    while (active) {
      const aoip::ReceiveBudget budget(now_ns(), uint64_t(nominal));
      unsigned drained = 0;
      bool yield = false;
      while (drained < 1024) {
        const auto *packet = receiver.acquire();
        if (!packet)
          break;
        if (drained && budget.exhausted(now_ns(), locked ? uint64_t(deadline) : UINT64_MAX)) {
          ++budget_yields;
          yield = true;
          break;
        }
        const uint64_t first = aoip::get64(packet->data + 16);
        const bool silence = aoip::get64(packet->data + 40) == 0;
        // Delayed PCM from before a silence transition cannot unmute newer time.
        if (locked && !silence && remote_silent && silence_first != UINT64_MAX &&
            first < silence_first) {
          counters.late_frames += aoip::get16(packet->data + 30);
          receiver.release();
          ++drained;
          continue;
        }
        const bool reacquire =
            locked && !silence &&
            (remote_silent || (last_packet && packet->time_ns - last_packet > 250000000));
        const bool far_ahead =
            locked && first > timeline.cursor && first - timeline.cursor > cfg.rate / 4;
        if (!locked || reacquire || far_ahead) {
          if (locked) {
            ++counters.resyncs;
            discontinuity = true;
          }
          timeline.reset(first);
          locked = true;
          remote_silent = false;
          silence_first = UINT64_MAX;
          deadline =
              double(packet->time_ns) + double(cfg.block + cfg.safety) * 1000000000.0 / cfg.rate;
        }
        last_packet = packet->time_ns;
        if (silence) {
          if (first < timeline.latest)
            timeline.insert_silence(first, first + aoip::get16(packet->data + 30));
          else
            silence_first = std::min(silence_first, first);
        } else {
          if (silence_first != UINT64_MAX && first >= silence_first) {
            timeline.insert_silence(silence_first, first);
            silence_first = UINT64_MAX;
          }
          remote_silent = false;
          counters.late_frames += timeline.insert(packet->data, &peer.inputs());
        }
        receiver.release();
        ++drained;
      }
      if (!locked) {
        if (now_ns() >= initial_timeout) {
          ++control_failures;
          halt();
          break;
        }
        HANDLE events[] = {stop_event, rx_event};
        WaitForMultipleObjects(2, events, FALSE, 20);
        continue;
      }
      const auto now = now_ns();
      if (double(now) < deadline) {
        if (yield || (drained == 1024 && receiver.size()))
          continue;
        const auto active_budget = uint64_t(nominal * 0.70),
                   remaining = last_callback < active_budget ? active_budget - last_callback : 0;
        const bool idle =
            cfg.energy_saving && (!peer.inputs().count || remote_silent) && output_silent;
        waiter.wait(uint64_t(deadline), stop_event,
                    peer.inputs().count && !remote_silent ? rx_event : nullptr,
                    idle ? 0
                         : std::min({uint64_t(cfg.audio_spin_us) * 1000, uint64_t(nominal / 2),
                                     remaining}));
        continue;
      }
      const auto late = now - uint64_t(deadline);
      max_counter(counters.max_wake_late_ns, late);
      if (late >= lateness_limit()) {
        const auto skipped = uint64_t(double(late) / nominal) * unsigned(cfg.block);
        ++counters.deadline_misses;
        counters.skipped_frames += skipped;
        timeline.cursor += skipped;
        tx_frame += skipped;
        position += skipped;
        discontinuity = true;
        deadline += double(skipped) * 1000000000.0 / cfg.rate;
      }
      const int64_t target = cfg.block + cfg.safety,
                    queued = int64_t(timeline.latest) - int64_t(timeline.cursor);
      if (peer.inputs().count && !remote_silent &&
          queued > target + std::max<int64_t>({2 * cfg.block, cfg.rate / 1000, cfg.safety})) {
        const auto skip = uint64_t((queued - target) / cfg.block) * unsigned(cfg.block);
        timeline.cursor += skip;
        counters.skipped_frames += skip;
        ++counters.resyncs;
        discontinuity = true;
      }
      const double error =
          peer.inputs().count && !remote_silent
              ? double(int64_t(timeline.latest) - int64_t(timeline.cursor) - target)
              : 0;
      const double correction = std::clamp(error / (cfg.rate * 0.5), -0.002, 0.002);
      unsigned signal = peer.inputs().count && !remote_silent ? unsigned(cfg.block) : 0;
      if (silence_first != UINT64_MAX)
        signal = timeline.cursor >= silence_first
                     ? 0
                     : unsigned(std::min<uint64_t>(signal, silence_first - timeline.cursor));
      const unsigned missing = signal ? timeline.read(capture.data(), signal) : 0;
      if (signal < unsigned(cfg.block)) {
        std::fill(capture.begin() + size_t(signal) * peer.inputs().count, capture.end(), 0);
        timeline.cursor += unsigned(cfg.block) - signal;
      }
      if (silence_first != UINT64_MAX && timeline.cursor >= silence_first)
        remote_silent = true;
      counters.missing_frames += missing;
      std::fill(render.begin(), render.end(), 0);
      const AudioBlock block{capture.data(),
                             render.data(),
                             unsigned(cfg.block),
                             peer.inputs().count,
                             peer.outputs().count,
                             cfg.bits,
                             missing,
                             peer.epoch(),
                             position,
                             uint64_t(deadline),
                             discontinuity};
      const auto before = now_ns();
      bool okay = false;
      try {
        okay = process(context, block);
      } catch (...) {
        okay = false;
      }
      const auto finished = now_ns();
      last_callback = finished - before;
      max_counter(counters.max_callback_ns, last_callback);
      if (last_callback > uint64_t(nominal))
        ++counters.host_overruns;
      ++counters.callbacks;
      if (!okay) {
        ++control_failures;
        halt();
        break;
      }
      if (finished > uint64_t(deadline) + lateness_limit()) {
        counters.expired_output_frames += cfg.block;
        tx_frame += cfg.block;
        discontinuity = true;
      } else {
        enqueue(finished);
        discontinuity = false;
      }
      position += cfg.block;
      deadline += nominal * (1.0 - correction);
    }
    audio_cpu = thread_cpu_ns();
  }
};
SessionEngine::SessionEngine() : state_(std::make_unique<State>()) {}
SessionEngine::~SessionEngine() = default;
bool SessionEngine::open(const Config &config, std::string &error) try {
  stop();
  state_ = std::make_unique<State>();
  auto &s = *state_;
  s.cfg = config;
  WSADATA data{};
  if (WSAStartup(MAKEWORD(2, 2), &data)) {
    error = "WSAStartup failed";
    return false;
  }
  s.wsa = true;
  if (!s.peer.prepare(s.cfg, error))
    return false;
  s.socket = WSASocketW(AF_INET, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0, WSA_FLAG_OVERLAPPED);
  if (s.socket == INVALID_SOCKET) {
    error = "UDP socket creation failed";
    return false;
  }
  BOOL exclusive = TRUE, no_reset = FALSE;
  DWORD bytes = 0;
  int buffer = 256 * 1024;
  setsockopt(s.socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char *>(&exclusive),
             sizeof(exclusive));
  setsockopt(s.socket, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char *>(&buffer),
             sizeof(buffer));
  buffer = 64 * 1024;
  setsockopt(s.socket, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char *>(&buffer),
             sizeof(buffer));
  WSAIoctl(s.socket, 0x9800000c, &no_reset, sizeof(no_reset), nullptr, 0, &bytes, nullptr, nullptr);
  sockaddr_in local{}, remote{};
  local.sin_family = remote.sin_family = AF_INET;
  local.sin_port = htons(s.cfg.port);
  remote.sin_port = htons(s.cfg.peer_port);
  if (inet_pton(AF_INET, s.cfg.peer, &remote.sin_addr) != 1 ||
      bind(s.socket, reinterpret_cast<sockaddr *>(&local), sizeof(local)) ||
      connect(s.socket, reinterpret_cast<sockaddr *>(&remote), sizeof(remote))) {
    error = "UDP bind/connect failed: " + std::to_string(WSAGetLastError());
    return false;
  }
  int local_length = sizeof(local);
  if (getsockname(s.socket, reinterpret_cast<sockaddr *>(&local), &local_length)) {
    error = "Cannot query bound UDP port";
    return false;
  }
  s.cfg.port = ntohs(local.sin_port);
  u_long nonblocking = 1;
  if (ioctlsocket(s.socket, FIONBIO, &nonblocking)) {
    error = "UDP nonblocking setup failed";
    return false;
  }
  s.stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  s.rx_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  s.tx_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!s.stop_event || !s.rx_event || !s.tx_event || !s.receiver.attach(s.socket) ||
      !s.start_gate.prepare(1 + unsigned(s.peer.inputs().count != 0) +
                            unsigned(s.peer.outputs().count != 0))) {
    error = "IOCP/event setup failed";
    return false;
  }
  s.timeline.prepare(std::max(1u, s.peer.inputs().count));
  s.timeline.reset(0);
  s.capture.assign(size_t(s.cfg.block) * s.peer.inputs().count, 0);
  s.render.assign(size_t(s.cfg.block) * s.peer.outputs().count, 0);
  s.packed_render.resize(s.render.size());
  s.outgoing = std::make_unique<aoip::SpscQueue<aoip::Packet, 1024>>();
  s.opened = true;
  return true;
} catch (const std::exception &e) {
  error = e.what();
  return false;
}
bool SessionEngine::start(ProcessBlock process, void *context, std::string &error) {
  auto &s = *state_;
  if (!s.opened || s.started || !process) {
    error = "Open a new session before start";
    return false;
  }
  s.started = true;
  s.process = process;
  s.context = context;
  s.active = true;
  try {
    if (s.peer.inputs().count)
      s.rx = std::thread([&s] { s.receive_loop(); });
    if (s.peer.outputs().count)
      s.tx = std::thread([&s] { s.send_loop(); });
    s.audio = std::thread([&s] { s.audio_loop(); });
    s.control = std::thread([&s] { s.control_loop(); });
  } catch (...) {
    s.stop();
    error = "Thread creation failed";
    return false;
  }
  // Thread creation, MMCSS and timers must precede remote capture. The gate also
  // keeps all worker reads of the epoch after the successful acknowledgement.
  if (!s.start_gate.wait_ready(s.stop_event, 3000)) {
    s.stop();
    error = "Audio/transport workers did not become ready";
    return false;
  }
  if (!s.peer.subscribe(s.cfg, error)) {
    s.stop();
    return false;
  }
  s.start_gate.release();
  return true;
}
void SessionEngine::stop() {
  state_->stop();
}
bool SessionEngine::running() const {
  return state_->active;
}
bool SessionEngine::wait_until_stopped(HANDLE external_stop, DWORD timeout_ms) const {
  if (!running() || !state_->stop_event)
    return true;
  HANDLE events[] = {state_->stop_event, external_stop};
  const auto result = WaitForMultipleObjects(external_stop ? 2 : 1, events, FALSE, timeout_ms);
  return result != WAIT_TIMEOUT;
}
const Config &SessionEngine::config() const {
  return state_->cfg;
}
const PeerSession &SessionEngine::peer() const {
  return state_->peer;
}
SessionStats SessionEngine::stats() const {
  const auto &s = *state_;
  return {s.counters.snapshot(),  s.receiver.metrics(), s.audio_cpu.load(),
          s.rx_cpu.load(),        s.tx_cpu.load(),      s.control_failures.load(),
          s.budget_yields.load(), s.suppressed.load(),  s.max_tx_age.load()};
}
} // namespace piaoip::engine
