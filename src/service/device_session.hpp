#pragma once
#include "../engine/audio_block.hpp"
#include "settings.hpp"
#include <deque>
#include <mutex>
namespace rey::service {
class DeviceSession {
public:
  DeviceSession(std::string id, std::wstring path, std::wstring key, Settings settings, bool digital_test);
  ~DeviceSession();
  void poll(uint64_t now, const std::wstring &usb_path, bool bridge);
  Settings settings() const;
  std::string status(const std::string &error = "") const;
  std::string summary() const;
  bool update(const Settings &, std::string &error);

private:
  void start_worker(const std::string &route, Settings settings,
                    std::string expected_usb, std::wstring path);
  void stop_worker();
  void worker(const std::string &route, Settings settings,
              const std::string &expected_usb, const std::wstring &path);
  void state(const std::string &code, const std::string &detail);
  std::string status_locked(const std::string &error = "") const;
  std::wstring path_, key_;
  std::string current_key_;
  uint64_t retry_at_ = 0;
  std::atomic<uint64_t> generation_{0};
  bool digital_test_;
  mutable std::mutex mutex_;
  Settings settings_;
  uint64_t revision_ = 0;
  std::string state_ = "idle", detail_, route_ = "none", identity_, backend_,
              usb_id_;
  unsigned usb_present_ = 0;
  bool bridge_present_ = false;
  piaoip::Config active_;
  rey::audio::Mixer mixer_;
  struct Event {
    uint64_t utc_ms;
    std::string code, detail;
  };
  std::deque<Event> events_;
  std::thread worker_;
  HANDLE worker_stop_ = nullptr;
  std::atomic<bool> worker_done_{true};
  std::atomic<uint64_t> callbacks_{0}, frames_{0}, missing_{0}, gap_max_{0};
  std::atomic<uint64_t> processing_sum_{0}, processing_max_{0};
  struct CallbackContext {
    DeviceSession *manager;
    void *bridge;
    bool echo;
    uint64_t previous = 0;
  };
  static bool process(void *, const piaoip::engine::AudioBlock &);
};
} // namespace rey::service
