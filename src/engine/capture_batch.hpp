#pragma once
#include "audio_block.hpp"
namespace rey::engine {
// Publish every already completed capture fragment before asking the host
// for render. No future packet is awaited and no audio queue is added.
inline bool publish_capture_batch(ProcessBlock process, void *context,
                                  const AudioBlock *blocks, unsigned count) {
  if (!process || !blocks || !count || count > 16)
    return false;
  for (unsigned i = 0; i < count; ++i) {
    auto block = blocks[i];
    block.capture_only = true;
    block.render_only = false;
    if (!process(context, block))
      return false;
  }
  return true;
}
inline bool render_captured_block(ProcessBlock process, void *context,
                                  const AudioBlock &source) {
  auto block = source;
  block.capture_only = false;
  block.render_only = true;
  return process && process(context, block);
}
} // namespace rey::engine
