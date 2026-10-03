#pragma once
#include "../engine/audio_block.hpp"
#include "settings.hpp"
#include <deque>
#include <mutex>
namespace rey::service {
class Manager {
public:
  Manager(std::wstring configuration, bool digital_test);
  ~Manager();
  bool initialize(std::string &error);
  void run(HANDLE stop);
  std::string request(const std::string &command);

private:
  void start_worker(const std::string &route, Settings settings,
                    std::string expected_usb);
  void stop_worker();
  void worker(const std::string &route, Settings settings,
              const std::string &expected_usb);
  void state(const std::string &code, const std::string &detail);
  std::string status_locked(const std::string &error = "") const;
  std::wstring path_;
  bool digital_test_;
  mutable std::mutex mutex_;
  Settings settings_;
  uint64_t revision_ = 0;
  std::string state_ = "idle", detail_, route_ = "none", identity_, backend_,
              usb_id_;
  unsigned usb_present_ = 0;
  bool bridge_present_ = false;
  piaoip::Config active_;
  struct Event {
    uint64_t utc_ms;
    std::string code, detail;
  };
  std::deque<Event> events_;
  std::thread worker_;
  HANDLE worker_stop_ = nullptr;
  std::atomic<bool> worker_done_{true};
  std::atomic<uint64_t> callbacks_{0}, frames_{0}, missing_{0}, gap_max_{0};
  struct CallbackContext {
    Manager *manager;
    void *bridge;
    bool echo;
    uint64_t previous = 0;
  };
  static bool process(void *, const piaoip::engine::AudioBlock &);
};
} // namespace rey::service
