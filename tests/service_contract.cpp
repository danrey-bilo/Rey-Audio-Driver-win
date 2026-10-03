#include "../src/service/settings.hpp"
#include <cstdio>
int main(int argc, char **argv) {
  if (argc != 2)
    return 1;
  rey::service::Settings s;
  std::string error;
  const std::string test = argv[1];
  bool ok = false;
  if (test == "defaults")
    ok = rey::service::valid(s, error) && s.usb_depth == 3 &&
         s.usb.safety == 0 && s.lan.block == 256 && s.lan.safety == 1536 &&
         !s.lan_enabled;
  else if (test == "numbers") {
    unsigned n = 0;
    ok = rey::service::parse_unsigned("192000", n, 192000) && n == 192000 &&
         !rey::service::parse_unsigned("-1", n, 8192) &&
         !rey::service::parse_unsigned("999999999999", n, 8192) &&
         !rey::service::parse_unsigned("3x", n, 16) &&
         !rey::service::parse_unsigned("17", n, 16) &&
         !rey::service::parse_unsigned("", n, 16);
  } else if (test == "profile") {
    s.usb.bits = 17;
    ok = !rey::service::valid(s, error);
    s = rey::service::Settings{};
    s.usb_depth = 0;
    ok = ok && !rey::service::valid(s, error);
    s = rey::service::Settings{};
    s.lan.safety = 8193;
    ok = ok && !rey::service::valid(s, error);
  } else if (test == "routing") {
    ok = rey::service::select_route(s, true) == "usb" &&
         rey::service::select_route(s, false) == "none";
    s.lan_enabled = true;
    ok = ok && rey::service::select_route(s, true) == "lan";
    s.preferred = "usb";
    ok = ok && rey::service::select_route(s, true) == "usb" &&
         rey::service::select_route(s, false) == "none";
    s.usb_auto = false;
    ok = ok && rey::service::select_route(s, true) == "none";
  } else if (test == "isolation") {
    s.usb.rate = 44100;
    s.usb.bits = 16;
    s.usb.safety = 32;
    s.usb_depth = 4;
    ok = s.lan.rate == 192000 && s.lan.bits == 32 && s.lan.safety == 1536 &&
         s.lan.block == 256;
    s.lan.safety = 512;
    ok = ok && s.usb.rate == 44100 && s.usb.bits == 16 && s.usb.safety == 32 &&
         s.usb_depth == 4;
  } else if (test == "json")
    ok = rey::service::json_string("x\"\\\n") == "\"x\\\"\\\\\\u000a\"";
  else if (test == "peer") {
    s.lan_enabled = true;
    ok = !rey::service::valid(s, error);
    std::snprintf(s.lan.peer, sizeof(s.lan.peer), "%s", "192.168.1.2");
    ok = ok && rey::service::valid(s, error);
    std::snprintf(s.lan.peer, sizeof(s.lan.peer), "%s",
                  "192.168.1.2 && command");
    ok = ok && !rey::service::valid(s, error);
  } else if (test == "persistence" || test == "corruption") {
    wchar_t temporary[MAX_PATH]{}, name[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, temporary) || !GetTempFileNameW(temporary, L"rey", 0, name)) return 1;
    s.usb.rate = 44100; s.usb.bits = 24; s.usb_depth = 4;
    s.lan.safety = 512; s.lan_enabled = true; s.preferred = "lan";
    s.mix.inputs[3].gain_cdb = -602; s.mix.inputs[3].solo = true;
    s.mix.outputs[7].invert = true; s.mix.master_mute = true;
    std::snprintf(s.lan.peer, sizeof(s.lan.peer), "%s", "192.168.1.2");
    rey::service::Settings restored;
    ok = rey::service::save(name, s, error) && rey::service::load(name, restored, error) &&
        restored.usb.rate == 44100 && restored.usb.bits == 24 && restored.usb_depth == 4 &&
        restored.lan.safety == 512 && restored.lan_enabled && restored.preferred == "lan" &&
        std::string(restored.lan.peer) == "192.168.1.2" && restored.mix.inputs[3].gain_cdb == -602 &&
        restored.mix.inputs[3].solo && restored.mix.outputs[7].invert && restored.mix.master_mute;
    if (test == "corruption" && ok) {
      auto file = CreateFileW(name, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
      DWORD written = 0; const BYTE bad[] = {0x52, 0x59, 0x53, 0x31};
      ok = file != INVALID_HANDLE_VALUE && WriteFile(file, bad, sizeof(bad), &written, nullptr);
      if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
      const auto before = restored.usb.rate;
      ok = ok && !rey::service::load(name, restored, error) && restored.usb.rate == before;
    }
    DeleteFileW(name);
  }
  std::printf("REY_CONTRACT %s %s\n", argv[1], ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
