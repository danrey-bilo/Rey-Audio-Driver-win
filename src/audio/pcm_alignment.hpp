#pragma once
#include <cstdint>
#include <cstring>
namespace rey::audio {
inline uint32_t wave_sample(int32_t sample, unsigned bits) {
  return uint32_t(sample) << (32 - bits);
}
inline int32_t wire_sample(uint32_t sample, unsigned bits) {
  const auto shift = 32 - bits;
  uint32_t value = sample >> shift;
  if (shift && (sample & 0x80000000u)) value |= ~uint32_t(0) << bits;
  int32_t result; std::memcpy(&result, &value, sizeof(result)); return result;
}
}
