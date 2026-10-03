#include "../src/service/manager.hpp"
#include <filesystem>
#include <iostream>
using namespace rey::service;
static bool good(const std::string &reply) { return reply.find("\"ok\":true") != std::string::npos; }
int main(int argc, char **argv) {
  if (argc != 2) return 1;
  const std::string test(argv[1]); bool ok = true;
  if (test == "empty") ok = select_usb({}, L"").path.empty() && select_usb({}, L"").warning.empty();
  else if (test == "single") ok = select_usb({L"a"}, L"").path == L"a" && select_usb({L"a"}, L"").warning.empty();
  else if (test == "sticky") {
    auto selection = select_usb({L"b", L"a"}, L"a");
    ok = selection.path == L"a" && !selection.warning.empty();
    ok &= select_usb({L"a", L"b"}, L"b").path == L"b";
  } else if (test == "ambiguous") ok = select_usb({L"a", L"b"}, L"").path.empty() && !select_usb({L"a", L"b"}, L"").warning.empty();
  else if (test == "disconnect") {
    ok = select_usb({}, L"a").path.empty() && select_usb({L"b"}, L"a").path == L"b" &&
         select_usb({L"b", L"c"}, L"a").path.empty() && select_usb({L"a"}, L"a").path == L"a";
  } else {
    const auto base = (std::filesystem::current_path() / ("usb-single-" + test + "-" + std::to_string(GetCurrentProcessId()) + ".dat")).wstring();
    Settings settings; settings.usb_auto = false; std::string error;
    ok = save(base, settings, error);
    {
      Manager m(base, false, true); ok &= m.initialize(error);
      if (test == "persistence") {
        ok &= good(m.request("MIX 0:6 4500 1 0 1 apply")) && good(m.request("MASTER 5700 0"));
        Settings x; ok &= load(base, x, error) && x.mix.inputs[6].gain_cdb == -1500 && x.mix.inputs[6].mute &&
            x.mix.inputs[6].invert && x.mix.master_cdb == -300;
        Manager restarted(base, false, true); ok &= restarted.initialize(error);
        ok &= restarted.request("STATUS").find("\"gain_cdb\":-1500") != std::string::npos;
      } else if (test == "retired") {
        const auto before = m.request("STATUS");
        for (const auto command : {"SCAN", "LAN_CONNECT", "LAN_DISCONNECT", "USE lan", "USE usb", "ADD_LAN 192.0.2.1 50021 64 0 32 0",
                                  "SELECT 00000000000000000000000000000001", "FORGET 00000000000000000000000000000001",
                                  "DEVICE 00000000000000000000000000000001 MIX_RESET"}) ok &= !good(m.request(command));
        const auto after = m.request("STATUS");
        ok &= before == after && after.find("\"device_limit\":1") != std::string::npos &&
            after.find("\"devices\"") == std::string::npos && after.find("\"lan\"") == std::string::npos;
      } else if (test == "asio_auth") {
        for (const auto command : {"ASIO INFO other", "ASIO OPEN auto 999 1 2 64 3", "ASIO CLOSE auto extra", "ASIO RATE auto 99999"})
          ok &= m.request(command).find("REY_ASIO_ERROR ") == 0;
        ok &= !valid_device_id("../bad") && !valid_device_id(std::string(32, '0')) &&
            valid_device_id("00000000000000000000000000000001");
      } else ok = false;
    }
    DeleteFileW(base.c_str());
  }
  std::cout << "REY_SINGLE_USB " << test << (ok ? " PASS\n" : " FAIL\n");
  return ok ? 0 : 1;
}
