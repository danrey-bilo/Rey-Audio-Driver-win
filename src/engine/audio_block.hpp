#pragma once
#include <cstdint>
namespace rey::engine {
struct AudioBlock {
  const int32_t *capture = nullptr;
  int32_t *render = nullptr;
  unsigned frames = 0, inputs = 0, outputs = 0, bits = 0;
  unsigned missing = 0;
  uint16_t epoch = 0;
  uint64_t frame_position = 0, scheduled_ns = 0;
  bool discontinuity = false;
};
using ProcessBlock = bool (*)(void *, const AudioBlock &);
} // namespace rey::engine
