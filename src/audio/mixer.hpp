#pragma once
#include "../engine/audio_block.hpp"
#include <array>
#include <atomic>
#include <cstdint>
namespace rey::audio {
struct Channel {
  int gain_cdb = 0;
  bool mute = false, solo = false, invert = false;
};
struct Mix {
  std::array<Channel, 8> inputs{}, outputs{};
  int master_cdb = 0;
  bool master_mute = false;
};
bool valid(const Mix &);
class Mixer {
public:
  Mixer();
  void publish(const Mix &); // Control thread only; pow/serialization stay here.
  void reset();             // Called only after the audio worker has joined.
  bool capture(const piaoip::engine::AudioBlock &, piaoip::engine::AudioBlock &);
  void render(const piaoip::engine::AudioBlock &);
  float peak(unsigned direction, unsigned channel) const;
  uint64_t clips(unsigned direction, unsigned channel) const;
private:
  struct Ramp { float value = 1, target = 1, step = 0; unsigned left = 0; };
  std::array<std::array<std::atomic<uint64_t>, 8>, 2> controls_{};
  std::array<std::atomic<unsigned>, 2> solo_{};
  std::atomic<uint32_t> master_{0};
  std::array<std::array<std::atomic<uint32_t>, 8>, 2> peaks_{};
  std::array<std::array<std::atomic<uint64_t>, 8>, 2> clips_{};
  std::array<std::array<Ramp, 8>, 2> ramps_{};
  std::array<int32_t, 256 * 8> capture_{};
  void apply(unsigned, const int32_t *, int32_t *, unsigned, unsigned, unsigned);
};
} // namespace rey::audio
