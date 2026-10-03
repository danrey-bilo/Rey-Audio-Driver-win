#include "../src/service/commands.hpp"
#include "../src/audio/pcm_alignment.hpp"
#include <cstdio>
#include <limits>
using namespace rey::service;
static bool write(const wchar_t *path, const std::vector<BYTE> &bytes) {
  const auto file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
  DWORD n = 0;
  const bool ok = file != INVALID_HANDLE_VALUE && WriteFile(file, bytes.data(), DWORD(bytes.size()), &n, nullptr) && n == bytes.size();
  if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
  return ok;
}
int main(int argc, char **argv) {
  if (argc != 2) return 1;
  Settings s; std::string error; const std::string test = argv[1]; bool ok = false;
  if (test == "defaults")
    ok = valid(s, error) && s.usb.rate == 192000 && s.usb.bits == 32 && s.usb_depth == 4 &&
         s.usb.block == 64 && s.usb.safety == 0 && s.usb.inputs == 8 && s.usb.outputs == 8;
  else if (test == "numbers") {
    unsigned n = 0;
    ok = parse_unsigned("192000", n, 192000) && n == 192000;
    for (const auto text : {"-1", "999999999999", "3x", "17", ""}) ok &= !parse_unsigned(text, n, 16);
  } else if (test == "profile") {
    ok = true;
    for (unsigned rate : {44100u, 48000u, 88200u, 96000u, 176400u, 192000u})
      for (unsigned bits : {16u, 24u, 32u}) { s.usb.rate = rate; s.usb.bits = uint16_t(bits); ok &= valid(s, error); }
    for (unsigned rate : {0u, 44101u, 192001u}) { s = Settings{}; s.usb.rate = rate; ok &= !valid(s, error); }
    for (unsigned block : {0u, 8u, 31u, 257u}) { s = Settings{}; s.usb.block = block; ok &= !valid(s, error); }
    for (unsigned depth : {0u, 17u}) { s = Settings{}; s.usb_depth = depth; ok &= !valid(s, error); }
    s = Settings{}; s.usb.bits = 17; ok &= !valid(s, error);
    s = Settings{}; s.usb.inputs = 7; ok &= !valid(s, error);
    s = Settings{}; s.usb.safety = 8193; ok &= !valid(s, error);
  } else if (test == "json") ok = json_string("x\"\\\n") == "\"x\\\"\\\\\\u000a\"";
  else if (test == "alignment") {
    ok = true;
    for (unsigned bits : {16u, 24u, 32u}) {
      const int32_t maximum = int32_t((uint32_t(1) << (bits - 1)) - 1);
      const int32_t minimum = -maximum - 1;
      for (int32_t value : {minimum, minimum + 1, -1, 0, 1, maximum - 1, maximum}) {
        auto left = rey::audio::wave_sample(value, bits);
        ok &= left == uint32_t(value) << (32 - bits) && rey::audio::wire_sample(left, bits) == value;
      }
    }
  } else if (test == "commands") {
    Settings next;
    ok = command_settings(s, "USB 44100 24 4 64 0 1", next, error) && next.usb.rate == 44100 && next.usb.bits == 24;
    ok &= command_settings(next, "MIX 0:7 5800 1 0 1 apply", s, error) && s.mix.inputs[7].gain_cdb == -200 && s.mix.inputs[7].mute;
    const auto before = s.usb.rate;
    for (const auto command : {"USB -1 32 3 64 0 1", "USB 192000 17 3 64 0 1", "USB 192000 32 3 64 0 1 extra",
                              "MIX 0:8 6000 0 0 0 apply", "MASTER 6601 0"}) ok &= !command_settings(s, command, next, error);
    ok &= s.usb.rate == before;
  } else if (test == "persistence" || test == "corruption" || test == "legacy_v1" || test == "legacy_v2") {
    wchar_t temporary[MAX_PATH]{}, name[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, temporary) || !GetTempFileNameW(temporary, L"rey", 0, name)) return 1;
    s.usb.rate = 44100; s.usb.bits = 24; s.usb_depth = 4; s.usb.safety = 96; s.usb_auto = false;
    s.mix.inputs[3].gain_cdb = -602; s.mix.inputs[3].solo = true;
    s.mix.outputs[7].invert = true; s.mix.master_mute = true;
    if (test == "legacy_v1" || test == "legacy_v2") {
      std::vector<BYTE> legacy; auto number = [&](uint32_t n) { for (unsigned i = 0; i < 4; ++i) legacy.push_back(BYTE(n >> (8 * i))); };
      const unsigned version = test == "legacy_v1" ? 1 : 2;
      const std::string retired_address = "192.0.2.1";
      for (auto n : {0x31535952u, version, 44100u, 24u, 4u, 64u, 96u, 0u, 1u, 50021u, 256u, 1536u, 32u, 0u, 2u, unsigned(retired_address.size())}) number(n);
      legacy.insert(legacy.end(), retired_address.begin(), retired_address.end());
      if (version == 2) {
        for (auto *group : {&s.mix.inputs, &s.mix.outputs}) for (auto &c : *group) {
          number(unsigned(c.gain_cdb + 6000)); number(unsigned(c.mute) | (unsigned(c.solo) << 1) | (unsigned(c.invert) << 2));
        }
        number(unsigned(s.mix.master_cdb + 6000)); number(unsigned(s.mix.master_mute));
      }
      Settings restored; unsigned format = 0;
      ok = write(name, legacy) && load(name, restored, error, L"unused", &format) && format == version &&
           restored.usb.rate == 44100 && restored.usb.bits == 24 && restored.usb_depth == 4 && restored.usb.safety == 96 && !restored.usb_auto;
      ok &= version == 1 ? restored.mix.inputs[3].gain_cdb == 0 && !restored.mix.master_mute :
          restored.mix.inputs[3].gain_cdb == -602 && restored.mix.inputs[3].solo && restored.mix.outputs[7].invert && restored.mix.master_mute;
      ok &= save(name, restored, error) && load(name, restored, error, L"unused", &format) && format == 3;
      const auto f = CreateFileW(name, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
      ok &= f != INVALID_HANDLE_VALUE && GetFileSize(f, nullptr) == 168;
      if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    } else {
      Settings restored; unsigned format = 0;
      ok = save(name, s, error) && load(name, restored, error, L"unused", &format) && format == 3 &&
          restored.usb.rate == 44100 && restored.usb.bits == 24 && restored.usb_depth == 4 &&
          restored.mix.inputs[3].gain_cdb == -602 && restored.mix.inputs[3].solo && restored.mix.outputs[7].invert && restored.mix.master_mute;
      if (test == "corruption" && ok) {
        const std::vector<BYTE> bad{0x52, 0x59, 0x53, 0x31};
        ok &= write(name, bad) && !load(name, restored, error) && restored.usb.rate == 44100;
        ok &= save(name, s, error);
        const auto f = CreateFileW(name, FILE_APPEND_DATA, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        BYTE tail = 0; DWORD n = 0;
        ok &= f != INVALID_HANDLE_VALUE && WriteFile(f, &tail, 1, &n, nullptr);
        if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
        ok &= !load(name, restored, error) && restored.mix.inputs[3].gain_cdb == -602;
      }
    }
    DeleteFileW(name);
  }
  std::printf("REY_CONTRACT %s %s\n", argv[1], ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
