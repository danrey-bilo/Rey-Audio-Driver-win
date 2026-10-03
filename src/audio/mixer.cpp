#include "mixer.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace rey::audio {
static_assert(std::atomic<uint64_t>::is_always_lock_free, "Mixer needs lock-free x64 atomics");
namespace {
uint32_t bits(float value) { uint32_t word; std::memcpy(&word, &value, 4); return word; }
float value(uint32_t word) { float result; std::memcpy(&result, &word, 4); return result; }
float gain(int cdb) { return std::pow(10.0f, float(cdb) / 2000.0f); }
}
bool valid(const Mix &m) {
  if (m.master_cdb < -6000 || m.master_cdb > 600) return false;
  for (const auto *group : {&m.inputs, &m.outputs})
    for (const auto &c : *group) if (c.gain_cdb < -6000 || c.gain_cdb > 1200) return false;
  return true;
}
Mixer::Mixer() { publish(Mix{}); }
void Mixer::publish(const Mix &mix) {
  if (!valid(mix)) return;
  const std::array<Channel, 8> *groups[] = {&mix.inputs, &mix.outputs};
  for (unsigned d = 0; d < 2; ++d) {
    unsigned solo = 0;
    for (unsigned ch = 0; ch < 8; ++ch) {
      const auto &c = (*groups[d])[ch];
      const auto amplitude = gain(c.gain_cdb) * (c.invert ? -1.f : 1.f);
      controls_[d][ch].store(uint64_t(bits(amplitude)) | (uint64_t(c.mute) << 32), std::memory_order_relaxed);
      if (c.solo) solo |= 1u << ch;
    }
    solo_[d].store(solo, std::memory_order_relaxed);
  }
  master_.store(bits(mix.master_mute ? 0.f : gain(mix.master_cdb)), std::memory_order_relaxed);
}
void Mixer::reset() {
  for (unsigned d = 0; d < 2; ++d) for (unsigned ch = 0; ch < 8; ++ch) {
    peaks_[d][ch].store(0, std::memory_order_relaxed); clips_[d][ch].store(0, std::memory_order_relaxed);
    const auto control = controls_[d][ch].load(std::memory_order_relaxed);
    const auto solo = solo_[d].load(std::memory_order_relaxed);
    const auto master = d == 1 ? value(master_.load(std::memory_order_relaxed)) : 1.f;
    const float target = (control >> 32 || (solo && !(solo & (1u << ch)))) ? 0.f : value(uint32_t(control)) * master;
    ramps_[d][ch] = {target, target, 0, 0};
  }
}
void Mixer::apply(unsigned d, const int32_t *source, int32_t *destination,
                  unsigned frames, unsigned channels, unsigned pcm_bits) {
  const double full = std::ldexp(1.0, int(pcm_bits) - 1), maximum = full - 1;
  const auto solo = solo_[d].load(std::memory_order_relaxed);
  const auto master = d == 1 ? value(master_.load(std::memory_order_relaxed)) : 1.f;
  // 64 samples smooth a parameter transition, without delaying any sample.
  for (unsigned ch = 0; ch < channels; ++ch) {
    const auto control = controls_[d][ch].load(std::memory_order_relaxed);
    const float target = (control >> 32 || (solo && !(solo & (1u << ch)))) ? 0.f : value(uint32_t(control)) * master;
    auto &r = ramps_[d][ch];
    if (target != r.target) { r.target = target; r.left = 64; r.step = (target - r.value) / 64.f; }
    double peak = 0; uint64_t clipped = 0;
    for (unsigned frame = 0; frame < frames; ++frame) {
      if (r.left && !--r.left) r.value = r.target;
      else if (r.left) r.value += r.step;
      const auto index = size_t(frame) * channels + ch;
      const double sample = double(source[index]) * r.value;
      if (sample > maximum || sample < -full) ++clipped;
      const double limited = std::max(-full, std::min(maximum, sample));
      destination[index] = int32_t(std::llround(limited));
      peak = std::max(peak, std::abs(limited) / full);
    }
    peaks_[d][ch].store(bits(float(peak)), std::memory_order_relaxed);
    if (clipped) clips_[d][ch].fetch_add(clipped, std::memory_order_relaxed);
  }
}
bool Mixer::capture(const piaoip::engine::AudioBlock &source, piaoip::engine::AudioBlock &adjusted) {
  if (!source.capture || !source.render || !source.frames || source.frames > 256 ||
      !source.inputs || source.inputs > 8 || !source.outputs || source.outputs > 8 ||
      (source.bits != 16 && source.bits != 24 && source.bits != 32)) return false;
  adjusted = source;
  apply(0, source.capture, capture_.data(), source.frames, source.inputs, source.bits);
  adjusted.capture = capture_.data();
  return true;
}
void Mixer::render(const piaoip::engine::AudioBlock &b) { apply(1, b.render, b.render, b.frames, b.outputs, b.bits); }
float Mixer::peak(unsigned d, unsigned ch) const { return value(peaks_.at(d).at(ch).load(std::memory_order_relaxed)); }
uint64_t Mixer::clips(unsigned d, unsigned ch) const { return clips_.at(d).at(ch).load(std::memory_order_relaxed); }
} // namespace rey::audio
