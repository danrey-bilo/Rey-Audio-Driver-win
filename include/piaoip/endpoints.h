#pragma once
#include <stdint.h>

/* One multichannel input/output plus four input/output pairs per board.
 * The same bounded PCM mapping is used by ACX and the host contract tests. */
#define PIAOIP_ENDPOINT_SLOTS 10u
#define PIAOIP_DEVICE_LIMIT 10u

static inline unsigned piaoip_endpoint_capture(unsigned slot) { return !(slot & 1u); }
static inline unsigned piaoip_endpoint_first_channel(unsigned slot) {
  return slot < 2 ? 0 : ((slot - 2) / 2) * 2;
}
static inline unsigned piaoip_endpoint_channels(unsigned slot, unsigned inputs, unsigned outputs) {
  unsigned total, first;
  if (slot >= PIAOIP_ENDPOINT_SLOTS || inputs > 8 || outputs > 8) return 0;
  total = piaoip_endpoint_capture(slot) ? inputs : outputs;
  if (slot < 2) return total;
  first = piaoip_endpoint_first_channel(slot);
  if (first >= total) return 0;
  return total - first > 2 ? 2 : total - first;
}
static inline void piaoip_endpoint_capture_pcm(int32_t *output, const int32_t *input,
    unsigned frames, unsigned endpoint_channels, unsigned source_channels, unsigned first) {
  unsigned frame, channel;
  for (frame = 0; frame < frames; ++frame)
    for (channel = 0; channel < endpoint_channels; ++channel)
      output[frame * endpoint_channels + channel] = input ? input[frame * source_channels + first + channel] : 0;
}
static inline unsigned piaoip_endpoint_render_pcm(int32_t *output, const int32_t *input,
    unsigned frames, unsigned endpoint_channels, unsigned destination_channels, unsigned first,
    unsigned valid_bits) {
  unsigned frame, channel, clipped = 0;
  const unsigned shift = 32u - valid_bits;
  const int64_t maximum = (int64_t)(0x7fffffffu >> shift) << shift;
  const int64_t minimum = -2147483647LL - 1;
  for (frame = 0; frame < frames; ++frame)
    for (channel = 0; channel < endpoint_channels; ++channel) {
      const unsigned target = frame * destination_channels + first + channel;
      int64_t sample = (int64_t)output[target] + input[frame * endpoint_channels + channel];
      if (sample > maximum) { sample = maximum; ++clipped; }
      if (sample < minimum) { sample = minimum; ++clipped; }
      output[target] = (int32_t)sample;
    }
  return clipped;
}
