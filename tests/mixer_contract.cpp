#include "../src/audio/mixer.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
int main(int argc, char **argv) {
  if (argc != 2) return 1;
  const std::string test = argv[1];
  rey::audio::Mixer mixer;
  rey::audio::Mix mix;
  std::array<int32_t, 256 * 8> source{}, render{};
  source.fill(4096);
  rey::engine::AudioBlock original{source.data(), render.data(), 128, 8, 8, 16}, block;
  bool ok = true;
  auto run = [&] {
    if (!mixer.capture(original, block)) return false;
    std::copy_n(block.capture, block.frames * block.inputs, block.render);
    mixer.render(block); return true;
  };
  if (test == "unity") {
    for (unsigned bits : {16u, 24u, 32u}) {
      original.bits = bits; const auto full = int64_t(1) << (bits - 1);
      for (unsigned i = 0; i < source.size(); ++i) source[i] = i % 3 == 0 ? int32_t(-full) : i % 3 == 1 ? int32_t(full - 1) : int32_t(full / 3);
      ok = ok && run() && std::equal(source.begin(), source.begin() + 128 * 8, render.begin());
      for (unsigned ch = 0; ch < 8; ++ch) ok = ok && mixer.clips(0, ch) == 0 && mixer.clips(1, ch) == 0;
    }
  } else if (test == "solo_mute") {
    mix.inputs[1].solo = true; mixer.publish(mix); ok = run();
    for (unsigned ch = 0; ch < 8; ++ch) ok = ok && render[127 * 8 + ch] == (ch == 1 ? 4096 : 0);
    mix.master_mute = true; mixer.publish(mix); ok = ok && run();
    for (unsigned ch = 0; ch < 8; ++ch) ok = ok && render[127 * 8 + ch] == 0;
  } else if (test == "gain_clip") {
    source.fill(30000); mix.inputs[0].gain_cdb = 1200; mixer.publish(mix); ok = run();
    ok = ok && render[127 * 8] == 32767 && mixer.clips(0, 0) > 0 && mixer.peak(0, 0) <= 1.f;
    source.fill(-32768); mix.inputs[0].gain_cdb = 0; mix.inputs[0].invert = true; mixer.publish(mix); ok = ok && run();
    ok = ok && render[127 * 8] == 32767 && render[127 * 8 + 1] == -32768;
  } else if (test == "ramp") {
    mix.inputs[0].mute = true; mixer.publish(mix); ok = run();
    ok = ok && block.capture[0] > 0 && block.capture[0] < source[0] && block.capture[63 * 8] == 0 && block.capture[127 * 8] == 0;
    for (unsigned f = 1; f < 64; ++f) ok = ok && block.capture[f * 8] <= block.capture[(f - 1) * 8];
    ok = ok && mixer.peak(0, 1) == .125f;
  } else if (test == "bounds") {
    original.frames = 257; ok = !mixer.capture(original, block);
    original.frames = 128; original.inputs = 9; ok = ok && !mixer.capture(original, block);
    original.inputs = 8; original.bits = 20; ok = ok && !mixer.capture(original, block);
    mix.master_cdb = 601; ok = ok && !rey::audio::valid(mix);
  } else return 1;
  std::printf("REY_MIXER %s %s\n", argv[1], ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
