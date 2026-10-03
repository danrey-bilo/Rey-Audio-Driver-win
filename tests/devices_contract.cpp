#include "../src/service/manager.hpp"
#include <filesystem>
#include <iostream>
namespace piaoip { HMODULE g_module = nullptr; std::atomic<long> g_objects{0}; }
using namespace rey::service;
static const std::string a = "00000000000000000000000000000001", b = "00000000000000000000000000000002";
static bool check(bool ok, const char *what) { if (!ok) std::cerr << what << '\n'; return ok; }
static bool good(const std::string &reply) { return reply.find("\"ok\":true") != std::string::npos; }
int main(int argc, char **argv) {
  if (argc != 2) return 1;
  const std::string test(argv[1]);
  const auto base = (std::filesystem::current_path() / ("cards-" + test + "-" + std::to_string(GetCurrentProcessId()) + ".dat")).wstring();
  DeviceStore store(base); Settings first, second; std::string error; bool ok = true;
  first.usb_auto = second.usb_auto = false; second.lan.port = 50022;
  if (test == "ports") {
    std::snprintf(first.lan.peer, sizeof(first.lan.peer), "192.0.2.1");
    std::snprintf(second.lan.peer, sizeof(second.lan.peer), "192.0.2.2");
    first.lan_enabled = second.lan_enabled = true;
  }
  ok &= check(store.save(a, first, error) && store.save(b, second, error), "save independent board profiles");
  {
    Manager m(base, false); ok &= check(m.initialize(error), error.c_str());
    if (test == "catalog") {
      ok &= check(store.devices(error).size() == 2, "two unique card identities");
      ok &= check(store.save(a, first, error) && store.devices(error).size() == 2, "same identity does not duplicate a card");
      ok &= check(!store.save("../bad", first, error), "reject non-identity storage path");
      const auto reply = m.request("STATUS");
      ok &= check(reply.find(a) != std::string::npos && reply.find(b) != std::string::npos, "both cards remain in catalog");
    } else if (test == "selection") {
      ok &= check(good(m.request("SELECT " + a)), "select first card");
      ok &= check(good(m.request("SELECT " + b)), "select second card");
      Settings x, y; store.load(a, x, error); store.load(b, y, error);
      ok &= check(!x.usb_auto && !y.usb_auto && !x.lan_enabled && !y.lan_enabled, "selector does not enable or disable transports");
      ok &= check(!good(m.request("SELECT 000000000000000000000000000000ff")), "reject unknown selection");
      ok &= check(store.selection() == b, "selection persisted independently of audio profiles");
    } else if (test == "isolation" || test == "restart") {
      m.request("SELECT " + a);
      ok &= check(good(m.request("DEVICE " + b + " MIX 0:6 4500 1 0 1 apply")), "explicit device command");
      ok &= check(good(m.request("DEVICE " + a + " MASTER 5700 0")), "independent master");
      Settings x, y; store.load(a, x, error); store.load(b, y, error);
      ok &= check(x.mix.inputs[6].gain_cdb == 0 && !x.mix.inputs[6].mute &&
          y.mix.inputs[6].gain_cdb == -1500 && y.mix.inputs[6].mute && y.mix.inputs[6].invert &&
          x.mix.master_cdb == -300 && y.mix.master_cdb == 0, "mixer and master changes isolated by DeviceId");
      ok &= check(store.selection() == a, "command to other card does not change view");
      ok &= check(!good(m.request("DEVICE " + a + " MIX 0:8 6000 0 0 0 apply")), "invalid scoped command rejected");
    } else if (test == "ports") {
      ok &= check(!good(m.request("DEVICE " + b + " LAN 192.0.2.2 50021 256 1536 32 0")), "duplicate active LAN ports rejected");
      ok &= check(good(m.request("DEVICE " + b + " LAN 192.0.2.2 50023 256 1536 32 0")), "distinct LAN port accepted");
    } else if (test == "forget") {
      m.request("SELECT " + a);
      ok &= check(good(m.request("FORGET " + a)) && store.devices(error).size() == 1,
                  "forget one offline profile");
      Settings x; ok &= check(store.load(b, x, error) && x.lan.port == 50022 && !x.usb_auto,
                  "another profile remains intact");
      const auto status = m.request("STATUS");
      ok &= check(status.find("\"selected_device\":\"" + b + "\"") != std::string::npos,
                  "show remaining card after forgetting selected offline card");
      ok &= check(!good(m.request("DEVICE " + a + " MIX_RESET")), "forgotten card cannot receive controls");
    } else return 1;
  }
  if (test == "restart") {
    Manager restarted(base, false); ok &= check(restarted.initialize(error), "service restores catalog");
    const auto response = restarted.request("STATUS");
    ok &= check(response.find("\"selected_device\":\"" + a + "\"") != std::string::npos,
                "selected card restored without merging its mixer with another");
    Settings y; store.load(b, y, error); ok &= check(y.mix.inputs[6].gain_cdb == -1500, "other mixer survives restart");
  }
  for (const auto &id : {a, b}) DeleteFileW(store.path(id).c_str());
  DeleteFileW((base + L".selected").c_str());
  return ok ? 0 : 2;
}
