#pragma once
#include <stdint.h>
#include <stddef.h>

// Versioned kernel/service contract. Buffered requests contain bounded PCM and
// values only; no user pointers, MDL addresses or writable kernel descriptors.
#define PIAOIP_BRIDGE_VERSION 1u
#define PIAOIP_BRIDGE_CHANNELS 8u
#define PIAOIP_BRIDGE_FRAMES 256u
#define PIAOIP_BRIDGE_PACKET_FRAMES 4096u
#define PIAOIP_BRIDGE_SAMPLES (PIAOIP_BRIDGE_CHANNELS * PIAOIP_BRIDGE_FRAMES)
#define PIAOIP_BRIDGE_DISCONTINUITY 1u
#define PIAOIP_BRIDGE_MISSING 2u
#define PIAOIP_BRIDGE_CAPTURE_ACTIVE 4u
#define PIAOIP_BRIDGE_RENDER_ACTIVE 8u
#define PIAOIP_BRIDGE_CTL(function) ((0x22u << 16) | (3u << 14) | ((function) << 2))
#define IOCTL_PIAOIP_ATTACH PIAOIP_BRIDGE_CTL(0x800u)
#define IOCTL_PIAOIP_DETACH PIAOIP_BRIDGE_CTL(0x801u)
#define IOCTL_PIAOIP_EXCHANGE PIAOIP_BRIDGE_CTL(0x802u)
#define IOCTL_PIAOIP_STATS PIAOIP_BRIDGE_CTL(0x803u)

typedef struct PIAOIP_BRIDGE_PROFILE {
  uint32_t size, version, rate, valid_bits, inputs, outputs, block, guard;
  char device_id[32]; // lowercase hexadecimal, without terminator
} PIAOIP_BRIDGE_PROFILE;

typedef struct PIAOIP_BRIDGE_EXCHANGE {
  uint32_t size, version, frames, capture_channels, render_channels, flags, epoch, reserved;
  uint64_t frame_position, qpc;
  int32_t samples[PIAOIP_BRIDGE_SAMPLES]; // interleaved, left-aligned valid bits
} PIAOIP_BRIDGE_EXCHANGE;

typedef struct PIAOIP_BRIDGE_STATS {
  uint32_t size, version, attached, capture_running, render_running, reserved;
  uint64_t exchanges, capture_frames, render_frames, capture_overruns, render_underruns,
      discontinuities;
} PIAOIP_BRIDGE_STATS;

static inline int piaoip_bridge_valid_profile(const PIAOIP_BRIDGE_PROFILE *p) {
  unsigned i, nonzero = 0;
  if (!p || p->size != sizeof(*p) || p->version != PIAOIP_BRIDGE_VERSION ||
      p->inputs > PIAOIP_BRIDGE_CHANNELS || p->outputs > PIAOIP_BRIDGE_CHANNELS ||
      !(p->inputs + p->outputs) ||
      (p->valid_bits != 16 && p->valid_bits != 24 && p->valid_bits != 32) || p->guard > 8192 ||
      (p->block != 16 && p->block != 32 && p->block != 64 && p->block != 128 && p->block != 256) ||
      (p->rate != 44100 && p->rate != 48000 && p->rate != 88200 && p->rate != 96000 &&
       p->rate != 176400 && p->rate != 192000))
    return 0;
  for (i = 0; i < 32; ++i) {
    char c = p->device_id[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
      return 0;
    nonzero |= (c != '0');
  }
  return nonzero != 0;
}
static inline int piaoip_bridge_valid_exchange(const PIAOIP_BRIDGE_EXCHANGE *x,
                                               const PIAOIP_BRIDGE_PROFILE *p) {
  return x && p && x->size == sizeof(*x) && x->version == PIAOIP_BRIDGE_VERSION && x->frames > 0 &&
         x->frames <= PIAOIP_BRIDGE_FRAMES && x->capture_channels == p->inputs &&
         x->render_channels == p->outputs && x->epoch > 0 && x->epoch <= 65535 &&
         x->frame_position <= UINT64_MAX - x->frames && !x->reserved &&
         !(x->flags & ~(PIAOIP_BRIDGE_DISCONTINUITY | PIAOIP_BRIDGE_MISSING));
}
static inline int piaoip_bridge_frame_gap(const PIAOIP_BRIDGE_EXCHANGE *x, uint64_t expected,
                                          unsigned max_gap, unsigned *gap) {
  if (!x || !gap || x->frame_position < expected || x->frame_position - expected > max_gap)
    return 0;
  *gap = (unsigned)(x->frame_position - expected);
  return !*gap || (x->flags & PIAOIP_BRIDGE_DISCONTINUITY);
}
