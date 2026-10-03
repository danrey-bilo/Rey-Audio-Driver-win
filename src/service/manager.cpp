#include "manager.hpp"
#include "commands.hpp"
#include "../bridge/driver_bridge.hpp"
#include "../engine/peer_session.hpp"
#include "../platform/device_names.hpp"
#include "../platform/firewall.hpp"
#include "../../include/piaoip/endpoints.h"
#ifdef PIAOIP_ENABLE_USB
#include "pi5ausb/windows.hpp"
#endif
#include <sstream>
#include <set>
namespace rey::service {
Manager::Manager(std::wstring path, bool test) : path_(std::move(path)), store_(path_), digital_test_(test) {}
Manager::~Manager() {
  sessions_.clear();
  if (winsock_) WSACleanup();
}
bool Manager::initialize(std::string &error) {
  WSADATA data{};
  if (WSAStartup(MAKEWORD(2, 2), &data)) { error = "Winsock initialization failed"; return false; }
  winsock_ = true;
  Settings defaults;
  if (!load(path_, defaults, error)) return false;
  draft_ = std::make_unique<DeviceSession>("", path_, L"Software\\ReyAudio", defaults, digital_test_);
  const auto ids = store_.devices(error);
  if (!error.empty() || ids.size() > PIAOIP_DEVICE_LIMIT) {
    if (error.empty()) error = "More than ten stored cards; remove unused profiles";
    return false;
  }
  for (const auto &id : ids) if (!add(id, error)) return false;
  selected_ = store_.selection();
  if (!sessions_.count(selected_)) selected_ = sessions_.empty() ? "" : sessions_.begin()->first;
  bridge_present_ = piaoip::bridge::DriverBridge::available();
  if (path_.empty()) {
    std::string warning;
    if (!rey::update_lan_firewall(ports(), warning)) catalog_error_ = warning;
  }
  return true;
}
DeviceSession *Manager::add(const std::string &id, std::string &error) {
  const auto existing = sessions_.find(id);
  if (existing != sessions_.end()) return existing->second.get();
  if (!valid_device_id(id)) { error = "Invalid DeviceId"; return nullptr; }
  if (sessions_.size() >= PIAOIP_DEVICE_LIMIT) { error = "Ten-card limit reached; forget an unused card"; return nullptr; }
  Settings s;
  // Legacy single-card USB/mixer preferences migrate to the first discovered
  // card only. A LAN peer is assigned only after read-only identity matching.
  if (sessions_.empty() && store_.devices(error).empty()) {
    s = draft_->settings(); s.lan_enabled = false; s.lan.peer[0] = 0; s.preferred = "auto";
  }
  s.lan.port = uint16_t(50021 + sessions_.size());
  if (!store_.load(id, s, error)) return nullptr;
  if (!store_.save(id, s, error)) return nullptr;
  auto session = std::make_unique<DeviceSession>(id, store_.path(id), store_.key(id), s, digital_test_);
  auto *result = session.get(); sessions_.emplace(id, std::move(session));
  if (selected_.empty()) selected_ = id;
  return result;
}
std::vector<uint16_t> Manager::ports(const std::string &id, const Settings *replacement) const {
  std::set<uint16_t> values;
  for (const auto &entry : sessions_) {
    const auto s = replacement && entry.first == id ? *replacement : entry.second->settings();
    if (s.lan_enabled || s.lan.peer[0]) values.insert(s.lan.port);
  }
  if (values.empty()) values.insert(50021);
  return {values.begin(), values.end()};
}
bool Manager::persist(DeviceSession &session, const std::string &id, const Settings &next, std::string &error) {
  if (next.lan_enabled)
    for (const auto &entry : sessions_) {
      if (entry.first == id) continue;
      const auto other = entry.second->settings();
      if (other.lan_enabled && next.lan.port == other.lan.port) {
        error = "Each LAN card needs a different local UDP port"; return false;
      }
    }
  const auto before = ports(), after = ports(id, &next);
  if (path_.empty() && before != after && !rey::update_lan_firewall(after, error)) return false;
  if (session.update(next, error)) return true;
  if (path_.empty() && before != after) { std::string ignored; rey::update_lan_firewall(before, ignored); }
  return false;
}
void Manager::scan_usb() {
#ifdef PIAOIP_ENABLE_USB
  std::string error;
  const auto paths = pi5ausb::WindowsDevice::interface_paths(error);
  for (auto i = usb_paths_.begin(); i != usb_paths_.end();) {
    if (std::find(paths.begin(), paths.end(), i->first) == paths.end()) i = usb_paths_.erase(i);
    else ++i;
  }
  for (const auto &path : paths) {
    if (usb_paths_.count(path)) continue;
    pi5ausb::WindowsDevice usb;
    if (!usb.open(path, error)) continue;
    const std::string id(usb.device().hello.id.data(), 32);
    bool duplicate = false;
    for (const auto &known : usb_paths_) duplicate |= known.second == id;
    if (duplicate) { catalog_error_ = "Duplicate USB DeviceId: " + id; continue; }
    if (!add(id, error)) { catalog_error_ = error; continue; }
    usb_paths_[path] = id;
    rey::normalize_usb_name(path);
  }
#endif
  bridge_present_ = piaoip::bridge::DriverBridge::available();
}
void Manager::run(HANDLE stop) {
  uint64_t last_scan = 0;
  // Migrate a remembered LAN connection by its identity, never by UI selection.
  Settings legacy; bool migrate;
  { std::lock_guard<std::mutex> lock(mutex_); legacy = draft_->settings(); migrate = sessions_.empty(); }
  if (legacy.lan_enabled && migrate) (void)connect_lan(legacy);
  while (WaitForSingleObject(stop, 200) == WAIT_TIMEOUT) {
    const auto now = GetTickCount64();
    std::lock_guard<std::mutex> lock(mutex_);
    if (now - last_scan >= 1000) { last_scan = now; scan_usb(); }
    for (auto &entry : sessions_) {
      std::wstring path;
      for (const auto &usb : usb_paths_) if (usb.second == entry.first) { path = usb.first; break; }
      entry.second->poll(now, path, bridge_present_);
    }
  }
  std::lock_guard<std::mutex> lock(mutex_);
  sessions_.clear(); // Stops each owned stream and detaches only its endpoints.
}
std::string Manager::status_locked(const std::string &error) const {
  const auto found = sessions_.find(selected_);
  std::string text = found == sessions_.end() ? draft_->status(error) : found->second->status(error);
  text.pop_back();
  text += ",\"selected_device\":" + json_string(selected_) + ",\"catalog_error\":" + json_string(catalog_error_) + ",\"devices\":[";
  bool comma = false;
  for (const auto &entry : sessions_) {
    if (comma) text += ',';
    comma = true; text += entry.second->summary();
  }
  return text + "]}";
}
std::string Manager::connect_lan(const Settings &settings) {
  auto cfg = settings.lan;
  piaoip::engine::PeerSession peer;
  std::string error;
  if (!peer.prepare(cfg, error)) {
    std::lock_guard<std::mutex> lock(mutex_); return status_locked(error);
  }
  const std::string id(peer.identity());
  std::lock_guard<std::mutex> lock(mutex_);
  const auto before = selected_;
  auto *session = add(id, error);
  if (!session) return status_locked(error);
  auto next = session->settings();
  next.lan = settings.lan; next.lan_enabled = true; next.preferred = "lan";
  if (!persist(*session, id, next, error)) { selected_ = before; return status_locked(error); }
  if (!store_.select(id, error)) return status_locked(error);
  selected_ = id; return status_locked();
}
std::string Manager::request(const std::string &command) {
  if (command == "SCAN") {
    piaoip::SettingsDialog discovery; piaoip::discover_devices(discovery);
    std::string text = "{\"ok\":true,\"devices\":["; bool comma = false;
    for (const auto &d : discovery.devices) {
      if (comma) text += ','; comma = true;
      text += "{\"ip\":" + json_string(d.ip) + ",\"rate\":" + std::to_string(d.rate) +
          ",\"bits\":" + std::to_string(d.bits) + ",\"inputs\":" + std::to_string(d.channels) +
          ",\"outputs\":" + std::to_string(d.outputs) + "}";
    }
    return text + "]}";
  }
  std::string action = command, id;
  {
    std::lock_guard<std::mutex> lock(mutex_); id = selected_;
    if (command.compare(0, 7, "DEVICE ") == 0) {
      if (command.size() <= 40 || command[39] != ' ') return status_locked("Malformed device command");
      id = command.substr(7, 32); action = command.substr(40);
      if (!valid_device_id(id) || !sessions_.count(id)) return status_locked("Unknown DeviceId");
    }
    if (action == "STATUS") return status_locked();
    if (action.compare(0, 7, "SELECT ") == 0) {
      const auto choice = action.substr(7); std::string error;
      if (!sessions_.count(choice)) return status_locked("Unknown DeviceId");
      if (!store_.select(choice, error)) return status_locked(error);
      selected_ = choice; return status_locked();
    }
    if (action.compare(0, 7, "FORGET ") == 0) {
      const auto choice = action.substr(7); const auto i = sessions_.find(choice);
      if (i == sessions_.end()) return status_locked("Unknown DeviceId");
      const auto s = i->second->settings();
      for (const auto &usb : usb_paths_) if (usb.second == choice) return status_locked("Disconnect USB before forgetting a card");
      if (s.lan_enabled) return status_locked("Disconnect LAN before forgetting a card");
      std::string error;
      if (!store_.forget(choice, error)) return status_locked(error);
      sessions_.erase(i);
      if (selected_ == choice) selected_ = sessions_.empty() ? "" : sessions_.begin()->first;
      if (!store_.select(selected_, error)) return status_locked(error);
      return status_locked();
    }
  }
  if (action.compare(0, 8, "ADD_LAN ") == 0 || action == "LAN_CONNECT") {
    Settings next;
    { std::lock_guard<std::mutex> lock(mutex_);
      const auto i = sessions_.find(id); next = i == sessions_.end() ? draft_->settings() : i->second->settings();
    }
    std::string error;
    if (action != "LAN_CONNECT" && !command_settings(next, "LAN " + action.substr(8), next, error)) {
      std::lock_guard<std::mutex> lock(mutex_); return status_locked(error);
    }
    return connect_lan(next);
  }
  std::lock_guard<std::mutex> lock(mutex_);
  const auto i = sessions_.find(id); auto &session = i == sessions_.end() ? *draft_ : *i->second;
  Settings next; std::string error;
  if (!command_settings(session.settings(), action, next, error) || !persist(session, id, next, error)) return status_locked(error);
  return status_locked();
}
}
