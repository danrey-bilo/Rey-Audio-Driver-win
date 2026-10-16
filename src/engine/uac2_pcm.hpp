#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
namespace rey::engine {
inline int32_t uac2_decode(int32_t pcm32, unsigned bits) {
  // Drop low bits with sign extension, including negative values below one LSB.
  const auto divisor = int64_t(1) << (32-bits);
  return int32_t(pcm32 >= 0 ? int64_t(pcm32)/divisor : -((-int64_t(pcm32)+divisor-1)/divisor));
}
inline int32_t uac2_encode(int32_t value, unsigned bits) {
  return int32_t(int64_t(value) * (int64_t(1) << (32-bits)));
}
// One audio thread owns both sides; no locks or allocation during push/pop.
class Uac2Queue {
  std::vector<int32_t> data_;
  unsigned capacity_;
  uint64_t read_ = 0, written_ = 0;
public:
  explicit Uac2Queue(unsigned frames) : data_(size_t(frames)*8), capacity_(frames) {}
  unsigned available() const { return unsigned(written_-read_); }
  uint64_t push(const int32_t *pcm, unsigned frames, unsigned bits) {
    uint64_t lost = 0;
    if (frames > capacity_) { lost = frames-capacity_; pcm += size_t(lost)*8; frames = capacity_; }
    if (available()+frames > capacity_) { const auto drop = available()+frames-capacity_; read_ += drop; lost += drop; }
    for (unsigned f = 0; f < frames; ++f)
      for (unsigned ch = 0; ch < 8; ++ch)
        data_[((written_+f)%capacity_)*8+ch] = uac2_encode(pcm[size_t(f)*8+ch], bits);
    written_ += frames; return lost;
  }
  unsigned pop(uint8_t *pcm32, unsigned frames) {
    const auto count = std::min(frames, available());
    for (unsigned f = 0; f < count; ++f)
      std::memcpy(pcm32 + size_t(f)*32, &data_[((read_+f)%capacity_)*8], 32);
    std::memset(pcm32 + size_t(count)*32, 0, size_t(frames-count)*32);
    read_ += count; return count;
  }
};
}
