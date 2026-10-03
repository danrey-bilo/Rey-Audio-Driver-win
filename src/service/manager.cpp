#include "manager.hpp"
#include "../bridge/driver_bridge.hpp"
#include "../engine/session_engine.hpp"
#include "../platform/device_names.hpp"
#include "../platform/firewall.hpp"
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
Manager::Manager(std::wstring configuration, bool test)
    : path_(std::move(configuration)), digital_test_(test) {}
Manager::~Manager() { stop_worker(); }
bool Manager::initialize(std::string &error) {
  if (!load(path_, settings_, error))
    return false;
  mixer_.publish(settings_.mix);
  if (path_.empty()) {
    std::string firewall_error;
    if (!rey::update_lan_firewall(settings_.lan.port, firewall_error)) state("firewall_warning", firewall_error);
  }
  WSADATA data{};
  if (WSAStartup(MAKEWORD(2, 2), &data)) {
    error = "Winsock initialization failed";
    return false;
  }
  state("idle", "Service is ready");
  return true;
}
void Manager::state(const std::string &code, const std::string &detail) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ == code && detail_ == detail)
    return;
  state_ = code;
  detail_ = detail;
  events_.push_back({utc_ms(), code, detail});
  while (events_.size() > 40)
    events_.pop_front();
}
void Manager::stop_worker() {
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
void Manager::start_worker(const std::string &route, Settings s,
                           std::string expected) {
  worker_stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!worker_stop_) {
    state("error", "Cannot allocate session stop event");
    return;
  }
  callbacks_ = frames_ = missing_ = gap_max_ = 0;
  processing_sum_ = processing_max_ = 0;
  worker_done_ = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    route_ = route;
  }
  state("connecting",
        route == "usb" ? "Opening USB device" : "Connecting to the LAN device");
  worker_ = std::thread([this, route, s, expected] {
    worker(route, s, expected);
    worker_done_.store(true, std::memory_order_release);
  });
}
bool Manager::process(void *opaque, const piaoip::engine::AudioBlock &b) {
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
void Manager::worker(const std::string &route, Settings settings,
                     const std::string &expected) {
  piaoip::bridge::DriverBridge bridge;
  CallbackContext context{this, &bridge, digital_test_};
  std::string error;
  if (route == "usb") {
#ifdef PIAOIP_ENABLE_USB
    piaoip::engine::UsbSession session;
    if (!session.open(settings.usb, expected, error)) {
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
      identity_ = usb_id_ = id;
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
      identity_ = session.peer().identity();
      backend_ = session.peer().backend();
      active_ = session.config(); // LAN format is negotiated from the Pi.
    }
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
void Manager::run(HANDLE stop) {
  std::string current_key, current_route = "none";
  uint64_t last_scan = 0, retry_at = 0;
  std::vector<std::wstring> known_paths;
  while (WaitForSingleObject(stop, 200) == WAIT_TIMEOUT) {
    const auto now = GetTickCount64();
    if (now - last_scan >= 1000) {
      last_scan = now;
      std::string error;
      std::vector<std::wstring> paths;
#ifdef PIAOIP_ENABLE_USB
      paths = pi5ausb::WindowsDevice::interface_paths(error);
#endif
      const bool changed = known_paths != paths;
      known_paths = paths;
      const bool available = piaoip::bridge::DriverBridge::available();
      bool needs_identity = false;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        usb_present_ = unsigned(paths.size());
        bridge_present_ = available;
        if (paths.empty())
          usb_id_.clear();
        needs_identity = usb_id_.empty();
      }
#ifdef PIAOIP_ENABLE_USB
      if (paths.size() == 1 && (changed || needs_identity) &&
          (current_route != "usb" ||
           worker_done_.load(std::memory_order_acquire))) {
        pi5ausb::WindowsDevice device;
        if (device.open(paths.front(), error)) {
          rey::normalize_usb_name(paths.front());
          std::lock_guard<std::mutex> lock(mutex_);
          usb_id_.assign(device.device().hello.id.data(), 32);
        }
      }
#endif
    }
    Settings s;
    std::string expected;
    unsigned present;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      s = settings_;
      expected = usb_id_;
      present = usb_present_;
    }
    const auto desired = select_route(s, present == 1 && !expected.empty());
    const auto key =
        session_key(desired, s) + (desired == "usb" ? expected : "");
    const bool changed = key != current_key;
    if (changed) {
      if (worker_.joinable())
        state("stopping", "Applying connection settings");
      stop_worker();
      current_key = key;
      current_route = desired;
      retry_at = 0;
      if (desired == "none")
        state(present > 1 ? "select_device" : "idle",
              present > 1 ? "This build supports one USB device at a time"
                          : "Waiting for a device");
    }
    if (desired != "none" && worker_done_.load(std::memory_order_acquire) &&
        now >= retry_at) {
      stop_worker();
      start_worker(desired, s, expected);
      retry_at = now + 3000;
    }
  }
  state("stopping", "Service is stopping");
  stop_worker();
  WSACleanup();
}
std::string Manager::status_locked(const std::string &error) const {
  const auto &s = settings_;
  std::ostringstream out;
  out << "{\"version\":\"2.6.1\",\"ok\":" << (error.empty() ? "true" : "false")
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
      << ",\"stats\":{\"callbacks\":" << callbacks_.load()
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
std::string Manager::request(const std::string &command) {
  if (command == "SCAN") {
    piaoip::SettingsDialog devices;
    piaoip::discover_devices(devices);
    std::ostringstream out;
    out << "{\"ok\":true,\"devices\":[";
    bool comma = false;
    for (const auto &d : devices.devices) {
      if (comma)
        out << ',';
      comma = true;
      out << "{\"ip\":" << json_string(d.ip) << ",\"rate\":" << d.rate
          << ",\"bits\":" << d.bits << ",\"inputs\":" << d.channels
          << ",\"outputs\":" << d.outputs << '}';
    }
    return out.str() + "]}";
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (command == "STATUS")
    return status_locked();
  std::istringstream input(command);
  std::vector<std::string> fields;
  for (std::string f; input >> f;)
    fields.push_back(f);
  Settings next = settings_;
  std::string error;
  bool accepted = false;
  if (fields.size() == 7 && fields[0] == "MIX") {
    unsigned direction = 0, channel = 0, biased_gain = 0, mute = 0, solo = 0, phase = 0;
    const auto split = fields[1].find(':');
    accepted = split != std::string::npos && parse_unsigned(fields[1].substr(0, split), direction, 1) &&
      parse_unsigned(fields[1].substr(split + 1), channel, 7) && parse_unsigned(fields[2], biased_gain, 7200) &&
      parse_unsigned(fields[3], mute, 1) && parse_unsigned(fields[4], solo, 1) && parse_unsigned(fields[5], phase, 1) && fields[6] == "apply";
    if (accepted) {
      auto &c = direction == 0 ? next.mix.inputs[channel] : next.mix.outputs[channel];
      c.gain_cdb = int(biased_gain) - 6000; c.mute = mute != 0; c.solo = solo != 0; c.invert = phase != 0;
    }
  } else if (fields.size() == 3 && fields[0] == "MASTER") {
    unsigned gain = 0, mute = 0;
    accepted = parse_unsigned(fields[1], gain, 6600) && parse_unsigned(fields[2], mute, 1);
    next.mix.master_cdb = int(gain) - 6000; next.mix.master_mute = mute != 0;
  } else if (command == "MIX_RESET") {
    next.mix = rey::audio::Mix{}; accepted = true;
  } else if (fields.size() == 7 && fields[0] == "USB") {
    unsigned rate = 0, bits = 0, depth = 0, block = 0, guard = 0, automatic = 0;
    accepted = parse_unsigned(fields[1], rate, 192000) &&
               parse_unsigned(fields[2], bits, 32) &&
               parse_unsigned(fields[3], depth, 16) &&
               parse_unsigned(fields[4], block, 256) &&
               parse_unsigned(fields[5], guard, 8192) &&
               parse_unsigned(fields[6], automatic, 1);
    next.usb.rate = rate;
    next.usb.bits = uint16_t(bits);
    next.usb_depth = depth;
    next.usb.block = block;
    next.usb.safety = guard;
    next.usb_auto = automatic != 0;
  } else if (fields.size() == 7 && fields[0] == "LAN") {
    unsigned port = 0, block = 0, guard = 0, frames = 0, energy = 0;
    accepted = fields[1].size() < sizeof(next.lan.peer) &&
               parse_unsigned(fields[2], port, 65535) &&
               parse_unsigned(fields[3], block, 256) &&
               parse_unsigned(fields[4], guard, 8192) &&
               parse_unsigned(fields[5], frames, 256) &&
               parse_unsigned(fields[6], energy, 1);
    std::snprintf(next.lan.peer, sizeof(next.lan.peer), "%s",
                  fields[1].c_str());
    next.lan.port = uint16_t(port);
    next.lan.block = block;
    next.lan.safety = guard;
    next.lan.capture_frames = frames;
    next.lan.energy_saving = energy != 0;
  } else if (command == "LAN_CONNECT") {
    next.lan_enabled = true;
    next.preferred = "lan";
    accepted = true;
  } else if (command == "LAN_DISCONNECT") {
    next.lan_enabled = false;
    next.preferred = "auto";
    accepted = true;
  } else if (fields.size() == 2 && fields[0] == "USE") {
    next.preferred = fields[1];
    if (fields[1] == "usb")
      next.usb_auto = true;
    accepted = true;
  }
  if (!accepted)
    return status_locked("Unknown or malformed command");
  if (!valid(next, error)) return status_locked(error);
  const bool changed_port = path_.empty() && next.lan.port != settings_.lan.port;
  if (changed_port && !rey::update_lan_firewall(next.lan.port, error)) return status_locked(error);
  if (!save(path_, next, error)) {
    if (changed_port) { std::string ignored; rey::update_lan_firewall(settings_.lan.port, ignored); }
    return status_locked(error);
  }
  settings_ = next;
  mixer_.publish(next.mix);
  ++revision_;
  return status_locked();
}
} // namespace rey::service
