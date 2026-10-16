#include "device_session.hpp"
#include "../bridge/driver_bridge.hpp"
#include "../engine/usb_session.hpp"
#include <sstream>
namespace rey::service {
namespace {
uint64_t utc_ms() {
  FILETIME time{};
  GetSystemTimeAsFileTime(&time);
  ULARGE_INTEGER value{};
  value.LowPart = time.dwLowDateTime;
  value.HighPart = time.dwHighDateTime;
  return value.QuadPart / 10000 - 11644473600000ull;
}
std::string session_key(const Settings &s) {
  const auto &c = s.usb;
  return std::to_string(c.rate) + " " + std::to_string(c.bits) + " " + std::to_string(c.block) +
      " " + std::to_string(c.safety) + " " + std::to_string(s.usb_depth);
}
} // namespace
DeviceSession::DeviceSession(std::string id, std::wstring path, std::wstring key, Settings settings, bool test, bool asio_only)
    : path_(std::move(path)), key_(std::move(key)), digital_test_(test), asio_only_(asio_only), settings_(settings), identity_(std::move(id)) {
  active_.rate = active_.bits = active_.inputs = active_.outputs = 0;
  mixer_.publish(settings_.mix);
}
DeviceSession::~DeviceSession() { stop_worker(); }
void DeviceSession::state(const std::string &code, const std::string &detail) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ == code && detail_ == detail)
    return;
  state_ = code;
  detail_ = detail;
  events_.push_back({utc_ms(), code, detail});
  while (events_.size() > 40)
    events_.pop_front();
}
void DeviceSession::stop_worker() {
  if (worker_stop_)
    SetEvent(worker_stop_);
  if (worker_.joinable())
    worker_.join();
  mixer_.reset();
  if (worker_stop_)
    CloseHandle(worker_stop_);
  worker_stop_ = nullptr;
  worker_done_ = true;
  std::lock_guard<std::mutex> lock(mutex_);
  route_ = "none";
  active_.rate = active_.bits = active_.inputs = active_.outputs = 0;
  backend_.clear();
}
void DeviceSession::start_worker(Settings s,
                           std::string expected, std::wstring path) {
  worker_stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!worker_stop_) {
    state("error", "Cannot allocate session stop event");
    return;
  }
  callbacks_ = frames_ = missing_ = gap_max_ = 0;
  processing_sum_ = processing_max_ = 0;
  uac2_stats_.reset();
  worker_done_ = false;
  generation_.fetch_add(1, std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    route_ = "usb";
  }
  state("connecting", "Opening USB device");
  worker_ = std::thread([this, s, expected, path] {
    worker(s, expected, path);
    worker_done_.store(true, std::memory_order_release);
  });
}
bool DeviceSession::process(void *opaque, const rey::engine::AudioBlock &b) {
  auto &context = *static_cast<CallbackContext *>(opaque);
  auto &m = *context.manager;
  const auto now = rey::now_ns();
  if (!b.render_only && context.previous) {
    const auto gap = now - context.previous;
    if (gap > m.gap_max_.load(std::memory_order_relaxed))
      m.gap_max_.store(gap, std::memory_order_relaxed);
  }
  if (!b.render_only) {
    context.previous = now;
    m.callbacks_.fetch_add(1, std::memory_order_relaxed);
    m.frames_.fetch_add(b.frames, std::memory_order_relaxed);
  }
  if (b.missing)
    m.missing_.fetch_add(b.frames, std::memory_order_relaxed);
  rey::engine::AudioBlock adjusted;
  adjusted = b;
  if (!b.render_only && !m.mixer_.capture(b, adjusted)) return false;
  bool ok = true;
  if (context.echo) {
    for (unsigned f = 0; f < b.frames; ++f)
      for (unsigned ch = 0; ch < b.outputs; ++ch)
        b.render[size_t(f) * b.outputs + ch] =
            ch < b.inputs ? adjusted.capture[size_t(f) * b.inputs + ch] : 0;
  } else {
    if (context.windows) ok = static_cast<rey::bridge::DriverBridge *>(context.bridge)->process(adjusted);
    else std::fill_n(b.render, size_t(b.frames) * b.outputs, 0);
    m.asio_.process(adjusted, now);
  }
  if (!b.capture_only) m.mixer_.render(adjusted);
  const auto elapsed = rey::now_ns() - now;
  m.processing_sum_.fetch_add(elapsed, std::memory_order_relaxed);
  if (elapsed > m.processing_max_.load(std::memory_order_relaxed)) m.processing_max_.store(elapsed, std::memory_order_relaxed);
  return ok;
}
void DeviceSession::worker(Settings settings,
                     const std::string &expected, const std::wstring &path) {
  rey::bridge::DriverBridge bridge;
  CallbackContext context{this, &bridge, digital_test_};
  struct AsioLifetime { rey::asio::Server &server; ~AsioLifetime() { server.offline(); } } asio_lifetime{asio_};
  std::string error;
  rey::engine::UsbSession session;
  rey::engine::Uac2Session audio;
  const bool uac2 = rey::engine::uac2_path(path);
  if (uac2 && digital_test_) { state("device_unavailable", "Digital echo test is not available for physical USB Audio"); return; }
  if (!(uac2 ? audio.open(settings.usb, path, expected, error) : session.open(settings.usb, expected, path, error))) {
    state("device_unavailable", error);
    return;
  }
  const auto id = uac2 ? audio.identity() : session.identity();
  const bool native = !uac2 && session.jack_audio();
  if(native && digital_test_) { state("device_unavailable","The JACK audio backend is not a digital echo fixture"); return; }
  REY_BRIDGE_PROFILE profile{sizeof(profile),
                                REY_BRIDGE_VERSION,
                                settings.usb.rate,
                                settings.usb.bits,
                                8,
                                8,
                                unsigned(settings.usb.block),
                                settings.usb.safety,
                                {}};
  std::memcpy(profile.device_id, id.data(), 32);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    usb_id_ = id;
    active_ = settings.usb;
    backend_ = uac2 ? "usb-audio2-akm" : native ? "winusb-jack" : "digital-loopback";
  }
  if (!digital_test_ && !asio_only_ && !bridge.attach(profile, error)) {
    state("driver_unavailable", error);
    return;
  }
  context.windows = !digital_test_ && !asio_only_;
  if (!digital_test_) asio_.configure(profile, settings.usb_depth, generation_.load());
  const auto ready = [](void *opaque, unsigned capture, unsigned render, unsigned delay) {
    auto &ctx = *static_cast<CallbackContext *>(opaque);
    ctx.manager->asio_.transport_buffers(capture, render, delay);
    if (ctx.manager->stream_requested_.load())
      ctx.manager->state("streaming", "USB ASIO stream is running");
  };
  const auto render_ready = [](void *opaque, uint64_t end_frame) {
    auto &ctx = *static_cast<CallbackContext *>(opaque);
    return ctx.manager->asio_.render_ready(end_frame);
  };
  const auto render_span = [](void *opaque, uint64_t &first, uint64_t &last, unsigned &block) {
    auto &ctx = *static_cast<CallbackContext *>(opaque);
    return ctx.manager->asio_.render_span(first,last,block);
  };
  if ((uac2 || native) && asio_only_) {
    do {
      context.previous = 0;
      error.clear();
      bool okay=true;
      if(uac2)okay=audio.run(process, &context, worker_stop_, uac2_stats_, error, ready, &stream_requested_, render_ready, render_span);
      else if(stream_requested_.load()) {
        state("streaming","USB ASIO stream is running");
        okay=session.run(process,&context,worker_stop_,0,settings.usb_depth,error,&stream_requested_,asio_.block_size());
      }
      if (!okay && stream_requested_.load()) {
        std::fprintf(stderr, "REY_USB_STREAM_ERROR %s\n", error.c_str());
        state("stream_error", error); return;
      }
      mixer_.reset();
      state("ready", okay ? "USB ASIO service is ready" : "USB audio is in use by Windows; stop playback before starting ASIO");
      while (WaitForSingleObject(worker_stop_, 20) == WAIT_TIMEOUT && !stream_requested_.load(std::memory_order_acquire)) {}
    } while (WaitForSingleObject(worker_stop_, 0) != WAIT_OBJECT_0);
  } else {
    state(digital_test_ ? "digital_test" : "streaming", digital_test_ ? "USB digital test is running" : "USB audio endpoints are attached");
    const bool okay = uac2 ? audio.run(process, &context, worker_stop_, uac2_stats_, error, ready)
        : session.run(process, &context, worker_stop_, 0, settings.usb_depth, error);
    if (!okay) state("stream_error", error.empty() ? "USB stream stopped with errors" : error);
  }

}
void DeviceSession::poll(uint64_t now, const std::wstring &path, bool bridge) {
  stream_requested_.store(asio_.running(), std::memory_order_release);
  Settings s;
  { std::lock_guard<std::mutex> lock(mutex_); s = settings_; bridge_present_ = bridge;
    usb_present_ = path.empty() ? 0 : 1; usb_id_ = path.empty() ? "" : identity_; }
  const std::string desired = s.usb_auto && !path.empty() ? "usb" : "none";
  const auto key = session_key(s) + (desired == "usb" ? std::string(path.begin(), path.end()) : "");
  if (key != current_key_) {
    stop_worker(); current_key_ = key; retry_at_ = 0;
    if (desired == "none") state("idle", "Waiting for a device");
  }
  if (desired != "none" && worker_done_.load(std::memory_order_acquire) && now >= retry_at_) {
    stop_worker(); start_worker(s, identity_, path); retry_at_ = now + 3000;
  }
}
Settings DeviceSession::settings() const { std::lock_guard<std::mutex> lock(mutex_); return settings_; }
std::string DeviceSession::status(const std::string &error) const {
  std::lock_guard<std::mutex> lock(mutex_); return status_locked(error);
}
bool DeviceSession::update(const Settings &next, std::string &error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!save(path_, next, error, key_)) return false;
  settings_ = next; mixer_.publish(next.mix); ++revision_; return true;
}
std::string DeviceSession::status_locked(const std::string &error) const {
  const auto &s = settings_;
  std::ostringstream out;
  out << "{\"version\":\"2.8.2-akm-dev.4\",\"ok\":" << (error.empty() ? "true" : "false")
      << ",\"error\":" << json_string(error)
      << ",\"state\":" << json_string(state_)
      << ",\"detail\":" << json_string(detail_)
      << ",\"route\":" << json_string(route_)
      << ",\"driver_present\":" << (bridge_present_ ? "true" : "false")
      << ",\"asio_only\":" << (asio_only_ ? "true" : "false")
      << ",\"digital_test\":" << (digital_test_ ? "true" : "false")
      << ",\"identity\":" << json_string(identity_)
      << ",\"board_mode\":" << json_string(s.board_effects ? "combined" : "soundcard")
      << ",\"backend\":" << json_string(backend_)
      << ",\"usb\":{\"present\":" << usb_present_
      << ",\"identity\":" << json_string(usb_id_) << ",\"rate\":" << s.usb.rate
      << ",\"bits\":" << s.usb.bits << ",\"depth\":" << s.usb_depth
      << ",\"block\":" << s.usb.block << ",\"guard\":" << s.usb.safety
      << ",\"automatic\":" << (s.usb_auto ? "true" : "false") << "}"
      << ",\"active\":{\"rate\":" << active_.rate
      << ",\"bits\":" << active_.bits
      << ",\"inputs\":" << (identity_.empty() ? 0 : active_.inputs)
      << ",\"outputs\":" << (identity_.empty() ? 0 : active_.outputs) << "}"
      << ",\"stats\":{\"generation\":" << generation_.load() << ",\"callbacks\":" << callbacks_.load()
      << ",\"frames\":" << frames_.load()
      << ",\"missing_frames\":" << missing_.load()
      << ",\"callback_gap_max_us\":" << gap_max_.load() / 1000.0
      << ",\"processing_max_us\":" << processing_max_.load() / 1000.0
      << ",\"processing_average_us\":" << (callbacks_.load() ? double(processing_sum_.load()) / callbacks_.load() / 1000.0 : 0)
      << "},\"uac2\":{\"capture_buffer_frames\":" << uac2_stats_.capture_buffer.load()
      << ",\"render_buffer_frames\":" << uac2_stats_.render_buffer.load()
      << ",\"startup_silence_frames\":" << uac2_stats_.startup_silence_frames.load()
      << ",\"render_wait_max_us\":" << uac2_stats_.render_wait_max_ns.load()/1000.0
      << ",\"render_wait_timeouts\":" << uac2_stats_.render_wait_timeouts.load()
      << ",\"capture_position\":" << uac2_stats_.capture_position.load()
      << ",\"render_position\":" << uac2_stats_.render_position.load()
      << ",\"capture_discontinuities\":" << uac2_stats_.discontinuities.load()
      << ",\"render_underrun_frames\":" << uac2_stats_.underrun_frames.load()
      << ",\"render_overflow_frames\":" << uac2_stats_.overflow_frames.load()
      << "},\"asio\":" << asio_.status()
      << ",\"mixer\":{\"master_cdb\":" << s.mix.master_cdb << ",\"master_mute\":" << (s.mix.master_mute ? "true" : "false");
  const std::array<rey::audio::Channel, 8> *groups[] = {&s.mix.inputs, &s.mix.outputs};
  for (unsigned d = 0; d < 2; ++d) {
    out << (d == 0 ? ",\"inputs\":[" : ",\"outputs\":[");
    for (unsigned ch = 0; ch < 8; ++ch) {
      if (ch) out << ',';
      const auto &c = (*groups[d])[ch];
      out << "{\"gain_cdb\":" << c.gain_cdb << ",\"mute\":" << (c.mute ? "true" : "false")
          << ",\"solo\":" << (c.solo ? "true" : "false") << ",\"invert\":" << (c.invert ? "true" : "false")
          << ",\"peak\":" << mixer_.peak(d, ch) << ",\"clips\":" << mixer_.clips(d, ch) << '}';
    }
    out << ']';
  }
  out << "},\"events\":[";
  bool comma = false;
  for (auto i = events_.rbegin(); i != events_.rend(); ++i) {
    if (comma)
      out << ',';
    comma = true;
    out << "{\"utc_ms\":" << i->utc_ms << ",\"code\":" << json_string(i->code)
        << ",\"detail\":" << json_string(i->detail) << '}';
  }
  return out.str() + "]}";
}
} // namespace rey::service
