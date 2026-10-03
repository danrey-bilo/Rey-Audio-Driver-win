#include "device_session.hpp"
#include "../bridge/driver_bridge.hpp"
#include "../engine/session_engine.hpp"
#ifdef PIAOIP_ENABLE_USB
#include "../engine/usb_session.hpp"
#endif
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
std::string session_key(const std::string &route, const Settings &s) {
  const auto &c = route == "usb" ? s.usb : s.lan;
  std::ostringstream text;
  text << route << ' ' << c.peer << ' ' << c.port << ' ' << c.block << ' '
       << c.safety << ' ';
  if (route == "usb")
    text << c.rate << ' ' << c.bits << ' ' << s.usb_depth;
  else
    text << c.capture_frames << ' ' << c.energy_saving;
  return text.str();
}
} // namespace
DeviceSession::DeviceSession(std::string id, std::wstring path, std::wstring key, Settings settings, bool test)
    : path_(std::move(path)), key_(std::move(key)), digital_test_(test), settings_(settings), identity_(std::move(id)) {
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
}
void DeviceSession::start_worker(const std::string &route, Settings s,
                           std::string expected, std::wstring path) {
  worker_stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!worker_stop_) {
    state("error", "Cannot allocate session stop event");
    return;
  }
  callbacks_ = frames_ = missing_ = gap_max_ = 0;
  processing_sum_ = processing_max_ = 0;
  worker_done_ = false;
  generation_.fetch_add(1, std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    route_ = route;
  }
  state("connecting",
        route == "usb" ? "Opening USB device" : "Connecting to the LAN device");
  worker_ = std::thread([this, route, s, expected, path] {
    worker(route, s, expected, path);
    worker_done_.store(true, std::memory_order_release);
  });
}
bool DeviceSession::process(void *opaque, const piaoip::engine::AudioBlock &b) {
  auto &context = *static_cast<CallbackContext *>(opaque);
  auto &m = *context.manager;
  const auto now = piaoip::now_ns();
  if (context.previous) {
    const auto gap = now - context.previous;
    if (gap > m.gap_max_.load(std::memory_order_relaxed))
      m.gap_max_.store(gap, std::memory_order_relaxed);
  }
  context.previous = now;
  m.callbacks_.fetch_add(1, std::memory_order_relaxed);
  m.frames_.fetch_add(b.frames, std::memory_order_relaxed);
  if (b.missing)
    m.missing_.fetch_add(b.frames, std::memory_order_relaxed);
  piaoip::engine::AudioBlock adjusted;
  if (!m.mixer_.capture(b, adjusted)) return false;
  bool ok = true;
  if (context.echo) {
    for (unsigned f = 0; f < b.frames; ++f)
      for (unsigned ch = 0; ch < b.outputs; ++ch)
        b.render[size_t(f) * b.outputs + ch] =
            ch < b.inputs ? adjusted.capture[size_t(f) * b.inputs + ch] : 0;
  } else ok = static_cast<piaoip::bridge::DriverBridge *>(context.bridge)->process(adjusted);
  m.mixer_.render(adjusted);
  const auto elapsed = piaoip::now_ns() - now;
  m.processing_sum_.fetch_add(elapsed, std::memory_order_relaxed);
  if (elapsed > m.processing_max_.load(std::memory_order_relaxed)) m.processing_max_.store(elapsed, std::memory_order_relaxed);
  return ok;
}
void DeviceSession::worker(const std::string &route, Settings settings,
                     const std::string &expected, const std::wstring &path) {
  piaoip::bridge::DriverBridge bridge;
  CallbackContext context{this, &bridge, digital_test_};
  std::string error;
  if (route == "usb") {
#ifdef PIAOIP_ENABLE_USB
    piaoip::engine::UsbSession session;
    if (!session.open(settings.usb, expected, path, error)) {
      state("device_unavailable", error);
      return;
    }
    const auto id = session.identity();
    PIAOIP_BRIDGE_PROFILE profile{sizeof(profile),
                                  PIAOIP_BRIDGE_VERSION,
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
      backend_ = "digital-loopback";
    }
    if (!digital_test_ && !bridge.attach(profile, error)) {
      state("driver_unavailable", error);
      return;
    }
    state(digital_test_ ? "digital_test" : "streaming",
          digital_test_ ? "USB digital test is running"
                        : "USB audio endpoints are attached");
    if (!session.run(process, &context, worker_stop_, 0, settings.usb_depth,
                     error))
      state("stream_error",
            error.empty() ? "USB stream stopped with errors" : error);
#else
    state("unsupported", "This build does not include USB transport");
#endif
  } else {
    piaoip::engine::SessionEngine session;
    if (!session.open(settings.lan, error)) {
      state("device_unavailable", error);
      return;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (identity_ != session.peer().identity()) {
        error = "LAN DeviceId differs from this card";
      }
      backend_ = session.peer().backend();
      active_ = session.config(); // LAN format is negotiated from the Pi.
    }
    if (!error.empty()) { state("device_unavailable", error); return; }
    if (!digital_test_ && !bridge.attach(session, error)) {
      state("driver_unavailable", error);
      return;
    }
    if (!session.start(process, &context, error)) {
      state("stream_error", error);
      return;
    }
    state(digital_test_ ? "digital_test" : "streaming",
          digital_test_ ? "LAN digital test is running"
                        : "LAN audio endpoints are attached");
    session.wait_until_stopped(worker_stop_);
    const bool lost = !session.running();
    session.stop();
    if (lost)
      state("stream_error", "LAN session stopped; reconnecting");
  }
}
void DeviceSession::poll(uint64_t now, const std::wstring &path, bool bridge) {
  Settings s;
  { std::lock_guard<std::mutex> lock(mutex_); s = settings_; bridge_present_ = bridge;
    usb_present_ = path.empty() ? 0 : 1; usb_id_ = path.empty() ? "" : identity_; }
  const auto desired = select_route(s, !path.empty());
  const auto key = session_key(desired, s) + (desired == "usb" ? std::string(path.begin(), path.end()) : "");
  if (key != current_key_) {
    stop_worker(); current_key_ = key; retry_at_ = 0;
    if (desired == "none") state("idle", "Waiting for a device");
  }
  if (desired != "none" && worker_done_.load(std::memory_order_acquire) && now >= retry_at_) {
    stop_worker(); start_worker(desired, s, identity_, path); retry_at_ = now + 3000;
  }
}
Settings DeviceSession::settings() const { std::lock_guard<std::mutex> lock(mutex_); return settings_; }
std::string DeviceSession::status(const std::string &error) const {
  std::lock_guard<std::mutex> lock(mutex_); return status_locked(error);
}
std::string DeviceSession::summary() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return "{\"id\":" + json_string(identity_) + ",\"name\":" + json_string("Rey Audio " + identity_.substr(24)) +
      ",\"route\":" + json_string(route_) + ",\"state\":" + json_string(state_) +
      ",\"usb\":" + (usb_present_ ? "true" : "false") +
      ",\"lan\":" + (settings_.lan_enabled ? "true" : "false") +
      ",\"callbacks\":" + std::to_string(callbacks_.load()) + "}";
}
bool DeviceSession::update(const Settings &next, std::string &error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!save(path_, next, error, key_)) return false;
  settings_ = next; mixer_.publish(next.mix); ++revision_; return true;
}
std::string DeviceSession::status_locked(const std::string &error) const {
  const auto &s = settings_;
  std::ostringstream out;
  out << "{\"version\":\"2.7.0\",\"ok\":" << (error.empty() ? "true" : "false")
      << ",\"error\":" << json_string(error)
      << ",\"state\":" << json_string(state_)
      << ",\"detail\":" << json_string(detail_)
      << ",\"route\":" << json_string(route_)
      << ",\"preferred\":" << json_string(s.preferred)
      << ",\"driver_present\":" << (bridge_present_ ? "true" : "false")
      << ",\"digital_test\":" << (digital_test_ ? "true" : "false")
      << ",\"identity\":" << json_string(identity_)
      << ",\"backend\":" << json_string(backend_)
      << ",\"usb\":{\"present\":" << usb_present_
      << ",\"identity\":" << json_string(usb_id_) << ",\"rate\":" << s.usb.rate
      << ",\"bits\":" << s.usb.bits << ",\"depth\":" << s.usb_depth
      << ",\"block\":" << s.usb.block << ",\"guard\":" << s.usb.safety
      << ",\"automatic\":" << (s.usb_auto ? "true" : "false") << "}"
      << ",\"lan\":{\"peer\":" << json_string(s.lan.peer)
      << ",\"enabled\":" << (s.lan_enabled ? "true" : "false")
      << ",\"port\":" << s.lan.port << ",\"block\":" << s.lan.block
      << ",\"guard\":" << s.lan.safety << ",\"frames\":" << s.lan.capture_frames
      << ",\"energy\":" << (s.lan.energy_saving ? "true" : "false") << "}"
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
      << "},\"mixer\":{\"master_cdb\":" << s.mix.master_cdb << ",\"master_mute\":" << (s.mix.master_mute ? "true" : "false");
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
